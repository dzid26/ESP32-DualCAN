/*
 * Host test for timer/action ownership in berry_registry.c.
 *
 * Runs the real Berry VM and the real registry against stubbed ESP-IDF
 * logging and a settable clock, then drives berry_timer_tick(). A timer
 * registered from inside a callback must inherit that callback's owner, so
 * berry_cleanup_script() revokes it.
 */
#include <unity.h>

#include "berry_test.h"

static const char *SCRIPT =
    "var beats = 0\n"
    "def inner_tick()\n"
    "  beats += 1\n"
    "end\n"
    "def arm_inner()\n"
    "  timer_every(500, inner_tick)\n"      /* armed inside a timer callback */
    "end\n"
    "def action_arm()\n"
    "  timer_every(500, inner_tick)\n"      /* armed inside an action body */
    "end\n"
    "def setup()\n"
    "  timer_after(1000, arm_inner)\n"
    "  action_register('arm', action_arm)\n"
    "end\n";

static bvm *vm;
/* be_top() right after binding registration. Every call into Berry must leave
 * the value stack at this watermark, or an error path is leaking values. */
static int stack_base;

void setUp(void)
{
    vm = be_vm_new();
    berry_register_registry(vm);
    stack_base = be_top(vm);
    stub_set_now_ms(0);
}

void tearDown(void)
{
    berry_set_current_script(SCRIPT_ID_NONE);
    be_vm_delete(vm);
    vm = NULL;
}

void test_timer_registered_in_timer_callback_is_revoked(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 7, SCRIPT));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    stub_set_now_ms(1000);
    berry_timer_tick(1000);         /* one-shot fires arm_inner() */
    stub_set_now_ms(1600);
    berry_timer_tick(1600);         /* inner timer beats once */

    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    berry_cleanup_script(7);
    stub_set_now_ms(2600);
    berry_timer_tick(2600);
    stub_set_now_ms(3600);
    berry_timer_tick(3600);

    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_timer_registered_in_action_is_revoked(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 8, SCRIPT));

    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("arm"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    stub_set_now_ms(600);
    berry_timer_tick(600);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));

    berry_cleanup_script(8);
    stub_set_now_ms(1600);          /* the pending one-shot was due here */
    berry_timer_tick(1600);
    stub_set_now_ms(2600);
    berry_timer_tick(2600);

    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_failed_calls_balance_stack(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_run_source(vm, "def bad_cb() raise 'x', 'y' end"));
    int bad_ref = berry_capture_global(vm, "bad_cb");
    int base = be_top(vm);

    for (int i = 0; i < 5; i++)
        TEST_ASSERT_EQUAL_INT(-2, berry_call_ref(vm, bad_ref, NULL, 0, NULL, 0));

    TEST_ASSERT_EQUAL_INT(base, be_top(vm));
}

/* The native platform links no test main of its own, so the suite provides
 * one, like the other tests-native suites. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_timer_registered_in_timer_callback_is_revoked);
    RUN_TEST(test_timer_registered_in_action_is_revoked);
    RUN_TEST(test_failed_calls_balance_stack);
    return UNITY_END();
}
