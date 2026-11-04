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

/* ---- helpers ------------------------------------------------------------ */

static void tick_at(unsigned now_ms)
{
    stub_set_now_ms(now_ms);
    berry_timer_tick(now_ms);
}

/* Run a chunk with an owner installed, like a script body does. Every
 * registration in this file goes through here: an ownerless entry is never
 * reclaimed and would eat a slot in the next test. */
static int run_as_owner(int owner, const char *src)
{
    berry_set_current_script(owner);
    int rc = berry_test_run_source(vm, src);
    berry_set_current_script(SCRIPT_ID_NONE);
    return rc;
}

/* Length of .refs, or -1 when the list has never been created. Allocation
 * reuses the lowest nil slot before appending, so the length is the high-water
 * mark of simultaneous refs (list memory), while refs_live() is the retention
 * budget. */
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

/* ---- registry limits and slot reuse ------------------------------------ */

/* 16 timers fill the table, the 17th returns nil, and the ref the failed
 * registration allocated before the failure is released again. */
static const char *TIMER_LIMIT_SCRIPT =
    "var filled = 0\n"
    "var overflow_nil = 0\n"
    "def cb()\n"
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

/* Register one timer past the limit; 1 when the call returned nil. */
static int timer_overflowed(int owner)
{
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(owner,
        "overflow_nil = 0\n"
        "var h = timer_after(1000, cb)\n"
        "if h == nil\n"
        "  overflow_nil = 1\n"
        "end\n"));
    return berry_test_global_int(vm, "overflow_nil");
}

void test_timer_table_holds_16_and_reuses_cancelled_slots(void)
{
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21, TIMER_LIMIT_SCRIPT));
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21, "filled = fill(16)\n"));
    TEST_ASSERT_EQUAL_INT(16, berry_test_global_int(vm, "filled"));
    TEST_ASSERT_EQUAL_INT(16, refs_live());

    /* The failed registration leaves a hole in the list but no live ref. */
    TEST_ASSERT_EQUAL_INT(1, timer_overflowed(21));
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

    berry_cleanup_script(21);
    TEST_ASSERT_EQUAL_INT(0, refs_live());
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));

    TEST_ASSERT_EQUAL_INT(0, run_as_owner(21, "refilled = fill(16)\n"));
    TEST_ASSERT_EQUAL_INT(16, berry_test_global_int(vm, "refilled"));
    TEST_ASSERT_EQUAL_INT(1, timer_overflowed(21));
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
    int live_before = refs_live();
    int len_before = refs_len();
    TEST_ASSERT_EQUAL_INT(32, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(32, live_before);

    /* The refusal happens before refs_alloc(), so nothing changes. */
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

/* ---- scheduling --------------------------------------------------------- */

static const char *ONE_SHOT_SCRIPT =
    "var beats = 0\n"
    "var h = -1\n"
    "def cb()\n"
    "  beats += 1\n"
    "end\n"
    "def setup()\n"
    "  h = timer_after(1000, cb)\n"
    "end\n";

void test_one_shot_fires_once_and_frees_its_slot(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 31, ONE_SHOT_SCRIPT));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "h"));

    tick_at(1000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));

    tick_at(2000);
    tick_at(3000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    TEST_ASSERT_EQUAL_INT(0, run_as_owner(31,
        "var h2 = timer_after(1000, cb)\n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "h2"));
}

static const char *REPEAT_SCRIPT =
    "var beats = 0\n"
    "var hr = -1\n"
    "def tick()\n"
    "  beats += 1\n"
    "end\n"
    "def setup()\n"
    "  hr = timer_every(1000, tick)\n"
    "end\n";

void test_repeat_fires_per_period_and_cancel_stops_it(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 32, REPEAT_SCRIPT));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "hr"));

    tick_at(999);
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "beats"));
    tick_at(1000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    tick_at(1500);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    tick_at(2000);
    TEST_ASSERT_EQUAL_INT(2, berry_test_global_int(vm, "beats"));
    tick_at(3000);
    TEST_ASSERT_EQUAL_INT(3, berry_test_global_int(vm, "beats"));

    TEST_ASSERT_EQUAL_INT(0, berry_test_run_source(vm, "timer_cancel(hr)\n"));
    tick_at(4000);
    tick_at(5000);
    TEST_ASSERT_EQUAL_INT(3, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* A callback may cancel itself; a later tick must not resurrect it. */
void test_cancel_from_inside_callback(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 33,
        "var beats = 0\n"
        "var self_h = -1\n"
        "def tick()\n"
        "  beats += 1\n"
        "  if beats == 2\n"
        "    timer_cancel(self_h)\n"
        "  end\n"
        "end\n"
        "def setup()\n"
        "  self_h = timer_every(1000, tick)\n"
        "end\n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "self_h"));

    tick_at(1000);
    tick_at(2000);
    TEST_ASSERT_EQUAL_INT(2, berry_test_global_int(vm, "beats"));

    tick_at(3000);
    tick_at(4000);
    TEST_ASSERT_EQUAL_INT(2, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_one_shot_callback_reuses_its_own_slot(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 34,
        "var n = 0\n"
        "var inner_h = -99\n"
        "def inner()\n"
        "  n += 10\n"
        "end\n"
        "def outer()\n"
        "  n += 1\n"
        "  inner_h = timer_after(1000, inner)\n"
        "end\n"
        "def setup()\n"
        "  timer_after(1000, outer)\n"
        "end\n"));

    tick_at(1000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "inner_h"));

    tick_at(2000);
    TEST_ASSERT_EQUAL_INT(11, berry_test_global_int(vm, "n"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_two_timers_due_in_one_tick_both_fire(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 35,
        "var a = 0\n"
        "var b = 0\n"
        "def da()\n"
        "  a += 1\n"
        "end\n"
        "def db()\n"
        "  b += 1\n"
        "end\n"
        "def setup()\n"
        "  timer_after(1000, da)\n"
        "  timer_every(1000, db)\n"
        "end\n"));

    tick_at(1000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "a"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "b"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* Due-time comparison must be signed: a timer armed just before the 32-bit
 * millisecond clock wraps is not due in the low numbers that follow. */
void test_due_comparison_survives_clock_wrap(void)
{
    stub_set_now_ms(0xFFFFFF00u);   /* 256 ms before the wrap */
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 36,
        "var w = 0\n"
        "def wcb()\n"
        "  w += 1\n"
        "end\n"
        "def setup()\n"
        "  timer_every(500, wcb)\n"
        "end\n"));

    tick_at(200);                   /* unsigned: 200 - 244 looks huge */
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "w"));
    tick_at(244);                   /* wrapped due time */
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "w"));
    tick_at(300);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "w"));
    tick_at(744);
    TEST_ASSERT_EQUAL_INT(2, berry_test_global_int(vm, "w"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* timer_every(0) pins the current semantics: period 0 is the one-shot marker
 * (timer_entry_t.period_ms), so it fires once and frees its slot. Flagged as a
 * design question for the owner, not asserted as a defect. */
void test_timer_every_zero_behaves_as_one_shot(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 37,
        "var z = 0\n"
        "def zcb()\n"
        "  z += 1\n"
        "end\n"
        "def setup()\n"
        "  timer_every(0, zcb)\n"
        "end\n"));

    tick_at(1000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "z"));
    tick_at(2000);
    tick_at(3000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "z"));
    /* The slot came back, so an ordinary one-shot reuses it. */
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(37,
        "var hz2 = timer_after(1000, zcb)\n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "hz2"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* ---- .refs growth on register/cancel churn ------------------------------ */
/* refs_alloc() reuses a freed nil slot, so churn must not grow .refs: 20
 * register/cancel cycles leave both the list length and the live-entry count
 * unchanged. */
void test_refs_do_not_grow_on_register_cancel_churn(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 38,
        "def cb()\n"
        "end\n"
        "def churn(n)\n"
        "  var i = 0\n"
        "  while i < n\n"
        "    var h = timer_after(1000, cb)\n"
        "    timer_cancel(h)\n"
        "    i += 1\n"
        "  end\n"
        "end\n"
        "def setup()\n"
        "  churn(1)\n"     /* warm up: creates .refs */
        "end\n"));

    int len_before = refs_len();
    int live_before = refs_live();
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(38, "churn(20)\n"));
    TEST_ASSERT_EQUAL_INT(live_before, refs_live());
    TEST_ASSERT_EQUAL_INT(len_before, refs_len());
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
    RUN_TEST(test_one_shot_fires_once_and_frees_its_slot);
    RUN_TEST(test_repeat_fires_per_period_and_cancel_stops_it);
    RUN_TEST(test_cancel_from_inside_callback);
    RUN_TEST(test_one_shot_callback_reuses_its_own_slot);
    RUN_TEST(test_two_timers_due_in_one_tick_both_fire);
    RUN_TEST(test_due_comparison_survives_clock_wrap);
    RUN_TEST(test_timer_every_zero_behaves_as_one_shot);
    RUN_TEST(test_refs_do_not_grow_on_register_cancel_churn);
    return UNITY_END();
}
