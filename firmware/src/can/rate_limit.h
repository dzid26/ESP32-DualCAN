#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RATE_LIMIT_MAX_IDS  64
#define RATE_LIMIT_DEFAULT_HZ 100

typedef struct {
    uint32_t id;
    uint32_t last_tx_us;
    uint32_t min_interval_us;
    uint32_t last_rx_us;      /* last RX frame timestamp this TX was gated on */
} rate_limit_entry_t;

typedef struct {
    rate_limit_entry_t entries[RATE_LIMIT_MAX_IDS];
    int count;
    uint32_t default_interval_us;
} rate_limiter_t;

void rate_limiter_init(rate_limiter_t *rl, uint32_t default_hz);

/* Returns true if the message is allowed to send now.
 * Updates the timestamp if allowed. */
bool rate_limiter_allow(rate_limiter_t *rl, uint32_t id, uint32_t now_us);

/* RX-gated variant: when the ID is being mirrored from a received frame
 * (have_rx true, rx_us is that frame's timestamp), allow at most one TX per
 * distinct received frame, in addition to the normal per-ID interval. IDs
 * with no received frame fall back to rate_limiter_allow(). */
bool rate_limiter_allow_frame(rate_limiter_t *rl, uint32_t id, uint32_t now_us,
                              uint32_t rx_us, bool have_rx);

/* Set a per-ID rate limit (0 = use default). */
void rate_limiter_set(rate_limiter_t *rl, uint32_t id, uint32_t hz);
