/*
 * Host test for the per-CAN-ID TX rate limiter.
 *
 * The limiter takes a "now" timestamp in microseconds, so the test fast-
 * forwards the clock instead of waiting in real time.
 *
 * Note: the limiter treats `last_tx_us == 0` as "previous send timestamp"
 * rather than "never sent", which is fine in production because
 * esp_timer_get_time() is many ms above zero by the time the first frame
 * goes out. Tests therefore start `now_us` past one interval window so
 * the first allow() call passes.
 */
#include <unity.h>
#include <stdint.h>

#include "../../src/can/rate_limit.h"
#include "../../src/can/rate_limit.c"

void setUp(void) {}
void tearDown(void) {}

/* Default rate is 100 Hz -> 10 ms minimum interval. */
void test_default_rate_blocks_within_window(void)
{
    rate_limiter_t rl;
    rate_limiter_init(&rl, 100);

    /* First send always allowed. */
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1000000));
    /* Same id 5 ms later: blocked. */
    TEST_ASSERT_FALSE(rate_limiter_allow(&rl, 0x123, 1005000));
    /* Same id 10 ms later: allowed. */
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1010000));
}

void test_different_ids_are_independent(void)
{
    rate_limiter_t rl;
    rate_limiter_init(&rl, 100);

    /* Each id has an independent timestamp slot. */
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x100, 1000000));
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x200, 1000000));
    /* Both blocked 1 ms later. */
    TEST_ASSERT_FALSE(rate_limiter_allow(&rl, 0x100, 1001000));
    TEST_ASSERT_FALSE(rate_limiter_allow(&rl, 0x200, 1001000));
}

void test_per_id_override_tighter(void)
{
    rate_limiter_t rl;
    rate_limiter_init(&rl, 100);                /* default 10 ms */
    rate_limiter_set(&rl, 0x123, 10);           /* 100 ms for 0x123 */

    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1000000));
    /* 50 ms later — would pass at 100 Hz but blocked at 10 Hz. */
    TEST_ASSERT_FALSE(rate_limiter_allow(&rl, 0x123, 1050000));
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1100000));

    /* Other ids still on default 10 ms interval. */
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x456, 1000000));
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x456, 1010000));
}

void test_per_id_override_looser(void)
{
    rate_limiter_t rl;
    rate_limiter_init(&rl, 10);                 /* default 100 ms */
    rate_limiter_set(&rl, 0x123, 1000);         /* 1 ms for 0x123 */

    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1000000));
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1001000));
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1002000));
}

void test_overflow_returns_false_for_new_ids(void)
{
    rate_limiter_t rl;
    rate_limiter_init(&rl, 100);

    /* Fill the table with unique ids (RATE_LIMIT_MAX_IDS == 64). */
    for (int i = 0; i < RATE_LIMIT_MAX_IDS; i++) {
        TEST_ASSERT_TRUE(rate_limiter_allow(&rl, (uint32_t)i, 1000000));
    }
    /* The next new id has nowhere to go — should be rejected, not crash. */
    TEST_ASSERT_FALSE(rate_limiter_allow(&rl, 0xDEADBEEF, 1000000));

    /* Existing ids still work after their interval elapses. */
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0, 1010000));
}

void test_zero_default_means_unlimited(void)
{
    rate_limiter_t rl;
    rate_limiter_init(&rl, 0);                  /* disabled */

    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1000000));
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1000001));
    TEST_ASSERT_TRUE(rate_limiter_allow(&rl, 0x123, 1000002));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_rate_blocks_within_window);
    RUN_TEST(test_different_ids_are_independent);
    RUN_TEST(test_per_id_override_tighter);
    RUN_TEST(test_per_id_override_looser);
    RUN_TEST(test_overflow_returns_false_for_new_ids);
    RUN_TEST(test_zero_default_means_unlimited);
    return UNITY_END();
}
