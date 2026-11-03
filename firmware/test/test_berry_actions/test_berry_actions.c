/*
 * Host test for action registry semantics in berry_registry.c.
 *
 * Actions are named closures owned by the script that registered them.
 * Re-registering a name replaces the entry and releases the old ref; the
 * owner of the entry is installed while the callback runs, so anything the
 * callback registers inherits it and is revoked on disable.
 */
#include <unity.h>

#include "berry_test.h"

/* Every case runs as this owner, so tearDown can sweep whatever a failing
 * assertion left behind in the process-wide action registry. */
#define OWNER 7

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
    berry_cleanup_script(OWNER);
    berry_set_current_script(SCRIPT_ID_NONE);
    be_vm_delete(vm);
    vm = NULL;
}

/* Live (non-nil) entries in the hidden ".refs" list: one per closure the C
 * side still holds. A replaced action must not add one. */
static int refs_live(bvm *vm)
{
    if (!be_getglobal(vm, ".refs") || be_isnil(vm, -1)) {
        be_pop(vm, 1);
        return 0;
    }
    int size = be_data_size(vm, -1);
    int live = 0;
    for (int i = 0; i < size; i++) {
        be_pushint(vm, i);
        be_getindex(vm, -2);
        if (!be_isnil(vm, -1)) live++;
        be_pop(vm, 2);   /* value and the key be_getindex left behind */
    }
    be_pop(vm, 1);
    return live;
}

/* Number of registered actions, and the name of the first one if any. */
static int action_count(const char **first)
{
    const char *names[8] = {0};
    int found = berry_actions_snapshot(names, 8);
    if (first) *first = names[0];
    return found;
}

void test_reregistering_a_name_replaces_the_entry_and_frees_the_old_ref(void)
{
    static const char *SCRIPT =
        "var hits = 0\n"
        "def first()\n"
        "  hits += 100\n"
        "end\n"
        "def second()\n"
        "  hits += 1\n"
        "end\n"
        "def setup()\n"
        "  action_register('dup', first)\n"
        "  action_register('dup', second)\n"
        "end\n";

    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, OWNER, SCRIPT));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    const char *name = NULL;
    TEST_ASSERT_EQUAL_INT(1, action_count(&name));
    TEST_ASSERT_EQUAL_STRING("dup", name);
    /* Two refs: the captured setup() plus the surviving action. The first
     * body's ref must be nil, not a third live entry. */
    TEST_ASSERT_EQUAL_INT(2, refs_live(vm));

    /* The second body won, and the first one is no longer reachable. */
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("dup"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "hits"));
    TEST_ASSERT_EQUAL_INT(2, refs_live(vm));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_invoke_after_disable_returns_minus_one(void)
{
    static const char *SCRIPT =
        "var hits = 0\n"
        "def body()\n"
        "  hits += 1\n"
        "end\n"
        "def setup()\n"
        "  action_register('gone', body)\n"
        "end\n";

    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, OWNER, SCRIPT));
    TEST_ASSERT_EQUAL_INT(1, action_count(NULL));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("gone"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "hits"));

    berry_cleanup_script(OWNER);

    TEST_ASSERT_EQUAL_INT(0, action_count(NULL));
    TEST_ASSERT_EQUAL_INT(-1, berry_action_invoke("gone"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "hits"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_action_registers_another_action_owned_by_the_same_script(void)
{
    static const char *SCRIPT =
        "var hits = 0\n"
        "def inner_body()\n"
        "  hits += 1\n"
        "end\n"
        "def outer_body()\n"
        "  action_register('inner', inner_body)\n"
        "end\n"
        "def setup()\n"
        "  action_register('outer', outer_body)\n"
        "end\n";

    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, OWNER, SCRIPT));
    TEST_ASSERT_EQUAL_INT(1, action_count(NULL));

    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("outer"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
    TEST_ASSERT_EQUAL_INT(2, action_count(NULL));

    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("inner"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "hits"));

    /* The action registered from inside a callback inherited the owner, so
     * disabling the script takes it down too. */
    berry_cleanup_script(OWNER);
    TEST_ASSERT_EQUAL_INT(0, action_count(NULL));
    TEST_ASSERT_EQUAL_INT(-1, berry_action_invoke("inner"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_action_reregisters_itself_during_invocation(void)
{
    static const char *SCRIPT =
        "var hits = 0\n"
        "def second()\n"
        "  hits += 10\n"
        "end\n"
        /* Berry resolves a name used in a function body at compile time, so
         * the replacement body has to be declared before the first one. */
        "def first()\n"
        "  hits += 1\n"
        "  action_register('self', second)\n"
        "end\n"
        "def setup()\n"
        "  action_register('self', first)\n"
        "end\n";

    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, OWNER, SCRIPT));
    TEST_ASSERT_EQUAL_INT(1, action_count(NULL));

    /* The running body replaces its own entry mid-call. The invocation must
     * finish on the snapshot it started with and leave one entry behind. */
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("self"));
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "hits"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    const char *name = NULL;
    TEST_ASSERT_EQUAL_INT(1, action_count(&name));
    TEST_ASSERT_EQUAL_STRING("self", name);

    /* The replacement is what the next invocation runs. */
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("self"));
    TEST_ASSERT_EQUAL_INT(11, berry_test_global_int(vm, "hits"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_snapshot_below_count_returns_the_found_total(void)
{
    static const char *SCRIPT =
        "def body()\n"
        "end\n"
        "def setup()\n"
        "  action_register('a', body)\n"
        "  action_register('b', body)\n"
        "  action_register('c', body)\n"
        "end\n";

    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, OWNER, SCRIPT));

    const char *one[1] = {0};
    TEST_ASSERT_EQUAL_INT(3, berry_actions_snapshot(one, 1));
    TEST_ASSERT_NOT_NULL(one[0]);

    const char *none[1] = {0};
    TEST_ASSERT_EQUAL_INT(3, berry_actions_snapshot(none, 0));
    TEST_ASSERT_NULL(none[0]);

    const char *all[8] = {0};
    TEST_ASSERT_EQUAL_INT(3, berry_actions_snapshot(all, 8));
    TEST_ASSERT_NOT_NULL(all[0]);
    TEST_ASSERT_NOT_NULL(all[1]);
    TEST_ASSERT_NOT_NULL(all[2]);
    TEST_ASSERT_NULL(all[3]);
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* The native platform links no test main of its own, so the suite provides
 * one, like the other tests-native suites. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reregistering_a_name_replaces_the_entry_and_frees_the_old_ref);
    RUN_TEST(test_invoke_after_disable_returns_minus_one);
    RUN_TEST(test_action_registers_another_action_owned_by_the_same_script);
    RUN_TEST(test_action_reregisters_itself_during_invocation);
    RUN_TEST(test_snapshot_below_count_returns_the_found_total);
    return UNITY_END();
}
