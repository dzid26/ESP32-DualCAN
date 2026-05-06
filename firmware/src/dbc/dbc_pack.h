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

/* Scoped lookup: only consider signals belonging to `msg`. Returns the global
 * signal index, or -1 if not found. DBC signal names are unique within a
 * message but may collide across messages, so prefer this when you know
 * the message context. */
int dbc_find_signal_in_msg(const dbc_t *dbc, const dbc_msg_t *msg, const char *name);

/* Convenience: msg_name + sig_name -> global sig index. -1 if either misses. */
int dbc_find_signal_by_msg_name(const dbc_t *dbc, const char *msg_name, const char *sig_name);

/* Look up a message by its name. Linear scan; fine for typical DBC sizes. */
const dbc_msg_t *dbc_find_msg_by_name(const dbc_t *dbc, const char *name);

static inline const char *dbc_str(const dbc_t *dbc, uint16_t off)
{
    return dbc->strs + off;
}
