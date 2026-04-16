#pragma once

#include <stddef.h>
#include <stdint.h>

#include "dbc/dbc_types.h"

typedef struct {
    const uint8_t      *blob;
    size_t              blob_len;
    const dbc_header_t *hdr;
    const dbc_msg_t    *msgs;   /* sorted by id */
    const dbc_sig_t    *sigs;
    const char         *strs;
} dbc_t;

int dbc_load(dbc_t *dbc, const uint8_t *blob, size_t len);
const dbc_msg_t *dbc_find_msg(const dbc_t *dbc, uint32_t id);
int dbc_find_signal(const dbc_t *dbc, const char *name);

static inline const char *dbc_str(const dbc_t *dbc, uint16_t off)
{
    return dbc->strs + off;
}
