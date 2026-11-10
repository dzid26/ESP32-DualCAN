/*
 * Host stubs for the Berry suites.
 *
 * esp_timer is the only hardware symbol the registry needs. The scripting
 * bindings also pull in the CAN, LED and NVS surfaces, so those live here as
 * controllable test doubles: the suite pushes frames into can_read()'s cache
 * and reads back what can_send() was asked to transmit.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "can/can.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "led/led_rgb.h"
#include "storage/state.h"

static int64_t g_now_us;

void stub_set_now_ms(unsigned ms) { g_now_us = (int64_t)ms * 1000; }

int64_t esp_timer_get_time(void) { return g_now_us; }

/* ---- CAN double ---- */

#define STUB_CAN_IDS 32

typedef struct {
    uint32_t id;
    bool     present;
    uint8_t  data[8];
    uint8_t  dlc;
    int      sends;
    uint8_t  last_data[8];
    uint8_t  last_dlc;
} stub_can_entry_t;

static stub_can_entry_t s_can[2][STUB_CAN_IDS];

static stub_can_entry_t *entry_for(int bus, uint32_t id, bool create)
{
    if (bus < 0 || bus > 1) return NULL;
    for (int i = 0; i < STUB_CAN_IDS; i++) {
        if (s_can[bus][i].id == id && s_can[bus][i].present) return &s_can[bus][i];
        if (s_can[bus][i].present) continue;
        /* empty slot */
        if (!create) return NULL;
        s_can[bus][i].id = id;
        s_can[bus][i].present = true;
        s_can[bus][i].dlc = 0;
        memset(s_can[bus][i].data, 0, 8);
        memset(s_can[bus][i].last_data, 0, 8);
        s_can[bus][i].sends = 0;
        s_can[bus][i].last_dlc = 0;
        return &s_can[bus][i];
    }
    return NULL;
}

void stub_can_reset(void)
{
    memset(s_can, 0, sizeof(s_can));
}

void stub_can_push(int bus, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    stub_can_entry_t *e = entry_for(bus, id, true);
    if (!e) return;
    if (dlc > 8) dlc = 8;
    memcpy(e->data, data, dlc);
    memset(e->data + dlc, 0, 8 - dlc);
    e->dlc = dlc;
}

/* can_read() registers the ID the first time a script asks for it, exactly
 * like the real cache. A pushed frame is returned until the next push. */
int can_read(can_t *c, uint32_t msg_id, uint8_t *out_data, uint8_t *out_dlc,
             uint32_t timeout_ms)
{
    (void)timeout_ms;
    int bus = c ? c->bus_id : 0;
    stub_can_entry_t *e = entry_for(bus, msg_id, true);
    if (!e || e->dlc == 0) return -1;
    *out_dlc = e->dlc;
    memcpy(out_data, e->data, e->dlc);
    return 0;
}

int can_send(can_t *c, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    int bus = c ? c->bus_id : 0;
    stub_can_entry_t *e = entry_for(bus, id, true);
    if (!e) return -1;
    if (dlc > 8) dlc = 8;
    e->sends++;
    e->last_dlc = dlc;
    memcpy(e->last_data, data, dlc);
    return 0;
}

int stub_can_send_count(int bus, uint32_t id)
{
    stub_can_entry_t *e = entry_for(bus, id, false);
    return e ? e->sends : 0;
}

int stub_can_total_sends(int bus)
{
    int total = 0;
    if (bus < 0 || bus > 1) return 0;
    for (int i = 0; i < STUB_CAN_IDS; i++) {
        if (s_can[bus][i].present) total += s_can[bus][i].sends;
    }
    return total;
}

uint8_t stub_can_last_dlc(int bus, uint32_t id)
{
    stub_can_entry_t *e = entry_for(bus, id, false);
    return e ? e->last_dlc : 0;
}

bool stub_can_last_data(int bus, uint32_t id, uint8_t out[8])
{
    stub_can_entry_t *e = entry_for(bus, id, false);
    if (!e) return false;
    memcpy(out, e->last_data, 8);
    return true;
}

/* ---- LED / NVS doubles ---- */

esp_err_t led_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    (void)r; (void)g; (void)b;
    return ESP_OK;
}

esp_err_t led_rgb_off(void) { return ESP_OK; }

esp_err_t state_set(const char *ns, const char *key, const char *value)
{
    (void)ns; (void)key; (void)value;
    return ESP_OK;
}

esp_err_t state_get(const char *ns, const char *key, char *buf, size_t buf_size)
{
    (void)ns; (void)key; (void)buf; (void)buf_size;
    return ESP_FAIL;
}

esp_err_t state_remove(const char *ns, const char *key)
{
    (void)ns; (void)key;
    return ESP_OK;
}
