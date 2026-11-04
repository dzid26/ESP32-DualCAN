/*
 * Host probe for test_scripts/manual/timer_owner_leak.be.
 *
 * The manual device check enables that script, waits for the inner beat and
 * disables it; beats must stop. This runs the same file on the host VM, so
 * the probe that gets used by hand on hardware is also exercised in CI.
 *
 * The suite runs with CWD = firmware/, so the script is read relative to the
 * project directory.
 */
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "berry_test.h"

#define OWNER 21
#define PROBE_PATH "test_scripts/manual/timer_owner_leak.be"
#define PROBE_MAX 4096

static bvm *vm;
/* be_top() right after binding registration. Every call into Berry must leave
 * the value stack at this watermark, or an error path is leaking values. */
static int stack_base;
static char probe_src[PROBE_MAX];

void setUp(void)
{
    vm = be_vm_new();
    berry_register_registry(vm);
    stack_base = be_top(vm);
    stub_set_now_ms(0);
    /* The action table is process-wide: fail here, not in whatever test the
     * leftover lands on. */
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));
}

void tearDown(void)
{
    berry_cleanup_script(OWNER);
    berry_set_current_script(SCRIPT_ID_NONE);
    be_vm_delete(vm);
    vm = NULL;
}

/* Read the probe script off the host filesystem. Returns its length, or -1 if
 * it cannot be read whole: a truncated chunk would compile to a Berry error
 * at a meaningless line, or silently drop the tail of the probe. */
static int load_probe(void)
{
    FILE *f = fopen(PROBE_PATH, "rb");
    if (!f) return -1;
    size_t n = fread(probe_src, 1, sizeof(probe_src) - 1, f);
    int complete = !ferror(f) && feof(f);
    fclose(f);
    if (!complete) return -1;
    probe_src[n] = '\0';
    return (int)n;
}

void test_probe_timers_stop_after_disable(void)
{
    /* Unity does not abort a test on a failed assertion in this harness
     * (unity_config.h excludes setjmp), so bail out by hand: the next call
     * would enable an empty script. */
    if (load_probe() <= 0) {
        TEST_FAIL_MESSAGE("cannot read " PROBE_PATH " - CWD must be firmware/");
        return;
    }

    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, OWNER, probe_src));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
    /* setup() registers the "arm" action plus the one-shot arming the beat. */
    TEST_ASSERT_EQUAL_INT(1, berry_actions_snapshot(NULL, 0));

    stub_set_now_ms(1000);
    berry_timer_tick(1000);         /* one-shot fires arm_inner() */
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "beats"));

    stub_set_now_ms(1600);
    berry_timer_tick(1600);         /* inner timer beats once */
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));

    stub_set_now_ms(2600);
    berry_timer_tick(2600);
    TEST_ASSERT_EQUAL_INT(2, berry_test_global_int(vm, "beats"));

    /* Disable: the timer armed from inside the callback must be revoked
     * along with the action the script registered. */
    berry_cleanup_script(OWNER);
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));

    stub_set_now_ms(3600);
    berry_timer_tick(3600);
    stub_set_now_ms(4600);
    berry_timer_tick(4600);

    TEST_ASSERT_EQUAL_INT(2, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* The native platform links no test main of its own, so the suite provides
 * one, like the other tests-native suites. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_probe_timers_stop_after_disable);
    return UNITY_END();
}
