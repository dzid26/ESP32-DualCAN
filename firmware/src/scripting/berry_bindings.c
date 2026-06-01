#include "scripting/berry_bindings.h"

#include <stdio.h>
#include <string.h>

#include "can/can.h"
#include "can/msg_codec.h"
#include "led/led_rgb.h"
#include "storage/state.h"
#include "esp_log.h"
#include "esp_timer.h"

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

static can_t *s_bus[2];
static bvm   *s_vm;
static int    s_current_script_id;

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

/* ---- Berry function ref storage (list at global ".refs") ---- */

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

int berry_call_ref(bvm *vm, int ref)
{
    if (ref < 0) return -1;
    if (!refs_push(vm, ref)) return -1;
    if (be_pcall(vm, 0) != 0) {
        ESP_LOGE(TAG_BB, "ref %d call error: %s", ref, be_tostring(vm, -1));
        be_pop(vm, 2);
        return -2;
    }
    be_pop(vm, 2);
    return 0;
}

/* ---- timer.* (polled from main loop via berry_timer_tick) ---- */

typedef struct {
    bool     in_use;
    int      ref;
    int      script_id;
    uint32_t next_fire_ms;
    uint32_t period_ms;     /* 0 = one-shot */
} timer_entry_t;

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
                ESP_LOGE(TAG_BB, "timer callback error: %s", be_tostring(s_vm, -1));
            }
            be_pop(s_vm, 2);
        }

        if (one_shot) refs_free(s_vm, ref);
    }
}

/* ---- actions.register / actions.invoke ---- */

typedef struct {
    bool in_use;
    char name[ACTION_NAME_MAX];
    int  ref;
    int  script_id;
} action_entry_t;

static action_entry_t s_actions[MAX_ACTIONS];

static int find_action(const char *name)
{
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (s_actions[i].in_use && strcmp(s_actions[i].name, name) == 0) return i;
    }
    return -1;
}

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

static int l_action_invoke(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    berry_action_invoke(be_tostring(vm, 1));
    be_return_nil(vm);
}

static int l_action_list(bvm *vm)
{
    be_newlist(vm);
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (!s_actions[i].in_use) continue;
        be_pushstring(vm, s_actions[i].name);
        be_data_push(vm, -2);
        be_pop(vm, 1);
    }
    be_return(vm);
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
        ESP_LOGE(TAG_BB, "action '%s' error: %s", name, be_tostring(s_vm, -1));
        be_pop(s_vm, 2);
        return -2;
    }
    be_pop(s_vm, 2);
    return 0;
}

/* ---- Per-script cleanup ---- */

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

/* ---- CAN: raw frames, message drafts ---- */

/* can_send_raw(bus:int, id:int, data:bytes) -> nil */
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

/* can_recv_raw(bus:int) -> [id, bytes] | nil */
static int l_can_recv_raw(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    int bus = be_toint(vm, 1);
    can_t *c = get_bus(bus);
    if (!c) be_return_nil(vm);
    twai_message_t rx;
    if (can_rx_pop(c, &rx)) {
        be_getglobal(vm, "list");
        be_newlist(vm);
        be_pushint(vm, (bint)rx.identifier);
        be_data_push(vm, -2);
        be_pop(vm, 1);
        be_pushbytes(vm, rx.data, rx.data_length_code);
        be_data_push(vm, -2);
        be_pop(vm, 1);
        be_call(vm, 1);
        be_pop(vm, 1);
        be_return(vm);
    }
    be_return_nil(vm);
}

/* can_msg_get(msg_id:int [, bus:int]) -> draft
 * Returns the latest received frame as a draft you can edit and send. */
static int l_can_msg_get(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    uint32_t msg_id = (uint32_t)be_toint(vm, 1);
    int bus = (be_top(vm) >= 2 && be_isint(vm, 2)) ? be_toint(vm, 2) : 0;
    can_t *eng = get_bus(bus);
    if (!eng) be_return_nil(vm);

    uint8_t data[8], dlc;
    if (can_read(eng, msg_id, data, &dlc) < 0) be_return_nil(vm);

    be_newobject(vm, "map");
    be_pushstring(vm, "id");      be_pushint(vm, (bint)msg_id);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "bus");     be_pushint(vm, bus);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "data");    be_pushbytes(vm, data, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "dlc");     be_pushint(vm, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_return(vm);
}

/* can_msg_send(draft) -> nil
 * Transmit a message draft. The draft must contain id, bus, data, dlc.
 * Signal encoding and counter/checksum update must be done in Berry
 * before calling this function. */
static int l_can_msg_send(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    if (!be_isinstance(vm, 1)) {
        ESP_LOGW(TAG_BB, "can_msg_send: trying to send empty message");
        be_return_nil(vm);
    }

    be_getmember(vm, 1, "bus");
    int bus = be_toint(vm, -1);
    be_pop(vm, 1);

    be_getmember(vm, 1, "id");
    uint32_t id = (uint32_t)be_toint(vm, -1);
    be_pop(vm, 1);

    be_getmember(vm, 1, "dlc");
    int dlc = be_toint(vm, -1);
    be_pop(vm, 1);

    can_t *eng = get_bus(bus);
    if (!eng) be_return_nil(vm);

    be_getmember(vm, 1, "data");
    size_t len = 0;
    const uint8_t *draft_data = be_tobytes(vm, -1, &len);
    uint8_t data[8];
    memcpy(data, draft_data, len > 8 ? 8 : len);
    be_pop(vm, 1);

    can_send(eng, id, data, (uint8_t)dlc);
    be_return_nil(vm);
}

/* ---- CAN utility functions (pure computation, no DBC) ---- */

/* bit_extract(data:bytes, start_bit:int, bit_length:int, big_endian:bool) -> int */
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
 * Returns a new bytes object with the bits inserted (data is immutable in Berry). */
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

/* signal_decode(data:bytes, start_bit:int, bit_length:int, big_endian:bool,
 *               is_signed:bool, scale:real, offset:real) -> real */
static int l_signal_decode(bvm *vm)
{
    CHECK_ARITY(vm, 7);
    CHECK_TYPE(vm, 1, be_isbytes(vm, 1), "bytes");
    size_t len = 0;
    const uint8_t *data = be_tobytes(vm, 1, &len);
    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    bool is_signed = be_tobool(vm, 5);
    float scale = (float)be_toreal(vm, 6);
    float offset = (float)be_toreal(vm, 7);
    float val = signal_decode(data, start_bit, bit_length, big_endian, is_signed, scale, offset);
    be_pushreal(vm, (breal)val);
    be_return(vm);
}

/* signal_encode(data:bytes, start_bit:int, bit_length:int, big_endian:bool,
 *               is_signed:bool, scale:real, offset:real, value:real) -> bytes */
static int l_signal_encode(bvm *vm)
{
    CHECK_ARITY(vm, 8);
    CHECK_TYPE(vm, 1, be_isbytes(vm, 1), "bytes");
    size_t len = 0;
    const uint8_t *src = be_tobytes(vm, 1, &len);
    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    bool is_signed = be_tobool(vm, 5);
    float scale = (float)be_toreal(vm, 6);
    float offset = (float)be_toreal(vm, 7);
    float value = (float)be_toreal(vm, 8);
    uint8_t buf[8];
    memcpy(buf, src, len > 8 ? 8 : len);
    signal_encode(buf, start_bit, bit_length, big_endian, is_signed, scale, offset, value);
    be_pushbytes(vm, buf, len > 8 ? 8 : len);
    be_return(vm);
}

/* ---- LED ---- */

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

static int l_led_off(bvm *vm)
{
    (void)vm;
    led_rgb_off();
    be_return_nil(vm);
}

/* ---- State (NVS) ---- */

static int l_state_set(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    CHECK_TYPE(vm, 2, be_isstring(vm, 2), "string");
    state_set("script", be_tostring(vm, 1), be_tostring(vm, 2));
    be_return_nil(vm);
}

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

static int l_state_remove(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    state_remove("script", be_tostring(vm, 1));
    be_return_nil(vm);
}

/* ---- millis() helper ---- */

static int l_millis(bvm *vm)
{
    be_pushint(vm, (bint)(esp_timer_get_time() / 1000));
    be_return(vm);
}

/* ---- Registration ---- */

void berry_register_bindings(bvm *vm)
{
    s_vm = vm;

    /* Raw CAN I/O */
    be_regfunc(vm, "can_send_raw",   l_can_send_raw);
    be_regfunc(vm, "can_recv_raw",   l_can_recv_raw);
    be_regfunc(vm, "can_msg_get",    l_can_msg_get);
    be_regfunc(vm, "can_msg_send",   l_can_msg_send);

    /* CAN utility functions (signal encode/decode without DBC) */
    be_regfunc(vm, "bit_extract",    l_bit_extract);
    be_regfunc(vm, "bit_insert",     l_bit_insert);
    be_regfunc(vm, "signal_decode",  l_signal_decode);
    be_regfunc(vm, "signal_encode",  l_signal_encode);

    be_regfunc(vm, "led_set", l_led_set);
    be_regfunc(vm, "led_off", l_led_off);

    be_regfunc(vm, "state_set", l_state_set);
    be_regfunc(vm, "state_get", l_state_get);
    be_regfunc(vm, "state_remove", l_state_remove);

    be_regfunc(vm, "timer_after", l_timer_after);
    be_regfunc(vm, "timer_every", l_timer_every);
    be_regfunc(vm, "timer_cancel", l_timer_cancel);

    be_regfunc(vm, "action_register", l_action_register);
    be_regfunc(vm, "action_invoke",   l_action_invoke);
    be_regfunc(vm, "action_list",     l_action_list);

    be_regfunc(vm, "millis", l_millis);
}
