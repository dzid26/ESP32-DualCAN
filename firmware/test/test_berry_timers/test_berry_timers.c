/*
 * Host test for timer/action ownership in berry_registry.c.
 *
 * Runs the real Berry VM and the real registry against stubbed ESP-IDF
 * logging and a settable clock, then drives berry_timer_tick(). A timer
 * registered from inside a callback must inherit that callback's owner, so
 * berry_cleanup_script() revokes it.
 */
/* berry.h's be_loadstring() macro calls strlen. */
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "berry.h"
#include "scripting/berry_registry.h"

/* Moves the clock esp_timer_get_time() reports (see stubs.c). */
void stub_set_now_ms(unsigned ms);

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

static int global_int(const char *name)
{
    be_getglobal(vm, name);
    int value = (int)be_toint(vm, -1);
    be_pop(vm, 1);
    return value;
}

static int run_source(const char *src)
{
    int base = be_top(vm);
    if (be_loadstring(vm, src) != 0 || be_pcall(vm, 0) != 0) {
        printf("berry error: %s\n", be_tostring(vm, -1));
        berry_stack_restore(vm, base);
        return -1;
    }
    berry_stack_restore(vm, base);
    return 0;
}

/* Enable a script the way script_loader_enable does: tag the owner, run the
 * chunk, capture setup() and call it. The owner tag is what setup()'s
 * registrations inherit. */
static int enable_script(int owner)
{
    berry_set_current_script(owner);
    if (run_source(SCRIPT) != 0) return -1;
    int setup_ref = berry_capture_global(vm, "setup");
    char err[128] = "";
    int rc = berry_call_ref(vm, setup_ref, err, sizeof(err), NULL, 0);
    berry_set_current_script(SCRIPT_ID_NONE);
    if (rc != 0) printf("setup error: %s\n", err);
    return rc;
}

void test_timer_registered_in_timer_callback_is_revoked(void)
{
    TEST_ASSERT_EQUAL_INT(0, enable_script(7));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    stub_set_now_ms(1000);
    berry_timer_tick(1000);         /* one-shot fires arm_inner() */
    stub_set_now_ms(1600);
    berry_timer_tick(1600);         /* inner timer beats once */

    TEST_ASSERT_EQUAL_INT(1, global_int("beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    berry_cleanup_script(7);
    stub_set_now_ms(2600);
    berry_timer_tick(2600);
    stub_set_now_ms(3600);
    berry_timer_tick(3600);

    TEST_ASSERT_EQUAL_INT(1, global_int("beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_timer_registered_in_action_is_revoked(void)
{
    TEST_ASSERT_EQUAL_INT(0, enable_script(8));

    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("arm"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    stub_set_now_ms(600);
    berry_timer_tick(600);
    TEST_ASSERT_EQUAL_INT(1, global_int("beats"));

    berry_cleanup_script(8);
    stub_set_now_ms(1600);          /* the pending one-shot was due here */
    berry_timer_tick(1600);
    stub_set_now_ms(2600);
    berry_timer_tick(2600);

    TEST_ASSERT_EQUAL_INT(1, global_int("beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_failed_calls_balance_stack(void)
{
    TEST_ASSERT_EQUAL_INT(0, run_source("def bad_cb() raise 'x', 'y' end"));
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
