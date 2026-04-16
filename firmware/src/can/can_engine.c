#include "can/can_engine.h"
#include "can/can_bus.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "can_eng";

int can_engine_init(can_engine_t *eng, int bus_id,
                    const uint8_t *dbc_blob, size_t dbc_len)
{
    memset(eng, 0, sizeof(*eng));
    eng->bus_id = bus_id;

    if (!dbc_blob || dbc_len == 0) {
        eng->loaded = false;
        return 0;
    }

    if (dbc_load(&eng->dbc, dbc_blob, dbc_len) != 0) {
        ESP_LOGE(TAG, "bus%d: DBC load failed", bus_id);
        return -1;
    }

    if (sig_cache_init(&eng->cache, eng->dbc.hdr->sig_count) != 0) {
        return -1;
    }

    if (msg_mirror_init(&eng->mirror, &eng->dbc) != 0) {
        sig_cache_free(&eng->cache);
        return -1;
    }

    eng->loaded = true;
    ESP_LOGI(TAG, "bus%d: loaded DBC with %d messages, %d signals",
             bus_id, eng->dbc.hdr->msg_count, eng->dbc.hdr->sig_count);
    return 0;
}

void can_engine_free(can_engine_t *eng)
{
    if (eng->loaded) {
        sig_cache_free(&eng->cache);
        msg_mirror_free(&eng->mirror);
    }
    memset(eng, 0, sizeof(*eng));
}

void can_engine_poll(can_engine_t *eng, uint32_t now_ms)
{
    twai_message_t rx;
    while (can_bus_receive(eng->bus_id, &rx, 0) == ESP_OK) {
        if (eng->loaded) {
            msg_mirror_rx(&eng->mirror, rx.identifier, rx.data,
                          rx.data_length_code);
            sig_cache_update(&eng->cache, &eng->dbc, rx.identifier,
                             rx.data, rx.data_length_code, now_ms);

            /* Fire callbacks for changed signals. */
            const dbc_msg_t *msg = dbc_find_msg(&eng->dbc, rx.identifier);
            if (msg) {
                for (int i = 0; i < msg->sig_count; i++) {
                    uint16_t si = msg->sig_start + i;
                    sig_cache_entry_t *e = &eng->cache.entries[si];
                    if (e->changed) {
                        for (int c = 0; c < eng->cb_count; c++) {
                            if (eng->callbacks[c].sig_idx == si) {
                                eng->callbacks[c].cb(si, e->value, e->prev,
                                                     eng->callbacks[c].ctx);
                            }
                        }
                    }
                }
            }
        }
    }
}

int can_engine_on_change(can_engine_t *eng, const char *sig_name,
                         can_signal_cb_t cb, void *ctx)
{
    if (!eng->loaded) return -1;
    int si = dbc_find_signal(&eng->dbc, sig_name);
    if (si < 0) return -1;
    if (eng->cb_count >= CAN_ENGINE_MAX_CALLBACKS) return -1;

    eng->callbacks[eng->cb_count++] = (can_callback_entry_t){
        .sig_idx = si, .cb = cb, .ctx = ctx
    };
    return 0;
}

const sig_cache_entry_t *can_engine_signal(const can_engine_t *eng,
                                            const char *name)
{
    if (!eng->loaded) return NULL;
    int si = dbc_find_signal(&eng->dbc, name);
    if (si < 0) return NULL;
    return &eng->cache.entries[si];
}

int can_engine_draft(can_engine_t *eng, uint32_t msg_id,
                     uint8_t *out_data, uint8_t *out_dlc)
{
    if (!eng->loaded) return -1;
    return msg_mirror_draft(&eng->mirror, msg_id, out_data, out_dlc);
}

void can_engine_encode(const can_engine_t *eng, int sig_idx,
                       uint8_t *data, float value)
{
    if (!eng->loaded || sig_idx < 0 ||
        sig_idx >= eng->dbc.hdr->sig_count) return;
    dbc_encode_signal(&eng->dbc.sigs[sig_idx], data, value);
}

int can_engine_send(can_engine_t *eng, int msg_idx,
                    uint8_t *data, uint8_t dlc)
{
    if (!eng->loaded) return -1;
    msg_mirror_prepare_tx(&eng->mirror, msg_idx, data);
    return can_bus_send(eng->bus_id,
                        eng->dbc.msgs[msg_idx].id,
                        data, dlc, 50);
}
