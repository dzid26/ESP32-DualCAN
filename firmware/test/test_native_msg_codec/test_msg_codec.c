#include <unity.h>
#include <string.h>
#include <math.h>

#include "../../src/can/msg_codec.h"
#include "../../src/can/msg_codec.c"

void setUp(void) {}
void tearDown(void) {}

/* ---- bit_extract (Intel / little-endian) ---- */

void test_extract_intel_8bit_from_first_byte(void)
{
    uint8_t data[8] = {0xAB, 0xCD};
    uint64_t raw = bit_extract(data, 0, 8, false);
    TEST_ASSERT_EQUAL_HEX64(0xAB, raw);
}

void test_extract_intel_4bit_from_high_nibble(void)
{
    uint8_t data[8] = {0xAB};
    uint64_t raw = bit_extract(data, 4, 4, false);
    TEST_ASSERT_EQUAL_HEX64(0x0A, raw);
}

void test_extract_intel_16bit_spanning_bytes(void)
{
    uint8_t data[8] = {0x34, 0x12};
    uint64_t raw = bit_extract(data, 0, 16, false);
    TEST_ASSERT_EQUAL_HEX64(0x1234, raw);
}

void test_extract_intel_single_bit(void)
{
    uint8_t data[8] = {0x08};
    TEST_ASSERT_EQUAL_HEX64(1, bit_extract(data, 3, 1, false));
    TEST_ASSERT_EQUAL_HEX64(0, bit_extract(data, 4, 1, false));
}

/* ---- bit_extract (Motorola / big-endian) ---- */

void test_extract_motorola_8bit(void)
{
    /* Motorola: start_bit=0 is MSB of byte 0. Reading 8 bits from
     * data[0]=0xAB gives raw=0xAB (bits preserve byte's natural order). */
    uint8_t data[8] = {0xAB, 0xCD};
    uint64_t raw = bit_extract(data, 0, 8, true);
    TEST_ASSERT_EQUAL_HEX64(0xAB, raw);
}

void test_extract_motorola_16bit_spanning_bytes(void)
{
    /* Motorola: start_bit=0 is MSB of byte 0. 16 bits span byte 0-1.
     * data[0]=0x12, data[1]=0x34 → raw = 0x1234 */
    uint8_t data[8] = {0x12, 0x34};
    uint64_t raw = bit_extract(data, 0, 16, true);
    TEST_ASSERT_EQUAL_HEX64(0x1234, raw);
}

void test_extract_motorola_mid_bytes(void)
{
    /* Motorola: start_bit=4 is MSB of signal. 16 bits span bytes 0-2.
     * data[0]=0x00 (bits 4-7 = 0), data[1]=0xFF (all 1), data[2]=0x00 (bits 0-3 = 0)
     * raw = 0x0FF0 (16 bits with a nibble of zeros on each side). */
    uint8_t data[8] = {0x00, 0xFF, 0x00};
    uint64_t raw = bit_extract(data, 4, 16, true);
    TEST_ASSERT_EQUAL_HEX64(0x0FF0, raw);
}

/* ---- bit_insert + bit_extract roundtrip ---- */

void test_insert_extract_roundtrip_intel(void)
{
    uint8_t data[8] = {0};
    bit_insert(data, 4, 10, false, 0x3FF);
    uint64_t raw = bit_extract(data, 4, 10, false);
    TEST_ASSERT_EQUAL_HEX64(0x3FF, raw);
}

void test_insert_extract_roundtrip_motorola(void)
{
    uint8_t data[8] = {0};
    bit_insert(data, 0, 12, true, 0xABC);
    uint64_t raw = bit_extract(data, 0, 12, true);
    TEST_ASSERT_EQUAL_HEX64(0xABC, raw);
}

void test_insert_preserves_other_bits(void)
{
    uint8_t data[8] = {0xFF, 0xFF, 0xFF};
    bit_insert(data, 16, 8, false, 0x42);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x42, data[2]);
}

/* ---- signal_decode ---- */

void test_signal_decode_unsigned_identity(void)
{
    uint8_t data[8] = {0x2A};
    float val = signal_decode(data, 0, 8, false, false, 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 42.0f, val);
}

void test_signal_decode_unsigned_with_scale_offset(void)
{
    /* raw=100, scale=0.1, offset=-5 → phys = 100*0.1 + (-5) = 5.0 */
    uint8_t data[8] = {0x64};
    float val = signal_decode(data, 0, 8, false, false, 0.1f, -5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, val);
}

void test_signal_decode_signed_negative(void)
{
    /* raw=0xFE (signed 8-bit = -2), scale=1, offset=0 → -2.0 */
    uint8_t data[8] = {0xFE};
    float val = signal_decode(data, 0, 8, false, true, 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.0f, val);
}

void test_signal_decode_signed_positive(void)
{
    /* raw=0x7F (signed 8-bit = +127), scale=1, offset=0 → 127.0 */
    uint8_t data[8] = {0x7F};
    float val = signal_decode(data, 0, 8, false, true, 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 127.0f, val);
}

void test_signal_decode_motorola_signed(void)
{
    /* Motorola 12-bit signed: data[0]=0x80, data[1]=0x00
     * raw = 0x800, sign-extend → -2048, scale=0.5 → -1024.0 */
    uint8_t data[8] = {0x80, 0x00};
    float val = signal_decode(data, 0, 12, true, true, 0.5f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1024.0f, val);
}

/* ---- signal_encode ---- */

void test_signal_encode_roundtrip_unsigned(void)
{
    uint8_t data[8] = {0};
    signal_encode(data, 0, 16, false, false, 0.01f, 0.0f, 123.45f);
    float val = signal_decode(data, 0, 16, false, false, 0.01f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.45f, val);
}

void test_signal_encode_roundtrip_signed(void)
{
    uint8_t data[8] = {0};
    signal_encode(data, 0, 13, false, true, 0.25f, 0.0f, -50.0f);
    float val = signal_decode(data, 0, 13, false, true, 0.25f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -50.0f, val);
}

void test_signal_encode_preserves_other_bits(void)
{
    uint8_t data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    /* Encode 8-bit signal at start_bit=16 */
    signal_encode(data, 16, 8, false, false, 1.0f, 0.0f, 42.0f);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x2A, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[3]);
}

void test_signal_encode_motorola_roundtrip(void)
{
    uint8_t data[8] = {0};
    /* Motorola 16-bit signed */
    signal_encode(data, 4, 12, true, true, 0.5f, 10.0f, 100.0f);
    float val = signal_decode(data, 4, 12, true, true, 0.5f, 10.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, val);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_extract_intel_8bit_from_first_byte);
    RUN_TEST(test_extract_intel_4bit_from_high_nibble);
    RUN_TEST(test_extract_intel_16bit_spanning_bytes);
    RUN_TEST(test_extract_intel_single_bit);
    RUN_TEST(test_extract_motorola_8bit);
    RUN_TEST(test_extract_motorola_16bit_spanning_bytes);
    RUN_TEST(test_extract_motorola_mid_bytes);
    RUN_TEST(test_insert_extract_roundtrip_intel);
    RUN_TEST(test_insert_extract_roundtrip_motorola);
    RUN_TEST(test_insert_preserves_other_bits);
    RUN_TEST(test_signal_decode_unsigned_identity);
    RUN_TEST(test_signal_decode_unsigned_with_scale_offset);
    RUN_TEST(test_signal_decode_signed_negative);
    RUN_TEST(test_signal_decode_signed_positive);
    RUN_TEST(test_signal_decode_motorola_signed);
    RUN_TEST(test_signal_encode_roundtrip_unsigned);
    RUN_TEST(test_signal_encode_roundtrip_signed);
    RUN_TEST(test_signal_encode_preserves_other_bits);
    RUN_TEST(test_signal_encode_motorola_roundtrip);
    return UNITY_END();
}
