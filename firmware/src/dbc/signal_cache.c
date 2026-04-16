#include "dbc/signal_cache.h"

#include <stdlib.h>
#include <string.h>

int sig_cache_init(sig_cache_t *cache, uint16_t count)
{
    cache->entries = calloc(count, sizeof(sig_cache_entry_t));
    if (!cache->entries) return -1;
    cache->count = count;
    return 0;
}

void sig_cache_free(sig_cache_t *cache)
{
    free(cache->entries);
    cache->entries = NULL;
    cache->count = 0;
}

void sig_cache_update(sig_cache_t *cache, const dbc_t *dbc,
                      uint32_t can_id, const uint8_t *data, uint8_t dlc,
                      uint32_t now_ms)
{
    const dbc_msg_t *msg = dbc_find_msg(dbc, can_id);
    if (!msg) return;

    /* For multiplexed messages, find the mux selector value first. */
    int mux_selector_value = -1;
    for (int i = 0; i < msg->sig_count; i++) {
        const dbc_sig_t *sig = &dbc->sigs[msg->sig_start + i];
        if ((sig->flags & DBC_SIG_MUX_MASK) == DBC_SIG_MUX_SELECTOR) {
            mux_selector_value = (int)dbc_extract_raw(
                data, sig->start_bit, sig->bit_length,
                (sig->flags & DBC_SIG_BIG_ENDIAN) != 0);
            break;
        }
    }

    for (int i = 0; i < msg->sig_count; i++) {
        uint16_t sig_idx = msg->sig_start + i;
        const dbc_sig_t *sig = &dbc->sigs[sig_idx];

        /* Skip multiplexed signals whose mux value doesn't match. */
        uint8_t mux_type = sig->flags & DBC_SIG_MUX_MASK;
        if (mux_type == DBC_SIG_MUX_MUXED) {
            if (mux_selector_value < 0 || sig->mux_value != mux_selector_value) {
                continue;
            }
        }

        float val = dbc_decode_signal(sig, data);
        sig_cache_entry_t *e = &cache->entries[sig_idx];
        e->prev = e->value;
        e->value = val;
        e->changed = (val != e->prev);
        e->last_rx_ms = now_ms;
    }
}
