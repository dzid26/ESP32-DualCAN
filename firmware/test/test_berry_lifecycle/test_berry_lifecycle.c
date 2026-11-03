/*
 * Host test for the script_loader lifecycle.
 *
 * Runs the real Berry VM, the real registry and the real script_loader
 * against stubbed ESP-IDF logging and a settable clock. P1 builds a loader
 * entry by hand (no filesystem) and drives script_loader_disable(), so the
 * teardown-before-sweep ordering and the clear/refs bookkeeping are covered
 * instead of berry_cleanup_script() in isolation.
 */
#include <unity.h>

#include "berry_test.h"
#include "scripting/script_loader.h"

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

/* ---- P1: script_loader_disable integration ---- */

static const char *HAND_BUILT_SCRIPT =
    "var beats = 0\n"
    "var teardown_ran = 0\n"
    "def tick()\n"
    "  beats += 1\n"
    "end\n"
    "def setup()\n"
    "  timer_every(100, tick)\n"
    "  action_register('ping', tick)\n"
    "end\n"
    "def teardown()\n"
    "  teardown_ran += 1\n"
    "  timer_every(100, tick)\n"      /* armed by teardown: must be swept after */
    "end\n";

/* Count the live (non-nil) entries in the VM's hidden refs list. */
static int refs_live(bvm *vm)
{
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) { be_pop(vm, 1); return 0; }
    int n = be_data_size(vm, -1);
    int live = 0;
    for (int i = 0; i < n; i++) {
        be_pushint(vm, i);
        be_getindex(vm, -2);       /* pushes the value, keeps the key */
        if (!be_isnil(vm, -1)) live++;
        be_pop(vm, 2);
    }
    be_pop(vm, 1);
    return live;
}

void test_disable_runs_teardown_then_sweeps(void)
{
    int setup_ref = -1, teardown_ref = -1;
    TEST_ASSERT_EQUAL_INT(0, berry_test_capture(vm, HAND_BUILT_SCRIPT,
                                                &setup_ref, &teardown_ref));
    TEST_ASSERT_TRUE(setup_ref >= 0);
    TEST_ASSERT_TRUE(teardown_ref >= 0);

    /* One loaded entry, built by hand: no scan, no files. */
    script_loader_t loader;
    memset(&loader, 0, sizeof(loader));
    loader.vm = vm;
    loader.count = 1;
    script_entry_t *entry = &loader.scripts[0];
    strcpy(entry->filename, "hand_built.be");
    entry->script_id = 7;
    entry->setup_ref = setup_ref;
    entry->teardown_ref = teardown_ref;
    entry->loaded = true;
    entry->enabled = true;

    /* Run setup() the way script_loader_enable does, under the owner. */
    berry_set_current_script(entry->script_id);
    char err[128] = "";
    TEST_ASSERT_EQUAL_INT(0, berry_call_ref(vm, entry->setup_ref,
                                            err, sizeof(err), NULL, 0));
    berry_set_current_script(SCRIPT_ID_NONE);
    TEST_ASSERT_EQUAL_STRING("", err);
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    /* Setup's timer and action are live. */
    TEST_ASSERT_EQUAL_INT(1, berry_actions_snapshot(NULL, 0));
    stub_set_now_ms(100);
    berry_timer_tick(100);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));

    TEST_ASSERT_EQUAL_INT(0, script_loader_disable(&loader, 0));

    /* Teardown ran once, before the sweep: the timer it armed is gone too.
     * If the sweep ran first, that timer would survive and beat below. */
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "teardown_ran"));
    TEST_ASSERT_FALSE(entry->loaded);
    TEST_ASSERT_FALSE(entry->enabled);
    TEST_ASSERT_EQUAL_INT(-1, entry->setup_ref);
    TEST_ASSERT_EQUAL_INT(-1, entry->teardown_ref);
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, refs_live(vm));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    stub_set_now_ms(200);
    berry_timer_tick(200);
    stub_set_now_ms(300);
    berry_timer_tick(300);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_disable_runs_teardown_then_sweeps);
    return UNITY_END();
}
