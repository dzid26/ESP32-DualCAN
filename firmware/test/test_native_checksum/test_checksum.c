#include <unity.h>
#include <stdint.h>

/*
 * Tesla CAN checksum: sum of the two address bytes plus every payload byte
 * except the byte that holds the checksum itself, truncated to 8 bits.
 *
 * Reference:
 *   https://github.com/commaai/opendbc/blob/901c138/opendbc/car/tesla/teslacan.py#L54-L60
 */
static uint8_t tesla_checksum(uint16_t addr, int checksum_byte_index,
                              const uint8_t *data, int len)
{
    uint32_t sum = (addr & 0xFF) + ((addr >> 8) & 0xFF);
    for (int i = 0; i < len; i++) {
        if (i != checksum_byte_index) {
            sum += data[i];
        }
    }
    return (uint8_t)(sum & 0xFF);
}

void setUp(void) {}
void tearDown(void) {}

/* address 0x100 -> bytes (0x00, 0x01) -> 0x01
 * payload (1+2+3+4+5+6+7, skipping last byte) = 0x1C
 * total = 0x1D                                                         */
void test_checksum_low_address(void)
{
    uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x1D, tesla_checksum(0x100, 7, data, 8));
}

/* address 0x000, all-zero payload, checksum at index 0 -> 0x00          */
void test_checksum_all_zero(void)
{
    uint8_t data[8] = {0};
    TEST_ASSERT_EQUAL_HEX8(0x00, tesla_checksum(0x000, 0, data, 8));
}

/* address 0x3A5 -> bytes (0xA5, 0x03) -> 0xA8
 * payload 0xAA skipped, 0xBB + 0xCC + 0xDD = 0x264
 * total = 0xA8 + 0x264 = 0x30C -> 0x0C                                  */
void test_checksum_high_address(void)
{
    uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT_EQUAL_HEX8(0x0C, tesla_checksum(0x3A5, 0, data, 4));
}

/* Changing bytes outside the checksum-byte slot must change the result. */
void test_checksum_is_sensitive_to_payload(void)
{
    uint8_t a[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x00};
    uint8_t b[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x71, 0x00};
    TEST_ASSERT_NOT_EQUAL(tesla_checksum(0x100, 7, a, 8),
                          tesla_checksum(0x100, 7, b, 8));
}

/* The value in the checksum-byte slot itself must be ignored.           */
void test_checksum_ignores_checksum_byte(void)
{
    uint8_t a[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00};
    uint8_t b[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xFF};
    TEST_ASSERT_EQUAL_HEX8(tesla_checksum(0x100, 7, a, 8),
                           tesla_checksum(0x100, 7, b, 8));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_checksum_low_address);
    RUN_TEST(test_checksum_all_zero);
    RUN_TEST(test_checksum_high_address);
    RUN_TEST(test_checksum_is_sensitive_to_payload);
    RUN_TEST(test_checksum_ignores_checksum_byte);
    return UNITY_END();
}
