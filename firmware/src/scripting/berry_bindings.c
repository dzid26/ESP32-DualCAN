#include "scripting/berry_bindings.h"

#include <string.h>

#include "can/can_bus.h"
#include "led/led_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"

static can_engine_t *s_eng[2];
static bvm *s_vm;

void berry_set_engines(can_engine_t *eng0, can_engine_t *eng1)
{
    s_eng[0] = eng0;
    s_eng[1] = eng1;
}

static can_engine_t *get_engine(int bus)
{
    if (bus < 0 || bus > 1 || !s_eng[bus]) return NULL;
    return s_eng[bus];
}

/* ---- Raw CAN bindings (kept from phase 0) ---- */

/* can_send(bus:int, id:int, data:bytes) -> nil */
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

/* can_receive(bus:int) -> nil | list [id:int, data:bytes] */
static int l_can_receive(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isint(vm, 1)) {
        int bus = be_toint(vm, 1);
        twai_message_t rx;
        if (can_bus_receive(bus, &rx, 0) == ESP_OK) {
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

/* ---- Decoded signal bindings ---- */

/* can_signal(name:str [, bus:int]) -> map {value, prev, age_ms, changed} | nil */
static int l_can_signal(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
        const char *name = be_tostring(vm, 1);
        int bus = (be_top(vm) >= 2 && be_isint(vm, 2)) ? be_toint(vm, 2) : 0;
        can_engine_t *eng = get_engine(bus);
        if (!eng) { be_return_nil(vm); }

        const sig_cache_entry_t *e = can_engine_signal(eng, name);
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
            be_pushstring(vm, "age_ms");
            be_pushint(vm, 0); /* TODO: compute from now - last_rx_ms */
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

/* Berry callback trampoline for can.on() — stores Berry function refs.
 * We keep a simple array of (sig_idx, berry_ref) and call them from C. */
#define MAX_BERRY_CBS 16

typedef struct {
    int sig_idx;
    int berry_ref;   /* Berry reference to the callback function */
    int bus;
} berry_cb_entry_t;

static berry_cb_entry_t s_berry_cbs[MAX_BERRY_CBS];
static int s_berry_cb_count = 0;

static void berry_signal_trampoline(int sig_idx, float value, float prev, void *ctx)
{
    if (!s_vm) return;
    int ref = (int)(intptr_t)ctx;

    be_getglobal(s_vm, ".refs");
    if (be_isnil(s_vm, -1)) { be_pop(s_vm, 1); return; }
    be_pushint(s_vm, ref);
    be_getindex(s_vm, -2);

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

    if (be_pcall(s_vm, 1) != 0) {
        ESP_LOGE("berry", "on_change callback error: %s", be_tostring(s_vm, -1));
        be_pop(s_vm, 1);
    }
    be_pop(s_vm, 2); /* pop result + .refs table */
}

/* can_on(name:str, fn:function [, bus:int]) -> nil */
static int l_can_on(bvm *vm)
{
    if (be_top(vm) >= 2 && be_isstring(vm, 1) && be_isfunction(vm, 2)) {
        const char *name = be_tostring(vm, 1);
        int bus = (be_top(vm) >= 3 && be_isint(vm, 3)) ? be_toint(vm, 3) : 0;
        can_engine_t *eng = get_engine(bus);
        if (!eng || s_berry_cb_count >= MAX_BERRY_CBS) { be_return_nil(vm); }

        /* Store the function in a global refs list so it won't be GC'd. */
        be_getglobal(vm, ".refs");
        if (be_isnil(vm, -1)) {
            be_pop(vm, 1);
            be_newlist(vm);
            be_setglobal(vm, ".refs");
            be_getglobal(vm, ".refs");
        }
        be_pushvalue(vm, 2); /* push the callback function */
        be_data_push(vm, -2); /* append to .refs list */
        be_pop(vm, 1);
        int ref = be_data_size(vm, -1) - 1;
        be_pop(vm, 1);

        can_engine_on_change(eng, name, berry_signal_trampoline,
                             (void *)(intptr_t)ref);
        s_berry_cb_count++;
    }
    be_return_nil(vm);
}

/* ---- Message draft/encode/send bindings ---- */

/* can_msg_draft(id:int [, bus:int]) -> map {id, bus, data:bytes, dlc, _idx} | nil */
static int l_can_msg_draft(bvm *vm)
{
    if (be_top(vm) >= 1 && be_isint(vm, 1)) {
        uint32_t msg_id = (uint32_t)be_toint(vm, 1);
        int bus = (be_top(vm) >= 2 && be_isint(vm, 2)) ? be_toint(vm, 2) : 0;
        can_engine_t *eng = get_engine(bus);
        if (!eng) { be_return_nil(vm); }

        uint8_t data[8], dlc;
        int idx = can_engine_draft(eng, msg_id, data, &dlc);
        if (idx < 0) { be_return_nil(vm); }

        be_newobject(vm, "map");
        be_pushstring(vm, "id");
        be_pushint(vm, (bint)msg_id);
        be_data_insert(vm, -3);
        be_pop(vm, 2);
        be_pushstring(vm, "bus");
        be_pushint(vm, bus);
        be_data_insert(vm, -3);
        be_pop(vm, 2);
        be_pushstring(vm, "data");
        be_pushbytes(vm, data, dlc);
        be_data_insert(vm, -3);
        be_pop(vm, 2);
        be_pushstring(vm, "dlc");
        be_pushint(vm, dlc);
        be_data_insert(vm, -3);
        be_pop(vm, 2);
        be_pushstring(vm, "_idx");
        be_pushint(vm, idx);
        be_data_insert(vm, -3);
        be_pop(vm, 2);
        be_return(vm);
    }
    be_return_nil(vm);
}

/* can_msg_set(draft:map, signal_name:str, value:real) -> nil */
static int l_can_msg_set(bvm *vm)
{
    if (be_top(vm) >= 3 && be_isinstance(vm, 1) &&
        be_isstring(vm, 2) && (be_isreal(vm, 3) || be_isint(vm, 3))) {

        /* Get bus from draft */
        be_getmember(vm, 1, "bus");
        int bus = be_toint(vm, -1);
        be_pop(vm, 1);

        can_engine_t *eng = get_engine(bus);
        if (!eng) { be_return_nil(vm); }

        const char *sig_name = be_tostring(vm, 2);
        float val = be_isreal(vm, 3) ? (float)be_toreal(vm, 3) : (float)be_toint(vm, 3);

        int si = dbc_find_signal(&eng->dbc, sig_name);
        if (si < 0) { be_return_nil(vm); }

        /* Get the data bytes from the draft, modify, put back */
        be_getmember(vm, 1, "data");
        size_t len = 0;
        const uint8_t *old_data = be_tobytes(vm, -1, &len);
        uint8_t data[8];
        memcpy(data, old_data, len > 8 ? 8 : len);
        be_pop(vm, 1);

        can_engine_encode(eng, si, data, val);

        be_pushstring(vm, "data");
        be_pushbytes(vm, data, len > 8 ? 8 : len);
        be_setindex(vm, 1);
        be_pop(vm, 2);
    }
    be_return_nil(vm);
}

/* can_msg_send(draft:map) -> nil */
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

        can_engine_t *eng = get_engine(bus);
        if (!eng) { be_return_nil(vm); }

        be_getmember(vm, 1, "data");
        size_t len = 0;
        const uint8_t *draft_data = be_tobytes(vm, -1, &len);
        uint8_t data[8];
        memcpy(data, draft_data, len > 8 ? 8 : len);
        be_pop(vm, 1);

        can_engine_send(eng, idx, data, (uint8_t)dlc);
    }
    be_return_nil(vm);
}

/* ---- LED bindings ---- */

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

/* ---- Registration ---- */

void berry_register_bindings(bvm *vm)
{
    s_vm = vm;

    /* Raw CAN */
    be_regfunc(vm, "can_send", l_can_send);
    be_regfunc(vm, "can_receive", l_can_receive);

    /* Decoded signals */
    be_regfunc(vm, "can_signal", l_can_signal);
    be_regfunc(vm, "can_on", l_can_on);

    /* Message draft/send */
    be_regfunc(vm, "can_msg_draft", l_can_msg_draft);
    be_regfunc(vm, "can_msg_set", l_can_msg_set);
    be_regfunc(vm, "can_msg_send", l_can_msg_send);

    /* LED */
    be_regfunc(vm, "led_set", l_led_set);
    be_regfunc(vm, "led_off", l_led_off);
}
