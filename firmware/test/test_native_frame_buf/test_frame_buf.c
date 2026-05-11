/*
 * Host test for the length-prefixed framing helper used by the BLE/WS
 * transports. Covers the chunking corner cases that the wire protocol
 * relies on:
 *   - one frame in one append
 *   - one frame split across multiple appends (BLE MTU < frame size)
 *   - multiple frames in one append
 *   - zero-length payload
 *   - oversized length prefix triggers reset
 *   - append overflow triggers reset
 */
#include <unity.h>
#include <string.h>

#include "../../src/protocol/frame_buf.h"
#include "../../src/protocol/frame_buf.c"

static uint8_t backing[64];
static frame_buf_t fb;

void setUp(void) { frame_buf_init(&fb, backing, sizeof(backing)); }
void tearDown(void) {}

/* Helper: prepend a 4-byte little-endian length header to a payload.
 * Default type is JSON (0). */
static void encode(uint8_t *out, const uint8_t *payload, size_t plen)
{
    out[0] = (uint8_t)(plen & 0xFF);
    out[1] = (uint8_t)((plen >> 8) & 0xFF);
    out[2] = (uint8_t)((plen >> 16) & 0xFF);
    out[3] = (uint8_t)((plen >> 24) & 0xFF);
    memcpy(out + 4, payload, plen);
}

/* Same as encode() but stamps a frame-type into the top nibble of the header. */
static void encode_typed(uint8_t *out, uint8_t type, const uint8_t *payload, size_t plen)
{
    encode(out, payload, plen);
    out[3] = (out[3] & 0x0F) | ((type & 0x0F) << 4);
}

void test_single_frame_in_one_append(void)
{
    uint8_t frame[4 + 5];
    encode(frame, (const uint8_t *)"hello", 5);

    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, frame, sizeof(frame)));

    const uint8_t *p; size_t plen;
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL(5, plen);
    TEST_ASSERT_EQUAL_MEMORY("hello", p, 5);

    frame_buf_consume(&fb);
    TEST_ASSERT_EQUAL(0, frame_buf_next(&fb, &p, &plen));
}

/* BLE chunked delivery: a single frame split into many writes. */
void test_frame_split_across_appends(void)
{
    uint8_t frame[4 + 8];
    encode(frame, (const uint8_t *)"abcd1234", 8);

    const uint8_t *p; size_t plen;

    /* One byte at a time. We should see "no frame yet" up to the last byte. */
    for (size_t i = 0; i < sizeof(frame) - 1; i++) {
        TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, &frame[i], 1));
        TEST_ASSERT_EQUAL(0, frame_buf_next(&fb, &p, &plen));
    }
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, &frame[sizeof(frame) - 1], 1));
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL(8, plen);
    TEST_ASSERT_EQUAL_MEMORY("abcd1234", p, 8);
}

/* Multiple frames packed into a single append must all be drainable in
 * order. This is the "client sent a request and a notification arrived
 * while we were buffering" case. */
void test_multiple_frames_in_one_append(void)
{
    uint8_t f1[4 + 3], f2[4 + 4], f3[4 + 1];
    encode(f1, (const uint8_t *)"foo", 3);
    encode(f2, (const uint8_t *)"barz", 4);
    encode(f3, (const uint8_t *)"x", 1);

    uint8_t combined[sizeof(f1) + sizeof(f2) + sizeof(f3)];
    memcpy(combined, f1, sizeof(f1));
    memcpy(combined + sizeof(f1), f2, sizeof(f2));
    memcpy(combined + sizeof(f1) + sizeof(f2), f3, sizeof(f3));

    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, combined, sizeof(combined)));

    const uint8_t *p; size_t plen;
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL_MEMORY("foo", p, 3); frame_buf_consume(&fb);
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL_MEMORY("barz", p, 4); frame_buf_consume(&fb);
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL_MEMORY("x", p, 1); frame_buf_consume(&fb);

    TEST_ASSERT_EQUAL(0, frame_buf_next(&fb, &p, &plen));
}

void test_zero_length_payload(void)
{
    uint8_t frame[4] = {0, 0, 0, 0};

    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, frame, sizeof(frame)));

    const uint8_t *p; size_t plen;
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL(0, plen);
    frame_buf_consume(&fb);
    TEST_ASSERT_EQUAL(0, frame_buf_next(&fb, &p, &plen));
}

/* Garbage / desynced length header bigger than capacity must trigger a
 * reset rather than corrupt later frames. */
void test_oversized_length_resets(void)
{
    uint8_t bad[] = {0xFF, 0xFF, 0xFF, 0xFF};   /* claims 4 GB payload */
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, bad, sizeof(bad)));

    const uint8_t *p; size_t plen;
    TEST_ASSERT_EQUAL(-1, frame_buf_next(&fb, &p, &plen));
    /* After reset, a new valid frame parses cleanly. */
    uint8_t good[4 + 2];
    encode(good, (const uint8_t *)"ok", 2);
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, good, sizeof(good)));
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL_MEMORY("ok", p, 2);
}

void test_append_overflow_resets(void)
{
    uint8_t junk[40];
    memset(junk, 0xAA, sizeof(junk));
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, junk, sizeof(junk)));
    /* Adding another 40 bytes would exceed the 64-byte backing. */
    TEST_ASSERT_EQUAL(-1, frame_buf_append(&fb, junk, sizeof(junk)));

    /* After reset, fresh state. */
    uint8_t good[4 + 1];
    encode(good, (const uint8_t *)"y", 1);
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, good, sizeof(good)));
    const uint8_t *p; size_t plen;
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL_MEMORY("y", p, 1);
}

/* The top nibble of the length header carries the frame type. JSON frames
 * have type 0 (default for legacy frames); binary opcodes use 1..15. */
void test_frame_type_round_trip(void)
{
    uint8_t json_frame[4 + 4];
    encode_typed(json_frame, FRAME_TYPE_JSON, (const uint8_t *)"json", 4);
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, json_frame, sizeof(json_frame)));

    const uint8_t *p; size_t plen;
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL(FRAME_TYPE_JSON, frame_buf_last_type(&fb));
    TEST_ASSERT_EQUAL_MEMORY("json", p, 4);
    frame_buf_consume(&fb);

    uint8_t bin_frame[4 + 3];
    encode_typed(bin_frame, FRAME_TYPE_OTA_WRITE, (const uint8_t *)"raw", 3);
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, bin_frame, sizeof(bin_frame)));
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL(FRAME_TYPE_OTA_WRITE, frame_buf_last_type(&fb));
    TEST_ASSERT_EQUAL(3, plen);
    TEST_ASSERT_EQUAL_MEMORY("raw", p, 3);
}

/* consume() with no preceding successful next() is a no-op. */
void test_consume_without_peek_is_safe(void)
{
    uint8_t frame[4 + 3];
    encode(frame, (const uint8_t *)"abc", 3);
    TEST_ASSERT_EQUAL(0, frame_buf_append(&fb, frame, sizeof(frame)));

    /* No frame_buf_next() yet — consume must NOT skip past the frame. */
    frame_buf_consume(&fb);

    const uint8_t *p; size_t plen;
    TEST_ASSERT_EQUAL(1, frame_buf_next(&fb, &p, &plen));
    TEST_ASSERT_EQUAL_MEMORY("abc", p, 3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_frame_in_one_append);
    RUN_TEST(test_frame_split_across_appends);
    RUN_TEST(test_multiple_frames_in_one_append);
    RUN_TEST(test_zero_length_payload);
    RUN_TEST(test_oversized_length_resets);
    RUN_TEST(test_append_overflow_resets);
    RUN_TEST(test_frame_type_round_trip);
    RUN_TEST(test_consume_without_peek_is_safe);
    return UNITY_END();
}
