#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ---- Bit-level encode/decode utilities (no DBC dependency) ---- */

uint64_t bit_extract(const uint8_t *data, uint8_t start_bit,
                     uint8_t bit_length, bool big_endian);

void bit_insert(uint8_t *data, uint8_t start_bit,
                uint8_t bit_length, bool big_endian, uint64_t raw);

float signal_decode(const uint8_t *data, uint8_t start_bit,
                    uint8_t bit_length, bool big_endian, bool is_signed,
                    float scale, float offset);

void signal_encode(uint8_t *data, uint8_t start_bit,
                   uint8_t bit_length, bool big_endian, bool is_signed,
                   float scale, float offset, float value);
