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
/* P6 drives the real loader through SCRIPT_DIR (berry_test_scripts/, CWD =
 * firmware/). One file per test; tearDown removes it. */
static script_loader_t loader;
static char created_file[SCRIPT_MAX_NAME];

void setUp(void)
{
    vm = be_vm_new();
    berry_register_registry(vm);
    stack_base = be_top(vm);
    stub_set_now_ms(0);

    memset(&loader, 0, sizeof(loader));
    loader.vm = vm;
    created_file[0] = '\0';
}

void tearDown(void)
{
    if (vm) {
        berry_set_current_script(SCRIPT_ID_NONE);
        /* A failed test may leave an entry loaded; close it out first. */
        for (int i = 0; i < loader.count; i++) {
            if (loader.scripts[i].loaded) script_loader_disable(&loader, i);
        }
        if (created_file[0]) script_loader_delete(&loader, created_file);
    }
    berry_set_current_script(SCRIPT_ID_NONE);
    be_vm_delete(vm);
    vm = NULL;
    memset(&loader, 0, sizeof(loader));
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

/* ---- P6: loader failure paths ---- */

static int loader_find(script_loader_t *l, const char *name)
{
    for (int i = 0; i < l->count; i++) {
        if (strcmp(l->scripts[i].filename, name) == 0) return i;
    }
    return -1;
}

/* Write one source file, scan, and return its index. Scans first so the
 * directory exists on a fresh checkout, and finds by name so leftover files
 * from an interrupted run do not shift the index. */
static int write_and_scan(const char *name, const char *source)
{
    script_loader_scan(&loader, vm);       /* creates SCRIPT_DIR if missing */
    snprintf(created_file, sizeof(created_file), "%s", name);
    if (script_loader_write(name, source, strlen(source)) != 0) return -1;
    script_loader_scan(&loader, vm);
    return loader_find(&loader, name);
}

void test_enable_syntax_error_reports_and_balances_stack(void)
{
    int idx = write_and_scan("lifecycle_syntax.be", "def broken(\n");
    TEST_ASSERT_TRUE(idx >= 0);

    TEST_ASSERT_EQUAL_INT(-1, script_loader_enable(&loader, idx));

    script_entry_t *entry = &loader.scripts[idx];
    TEST_ASSERT_TRUE(entry->errored);
    TEST_ASSERT_FALSE(entry->loaded);
    TEST_ASSERT_EQUAL_INT(-1, entry->setup_ref);
    TEST_ASSERT_EQUAL_INT(-1, entry->teardown_ref);
    TEST_ASSERT_TRUE(strlen(entry->error) > 0);
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, refs_live(vm));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* Sources are written as one line on purpose: script_loader_write opens the
 * file in text mode, so on Windows a '\n' becomes CRLF while the loader sizes
 * the buffer from ftell() and reads fewer bytes back, leaving malloc garbage
 * in the chunk. One line sidesteps the host translation without touching the
 * firmware. */

void test_enable_setup_error_revokes_partial_registrations(void)
{
    int idx = write_and_scan("lifecycle_setup_raise.be",
        "import global "
        "global.setmember('beats', 0) "
        "def tick() global.setmember('beats', global.member('beats') + 1) end "
        "def setup() "
        "  timer_every(100, tick) "
        "  raise 'boom', 'setup failed' "
        "end");
    TEST_ASSERT_TRUE(idx >= 0);

    TEST_ASSERT_EQUAL_INT(-1, script_loader_enable(&loader, idx));

    script_entry_t *entry = &loader.scripts[idx];
    TEST_ASSERT_TRUE(entry->errored);
    TEST_ASSERT_FALSE(entry->loaded);
    TEST_ASSERT_EQUAL_INT(-1, entry->setup_ref);
    TEST_ASSERT_EQUAL_INT(-1, entry->teardown_ref);
    TEST_ASSERT_EQUAL_INT(0, berry_actions_snapshot(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, refs_live(vm));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    /* The timer armed by the half-run setup() was revoked. */
    stub_set_now_ms(100);
    berry_timer_tick(100);
    stub_set_now_ms(200);
    berry_timer_tick(200);
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

void test_teardown_error_still_cleans_up(void)
{
    int idx = write_and_scan("lifecycle_teardown_raise.be",
        "import global "
        "global.setmember('beats', 0) "
        "global.setmember('teardown_ran', 0) "
        "def tick() global.setmember('beats', global.member('beats') + 1) end "
        "def setup() "
        "  timer_every(100, tick) "
        "  action_register('lifecycle_ping', tick) "
        "end "
        "def teardown() "
        "  global.setmember('teardown_ran', global.member('teardown_ran') + 1) "
        "  raise 'boom', 'teardown failed' "
        "end");
    TEST_ASSERT_TRUE(idx >= 0);

    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, idx));

    stub_set_now_ms(100);
    berry_timer_tick(100);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "beats"));
    TEST_ASSERT_EQUAL_INT(1, berry_actions_snapshot(NULL, 0));

    TEST_ASSERT_EQUAL_INT(0, script_loader_disable(&loader, idx));

    script_entry_t *entry = &loader.scripts[idx];
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "teardown_ran"));
    TEST_ASSERT_FALSE(entry->loaded);
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
    RUN_TEST(test_enable_syntax_error_reports_and_balances_stack);
    RUN_TEST(test_enable_setup_error_revokes_partial_registrations);
    RUN_TEST(test_teardown_error_still_cleans_up);
    return UNITY_END();
}
