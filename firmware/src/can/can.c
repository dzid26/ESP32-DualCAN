#include "can/can.h"
#include "can/can_driver.h"

#include <stdlib.h>
#include <string.h>

#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "can";

int can_init(can_t *c, int bus_id, const uint8_t *dbc_blob, size_t dbc_len,
             tx_finalize_fn_t finalize)
{
    memset(c, 0, sizeof(*c));
    c->bus_id = bus_id;
    c->finalize_fn = finalize;

    if (!dbc_blob || dbc_len == 0) {
        c->loaded = false;
        return 0;
    }

    if (dbc_load(&c->dbc, dbc_blob, dbc_len) != 0) {
        ESP_LOGE(TAG, "bus%d: DBC load failed", bus_id);
        return -1;
    }

    c->signals = calloc(c->dbc.hdr->sig_count, sizeof(signal_state_t));
    c->messages = calloc(c->dbc.hdr->msg_count, sizeof(can_msg_state_t));
    if (!c->signals || !c->messages) {
        free(c->signals);
        free(c->messages);
        return -1;
    }

    rate_limiter_init(&c->rate_limiter, RATE_LIMIT_DEFAULT_HZ);

    c->loaded = true;
    ESP_LOGI(TAG, "bus%d: %d messages, %d signals",
             bus_id, c->dbc.hdr->msg_count, c->dbc.hdr->sig_count);
    return 0;
}

void can_free(can_t *c)
{
    free(c->signals);
    free(c->messages);
    memset(c, 0, sizeof(*c));
}

static int msg_index(const can_t *c, uint32_t id)
{
    const dbc_msg_t *m = dbc_find_msg(&c->dbc, id);
    return m ? (int)(m - c->dbc.msgs) : -1;
}

void can_poll(can_t *c, uint32_t now_ms)
{
    twai_message_t rx;
    while (can_bus_receive(c->bus_id, &rx, 0) == ESP_OK) {
        if (!c->loaded) continue;

        int mi = msg_index(c, rx.identifier);
        if (mi >= 0) {
            can_msg_state_t *ms = &c->messages[mi];
            memcpy(ms->data, rx.data, rx.data_length_code);
            ms->dlc = rx.data_length_code;
            ms->received = true;
        }

        const dbc_msg_t *msg = dbc_find_msg(&c->dbc, rx.identifier);
        if (msg) {
            msg_decode_frame(&c->dbc, msg, rx.data, c->signals, now_ms);

            for (int i = 0; i < msg->sig_count; i++) {
                uint16_t si = msg->sig_start + i;
                signal_state_t *s = &c->signals[si];
                if (s->changed) {
                    for (int cb = 0; cb < c->cb_count; cb++) {
                        if (c->callbacks[cb].sig_idx == si) {
                            c->callbacks[cb].cb(si, s->value, s->prev,
                                                c->callbacks[cb].ctx);
                        }
                    }
                }
            }
        }
    }
}

int can_on_change(can_t *c, const char *sig_name, can_signal_cb_t cb, void *ctx)
{
    if (!c->loaded) return -1;
    int si = dbc_find_signal(&c->dbc, sig_name);
    if (si < 0 || c->cb_count >= CAN_MAX_CALLBACKS) return -1;
    c->callbacks[c->cb_count++] = (typeof(c->callbacks[0])){
        .sig_idx = si, .cb = cb, .ctx = ctx
    };
    return 0;
}

const signal_state_t *can_signal(const can_t *c, const char *name)
{
    if (!c->loaded) return NULL;
    int si = dbc_find_signal(&c->dbc, name);
    return (si >= 0) ? &c->signals[si] : NULL;
}

int can_draft(can_t *c, uint32_t msg_id, uint8_t *out_data, uint8_t *out_dlc)
{
    if (!c->loaded) return -1;
    int mi = msg_index(c, msg_id);
    if (mi < 0) return -1;
    const can_msg_state_t *ms = &c->messages[mi];
    memcpy(out_data, ms->data, 8);
    *out_dlc = ms->received ? ms->dlc : c->dbc.msgs[mi].dlc;
    return mi;
}

void can_encode(const can_t *c, int sig_idx, uint8_t *data, float value)
{
    if (!c->loaded || sig_idx < 0 || sig_idx >= c->dbc.hdr->sig_count) return;
    msg_encode_signal(&c->dbc.sigs[sig_idx], data, value);
}

int can_send(can_t *c, int msg_idx, uint8_t *data, uint8_t dlc)
{
    if (!c->loaded) return -1;

    uint32_t id = c->dbc.msgs[msg_idx].id;
    uint32_t now_us = (uint32_t)(esp_timer_get_time());
    if (!rate_limiter_allow(&c->rate_limiter, id, now_us)) {
        ESP_LOGD(TAG, "bus%d: rate-limited id=0x%03" PRIx32, c->bus_id, id);
        return -1;
    }

    msg_finalize_tx(&c->dbc, msg_idx, &c->messages[msg_idx].counter,
                    data, c->finalize_fn);

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
