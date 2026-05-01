#include "scripting/berry_bindings.h"

#include <string.h>

#include "can/can.h"
#include "can/can_driver.h"
#include "led/led_rgb.h"
#include "storage/state.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG_BB = "berry";

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

/* ---- Berry function ref storage (list at global ".refs") ----
 * Each registered callback (can.on, timer, action) stores its function in
 * the .refs list and remembers the index. To revoke, we nil out the slot
 * (the list grows monotonically; we don't compact it). */

static int refs_alloc(bvm *vm, int fn_index)
{
    /* Resolve negative index before we change the stack. */
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
    be_pushint(vm, ref);       /* key: slot index */
    be_pushnil(vm);            /* value: nil to free the slot */
    be_setindex(vm, -3);       /* .refs[ref] = nil */
    be_pop(vm, 3);             /* pop value, key, and .refs */
}

static bool refs_push(bvm *vm, int ref)
{
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) { be_pop(vm, 1); return false; }
    be_pushint(vm, ref);
    be_getindex(vm, -2);          /* stack: [.refs, ref, fn] */
    be_remove(vm, -2);            /* Removes 'ref', leaving [.refs, fn] */
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 2);
        return false;
    }
    /* leave [.refs, fn] on stack — caller will use fn and pop both */
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
    if (!refs_push(vm, ref)) return -1;     /* stack: [.refs, fn] */
    if (be_pcall(vm, 0) != 0) {
        ESP_LOGE(TAG_BB, "ref %d call error: %s", ref, be_tostring(vm, -1));
        be_pop(vm, 2);
        return -2;
    }
    be_pop(vm, 2);
    return 0;
}

/* ---- can.on signal callbacks ---- */

#define MAX_BERRY_CBS 16

typedef struct {
    bool in_use;
    int  ref;
    int  bus;
    int  sig_idx;
    int  script_id;
} cb_entry_t;

static cb_entry_t s_can_cbs[MAX_BERRY_CBS];

static void can_on_trampoline(int sig_idx, float value, float prev, void *ctx)
{
    if (!s_vm) return;
    int ref = (int)(intptr_t)ctx;

    if (!refs_push(s_vm, ref)) return;            /* stack: [.refs, fn] */

    /* Build the signal map argument */
    be_newobject(s_vm, "map");
    be_pushstring(s_vm, "value");
    be_pushreal(s_vm, (breal)value);
    be_data_insert(s_vm, -3);
    be_pop(s_vm, 2);
    be_pushstring(s_vm, "prev");
    be_pushreal(s_vm, (breal)prev);
    be_data_insert(s_vm, -3);
    be_pop(s_vm, 2);
    be_pushstring(s_vm, "sig_idx");
    be_pushint(s_vm, sig_idx);
    be_data_insert(s_vm, -3);
    be_pop(s_vm, 2);

    if (be_pcall(s_vm, 1) != 0) {
        ESP_LOGE(TAG_BB, "can.on callback error: %s", be_tostring(s_vm, -1));
    }
    be_pop(s_vm, 2);   /* pop result + .refs */
}

/* can_on(name:str, fn:function [, bus:int]) -> nil */
static int l_can_on(bvm *vm)
{
    if (be_top(vm) < 2 || !be_isstring(vm, 1) || !be_isfunction(vm, 2)) be_return_nil(vm);
    const char *name = be_tostring(vm, 1);
    int bus = (be_top(vm) >= 3 && be_isint(vm, 3)) ? be_toint(vm, 3) : 0;
    can_t *eng = get_bus(bus);
    if (!eng) be_return_nil(vm);

    int slot = -1;
    for (int i = 0; i < MAX_BERRY_CBS; i++) {
        if (!s_can_cbs[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        ESP_LOGE(TAG_BB, "can.on registry full");
        be_return_nil(vm);
    }

    int ref = refs_alloc(vm, 2);
    if (can_on_change(eng, name, can_on_trampoline,
                      (void *)(intptr_t)ref, s_current_script_id) != 0) {
        refs_free(vm, ref);
        ESP_LOGW(TAG_BB, "can.on: signal '%s' not found on bus %d", name, bus);
        be_return_nil(vm);
    }

    s_can_cbs[slot] = (cb_entry_t){
        .in_use = true, .ref = ref, .bus = bus,
        .sig_idx = -1,  /* not used directly; kept for symmetry */
        .script_id = s_current_script_id,
    };
    be_return_nil(vm);
}

/* ---- timer.* (polled from main loop via berry_timer_tick) ---- */

#define MAX_TIMERS 16

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
    if (be_top(vm) < 2 || !be_isint(vm, 1) || !be_isfunction(vm, 2)) be_return_nil(vm);
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
    if (be_top(vm) < 2 || !be_isint(vm, 1) || !be_isfunction(vm, 2)) be_return_nil(vm);
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
    if (be_top(vm) >= 1 && be_isint(vm, 1)) {
        int h = be_toint(vm, 1);
        if (h >= 0 && h < MAX_TIMERS && s_timers[h].in_use) {
            refs_free(vm, s_timers[h].ref);
            s_timers[h].in_use = false;
        }
    }
    be_return_nil(vm);
}

void berry_timer_tick(uint32_t now_ms);    /* fwd decl */

void berry_timer_tick(uint32_t now_ms)
{
    if (!s_vm) return;
    for (int i = 0; i < MAX_TIMERS; i++) {
        timer_entry_t *t = &s_timers[i];
        if (!t->in_use) continue;
        /* unsigned comparison handles wrap */
        if ((int32_t)(now_ms - t->next_fire_ms) < 0) continue;

        int ref = t->ref;
        bool one_shot = (t->period_ms == 0);
        if (one_shot) {
            t->in_use = false;       /* free slot before firing */
        } else {
            t->next_fire_ms = now_ms + t->period_ms;
        }

        if (refs_push(s_vm, ref)) {
            if (be_pcall(s_vm, 0) != 0) {
                ESP_LOGE(TAG_BB, "timer callback error: %s", be_tostring(s_vm, -1));
            }
            be_pop(s_vm, 2);   /* pop result + .refs */
        }

        if (one_shot) refs_free(s_vm, ref);
    }
}

/* ---- actions.register / actions.invoke ---- */

#define MAX_ACTIONS 32
#define ACTION_NAME_MAX 40

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

/* actions.register(name:str, fn:function) -> nil */
static int l_action_register(bvm *vm)
{
    if (be_top(vm) < 2 || !be_isstring(vm, 1) || !be_isfunction(vm, 2)) be_return_nil(vm);
    const char *name = be_tostring(vm, 1);

    /* Replace existing same-name action (re-registration). */
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

/* actions.invoke(name:str) -> nil (calls action's fn, no args) */
static int l_action_invoke(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
        berry_action_invoke(be_tostring(vm, 1));
    }
    be_return_nil(vm);
}

/* actions.list() -> [name, …] */
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
    if (script_id == 0) return;   /* never auto-clean unscripted (C) refs */

    /* Cancel timers */
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (s_timers[i].in_use && s_timers[i].script_id == script_id) {
            refs_free(s_vm, s_timers[i].ref);
            s_timers[i].in_use = false;
        }
    }

    /* Drop can.on callbacks */
    for (int i = 0; i < MAX_BERRY_CBS; i++) {
        if (s_can_cbs[i].in_use && s_can_cbs[i].script_id == script_id) {
            refs_free(s_vm, s_can_cbs[i].ref);
            s_can_cbs[i].in_use = false;
        }
    }
    /* Tell each can_t to drop matching tags */
    for (int b = 0; b < 2; b++) {
        if (s_bus[b]) can_off_by_tag(s_bus[b], script_id);
    }

    /* Drop actions */
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (s_actions[i].in_use && s_actions[i].script_id == script_id) {
            refs_free(s_vm, s_actions[i].ref);
            s_actions[i].in_use = false;
        }
    }
}

/* ---- Raw CAN, signals query, draft/encode/send (unchanged from before) ---- */

static int l_can_send(bvm *vm)
{
    int argc = be_top(vm);
    if (argc >= 3 && be_isint(vm, 1) && be_isint(vm, 2) && be_isbytes(vm, 3)) {
        int bus = be_toint(vm, 1);
        uint32_t id = (uint32_t)be_toint(vm, 2);
        size_t len = 0;
        const uint8_t *data = be_tobytes(vm, 3, &len);
        if (len > 8) len = 8;
        can_bus_send(bus, id, data, (uint8_t)len, 50);
    }
    be_return_nil(vm);
}

static int l_can_receive(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isint(vm, 1)) {
        int bus = be_toint(vm, 1);
        can_t *c = get_bus(bus);
        if (!c) be_return_nil(vm);
        twai_message_t rx;
        if (can_rx_pop(c, &rx)) {
            be_newlist(vm);
            be_pushint(vm, (bint)rx.identifier);
            be_data_push(vm, -2);
            be_pop(vm, 1);
            be_pushbytes(vm, rx.data, rx.data_length_code);
            be_data_push(vm, -2);
            be_pop(vm, 1);
            be_return(vm);
        }
    }
    be_return_nil(vm);
}

static int l_can_signal(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
        const char *name = be_tostring(vm, 1);
        int bus = (be_top(vm) >= 2 && be_isint(vm, 2)) ? be_toint(vm, 2) : 0;
        can_t *eng = get_bus(bus);
        if (!eng) be_return_nil(vm);

        const signal_state_t *e = can_signal(eng, name);
        if (e) {
            be_newobject(vm, "map");
            be_pushstring(vm, "value");
            be_pushreal(vm, (breal)e->value);
            be_data_insert(vm, -3);
            be_pop(vm, 2);
            be_pushstring(vm, "prev");
            be_pushreal(vm, (breal)e->prev);
            be_data_insert(vm, -3);
            be_pop(vm, 2);
            be_pushstring(vm, "changed");
            be_pushbool(vm, e->changed);
            be_data_insert(vm, -3);
            be_pop(vm, 2);
            be_return(vm);
        }
    }
    be_return_nil(vm);
}

static int l_can_msg_draft(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isint(vm, 1)) {
        uint32_t msg_id = (uint32_t)be_toint(vm, 1);
        int bus = (be_top(vm) >= 2 && be_isint(vm, 2)) ? be_toint(vm, 2) : 0;
        can_t *eng = get_bus(bus);
        if (!eng) be_return_nil(vm);

        uint8_t data[8], dlc;
        int idx = can_draft(eng, msg_id, data, &dlc);
        if (idx < 0) be_return_nil(vm);

        be_newobject(vm, "map");
        be_pushstring(vm, "id");      be_pushint(vm, (bint)msg_id);
        be_data_insert(vm, -3); be_pop(vm, 2);
        be_pushstring(vm, "bus");     be_pushint(vm, bus);
        be_data_insert(vm, -3); be_pop(vm, 2);
        be_pushstring(vm, "data");    be_pushbytes(vm, data, dlc);
        be_data_insert(vm, -3); be_pop(vm, 2);
        be_pushstring(vm, "dlc");     be_pushint(vm, dlc);
        be_data_insert(vm, -3); be_pop(vm, 2);
        be_pushstring(vm, "_idx");    be_pushint(vm, idx);
        be_data_insert(vm, -3); be_pop(vm, 2);
        be_return(vm);
    }
    be_return_nil(vm);
}

static int l_can_msg_set(bvm *vm)
{
    if (be_top(vm) >= 3 && be_isinstance(vm, 1) &&
        be_isstring(vm, 2) && (be_isreal(vm, 3) || be_isint(vm, 3))) {

        be_getmember(vm, 1, "bus");
        int bus = be_toint(vm, -1);
        be_pop(vm, 1);

        can_t *eng = get_bus(bus);
        if (!eng) be_return_nil(vm);

        const char *sig_name = be_tostring(vm, 2);
        float val = be_isreal(vm, 3) ? (float)be_toreal(vm, 3) : (float)be_toint(vm, 3);

        int si = dbc_find_signal(&eng->dbc, sig_name);
        if (si < 0) be_return_nil(vm);

        be_getmember(vm, 1, "data");
        size_t len = 0;
        const uint8_t *old_data = be_tobytes(vm, -1, &len);
        uint8_t data[8];
        memcpy(data, old_data, len > 8 ? 8 : len);
        be_pop(vm, 1);

        can_encode(eng, si, data, val);

        be_pushstring(vm, "data");
        be_pushbytes(vm, data, len > 8 ? 8 : len);
        be_setindex(vm, 1);
        be_pop(vm, 2);
    }
    be_return_nil(vm);
}

static int l_can_msg_send(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isinstance(vm, 1)) {
        be_getmember(vm, 1, "bus");
        int bus = be_toint(vm, -1);
        be_pop(vm, 1);

        be_getmember(vm, 1, "_idx");
        int idx = be_toint(vm, -1);
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

        can_send(eng, idx, data, (uint8_t)dlc);
    }
    be_return_nil(vm);
}

static int l_led_set(bvm *vm)
{
    if (be_top(vm) >= 3) {
        uint8_t r = (uint8_t)be_toint(vm, 1);
        uint8_t g = (uint8_t)be_toint(vm, 2);
        uint8_t b = (uint8_t)be_toint(vm, 3);
        led_rgb_set(r, g, b);
    }
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
    if (be_top(vm) >= 2 && be_isstring(vm, 1) && be_isstring(vm, 2)) {
        state_set("script", be_tostring(vm, 1), be_tostring(vm, 2));
    }
    be_return_nil(vm);
}

static int l_state_get(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
        static char buf[256];
        if (state_get("script", be_tostring(vm, 1), buf, sizeof(buf)) == ESP_OK) {
            be_pushstring(vm, buf);
            be_return(vm);
        }
        if (be_top(vm) >= 2) {
            be_pushvalue(vm, 2);
            be_return(vm);
        }
    }
    be_return_nil(vm);
}

static int l_state_remove(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
        state_remove("script", be_tostring(vm, 1));
    }
    be_return_nil(vm);
}

/* berry_set_log_handler() and berry_log_push() live in the Berry port
 * (components/berry/port/be_port.c) so they can intercept Berry's built-in
 * print(), which routes through be_writebuffer there. */

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

    be_regfunc(vm, "can_send", l_can_send);
    be_regfunc(vm, "can_receive", l_can_receive);
    be_regfunc(vm, "can_signal", l_can_signal);
    be_regfunc(vm, "can_on", l_can_on);
    be_regfunc(vm, "can_msg_draft", l_can_msg_draft);
    be_regfunc(vm, "can_msg_set", l_can_msg_set);
    be_regfunc(vm, "can_msg_send", l_can_msg_send);

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
