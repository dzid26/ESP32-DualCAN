#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "dbc/dbc_decode.h"

typedef struct {
    float    value;
    float    prev;
    uint32_t last_rx_ms;
    bool     changed;
} sig_cache_entry_t;

typedef struct {
    sig_cache_entry_t *entries;    /* array of sig_count entries */
    uint16_t           count;
} sig_cache_t;

/* Allocate a cache with `count` entries (one per signal in the DBC).
 * Returns 0 on success, -1 on alloc failure. */
int sig_cache_init(sig_cache_t *cache, uint16_t count);

/* Free the cache. */
void sig_cache_free(sig_cache_t *cache);

/* Update cache entries for all signals in a message.
 * Decodes the frame using the DBC, updates value/prev/changed/timestamp.
 * `now_ms` is the current monotonic time. */
void sig_cache_update(sig_cache_t *cache, const dbc_t *dbc,
                      uint32_t can_id, const uint8_t *data, uint8_t dlc,
                      uint32_t now_ms);
