/*
 * Host test for DBC binary decoder, signal cache, and encode.
 *
 * We build a tiny binary DBC in memory (2 messages, 4 signals)
 * rather than loading from a file, so the test is self-contained.
 */
#include <unity.h>
#include <string.h>
#include <stdlib.h>

/* Include the implementation directly since we're building natively
 * without the ESP-IDF component system. */
#include "../../src/dbc/dbc_types.h"
#include "../../src/dbc/dbc_decode.h"
#include "../../src/dbc/dbc_decode.c"
#include "../../src/dbc/signal_cache.h"
#include "../../src/dbc/signal_cache.c"

/*
 * Build a tiny test DBC binary blob in memory:
 *
 *   Message 0x100 (DLC 8), 2 signals:
 *     "speed"    start_bit=0  len=16  LE unsigned  scale=0.01 offset=0
 *     "counter"  start_bit=16 len=8   LE unsigned  scale=1    offset=0
 *
 *   Message 0x200 (DLC 8), 2 signals (multiplexed):
 *     "mux_sel"  start_bit=0  len=2   LE unsigned  (MUX SELECTOR)
 *     "temp_a"   start_bit=8  len=8   LE signed    mux_value=0  scale=1 offset=-40
 *     "temp_b"   start_bit=8  len=8   LE signed    mux_value=1  scale=1 offset=-40
 */

static uint8_t test_blob[512];
static size_t test_blob_len;

static void build_test_blob(void)
{
    memset(test_blob, 0, sizeof(test_blob));

    /* String table */
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

    /* Signals (5 total) */
    dbc_sig_t sigs[5];
    memset(sigs, 0, sizeof(sigs));

    /* msg 0x100: speed */
    sigs[0].name_off = str_offsets[0];
    sigs[0].start_bit = 0;
    sigs[0].bit_length = 16;
    sigs[0].flags = 0; /* LE unsigned */
    sigs[0].scale = 0.01f;
    sigs[0].offset = 0.0f;

    /* msg 0x100: counter */
    sigs[1].name_off = str_offsets[1];
    sigs[1].start_bit = 16;
    sigs[1].bit_length = 8;
    sigs[1].flags = 0;
    sigs[1].scale = 1.0f;
    sigs[1].offset = 0.0f;

    /* msg 0x200: mux_sel (multiplexor) */
    sigs[2].name_off = str_offsets[3];
    sigs[2].start_bit = 0;
    sigs[2].bit_length = 2;
    sigs[2].flags = DBC_SIG_MUX_SELECTOR;
    sigs[2].scale = 1.0f;
    sigs[2].offset = 0.0f;

    /* msg 0x200: temp_a (muxed, value 0) */
    sigs[3].name_off = str_offsets[4];
    sigs[3].start_bit = 8;
    sigs[3].bit_length = 8;
    sigs[3].flags = DBC_SIG_SIGNED | DBC_SIG_MUX_MUXED;
    sigs[3].mux_value = 0;
    sigs[3].scale = 1.0f;
    sigs[3].offset = -40.0f;

    /* msg 0x200: temp_b (muxed, value 1) */
    sigs[4].name_off = str_offsets[5];
    sigs[4].start_bit = 8;
    sigs[4].bit_length = 8;
    sigs[4].flags = DBC_SIG_SIGNED | DBC_SIG_MUX_MUXED;
    sigs[4].mux_value = 1;
    sigs[4].scale = 1.0f;
    sigs[4].offset = -40.0f;

    /* Messages (sorted by ID) */
    dbc_msg_t msgs[2];
    memset(msgs, 0, sizeof(msgs));

    msgs[0].id = 0x100;
    msgs[0].name_off = str_offsets[2]; /* "msg100" */
    msgs[0].dlc = 8;
    msgs[0].sig_count = 2;
    msgs[0].sig_start = 0;

    msgs[1].id = 0x200;
    msgs[1].name_off = str_offsets[6]; /* "msg200" */
    msgs[1].dlc = 8;
    msgs[1].sig_count = 3;
    msgs[1].sig_start = 2;

    /* Header */
    dbc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "DBC\0", 4);
    hdr.version = DBC_VERSION;
    hdr.bus_id = 0;
    hdr.msg_count = 2;
    hdr.sig_count = 5;
    hdr.msgs_offset = sizeof(dbc_header_t);
    hdr.sigs_offset = hdr.msgs_offset + sizeof(msgs);
    hdr.strs_offset = hdr.sigs_offset + sizeof(sigs);
    hdr.strs_size = (uint32_t)str_pos;

    /* Assemble */
    uint8_t *p = test_blob;
    memcpy(p, &hdr, sizeof(hdr));           p += sizeof(hdr);
    memcpy(p, msgs, sizeof(msgs));          p += sizeof(msgs);
    memcpy(p, sigs, sizeof(sigs));          p += sizeof(sigs);
    memcpy(p, strs, str_pos);              p += str_pos;
    test_blob_len = (size_t)(p - test_blob);
}

static dbc_t dbc;

void setUp(void)
{
    build_test_blob();
    TEST_ASSERT_EQUAL(0, dbc_load(&dbc, test_blob, test_blob_len));
}

void tearDown(void) {}

void test_load_header(void)
{
    TEST_ASSERT_EQUAL(2, dbc.hdr->msg_count);
    TEST_ASSERT_EQUAL(5, dbc.hdr->sig_count);
}

void test_find_msg(void)
{
    const dbc_msg_t *m = dbc_find_msg(&dbc, 0x100);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_HEX32(0x100, m->id);
    TEST_ASSERT_EQUAL(2, m->sig_count);
    TEST_ASSERT_EQUAL_STRING("msg100", dbc_str(&dbc, m->name_off));

    TEST_ASSERT_NULL(dbc_find_msg(&dbc, 0x999));
}

void test_find_signal(void)
{
    TEST_ASSERT_EQUAL(0, dbc_find_signal(&dbc, "speed"));
    TEST_ASSERT_EQUAL(1, dbc_find_signal(&dbc, "counter"));
    TEST_ASSERT_EQUAL(3, dbc_find_signal(&dbc, "temp_a"));
    TEST_ASSERT_EQUAL(-1, dbc_find_signal(&dbc, "nonexistent"));
}

void test_decode_simple(void)
{
    /* speed = 0x1234 raw → 0x1234 * 0.01 = 46.60
     * counter = 0x05 */
    uint8_t data[8] = {0x34, 0x12, 0x05, 0, 0, 0, 0, 0};

    float speed = dbc_decode_signal(&dbc.sigs[0], data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 46.60f, speed);

    float counter = dbc_decode_signal(&dbc.sigs[1], data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, counter);
}

void test_decode_signed(void)
{
    /* temp_a: raw 0xD8 = -40 signed → -40 * 1 + (-40) = -80 */
    uint8_t data[8] = {0x00, 0xD8, 0, 0, 0, 0, 0, 0}; /* mux=0, temp=0xD8 */
    float temp = dbc_decode_signal(&dbc.sigs[3], data);
    /* 0xD8 signed 8-bit = -40, physical = -40 + (-40) = -80 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -80.0f, temp);
}

void test_signal_cache_mux(void)
{
    sig_cache_t cache;
    TEST_ASSERT_EQUAL(0, sig_cache_init(&cache, dbc.hdr->sig_count));

    /* Send mux=0 frame: mux_sel=0, data byte[1]=50 → temp_a raw=50, phys=50-40=10 */
    uint8_t frame0[8] = {0x00, 50, 0, 0, 0, 0, 0, 0};
    sig_cache_update(&cache, &dbc, 0x200, frame0, 8, 1000);

    /* temp_a (idx 3) should be updated, temp_b (idx 4) should NOT */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, cache.entries[3].value);
    TEST_ASSERT_EQUAL(1000, cache.entries[3].last_rx_ms);
    TEST_ASSERT_EQUAL(0, cache.entries[4].last_rx_ms); /* never updated */

    /* Send mux=1 frame: mux_sel=1, data byte[1]=30 → temp_b raw=30, phys=30-40=-10 */
    uint8_t frame1[8] = {0x01, 30, 0, 0, 0, 0, 0, 0};
    sig_cache_update(&cache, &dbc, 0x200, frame1, 8, 2000);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, cache.entries[4].value);
    TEST_ASSERT_EQUAL(2000, cache.entries[4].last_rx_ms);
    /* temp_a should still be 10.0 from the previous frame */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, cache.entries[3].value);

    sig_cache_free(&cache);
}

void test_encode_roundtrip(void)
{
    /* Encode speed=123.45 into a zeroed frame, then decode and compare */
    uint8_t data[8] = {0};
    dbc_encode_signal(&dbc.sigs[0], data, 123.45f);
    float decoded = dbc_decode_signal(&dbc.sigs[0], data);
    /* Quantization: raw = round(123.45 / 0.01) = 12345, decoded = 123.45 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.45f, decoded);
}

void test_encode_preserves_other_bits(void)
{
    /* Fill frame with 0xFF, encode counter=42, check speed bits are preserved */
    uint8_t data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    dbc_encode_signal(&dbc.sigs[1], data, 42.0f); /* counter: bits 16-23 */
    TEST_ASSERT_EQUAL_HEX8(42, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[0]); /* speed LSB preserved */
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[1]); /* speed MSB preserved */
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[3]); /* rest preserved */
}

void test_cache_changed_flag(void)
{
    sig_cache_t cache;
    sig_cache_init(&cache, dbc.hdr->sig_count);

    uint8_t data[8] = {0x10, 0x00, 0x01, 0, 0, 0, 0, 0};
    sig_cache_update(&cache, &dbc, 0x100, data, 8, 100);
    TEST_ASSERT_TRUE(cache.entries[0].changed);  /* first update: 0→0.16 */

    /* Same data again — value unchanged */
    sig_cache_update(&cache, &dbc, 0x100, data, 8, 200);
    TEST_ASSERT_FALSE(cache.entries[0].changed);

    /* Different data */
    data[0] = 0x20;
    sig_cache_update(&cache, &dbc, 0x100, data, 8, 300);
    TEST_ASSERT_TRUE(cache.entries[0].changed);

    sig_cache_free(&cache);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_header);
    RUN_TEST(test_find_msg);
    RUN_TEST(test_find_signal);
    RUN_TEST(test_decode_simple);
    RUN_TEST(test_decode_signed);
    RUN_TEST(test_signal_cache_mux);
    RUN_TEST(test_encode_roundtrip);
    RUN_TEST(test_encode_preserves_other_bits);
    RUN_TEST(test_cache_changed_flag);
    return UNITY_END();
}
