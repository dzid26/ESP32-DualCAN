#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "dbc/dbc_decode.h"

/* Per-message state: latest received raw bytes + metadata. */
typedef struct {
    uint8_t  data[8];
    uint8_t  dlc;
    uint8_t  counter;       /* auto-incrementing counter for *_Counter signals */
    bool     received;      /* true after at least one rx */
} msg_mirror_entry_t;

typedef struct {
    msg_mirror_entry_t *entries;  /* one per message in the DBC */
    uint16_t            count;
    const dbc_t        *dbc;
} msg_mirror_t;

int msg_mirror_init(msg_mirror_t *mirror, const dbc_t *dbc);
void msg_mirror_free(msg_mirror_t *mirror);

/* Update the mirror with a received frame. */
void msg_mirror_rx(msg_mirror_t *mirror, uint32_t can_id,
                   const uint8_t *data, uint8_t dlc);

/* Get a writable copy of the latest mirrored frame for a message.
 * Returns the message index, or -1 if the message ID is unknown.
 * The caller modifies `out_data` and calls msg_mirror_prepare_tx(). */
int msg_mirror_draft(const msg_mirror_t *mirror, uint32_t msg_id,
                     uint8_t *out_data, uint8_t *out_dlc);

/* Finalize a frame for transmission:
 *  - Auto-increment *_Counter signals
 *  - Auto-compute *_Checksum signals (Tesla checksum algorithm)
 * Operates on `data` in place. */
void msg_mirror_prepare_tx(msg_mirror_t *mirror, int msg_idx, uint8_t *data);

/* Tesla CAN checksum. Exported for direct use from Berry scripts. */
uint8_t tesla_checksum(uint16_t addr, int checksum_byte_idx,
                       const uint8_t *data, int len);
