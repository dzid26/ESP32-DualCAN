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

/* ---- Ring buffer helpers ----
 *
 * Each cache slot is a depth-3 ring buffer.  `ring_latest` only returns
 * the most recently pushed frame — the older 2 frames are never read.
 * They are reserved for future sliding-window or burst analysis use. */

static void ring_push(msg_ring_t *ring, const twai_message_t *msg)
{
    ring->frames[ring->head] = *msg;
    ring->head = (ring->head + 1) % CAN_FRAME_CACHE;
    if (ring->count < CAN_FRAME_CACHE) ring->count++;
}

static const twai_message_t *ring_latest(const msg_ring_t *ring)
{
    if (ring->count == 0) return NULL;
    uint8_t pos = (ring->head + CAN_FRAME_CACHE - 1) % CAN_FRAME_CACHE;
    return &ring->frames[pos];
}

/* ---- Cache helpers ---- */

static int cache_find(msg_ring_t *cache, uint32_t id)
{
    for (int i = 0; i < CAN_MSG_CACHE_MAX; i++) {
        if (cache[i].frames[0].identifier == id) return i;
    }
    return -1;
}

static int cache_alloc(msg_ring_t *cache, uint32_t msg_id)
{
    int i = cache_find(cache, msg_id);
    if (i >= 0) return i;  /* already registered */
    i = cache_find(cache, 0);  /* find free slot */
    if (i < 0) return -1;
    cache[i].frames[0].identifier = msg_id;
    cache[i].head = 0;
    cache[i].count = 0;
    return i;
}

static void cache_update(msg_ring_t *cache, const twai_message_t *rx)
{
    int i = cache_find(cache, rx->identifier);
    if (i < 0) return;
    ring_push(&cache[i], rx);
}

static const twai_message_t *cache_read(msg_ring_t *cache, uint32_t msg_id)
{
    int i = cache_alloc(cache, msg_id);
        if (i < 0) return NULL;
    return ring_latest(&cache[i]);
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

int can_poll(can_t *c, uint32_t now_ms)
{
    can_bus_off_recover(c->bus_id);
    int rx_count = 0;
    twai_message_t rx;
    while (can_bus_receive(c->bus_id, &rx, 0) == ESP_OK) {
        rx_count++;
        s_rate_count[c->bus_id]++;
        s_last_rx_ms[c->bus_id] = now_ms;
        if (s_raw_observer) s_raw_observer(c->bus_id, &rx, now_ms);

        /* Update frame cache — only if a script has registered this ID */
        cache_update(c->msg_cache, &rx);
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
    const twai_message_t *msg = cache_read(c->msg_cache, msg_id);
    if (!msg) return -1;
    memcpy(out_data, msg->data, 8);
    *out_dlc = msg->data_length_code;
    return 0;
}

int can_send(can_t *c, uint32_t id, const uint8_t *data, uint8_t dlc)
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
