/*
 * Host test for timer/action ownership and scheduling in berry_registry.c.
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
    /* s_timers[]/s_actions[] are file-static and outlive the VM, so a test that
     * leaves entries behind would eat into the next test's budget. Hand every
     * slot back before the VM goes away. */
    berry_set_current_script(SCRIPT_ID_NONE);
    for (int id = 1; id <= 64; id++) berry_cleanup_script(id);
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

/* ---- helpers ------------------------------------------------------------
 * Registrations are always made under an owner: an ownerless entry is never
 * reclaimed and would relocate into the next test's slot budget.
 */

/* Run a chunk the way a script body runs, with an owner installed and no
 * setup() capture. Keeps .refs countable: only the bindings allocate. */
static int run_as_owner(int owner, const char *src)
{
    berry_set_current_script(owner);
    int rc = berry_test_run_source(vm, src);
    berry_set_current_script(SCRIPT_ID_NONE);
    return rc;
}

/* Length of .refs, or -1 when the list has never been created. refs_alloc()
 * appends and refs_free() nils the hole, so the length only grows: live
 * entries are the retention budget, the length is list memory. */
static int refs_len(void)
{
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) {
        be_pop(vm, 1);
        return -1;
    }
    int size = be_data_size(vm, -1);
    be_pop(vm, 1);
    return size;
}

static int refs_live(void)
{
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) {
        be_pop(vm, 1);
        return 0;
    }
    int size = be_data_size(vm, -1);
    int live = 0;
    for (int i = 0; i < size; i++) {
        be_pushint(vm, i);
        be_getindex(vm, -2);        /* leaves list, key, value */
        if (!be_isnil(vm, -1)) live++;
        be_pop(vm, 2);              /* value and key */
    }
    be_pop(vm, 1);
    return live;
}

/* ---- P3: registry limits and slot reuse --------------------------------- */

/* 16 timers fill the table, the 17th returns nil, and the ref the failed
 * registration allocated before the failure is released again. */
static const char *TIMER_LIMIT_SCRIPT =
    "var fired = 0\n"
    "var filled = 0\n"
    "var overflow_nil = 0\n"
    "def cb()\n"
    "  fired += 1\n"
    "end\n"
    "def fill(n)\n"
    "  var i = 0\n"
    "  var c = 0\n"
    "  while i < n\n"
    "    var h = timer_after(1000, cb)\n"
    "    if h != nil\n"
    "      c += 1\n"
    "    end\n"
    "    i += 1\n"
    "  end\n"
    "  return c\n"
    "end\n";

void test_timer_table_holds_16_and_reuses_cancelled_slots(void)
{
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21, TIMER_LIMIT_SCRIPT));
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21, "filled = fill(16)\n"));
    TEST_ASSERT_EQUAL_INT(16, berry_test_global_int(vm, "filled"));
    TEST_ASSERT_EQUAL_INT(16, refs_live());

    /* Timer 17 is refused with nil, and the ref it allocated first must not
     * stay live (live slots stay at 16 even though the list got a hole). */
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21,
        "var h = timer_after(1000, cb)\n"
        "if h == nil\n"
        "  overflow_nil = 1\n"
        "end\n"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "overflow_nil"));
    TEST_ASSERT_EQUAL_INT(16, refs_live());
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    /* Cancel one and register again: the freed slot is reused, so the new
     * call gets a real handle instead of nil. */
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21,
        "var reused = -1\n"
        "timer_cancel(0)\n"
        "var nh = timer_after(1000, cb)\n"
        "if nh != nil\n"
        "  reused = nh\n"
        "end\n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "reused"));
    TEST_ASSERT_EQUAL_INT(16, refs_live());

    /* cleanup frees the remaining slots for the next script. */
    berry_cleanup_script(21);
    TEST_ASSERT_EQUAL_INT(0, refs_live());
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));

    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21, "refilled = fill(16)\n"));
    TEST_ASSERT_EQUAL_INT(16, berry_test_global_int(vm, "refilled"));
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21,
        "var second_overflow = 0\n"
        "var h = timer_after(1000, cb)\n"
        "if h == nil\n"
        "  second_overflow = 1\n"
        "end\n"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "second_overflow"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* Action table: 32 entries, the 33rd is dropped without allocating a ref. */
static void register_action_range(int owner, int first, int count)
{
    berry_set_current_script(owner);
    TEST_ASSERT_EQUAL_INT(0, berry_test_run_source(vm, "def act() end"));
    for (int i = first; i < first + count; i++) {
        char src[64];
        snprintf(src, sizeof(src), "action_register('a%d', act)", i);
        TEST_ASSERT_EQUAL_INT(0, berry_test_run_source(vm, src));
    }
    berry_set_current_script(SCRIPT_ID_NONE);
}

void test_action_table_holds_32_and_cleanup_frees_slots(void)
{
    register_action_range(22, 0, 32);
    TEST_ASSERT_EQUAL_INT(32, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(32, refs_live());
    int live_before = refs_live();
    int len_before = refs_len();

    /* The 33rd registration is refused before it allocates a ref, so neither
     * the table nor the ref list changes. */
    register_action_range(22, 32, 1);
    TEST_ASSERT_EQUAL_INT(32, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(live_before, refs_live());
    TEST_ASSERT_EQUAL_INT(len_before, refs_len());

    berry_cleanup_script(22);
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, refs_live());

    register_action_range(22, 0, 32);
    TEST_ASSERT_EQUAL_INT(32, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* The native platform links no test main of its own, so the suite provides
 * one, like the other tests-native suites. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_timer_registered_in_timer_callback_is_revoked);
    RUN_TEST(test_timer_registered_in_action_is_revoked);
    RUN_TEST(test_failed_calls_balance_stack);
    RUN_TEST(test_timer_table_holds_16_and_reuses_cancelled_slots);
    RUN_TEST(test_action_table_holds_32_and_cleanup_frees_slots);
    return UNITY_END();
}
