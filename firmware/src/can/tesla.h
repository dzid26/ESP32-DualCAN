#pragma once

#include <stdint.h>

#include "dbc/dbc_pack.h"

uint8_t tesla_checksum(uint16_t addr, int checksum_byte_idx,
                       const uint8_t *data, int len);

/* TX finalize for Tesla: auto-increment *_Counter, auto-compute *_Checksum. */
void tesla_finalize_tx(const dbc_t *dbc, int msg_idx,
                       uint8_t *counter, uint8_t *data);
