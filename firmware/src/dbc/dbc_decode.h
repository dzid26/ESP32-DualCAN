#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "dbc/dbc_types.h"

typedef struct {
    const uint8_t      *blob;      /* raw binary buffer (owned by caller) */
    size_t              blob_len;
    const dbc_header_t *hdr;
    const dbc_msg_t    *msgs;      /* msg_count entries, sorted by id */
    const dbc_sig_t    *sigs;      /* sig_count entries */
    const char         *strs;      /* string table */
} dbc_t;

/* Load a binary DBC blob. Returns 0 on success, -1 on format error.
 * The blob must remain valid for the lifetime of the dbc_t. */
int dbc_load(dbc_t *dbc, const uint8_t *blob, size_t len);

/* Find a message by CAN ID. Returns NULL if not found. */
const dbc_msg_t *dbc_find_msg(const dbc_t *dbc, uint32_t id);

/* Find a signal by name (linear scan). Returns signal index or -1. */
int dbc_find_signal(const dbc_t *dbc, const char *name);

/* Get the string for an offset into the string table. */
static inline const char *dbc_str(const dbc_t *dbc, uint16_t off)
{
    return dbc->strs + off;
}

/* Extract raw unsigned bits from a CAN frame. Handles both
 * little-endian and big-endian DBC bit numbering. */
uint64_t dbc_extract_raw(const uint8_t *data, uint8_t start_bit,
                         uint8_t bit_length, bool big_endian);

/* Decode a signal to its physical float value. */
float dbc_decode_signal(const dbc_sig_t *sig, const uint8_t *data);

/* Encode a physical float value into CAN frame bytes.
 * Modifies only the bits belonging to this signal. */
void dbc_encode_signal(const dbc_sig_t *sig, uint8_t *data, float value);
