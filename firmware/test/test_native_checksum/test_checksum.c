#include <unity.h>
#include <stdint.h>

#include "../../src/can/tesla.h"
#include "../../src/can/tesla.c"

void setUp(void) {}
void tearDown(void) {}

void test_checksum_low_address(void)
{
    uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x1D, tesla_checksum(0x100, 7, data, 8));
}

void test_checksum_all_zero(void)
{
    uint8_t data[8] = {0};
    TEST_ASSERT_EQUAL_HEX8(0x00, tesla_checksum(0x000, 0, data, 8));
}

void test_checksum_high_address(void)
{
    uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT_EQUAL_HEX8(0x0C, tesla_checksum(0x3A5, 0, data, 4));
}

void test_checksum_is_sensitive_to_payload(void)
{
    uint8_t a[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x00};
    uint8_t b[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x71, 0x00};
    TEST_ASSERT_NOT_EQUAL(tesla_checksum(0x100, 7, a, 8),
                          tesla_checksum(0x100, 7, b, 8));
}

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
