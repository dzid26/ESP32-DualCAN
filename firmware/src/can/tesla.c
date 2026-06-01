#include "can/tesla.h"

uint8_t tesla_checksum(uint16_t addr, int checksum_byte_idx,
                       const uint8_t *data, int len)
{
    uint32_t sum = (addr & 0xFF) + ((addr >> 8) & 0xFF);
    for (int i = 0; i < len; i++) {
        if (i != checksum_byte_idx) sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}
