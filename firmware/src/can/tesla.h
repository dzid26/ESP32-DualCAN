#pragma once

#include <stdint.h>

/* Tesla-specific checksum: additive sum over data bytes, skipping checksum_byte_idx,
 * with the CAN address mixed in. */
uint8_t tesla_checksum(uint16_t addr, int checksum_byte_idx,
                       const uint8_t *data, int len);
