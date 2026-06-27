#include "scripting/berry_bindings.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "can/can.h"
#include "can/msg_codec.h"
#include "led/led_rgb.h"
#include "storage/state.h"
#include "esp_log.h"
#include "esp_timer.h"

/* Berry internal headers for tracestack inspection. */
#include "be_vm.h"
#include "be_var.h"
#include "be_vector.h"
#include "be_string.h"

#include "be_debug.h"

/* ---- Internals ---- */

static const char *TAG_BB = "berry";

/* ---- Limits ---- */
#define MAX_TIMERS    16    /* max concurrent timer.after/every */
#define MAX_ACTIONS   32    /* max actions registered at once */
#define ACTION_NAME_MAX 40  /* max length of action name */

#define CHECK_TYPE(vm, n, pred, expected) \
    if (!(pred)) { \
        be_raise(vm, "type_error", be_pushfstring(vm, \
            "argument %d: expected %s, got %s", \
            (int)(n), (expected), be_typename(vm, (n)))); \
    }

#define CHECK_ARITY(vm, n) \
    if (be_top(vm) < (n)) { \
        be_raise(vm, "type_error", be_pushfstring(vm, \
            "expected %d argument(s), got %d", \
            (int)(n), be_top(vm))); \
    }

/* CAN engine pointers set by berry_set_buses() */
static can_t *s_bus[2];
/* The active Berry VM; set once by berry_register_bindings(). */
static bvm   *s_vm;
/* Script context used to tag timer/action registrations for cleanup. */
static int    s_current_script_id;
/* Optional handler invoked when a timer callback throws. */
static berry_timer_error_handler_t s_timer_error_handler;
/* Optional callback to resolve script_id → filename for action errors. */
static berry_script_name_cb_t s_script_name_cb;

void berry_set_buses(can_t *eng0, can_t *eng1)
{
    s_bus[0] = eng0;
    s_bus[1] = eng1;
}

can_t *berry_get_bus(int bus)
{
    if (bus < 0 || bus > 1) return NULL;
    return s_bus[bus];
}

void berry_set_current_script(int script_id) { s_current_script_id = script_id; }
int  berry_current_script(void)              { return s_current_script_id; }

static can_t *get_bus(int bus)
{
    if (bus < 0 || bus > 1 || !s_bus[bus]) return NULL;
    return s_bus[bus];
}

/* ---- Berry function ref storage ----
 * Keeps a hidden global list ".refs" on the VM containing function references
 * that C code holds across script invocations (timers, actions). Each ref is
 * just the list index. When the function is no longer needed, the list slot
 * is set to nil (refs_free). This prevents GC from collecting the closures. */

static int refs_alloc(bvm *vm, int fn_index)
{
    if (fn_index < 0) fn_index = be_top(vm) + fn_index + 1;

    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) {
        be_pop(vm, 1);
        be_newlist(vm);
        be_setglobal(vm, ".refs");
        be_getglobal(vm, ".refs");
    }
    be_pushvalue(vm, fn_index);
    be_data_push(vm, -2);
    be_pop(vm, 1);
    int ref = be_data_size(vm, -1) - 1;
    be_pop(vm, 1);
    return ref;
}

static void refs_free(bvm *vm, int ref)
{
    if (ref < 0) return;
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) { be_pop(vm, 1); return; }
    be_pushint(vm, ref);
    be_pushnil(vm);
    be_setindex(vm, -3);
    be_pop(vm, 3);
}

static bool refs_push(bvm *vm, int ref)
{
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) { be_pop(vm, 1); return false; }
    be_pushint(vm, ref);
    be_getindex(vm, -2);
    be_remove(vm, -2);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 2);
        return false;
    }
    return true;
}

int berry_capture_global(bvm *vm, const char *global_name)
{
    be_getglobal(vm, global_name);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 1);
        return -1;
    }
    int ref = refs_alloc(vm, -1);
    be_pop(vm, 1);
    be_pushnil(vm);
    be_setglobal(vm, global_name);
    return ref;
}

void berry_release_ref(bvm *vm, int ref)
{
    refs_free(vm, ref);
}

int berry_call_ref(bvm *vm, int ref, char *err_out, size_t err_size, char *type_out, size_t type_size)
{
    if (ref < 0) return -1;
    if (!refs_push(vm, ref)) return -1;
    if (be_pcall(vm, 0) != 0) {
        const char *msg = be_tostring(vm, -1);
        const char *type = be_tostring(vm, -2);
        if (err_out && err_size > 0)
            snprintf(err_out, err_size, "%s", msg ? msg : "unknown error");
        if (type_out && type_size > 0)
            snprintf(type_out, type_size, "%s", type ? type : "error");
        be_pop(vm, 2);
        return -2;
    }
    be_pop(vm, 2);
    return 0;
}

/* ---- timer.* (polled from main loop via berry_timer_tick) ----
 * All timers are polled (not interrupt-driven). Each timer_after/every call
 * allocates a slot in s_timers[] and stores a Berry function ref. The main
 * loop calls berry_timer_tick() which walks the table and fires due timers. */

typedef struct {
    bool     in_use;
    int      ref;
    int      script_id;
    uint32_t next_fire_ms;
    uint32_t period_ms;     /* 0 = one-shot */
} timer_entry_t;

/* Registry of all active timers. Index == handle returned to Berry. */
static timer_entry_t s_timers[MAX_TIMERS];

static int alloc_timer(int ref, uint32_t period_ms, uint32_t now_ms)
{
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!s_timers[i].in_use) {
            s_timers[i] = (timer_entry_t){
                .in_use = true, .ref = ref,
                .script_id = s_current_script_id,
                .next_fire_ms = now_ms + period_ms,
                .period_ms = period_ms,
            };
            return i;
        }
    }
    return -1;
}

/* timer_after(ms:int, fn:function) -> handle:int
 * Schedule a one-shot callback. fn is called once after ms milliseconds.
 * Returns an integer handle (pass to timer_cancel). */
static int l_timer_after(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isfunction(vm, 2), "function");
    uint32_t ms = (uint32_t)be_toint(vm, 1);
    int ref = refs_alloc(vm, 2);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    int h = alloc_timer(ref, ms, now);
    if (h < 0) {
        refs_free(vm, ref);
        ESP_LOGE(TAG_BB, "timer.after: registry full");
        be_return_nil(vm);
    }
    s_timers[h].period_ms = 0;   /* one-shot */
    be_pushint(vm, h);
    be_return(vm);
}

/* timer_every(ms:int, fn:function) -> handle:int
 * Schedule a repeating callback. fn is called every ms milliseconds.
 * Returns an integer handle (pass to timer_cancel). */
static int l_timer_every(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isfunction(vm, 2), "function");
    uint32_t ms = (uint32_t)be_toint(vm, 1);
    int ref = refs_alloc(vm, 2);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    int h = alloc_timer(ref, ms, now);
    if (h < 0) {
        refs_free(vm, ref);
        ESP_LOGE(TAG_BB, "timer.every: registry full");
        be_return_nil(vm);
    }
    be_pushint(vm, h);
    be_return(vm);
}

/* timer_cancel(handle:int) -> nil
 * Cancel a pending timer. No-op if the handle is invalid or already fired. */
static int l_timer_cancel(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    int h = be_toint(vm, 1);
    if (h >= 0 && h < MAX_TIMERS && s_timers[h].in_use) {
        refs_free(vm, s_timers[h].ref);
        s_timers[h].in_use = false;
    }
    be_return_nil(vm);
}

void berry_timer_tick(uint32_t now_ms)
{
    if (!s_vm) return;
    for (int i = 0; i < MAX_TIMERS; i++) {
        timer_entry_t *t = &s_timers[i];
        if (!t->in_use) continue;
        if ((int32_t)(now_ms - t->next_fire_ms) < 0) continue;

        int ref = t->ref;
        bool one_shot = (t->period_ms == 0);
        if (one_shot) {
            t->in_use = false;
        } else {
            t->next_fire_ms = now_ms + t->period_ms;
        }

        if (refs_push(s_vm, ref)) {
            if (be_pcall(s_vm, 0) != 0) {
                const char *err_type = be_tostring(s_vm, -2);
                const char *err_msg  = be_tostring(s_vm, -1);
                if (!one_shot) t->in_use = false;
                refs_free(s_vm, ref);
                ref = -1;
                if (s_timer_error_handler) {
                    s_timer_error_handler(t->script_id, err_type, err_msg);
                }
            }
            be_pop(s_vm, 2);
        }

        if (one_shot && ref >= 0) refs_free(s_vm, ref);
    }
}

/* ---- actions.register / actions.invoke ----
 * Dashboard actions are named Berry closures callable from the web UI or
 * from other scripts. Each entry stores the name string and a Berry ref.
 * Berry names the function "action_register"/"action_invoke" at the global
 * level; C uses "actions.register"/"actions.invoke" internally. */

typedef struct {
    bool in_use;
    char name[ACTION_NAME_MAX];
    int  ref;
    int  script_id;
} action_entry_t;

/* Registry of all registered actions. Name is the lookup key. */
static action_entry_t s_actions[MAX_ACTIONS];

static int find_action(const char *name)
{
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (s_actions[i].in_use && strcmp(s_actions[i].name, name) == 0) return i;
    }
    return -1;
}

/* action_register(name:str, fn:function) -> nil
 * Register a named action visible on the Dashboard. If an action with the
 * same name already exists, it is replaced. */
static int l_action_register(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    CHECK_TYPE(vm, 2, be_isfunction(vm, 2), "function");
    const char *name = be_tostring(vm, 1);

    int existing = find_action(name);
    if (existing >= 0) {
        refs_free(vm, s_actions[existing].ref);
        s_actions[existing].in_use = false;
    }

    int slot = -1;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (!s_actions[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        ESP_LOGE(TAG_BB, "actions registry full");
        be_return_nil(vm);
    }

    int ref = refs_alloc(vm, 2);
    s_actions[slot].in_use = true;
    s_actions[slot].ref = ref;
    s_actions[slot].script_id = s_current_script_id;
    strncpy(s_actions[slot].name, name, ACTION_NAME_MAX - 1);
    s_actions[slot].name[ACTION_NAME_MAX - 1] = '\0';

    ESP_LOGI(TAG_BB, "action registered: %s (script_id=%d)", name, s_current_script_id);
    be_return_nil(vm);
}

/* action_invoke(name:str) -> nil
 * Programmatically invoke a registered action by name. */
static int l_action_invoke(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    berry_action_invoke(be_tostring(vm, 1));
    be_return_nil(vm);
}

int berry_actions_snapshot(const char **out_names, int max)
{
    int n = 0, found = 0;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (!s_actions[i].in_use) continue;
        if (n < max) out_names[n++] = s_actions[i].name;
        found++;
    }
    return found;
}

int berry_action_invoke(const char *name)
{
    if (!s_vm || !name) return -1;
    int idx = find_action(name);
    if (idx < 0) return -1;

    if (!refs_push(s_vm, s_actions[idx].ref)) return -1;
    if (be_pcall(s_vm, 0) != 0) {
        const char *emsg = be_tostring(s_vm, -1);
        const char *etype = be_tostring(s_vm, -2);
        const char *func_name = NULL;
        int line = berry_error_line(&func_name);
        const char *fname = s_script_name_cb ? s_script_name_cb(s_actions[idx].script_id) : NULL;
        if (fname) {
            char logbuf[256];
            berry_format_error(logbuf, sizeof(logbuf), fname, line, func_name, etype, emsg);
            ESP_LOGE("scripts", "%s", logbuf);
        } else if (line > 0) {
            ESP_LOGE("scripts", "action '%s' (line %d): %s", name, line, emsg);
        } else {
            ESP_LOGE("scripts", "action '%s': %s", name, emsg);
        }
        be_pop(s_vm, 2);
        return -2;
    }
    be_pop(s_vm, 2);
    return 0;
}

/* ---- Per-script cleanup ----
 * Called when a script is disabled. Releases all timers and actions tagged
 * with script_id. Script 0 is reserved for system use and never cleaned up. */

void berry_cleanup_script(int script_id)
{
    if (script_id == 0) return;

    /* Cancel timers */
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (s_timers[i].in_use && s_timers[i].script_id == script_id) {
            refs_free(s_vm, s_timers[i].ref);
            s_timers[i].in_use = false;
        }
    }

    /* Drop actions */
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (s_actions[i].in_use && s_actions[i].script_id == script_id) {
            refs_free(s_vm, s_actions[i].ref);
            s_actions[i].in_use = false;
        }
    }
}

/* ---- CAN: raw frames, message drafts ----
 * Naming convention:
 *   can_*_raw / can_send_raw, can_recv_raw     — raw CAN, no DBC
 *   can_msg_* / can_msg_get, can_msg_new, can_msg_send — named messages
 *
 * All CAN functions take a bus argument (0 or 1). In simulation mode
 * (can_t.sim_mode), can_send_raw logs the frame instead of transmitting. */




/* can_send_raw(bus:int, id:int, data:bytes) -> nil
 * Transmit a raw CAN frame. No DBC encoding, no signal modification, no
 * checksum/counter injection. bus = 0 or 1, id = 11-bit CAN ID,
 * data = bytes object (max 8 bytes). In sim mode, logs to the UI instead. */
static int l_can_send_raw(bvm *vm)
{
    CHECK_ARITY(vm, 3);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isbytes(vm, 3), "bytes");
    int bus = be_toint(vm, 1);
    uint32_t id = (uint32_t)be_toint(vm, 2);
    size_t len = 0;
    const uint8_t *data = be_tobytes(vm, 3, &len);
    if (len > 8) len = 8;
    can_t *c = get_bus(bus);
    if (c && c->sim_mode) {
        char hex[17] = {0};
        for (size_t i = 0; i < len; i++) {
            hex[2*i]     = "0123456789ABCDEF"[(data[i] >> 4) & 0xF];
            hex[2*i + 1] = "0123456789ABCDEF"[data[i]        & 0xF];
        }
        char msg[48];
        snprintf(msg, sizeof(msg), "SIM tx bus%d 0x%03X [%d] %s", bus, (unsigned)id, (int)len, hex);
        berry_log_push(msg);
    } else {
        can_send(c, id, data, (uint8_t)len);
    }
    be_return_nil(vm);
}

/* can_recv_raw(bus:int, msg_id:int [, timeout_ms:int]) -> bytes | nil
 * Returns the last payload for a CAN ID.  Default timeout is 1000 ms —
 * the first read blocks up to 1 s waiting for initial data.  Pass an
 * explicit timeout to override, or 0 for instant return.
 * Stalls the Berry VM while blocking.
 * waits, never inside a timer callback. */
static int l_can_recv_raw(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    int bus = be_toint(vm, 1);
    uint32_t msg_id = (uint32_t)be_toint(vm, 2);
    can_t *c = get_bus(bus);
    if (!c) be_return_nil(vm);

    uint32_t timeout = be_top(vm) >= 3 ? (uint32_t)be_toint(vm, 3) : 1000;
    uint8_t data[8], dlc;
    if (can_read(c, msg_id, data, &dlc, timeout) >= 0) {
        be_pushbytes(vm, data, dlc);
        be_return(vm);
    }
    be_return_nil(vm);
}

/* can_msg_get(bus:int, msg_id:int) -> draft | nil
 * Returns the latest received frame for msg_id as a message draft (map
 * instance with keys: id, data, dlc). Returns nil if no matching frame has
 * been received. The draft can be modified with msg_sig_set and transmitted
 * via can_msg_send. bus is first for consistency with can_send_raw/
 * can_recv_raw. */
static int l_can_msg_get(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int"); //bus
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int"); //msg_id
    int bus = be_toint(vm, 1);
    uint32_t msg_id = (uint32_t)be_toint(vm, 2);
    can_t *eng = get_bus(bus);
    if (!eng) be_return_nil(vm);

    uint8_t data[8], dlc;
    if (can_read(eng, msg_id, data, &dlc, 0) < 0) be_return_nil(vm);

    // https://berry.readthedocs.io/en/latest/source/en/FFI-Example.html#instantiate-a-list-object-in-a-native-function
    be_getglobal(vm, "map");
    be_newmap(vm);
    be_pushstring(vm, "id");      be_pushint(vm, (bint)msg_id);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "data");    be_pushbytes(vm, data, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "dlc");     be_pushint(vm, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_call(vm, 1);
    be_pop(vm, 1); // pop the arguments
    be_return(vm);
}

/* can_msg_new(id:int, dlc:int) -> draft
 * Creates a zeroed message draft (all bytes = 0) for encoding and
 * transmission. The draft is a bus-free map instance with keys: id, data,
 * dlc. Pass the bus at send time via can_msg_send(bus, draft). The
 * preprocessor rewrites can_msg_new("msg_name") to call this with the
 * resolved ID and DLC from the DBC. */
static int l_can_msg_new(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int"); //msg_id
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int"); //dlc
    uint32_t msg_id = (uint32_t)be_toint(vm, 1);
    int dlc = be_toint(vm, 2);
    if (dlc <= 0 || dlc > 8) dlc = 8;

    uint8_t zeros[8] = {0};

    be_getglobal(vm, "map");
    be_newmap(vm);
    be_pushstring(vm, "id");      be_pushint(vm, (bint)msg_id);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "data");    be_pushbytes(vm, zeros, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "dlc");     be_pushint(vm, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_call(vm, 1);
    be_pop(vm, 1);
    be_return(vm);
}

/* can_msg_send(bus:int, draft) -> nil
 * Transmit a message draft on the given bus. Reads id, data, dlc from the
 * draft map instance via dot-member access. bus is first for consistency
 * with can_send_raw/can_recv_raw. The draft is kept bus-agnostic — pass the
 * bus at send time. Signal encoding and counter/checksum update must be done
 * in Berry before calling (use msg_sig_set). */
static int l_can_msg_send(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int"); //bus
    if (!be_isinstance(vm, 2)) {
        ESP_LOGW(TAG_BB, "can_msg_send: trying to send empty message");
        be_return_nil(vm);
    }

    int bus = be_toint(vm, 1);
    can_t *eng = get_bus(bus);
    if (!eng) be_return_nil(vm);

    be_getmember(vm, 2, "id");
    uint32_t id = (uint32_t)be_toint(vm, -1);
    be_pop(vm, 1);

    be_getmember(vm, 2, "dlc");
    int dlc = be_toint(vm, -1);
    be_pop(vm, 1);

    be_getmember(vm, 2, "data");
    size_t len = 0;
    const uint8_t *draft_data = be_tobytes(vm, -1, &len);
    uint8_t data[8];
    memcpy(data, draft_data, len > 8 ? 8 : len);
    be_pop(vm, 1);

    can_send(eng, id, data, (uint8_t)dlc);
    be_return_nil(vm);
}

/* ---- CAN utility functions (pure computation, no DBC) ----
 * These operate on raw bytes and are called by the preprocessor-generated
 * code. They implement the CAN signal encoding layer: DBC signal metadata
 * (start_bit, length, byte order, scaling) is resolved at preprocess time
 * and baked into the generated Berry code as integer/float literals. */

/* bit_extract(data:bytes, start_bit:int, bit_length:int, big_endian:bool) -> int
 * Extract an integer bit field from a bytes object. Motorola (big-endian)
 * or Intel (little-endian) byte order. Returns the raw unscaled value. */
static int l_bit_extract(bvm *vm)
{
    CHECK_ARITY(vm, 4);
    CHECK_TYPE(vm, 1, be_isbytes(vm, 1), "bytes");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    size_t len = 0;
    const uint8_t *data = be_tobytes(vm, 1, &len);
    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    uint64_t raw = bit_extract(data, start_bit, bit_length, big_endian);
    be_pushint(vm, (bint)raw);
    be_return(vm);
}

/* bit_insert(data:bytes, start_bit:int, bit_length:int, big_endian:bool, raw:int) -> bytes
 * Returns a new bytes object with the raw integer value inserted at the
 * specified bit position. Berry bytes are immutable so this allocates a new
 * copy (8 bytes max). Used by the preprocessor for msg_sig_set. */
static int l_bit_insert(bvm *vm)
{
    CHECK_ARITY(vm, 5);
    CHECK_TYPE(vm, 1, be_isbytes(vm, 1), "bytes");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    CHECK_TYPE(vm, 5, be_isint(vm, 5), "int");
    size_t len = 0;
    const uint8_t *src = be_tobytes(vm, 1, &len);
    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    uint64_t raw = (uint64_t)(unsigned)be_toint(vm, 5);
    uint8_t buf[8];
    memcpy(buf, src, len > 8 ? 8 : len);
    bit_insert(buf, start_bit, bit_length, big_endian, raw);
    be_pushbytes(vm, buf, len > 8 ? 8 : len);
    be_return(vm);
}

/* msg_sig_get(draft, start_bit:int, bit_length:int, big_endian:bool,
 *             is_signed:bool, scale:real, offset:real) -> real
 * Decode a DBC signal from a message draft: extracts draft.data bytes,
 * reads the bitfield, applies sign, scale, offset. Returns physical value. */
static int l_msg_sig_get(bvm *vm)
{
    CHECK_ARITY(vm, 7);
    if (!be_isinstance(vm, 1)) be_return_nil(vm);
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    CHECK_TYPE(vm, 5, be_isbool(vm, 5), "bool");

    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    bool is_signed = be_tobool(vm, 5);
    float scale = (float)be_toreal(vm, 6);
    float offset = (float)be_toreal(vm, 7);

    /* Berry class instances store internal data under a hidden `.p` member.
     * be_getmember gets it, then be_getindex looks up "data" within it
     * (same pattern as be_maplib.c:m_item for map["key"]). */
    be_getmember(vm, 1, ".p");
    be_pushstring(vm, "data");
    be_getindex(vm, -2);
    if (!be_isbytes(vm, -1)) {
        be_pop(vm, 1);
        be_return_nil(vm);
    }
    size_t len = 0;
    const uint8_t *data = be_tobytes(vm, -1, &len);
    be_pop(vm, 1);

    float val = signal_decode(data, start_bit, bit_length, big_endian, is_signed, scale, offset);
    be_pushreal(vm, (breal)val);
    be_return(vm);
}

/* msg_sig_set(draft, start_bit:int, bit_length:int, big_endian:bool,
 *             is_signed:bool, scale:real, offset:real, value:real) -> nil
 * Encode a physical value into a message draft: converts to raw, writes
 * the bitfield in draft.data bytes in-place. No return value. */
static int l_msg_sig_set(bvm *vm)
{
    CHECK_ARITY(vm, 8);
    if (!be_isinstance(vm, 1)) {
        ESP_LOGW(TAG_BB, "msg_sig_set: draft is nil");
        be_return_nil(vm);
    }
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    CHECK_TYPE(vm, 5, be_isbool(vm, 5), "bool");

    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    bool is_signed = be_tobool(vm, 5);
    float scale = (float)be_toreal(vm, 6);
    float offset = (float)be_toreal(vm, 7);
    float value = (float)be_toreal(vm, 8);

    /* Same `.p` + getindex pattern as l_msg_sig_get. */
    be_getmember(vm, 1, ".p");
    be_pushstring(vm, "data");
    be_getindex(vm, -2);
    if (!be_isbytes(vm, -1)) {
        be_pop(vm, 1);
        ESP_LOGW(TAG_BB, "msg_sig_set: draft has no data bytes");
        be_return_nil(vm);
    }
    size_t len = 0;
    uint8_t *data = (uint8_t *)be_tobytes(vm, -1, &len);
    signal_encode(data, start_bit, bit_length, big_endian, is_signed, scale, offset, value);
    be_pop(vm, 1);

    be_return_nil(vm);
}

/* ---- LED ---- */

/* led_set(r:int, g:int, b:int) -> nil
 * Set the on-board RGB LED to the given colour. Each channel 0–255. */
static int l_led_set(bvm *vm)
{
    CHECK_ARITY(vm, 3);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    uint8_t r = (uint8_t)be_toint(vm, 1);
    uint8_t g = (uint8_t)be_toint(vm, 2);
    uint8_t b = (uint8_t)be_toint(vm, 3);
    led_rgb_set(r, g, b);
    be_return_nil(vm);
}

/* led_off() -> nil
 * Turn the on-board RGB LED off. */
static int l_led_off(bvm *vm)
{
    (void)vm;
    led_rgb_off();
    be_return_nil(vm);
}

/* ---- State (NVS) ----
 * Key-value storage persisted to NVS flash under the "script" namespace.
 * Values survive reboot. All keys and values are strings. */

/* state_set(key:str, value:str) -> nil
 * Write a string value to persistent storage. Overwrites existing. */
static int l_state_set(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    CHECK_TYPE(vm, 2, be_isstring(vm, 2), "string");
    state_set("script", be_tostring(vm, 1), be_tostring(vm, 2));
    be_return_nil(vm);
}

/* state_get(key:str [, default]) -> str | default | nil
 * Read a string value from persistent storage. If key is missing, returns
 * the optional default or nil if no default given. */
static int l_state_get(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    static char buf[256];
    if (state_get("script", be_tostring(vm, 1), buf, sizeof(buf)) == ESP_OK) {
        be_pushstring(vm, buf);
        be_return(vm);
    }
    if (be_top(vm) >= 2) {
        be_pushvalue(vm, 2);
        be_return(vm);
    }
    be_return_nil(vm);
}

/* state_remove(key:str) -> nil
 * Delete a key from persistent storage. No-op if key does not exist. */
static int l_state_remove(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    state_remove("script", be_tostring(vm, 1));
    be_return_nil(vm);
}

/* ---- millis() helper ---- */

/* millis() -> int
 * Milliseconds since boot. Wraps at 2^31 (~24 days). */
static int l_millis(bvm *vm)
{
    be_pushint(vm, (bint)(esp_timer_get_time() / 1000));
    be_return(vm);
}

/* ---- Registration ----
 * Called once during Berry VM initialisation to expose all native functions.
 * The names here are what Berry scripts see at global scope. Keep the
 * completions in webui/src/editor/berry-completions.ts in sync. */

void berry_register_bindings(bvm *vm)
{
    s_vm = vm;

    /* Raw CAN I/O: no DBC, no encoding */
    be_regfunc(vm, "can_send_raw",   l_can_send_raw);
    be_regfunc(vm, "can_recv_raw",   l_can_recv_raw);

    /* Message drafts: DBC-aware, named fields {id, bus, data, dlc} */
    be_regfunc(vm, "can_msg_get",    l_can_msg_get);
    be_regfunc(vm, "can_msg_new",    l_can_msg_new);
    be_regfunc(vm, "can_msg_send",   l_can_msg_send);

    /* CAN bit-level utilities: used by preprocessor-generated code */
    be_regfunc(vm, "bit_extract",    l_bit_extract);
    be_regfunc(vm, "bit_insert",     l_bit_insert);
    be_regfunc(vm, "msg_sig_get", l_msg_sig_get);
    be_regfunc(vm, "msg_sig_set", l_msg_sig_set);

    /* On-board RGB LED */
    be_regfunc(vm, "led_set", l_led_set);
    be_regfunc(vm, "led_off", l_led_off);

    /* Persistent key-value storage (NVS flash, "script" namespace) */
    be_regfunc(vm, "state_set", l_state_set);
    be_regfunc(vm, "state_get", l_state_get);
    be_regfunc(vm, "state_remove", l_state_remove);

    /* Timers (polled) */
    be_regfunc(vm, "timer_after", l_timer_after);
    be_regfunc(vm, "timer_every", l_timer_every);
    be_regfunc(vm, "timer_cancel", l_timer_cancel);

    /* Dashboard actions */
    be_regfunc(vm, "action_register", l_action_register);
    be_regfunc(vm, "action_invoke",   l_action_invoke);

    /* System */
    be_regfunc(vm, "millis", l_millis);
}

void berry_set_timer_error_handler(berry_timer_error_handler_t fn)
{
    s_timer_error_handler = fn;
}

void berry_set_script_name_callback(berry_script_name_cb_t fn)
{
    s_script_name_cb = fn;
}

/* Repair tracestack IPs after native-function errors.
 * repair_stack() is a copy from berry be_debug.c:228
 * Native frames don't save ip, but the recycled slot retains the
 * Berry closure's return address. This copies it to the closure frame. */
static void repair_stack(bvm *vm)
{
    bcallsnapshot *cf;
    bcallsnapshot *base = be_stack_base(&vm->tracestack);
    bcallsnapshot *top = be_stack_top(&vm->tracestack);
    /* Because the native function does not push `ip` to the
     * stack, the ip on the native function frame corresponds
     * to the previous Berry closure. */
    for (cf = top; cf >= base; --cf) {
        if (!var_isclosure(&cf->func)) {
            /* the last native function stack frame has the `ip` of
             * the previous Berry frame */
            binstruction *ip = cf->ip;
            /* skip native function stack frames */
            for (; cf >= base && !var_isclosure(&cf->func); --cf);
            /* fixed `ip` of Berry closure frame near native function frame */
            if (cf >= base) cf->ip = ip;
        }
    }
}

int berry_error_line(const char **name_out)
{
    if (name_out) *name_out = NULL;
    if (!s_vm) return 0;
    if (be_stack_isempty(&s_vm->tracestack)) return 0;

    repair_stack(s_vm);

    bcallsnapshot *base = be_stack_base(&s_vm->tracestack);
    bcallsnapshot *top = be_stack_top(&s_vm->tracestack);
    for (bcallsnapshot *cf = top; cf >= base; --cf) {
        if (!var_isclosure(&cf->func)) continue;
        bclosure *cl = var_toobj(&cf->func);
        if (!cl || !cl->proto) continue;
        bproto *proto = cl->proto;
        if (name_out && proto->name) *name_out = str(proto->name);
#if BE_DEBUG_RUNTIME_INFO
        if (proto->lineinfo && proto->nlineinfo) {
            int pc = (int)(cf->ip - proto->code - 1);
            if (pc < 0) pc = 0;
            blineinfo *it = proto->lineinfo;
            blineinfo *end = it + proto->nlineinfo;
            for (; it < end && pc > it->endpc; ++it) {}
            if (it < end) return it->linenumber;
        }
#else
        (void)proto;
#endif
        return 0;
    }
    return 0;
}

static int buprintf(char *buf, size_t size, int pos, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + pos, size - pos, fmt, args);
    va_end(args);
    if (n > 0) pos = ((size_t)(pos + n) < size) ? pos + n : (int)size - 1;
    return pos;
}

int berry_format_error(char *buf, size_t bufsize,
                       const char *filename, int line,
                       const char *funcname,
                       const char *type, const char *msg)
{
    if (!msg) msg = "unknown error";
    buf[0] = '\0';

    int pos = 0;
    if (line > 0 && filename)   pos = buprintf(buf, bufsize, pos, "%s:%d: ", filename, line);
    if (funcname)               pos = buprintf(buf, bufsize, pos, "%s(...): ", funcname);
    if (type)                   pos = buprintf(buf, bufsize, pos, "%s: ", type);
    pos = buprintf(buf, bufsize, pos, "%s", msg);
    return pos;
}
