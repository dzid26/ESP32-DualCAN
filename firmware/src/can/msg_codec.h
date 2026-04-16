#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dbc/dbc_pack.h"

/* Runtime signal state — one per signal in the loaded DBC. */
typedef struct {
    float    value;
    float    prev;
    uint32_t last_rx_ms;
    bool     changed;
} signal_state_t;

/* Brand-specific TX finalization (checksum + counter). */
typedef void (*tx_finalize_fn_t)(const dbc_t *dbc, int msg_idx,
                                  uint8_t *counter, uint8_t *data);

/* ---- Signal-level encode/decode ---- */

uint64_t msg_extract_raw(const uint8_t *data, uint8_t start_bit,
                         uint8_t bit_length, bool big_endian);

float msg_decode_signal(const dbc_sig_t *sig, const uint8_t *data);

void msg_encode_signal(const dbc_sig_t *sig, uint8_t *data, float value);

/* ---- Frame-level operations ---- */

/* Decode all signals in a message, update signal states.
 * Handles multiplexed signals (skips muxed signals whose value
 * doesn't match the current multiplexor). */
void msg_decode_frame(const dbc_t *dbc, const dbc_msg_t *msg,
                      const uint8_t *data, signal_state_t *states,
                      uint32_t now_ms);

/* Apply brand-specific counter/checksum to a TX frame. */
void msg_finalize_tx(const dbc_t *dbc, int msg_idx, uint8_t *counter,
                     uint8_t *data, tx_finalize_fn_t finalize);
