/*
 * Host test for TX finalization (Tesla checksum + counter).
 */
#include <unity.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/dbc/dbc_types.h"
#include "../../src/dbc/dbc_pack.h"
#include "../../src/dbc/dbc_pack.c"
#include "../../src/can/msg_codec.h"
#include "../../src/can/msg_codec.c"
#include "../../src/can/tesla.h"
#include "../../src/can/tesla.c"

/* Test DBC: msg 0x118, 3 signals: torque (13-bit signed), counter (4-bit), checksum (8-bit) */
static uint8_t blob[512];
static size_t blob_len;

static void build_blob(void)
{
    memset(blob, 0, sizeof(blob));
    char strs[128]; uint16_t offs[4]; int sp = 0;
    const char *names[] = {"DI_torqueEstimate", "DI_counter", "DI_checksum", "msg_118"};
    for (int i = 0; i < 4; i++) {
        offs[i] = (uint16_t)sp;
        int n = strlen(names[i]) + 1;
        memcpy(strs + sp, names[i], n); sp += n;
    }

    dbc_sig_t sigs[3] = {0};
    sigs[0] = (dbc_sig_t){.name_off=offs[0], .start_bit=0, .bit_length=13,
                           .flags=DBC_SIG_SIGNED, .scale=0.25f};
    sigs[1] = (dbc_sig_t){.name_off=offs[1], .start_bit=48, .bit_length=4, .scale=1.0f};
    sigs[2] = (dbc_sig_t){.name_off=offs[2], .start_bit=56, .bit_length=8, .scale=1.0f};

    dbc_msg_t msgs[1] = {{.id=0x118, .name_off=offs[3], .dlc=8, .sig_count=3}};

    dbc_header_t hdr = {0};
    memcpy(hdr.magic, DBC_MAGIC, 4);
    hdr.version = DBC_VERSION; hdr.msg_count = 1; hdr.sig_count = 3;
    hdr.msgs_offset = sizeof(hdr);
    hdr.sigs_offset = hdr.msgs_offset + sizeof(msgs);
    hdr.strs_offset = hdr.sigs_offset + sizeof(sigs);
    hdr.strs_size = (uint32_t)sp;

    uint8_t *p = blob;
    memcpy(p, &hdr, sizeof(hdr)); p += sizeof(hdr);
    memcpy(p, msgs, sizeof(msgs)); p += sizeof(msgs);
    memcpy(p, sigs, sizeof(sigs)); p += sizeof(sigs);
    memcpy(p, strs, sp); p += sp;
    blob_len = (size_t)(p - blob);
}

static dbc_t dbc;

void setUp(void) { build_blob(); TEST_ASSERT_EQUAL(0, dbc_load(&dbc, blob, blob_len)); }
void tearDown(void) {}

void test_tesla_checksum_direct(void) {
    uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x35, tesla_checksum(0x118, 7, data, 8));
}

void test_counter_increments(void) {
    uint8_t data[8] = {0};
    uint8_t counter = 0;
    tesla_finalize_tx(&dbc, 0, &counter, data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, msg_decode_signal(&dbc.sigs[1], data));
    tesla_finalize_tx(&dbc, 0, &counter, data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, msg_decode_signal(&dbc.sigs[1], data));
}

void test_checksum_computed(void) {
    uint8_t data[8] = {0x10, 0, 0, 0, 0, 0, 0, 0};
    uint8_t counter = 0;
    tesla_finalize_tx(&dbc, 0, &counter, data);
    TEST_ASSERT_EQUAL_HEX8(tesla_checksum(0x118, 7, data, 8), data[7]);
}

void test_encode_then_finalize(void) {
    uint8_t data[8] = {0};
    uint8_t counter = 0;
    msg_encode_signal(&dbc.sigs[0], data, 100.0f);
    tesla_finalize_tx(&dbc, 0, &counter, data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, msg_decode_signal(&dbc.sigs[0], data));
    TEST_ASSERT_EQUAL_HEX8(tesla_checksum(0x118, 7, data, 8), data[7]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tesla_checksum_direct);
    RUN_TEST(test_counter_increments);
    RUN_TEST(test_checksum_computed);
    RUN_TEST(test_encode_then_finalize);
    return UNITY_END();
}
