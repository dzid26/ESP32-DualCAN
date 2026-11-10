#include "can/rate_limit.h"

#include <stddef.h>

void rate_limiter_init(rate_limiter_t *rl, uint32_t default_hz)
{
    rl->count = 0;
    rl->default_interval_us = default_hz > 0 ? 1000000 / default_hz : 0;
}

static rate_limit_entry_t *find_or_create(rate_limiter_t *rl, uint32_t id)
{
    for (int i = 0; i < rl->count; i++) {
        if (rl->entries[i].id == id) return &rl->entries[i];
    }
    if (rl->count >= RATE_LIMIT_MAX_IDS) return NULL;
    rate_limit_entry_t *e = &rl->entries[rl->count++];
    e->id = id;
    e->last_tx_us = 0;
    e->min_interval_us = 0;
    e->last_rx_us = 0;
    return e;
}

bool rate_limiter_allow(rate_limiter_t *rl, uint32_t id, uint32_t now_us)
{
    rate_limit_entry_t *e = find_or_create(rl, id);
    if (!e) return false;

    uint32_t interval = e->min_interval_us ? e->min_interval_us
                                           : rl->default_interval_us;
    if (interval == 0) return true;

    uint32_t elapsed = now_us - e->last_tx_us;
    if (elapsed < interval) return false;

    e->last_tx_us = now_us;
    return true;
}

bool rate_limiter_allow_frame(rate_limiter_t *rl, uint32_t id, uint32_t now_us,
                              uint32_t rx_us, bool have_rx)
{
    if (!have_rx || rx_us == 0) {
        return rate_limiter_allow(rl, id, now_us);
    }

    rate_limit_entry_t *e = find_or_create(rl, id);
    if (!e) return false;

    /* A script may answer each received frame at most once. This is the
     * defence-in-depth rule: even if script state collides and it tries to
     * send on every poll, only one echo of a given car frame reaches the bus. */
    if (e->last_rx_us == rx_us) return false;

    uint32_t interval = e->min_interval_us ? e->min_interval_us
                                           : rl->default_interval_us;
    if (interval != 0) {
        uint32_t elapsed = now_us - e->last_tx_us;
        if (elapsed < interval) return false;
    }

    e->last_rx_us = rx_us;
    e->last_tx_us = now_us;
    return true;
}

void rate_limiter_set(rate_limiter_t *rl, uint32_t id, uint32_t hz)
{
    rate_limit_entry_t *e = find_or_create(rl, id);
    if (e) {
        e->min_interval_us = hz > 0 ? 1000000 / hz : 0;
    }
}
