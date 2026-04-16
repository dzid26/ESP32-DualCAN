#pragma once

#include <stdint.h>

#include "dbc/dbc_decode.h"
#include "dbc/signal_cache.h"
#include "dbc/msg_mirror.h"

#define CAN_ENGINE_MAX_CALLBACKS 32

typedef void (*can_signal_cb_t)(int sig_idx, float value, float prev, void *ctx);

typedef struct {
    int              sig_idx;
    can_signal_cb_t  cb;
    void            *ctx;
} can_callback_entry_t;

typedef struct {
    int              bus_id;
    dbc_t            dbc;
    sig_cache_t      cache;
    msg_mirror_t     mirror;
    can_callback_entry_t callbacks[CAN_ENGINE_MAX_CALLBACKS];
    int              cb_count;
    bool             loaded;  /* true if a DBC is loaded for this bus */
} can_engine_t;

/* Initialize the engine for a bus. `dbc_blob` may be NULL (no DBC loaded). */
int can_engine_init(can_engine_t *eng, int bus_id,
                    const uint8_t *dbc_blob, size_t dbc_len);

void can_engine_free(can_engine_t *eng);

/* Poll the CAN bus, decode any received frames, update cache + mirror,
 * fire callbacks for changed signals. Call from the main loop. */
void can_engine_poll(can_engine_t *eng, uint32_t now_ms);

/* Register a callback for when a signal's value changes.
 * Returns 0 on success, -1 if callback table is full or signal not found. */
int can_engine_on_change(can_engine_t *eng, const char *sig_name,
                         can_signal_cb_t cb, void *ctx);

/* Look up a signal's current cache entry. Returns NULL if not found. */
const sig_cache_entry_t *can_engine_signal(const can_engine_t *eng,
                                            const char *name);

/* Prepare a TX frame: draft from mirror, returns msg index for encode/send.
 * Returns -1 if message not found. */
int can_engine_draft(can_engine_t *eng, uint32_t msg_id,
                     uint8_t *out_data, uint8_t *out_dlc);

/* Encode a signal value into a drafted frame. */
void can_engine_encode(const can_engine_t *eng, int sig_idx,
                       uint8_t *data, float value);

/* Finalize and send a drafted frame (auto checksum/counter). */
int can_engine_send(can_engine_t *eng, int msg_idx,
                    uint8_t *data, uint8_t dlc);
