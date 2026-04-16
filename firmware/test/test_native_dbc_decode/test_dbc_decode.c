/*
 * Host test for DBC binary decoder, signal state, and encode.
 */
#include <unity.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/dbc/dbc_types.h"
#include "../../src/dbc/dbc_pack.h"
#include "../../src/dbc/dbc_pack.c"
#include "../../src/can/msg_codec.h"
#include "../../src/can/msg_codec.c"

/*
 * Test DBC: 2 messages, 5 signals (including multiplexed).
 * See original test for full layout description.
 */
static uint8_t test_blob[512];
static size_t test_blob_len;

static void build_test_blob(void)
{
    memset(test_blob, 0, sizeof(test_blob));

    const char *str_table[] = {"speed", "counter", "msg100", "mux_sel", "temp_a", "temp_b", "msg200"};
    char strs[256];
    uint16_t str_offsets[7];
    int str_pos = 0;
    for (int i = 0; i < 7; i++) {
        str_offsets[i] = (uint16_t)str_pos;
        int slen = strlen(str_table[i]) + 1;
        memcpy(strs + str_pos, str_table[i], slen);
        str_pos += slen;
    }

    dbc_sig_t sigs[5];
    memset(sigs, 0, sizeof(sigs));

    sigs[0] = (dbc_sig_t){.name_off = str_offsets[0], .start_bit = 0, .bit_length = 16,
                           .flags = 0, .scale = 0.01f, .offset = 0.0f};
    sigs[1] = (dbc_sig_t){.name_off = str_offsets[1], .start_bit = 16, .bit_length = 8,
                           .flags = 0, .scale = 1.0f, .offset = 0.0f};
    sigs[2] = (dbc_sig_t){.name_off = str_offsets[3], .start_bit = 0, .bit_length = 2,
                           .flags = DBC_SIG_MUX_SELECTOR, .scale = 1.0f, .offset = 0.0f};
    sigs[3] = (dbc_sig_t){.name_off = str_offsets[4], .start_bit = 8, .bit_length = 8,
                           .flags = DBC_SIG_SIGNED | DBC_SIG_MUX_MUXED, .mux_value = 0,
                           .scale = 1.0f, .offset = -40.0f};
    sigs[4] = (dbc_sig_t){.name_off = str_offsets[5], .start_bit = 8, .bit_length = 8,
                           .flags = DBC_SIG_SIGNED | DBC_SIG_MUX_MUXED, .mux_value = 1,
                           .scale = 1.0f, .offset = -40.0f};

    dbc_msg_t msgs[2];
    memset(msgs, 0, sizeof(msgs));
    msgs[0] = (dbc_msg_t){.id = 0x100, .name_off = str_offsets[2], .dlc = 8,
                           .sig_count = 2, .sig_start = 0};
    msgs[1] = (dbc_msg_t){.id = 0x200, .name_off = str_offsets[6], .dlc = 8,
                           .sig_count = 3, .sig_start = 2};

    dbc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, DBC_MAGIC, 4);
    hdr.version = DBC_VERSION;
    hdr.msg_count = 2; hdr.sig_count = 5;
    hdr.msgs_offset = sizeof(hdr);
    hdr.sigs_offset = hdr.msgs_offset + sizeof(msgs);
    hdr.strs_offset = hdr.sigs_offset + sizeof(sigs);
    hdr.strs_size = (uint32_t)str_pos;

    uint8_t *p = test_blob;
    memcpy(p, &hdr, sizeof(hdr)); p += sizeof(hdr);
    memcpy(p, msgs, sizeof(msgs)); p += sizeof(msgs);
    memcpy(p, sigs, sizeof(sigs)); p += sizeof(sigs);
    memcpy(p, strs, str_pos); p += str_pos;
    test_blob_len = (size_t)(p - test_blob);
}

static dbc_t dbc;

void setUp(void) { build_test_blob(); TEST_ASSERT_EQUAL(0, dbc_load(&dbc, test_blob, test_blob_len)); }
void tearDown(void) {}

void test_load_header(void) {
    TEST_ASSERT_EQUAL(2, dbc.hdr->msg_count);
    TEST_ASSERT_EQUAL(5, dbc.hdr->sig_count);
}

void test_find_msg(void) {
    TEST_ASSERT_NOT_NULL(dbc_find_msg(&dbc, 0x100));
    TEST_ASSERT_NULL(dbc_find_msg(&dbc, 0x999));
}

void test_find_signal(void) {
    TEST_ASSERT_EQUAL(0, dbc_find_signal(&dbc, "speed"));
    TEST_ASSERT_EQUAL(-1, dbc_find_signal(&dbc, "nonexistent"));
}

void test_decode_simple(void) {
    uint8_t data[8] = {0x34, 0x12, 0x05, 0, 0, 0, 0, 0};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 46.60f, msg_decode_signal(&dbc.sigs[0], data));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, msg_decode_signal(&dbc.sigs[1], data));
}

void test_decode_signed(void) {
    uint8_t data[8] = {0x00, 0xD8, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -80.0f, msg_decode_signal(&dbc.sigs[3], data));
}

void test_decode_frame_mux(void) {
    signal_state_t states[5] = {0};

    uint8_t frame0[8] = {0x00, 50, 0, 0, 0, 0, 0, 0};
    msg_decode_frame(&dbc, &dbc.msgs[1], frame0, states, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, states[3].value);
    TEST_ASSERT_EQUAL(0, states[4].last_rx_ms);

    uint8_t frame1[8] = {0x01, 30, 0, 0, 0, 0, 0, 0};
    msg_decode_frame(&dbc, &dbc.msgs[1], frame1, states, 2000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, states[4].value);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, states[3].value);
}

void test_encode_roundtrip(void) {
    uint8_t data[8] = {0};
    msg_encode_signal(&dbc.sigs[0], data, 123.45f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.45f, msg_decode_signal(&dbc.sigs[0], data));
}

void test_encode_preserves_other_bits(void) {
    uint8_t data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    msg_encode_signal(&dbc.sigs[1], data, 42.0f);
    TEST_ASSERT_EQUAL_HEX8(42, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[0]);
}

void test_changed_flag(void) {
    signal_state_t states[5] = {0};
    uint8_t data[8] = {0x10, 0x00, 0x01, 0, 0, 0, 0, 0};

    msg_decode_frame(&dbc, &dbc.msgs[0], data, states, 100);
    TEST_ASSERT_TRUE(states[0].changed);

    msg_decode_frame(&dbc, &dbc.msgs[0], data, states, 200);
    TEST_ASSERT_FALSE(states[0].changed);

    data[0] = 0x20;
    msg_decode_frame(&dbc, &dbc.msgs[0], data, states, 300);
    TEST_ASSERT_TRUE(states[0].changed);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_load_header);
    RUN_TEST(test_find_msg);
    RUN_TEST(test_find_signal);
    RUN_TEST(test_decode_simple);
    RUN_TEST(test_decode_signed);
    RUN_TEST(test_decode_frame_mux);
    RUN_TEST(test_encode_roundtrip);
    RUN_TEST(test_encode_preserves_other_bits);
    RUN_TEST(test_changed_flag);
    return UNITY_END();
}
