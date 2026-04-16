/*
 * Host test for message mirror, auto-checksum, and auto-counter.
 *
 * Builds a DBC with a message that has _Checksum and _Counter signals,
 * verifies that prepare_tx auto-computes them correctly.
 */
#include <unity.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/dbc/dbc_types.h"
#include "../../src/dbc/dbc_decode.h"
#include "../../src/dbc/dbc_decode.c"
#include "../../src/dbc/signal_cache.h"
#include "../../src/dbc/signal_cache.c"
#include "../../src/dbc/msg_mirror.h"
#include "../../src/dbc/msg_mirror.c"

/*
 * Test DBC: one message 0x118 (ID 280), DLC 8.
 * Signals:
 *   DI_torqueEstimate   start=0  len=13  LE signed  scale=0.25  offset=0
 *   DI_counter          start=48 len=4   LE unsigned scale=1     offset=0
 *   DI_checksum         start=56 len=8   LE unsigned scale=1     offset=0
 */
static uint8_t blob[512];
static size_t blob_len;

static void build_blob(void)
{
    memset(blob, 0, sizeof(blob));

    char strs[128];
    uint16_t offs[4];
    int sp = 0;
    const char *names[] = {"DI_torqueEstimate", "DI_counter", "DI_checksum", "msg_118"};
    for (int i = 0; i < 4; i++) {
        offs[i] = (uint16_t)sp;
        int n = strlen(names[i]) + 1;
        memcpy(strs + sp, names[i], n);
        sp += n;
    }

    dbc_sig_t sigs[3];
    memset(sigs, 0, sizeof(sigs));

    sigs[0].name_off = offs[0];
    sigs[0].start_bit = 0;
    sigs[0].bit_length = 13;
    sigs[0].flags = DBC_SIG_SIGNED;
    sigs[0].scale = 0.25f;
    sigs[0].offset = 0.0f;

    sigs[1].name_off = offs[1];
    sigs[1].start_bit = 48;
    sigs[1].bit_length = 4;
    sigs[1].flags = 0;
    sigs[1].scale = 1.0f;
    sigs[1].offset = 0.0f;

    sigs[2].name_off = offs[2];
    sigs[2].start_bit = 56;
    sigs[2].bit_length = 8;
    sigs[2].flags = 0;
    sigs[2].scale = 1.0f;
    sigs[2].offset = 0.0f;

    dbc_msg_t msgs[1];
    memset(msgs, 0, sizeof(msgs));
    msgs[0].id = 0x118;
    msgs[0].name_off = offs[3];
    msgs[0].dlc = 8;
    msgs[0].sig_count = 3;
    msgs[0].sig_start = 0;

    dbc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "DBC\0", 4);
    hdr.version = DBC_VERSION;
    hdr.msg_count = 1;
    hdr.sig_count = 3;
    hdr.msgs_offset = sizeof(hdr);
    hdr.sigs_offset = hdr.msgs_offset + sizeof(msgs);
    hdr.strs_offset = hdr.sigs_offset + sizeof(sigs);
    hdr.strs_size = (uint32_t)sp;

    uint8_t *p = blob;
    memcpy(p, &hdr, sizeof(hdr));  p += sizeof(hdr);
    memcpy(p, msgs, sizeof(msgs)); p += sizeof(msgs);
    memcpy(p, sigs, sizeof(sigs)); p += sizeof(sigs);
    memcpy(p, strs, sp);           p += sp;
    blob_len = (size_t)(p - blob);
}

static dbc_t dbc;
static msg_mirror_t mirror;

void setUp(void)
{
    build_blob();
    TEST_ASSERT_EQUAL(0, dbc_load(&dbc, blob, blob_len));
    TEST_ASSERT_EQUAL(0, msg_mirror_init(&mirror, &dbc));
}

void tearDown(void)
{
    msg_mirror_free(&mirror);
}

void test_tesla_checksum_direct(void)
{
    /* Known test vector: address 0x118 = bytes (0x18, 0x01).
     * sum_addr = 0x18 + 0x01 = 0x19.
     * data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00} (cksum at [7])
     * sum_data = 1+2+3+4+5+6+7 = 28 = 0x1C.
     * total = 0x19 + 0x1C = 0x35. */
    uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00};
    uint8_t ck = tesla_checksum(0x118, 7, data, 8);
    TEST_ASSERT_EQUAL_HEX8(0x35, ck);
}

void test_mirror_rx_draft(void)
{
    uint8_t rx_data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
    msg_mirror_rx(&mirror, 0x118, rx_data, 8);

    uint8_t out[8];
    uint8_t dlc;
    int idx = msg_mirror_draft(&mirror, 0x118, out, &dlc);
    TEST_ASSERT_GREATER_OR_EQUAL(0, idx);
    TEST_ASSERT_EQUAL(8, dlc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rx_data, out, 8);
}

void test_prepare_tx_counter_increments(void)
{
    uint8_t data[8] = {0};

    int idx = msg_mirror_draft(&mirror, 0x118, data, &(uint8_t){0});
    msg_mirror_prepare_tx(&mirror, idx, data);
    float c1 = dbc_decode_signal(&dbc.sigs[1], data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, c1);

    msg_mirror_prepare_tx(&mirror, idx, data);
    float c2 = dbc_decode_signal(&dbc.sigs[1], data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, c2);
}

void test_prepare_tx_checksum_computed(void)
{
    uint8_t data[8] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    int idx = msg_mirror_draft(&mirror, 0x118, data, &(uint8_t){0});
    msg_mirror_prepare_tx(&mirror, idx, data);

    /* Verify checksum at byte 7 matches the Tesla algorithm. */
    uint8_t expected = tesla_checksum(0x118, 7, data, 8);
    TEST_ASSERT_EQUAL_HEX8(expected, data[7]);
}

void test_encode_signal_then_checksum(void)
{
    uint8_t data[8] = {0};
    int idx = msg_mirror_draft(&mirror, 0x118, data, &(uint8_t){0});

    /* Set torque to 100.0 (raw = 100/0.25 = 400) */
    dbc_encode_signal(&dbc.sigs[0], data, 100.0f);

    msg_mirror_prepare_tx(&mirror, idx, data);

    /* Torque should survive the prepare_tx */
    float torque = dbc_decode_signal(&dbc.sigs[0], data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, torque);

    /* Checksum should be valid */
    uint8_t expected = tesla_checksum(0x118, 7, data, 8);
    TEST_ASSERT_EQUAL_HEX8(expected, data[7]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tesla_checksum_direct);
    RUN_TEST(test_mirror_rx_draft);
    RUN_TEST(test_prepare_tx_counter_increments);
    RUN_TEST(test_prepare_tx_checksum_computed);
    RUN_TEST(test_encode_signal_then_checksum);
    return UNITY_END();
}
