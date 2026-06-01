#include "can/msg_codec.h"

#include <math.h>
#include <string.h>

/* ---- Bit extraction / insertion ---- */

uint64_t bit_extract(const uint8_t *data, uint8_t start_bit,
                     uint8_t bit_length, bool big_endian)
{
    uint64_t result = 0;

    if (!big_endian) {
        /* Intel: start_bit is LSB. Linear walk, LSB-0 bit addressing. */
        for (int i = 0; i < bit_length; i++) {
            int bp = start_bit + i;
            if (data[bp / 8] & (1u << (bp % 8)))
                result |= (1ULL << i);
        }
    } else {
        /* Motorola: start_bit is MSB (DBC convention: bit 0 = MSB of byte 0).
         * Linear walk, but bits within a byte are inverted (MSB-0 → LSB-0).
         * First extracted bit (MSB of signal) goes to result bit (bit_length-1)
         * so the extracted integer preserves the byte's natural order. */
        for (int i = 0; i < bit_length; i++) {
            int bp = start_bit + i;
            int lsb_bit = 7 - (bp % 8);
            if (data[bp / 8] & (1u << lsb_bit))
                result |= (1ULL << (bit_length - 1 - i));
        }
    }
    return result;
}

static void insert_raw(uint8_t *data, uint8_t start_bit,
                       uint8_t bit_length, bool big_endian, uint64_t raw)
{
    if (!big_endian) {
        for (int i = 0; i < bit_length; i++) {
            int bp = start_bit + i;
            if (raw & (1ULL << i))
                data[bp / 8] |= (1u << (bp % 8));
            else
                data[bp / 8] &= ~(1u << (bp % 8));
        }
    } else {
        for (int i = 0; i < bit_length; i++) {
            int bp = start_bit + i;
            int lsb_bit = 7 - (bp % 8);
            if (raw & (1ULL << (bit_length - 1 - i)))
                data[bp / 8] |= (1u << lsb_bit);
            else
                data[bp / 8] &= ~(1u << lsb_bit);
        }
    }
}

void bit_insert(uint8_t *data, uint8_t start_bit,
                uint8_t bit_length, bool big_endian, uint64_t raw)
{
    insert_raw(data, start_bit, bit_length, big_endian, raw);
}

/* ---- Physical value encode/decode ---- */

float signal_decode(const uint8_t *data, uint8_t start_bit,
                    uint8_t bit_length, bool big_endian, bool is_signed,
                    float scale, float offset)
{
    uint64_t raw = bit_extract(data, start_bit, bit_length, big_endian);

    double phys;
    if (is_signed) {
        uint64_t mask = 1ULL << (bit_length - 1);
        int64_t signed_raw = (int64_t)((raw ^ mask) - mask);
        phys = (double)signed_raw * (double)scale + (double)offset;
    } else {
        phys = (double)raw * (double)scale + (double)offset;
    }
    return (float)phys;
}

void signal_encode(uint8_t *data, uint8_t start_bit,
                   uint8_t bit_length, bool big_endian, bool is_signed,
                   float scale, float offset, float value)
{
    double raw_d = ((double)value - (double)offset) / (double)scale;

    if (is_signed) {
        int64_t raw = (int64_t)round(raw_d);
        uint64_t mask = (1ULL << bit_length) - 1;
        insert_raw(data, start_bit, bit_length, big_endian,
                   (uint64_t)raw & mask);
    } else {
        uint64_t raw = (uint64_t)round(raw_d);
        insert_raw(data, start_bit, bit_length, big_endian, raw);
    }
}
