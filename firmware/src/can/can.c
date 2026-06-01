#include "can/can.h"
#include "can/can_driver.h"

#include <stdlib.h>
#include <string.h>

#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "can";

static can_raw_observer_t s_raw_observer;
static uint32_t           s_last_rx_ms[CAN_BUS_COUNT];

/* Rolling RX rate, recomputed on each can_poll() once the window elapses. */
#define RATE_WINDOW_MS 1000U
static uint32_t s_rate_window_start[CAN_BUS_COUNT];
static uint32_t s_rate_count[CAN_BUS_COUNT];
static uint16_t s_rx_rate[CAN_BUS_COUNT];

void can_set_raw_observer(can_raw_observer_t cb) { s_raw_observer = cb; }

uint32_t can_last_rx_ms(int bus_id)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT) return 0;
    return s_last_rx_ms[bus_id];
}

uint16_t can_bus_rx_rate(int bus_id)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT) return 0;
    return s_rx_rate[bus_id];
}

static int cache_idx(uint32_t id)
{
    return (int)(id % CAN_FRAME_CACHE);
}

int can_init(can_t *c, int bus_id)
{
    memset(c, 0, sizeof(*c));
    c->bus_id = bus_id;
    rate_limiter_init(&c->rate_limiter, RATE_LIMIT_DEFAULT_HZ);
    c->loaded = true;
    ESP_LOGI(TAG, "bus%d: initialized (no DBC — scripts handle signal processing)", bus_id);
    return 0;
}

void can_free(can_t *c)
{
    memset(c, 0, sizeof(*c));
}

static void rx_buf_push(can_t *c, const twai_message_t *frame)
{
    if (c->rx_count >= CAN_RX_BUF) {
        c->rx_head = (c->rx_head + 1) % CAN_RX_BUF;
        c->rx_count--;
    }
    uint8_t tail = (c->rx_head + c->rx_count) % CAN_RX_BUF;
    c->rx_buf[tail] = *frame;
    c->rx_count++;
}

bool can_rx_pop(can_t *c, twai_message_t *out)
{
    if (c->rx_count == 0) return false;
    *out = c->rx_buf[c->rx_head];
    c->rx_head = (c->rx_head + 1) % CAN_RX_BUF;
    c->rx_count--;
    return true;
}

int can_poll(can_t *c, uint32_t now_ms)
{
    can_bus_off_recover(c->bus_id);
    int rx_count = 0;
    twai_message_t rx;
    while (can_bus_receive(c->bus_id, &rx, 0) == ESP_OK) {
        rx_count++;
        s_rate_count[c->bus_id]++;
        s_last_rx_ms[c->bus_id] = now_ms;
        rx_buf_push(c, &rx);
        if (s_raw_observer) s_raw_observer(c->bus_id, &rx, now_ms);

        /* Update frame cache */
        int i = cache_idx(rx.identifier);
        c->frames[i].id = rx.identifier;
        memcpy(c->frames[i].data, rx.data, rx.data_length_code);
        c->frames[i].dlc = rx.data_length_code;
        c->frames[i].received = true;
    }

    /* Roll the rate window */
    if (s_rate_window_start[c->bus_id] == 0) {
        s_rate_window_start[c->bus_id] = now_ms;
    } else {
        uint32_t elapsed = now_ms - s_rate_window_start[c->bus_id];
        if (elapsed >= RATE_WINDOW_MS) {
            uint32_t rate = (s_rate_count[c->bus_id] * 1000U) / elapsed;
            s_rx_rate[c->bus_id] = rate > UINT16_MAX ? UINT16_MAX : (uint16_t)rate;
            s_rate_count[c->bus_id] = 0;
            s_rate_window_start[c->bus_id] = now_ms;
        }
    }
    return rx_count;
}

int can_read(can_t *c, uint32_t msg_id, uint8_t *out_data, uint8_t *out_dlc)
{
    int i = cache_idx(msg_id);
    cached_frame_t *f = &c->frames[i];
    if (!f->received || f->id != msg_id) return -1;
    memcpy(out_data, f->data, 8);
    *out_dlc = f->dlc;
    return i;
}

int can_send(can_t *c, uint32_t id, uint8_t *data, uint8_t dlc)
{
    uint32_t now_us = (uint32_t)(esp_timer_get_time());
    if (!rate_limiter_allow(&c->rate_limiter, id, now_us)) {
        ESP_LOGD(TAG, "bus%d: rate-limited id=0x%03" PRIx32, c->bus_id, id);
        return -1;
    }

    if (c->sim_mode) {
        ESP_LOGI(TAG, "SIM bus%d tx id=0x%03" PRIx32 " dlc=%d", c->bus_id, id, dlc);
        return 0;
    }
    return can_bus_send(c->bus_id, id, data, dlc, 50);
}

void can_set_sim_mode(can_t *c, bool enabled)
{
    c->sim_mode = enabled;
    ESP_LOGI(TAG, "bus%d: simulation mode %s", c->bus_id, enabled ? "ON" : "OFF");
}
