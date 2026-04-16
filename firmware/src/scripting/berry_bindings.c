#include "scripting/berry_bindings.h"

#include <string.h>

#include "can/can_bus.h"
#include "led/led_rgb.h"
#include "esp_log.h"

/* ---- CAN bindings ---- */

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
            be_newlist(vm);                              /* stack: list */
            be_pushint(vm, (bint)rx.identifier);         /* stack: list, id */
            be_data_push(vm, -2);                        /* list.push(id) */
            be_pop(vm, 1);                               /* stack: list */
            be_pushbytes(vm, rx.data, rx.data_length_code); /* stack: list, data */
            be_data_push(vm, -2);                        /* list.push(data) */
            be_pop(vm, 1);                               /* stack: list */
            be_return(vm);
        }
    }
    be_return_nil(vm);
}

/* ---- LED bindings ---- */

/* led_set(r:int, g:int, b:int) -> nil */
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

/* led_off() -> nil */
static int l_led_off(bvm *vm)
{
    (void)vm;
    led_rgb_off();
    be_return_nil(vm);
}

/* ---- Registration ---- */

void berry_register_bindings(bvm *vm)
{
    be_regfunc(vm, "can_send", l_can_send);
    be_regfunc(vm, "can_receive", l_can_receive);
    be_regfunc(vm, "led_set", l_led_set);
    be_regfunc(vm, "led_off", l_led_off);
}
