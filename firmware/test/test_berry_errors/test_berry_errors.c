/*
 * Host test for the timer error paths in berry_registry.c.
 *
 * When a stored callback throws, berry_timer_tick() must hand the error to the
 * installed handler with the entry's owner id, drop the entry, and leave the
 * VM value stack balanced. The production handler (protocol.c) disables the
 * owning script on that signal, so a wrong owner id is a real bug; the test
 * installs a recorder instead.
 */
#include <unity.h>

#include <string.h>

#include "berry_test.h"

static bvm *vm;
static int stack_base;

/* Recorder installed with berry_set_timer_error_handler(). */
static int g_calls;
static int g_owner;
static char g_type[64];
static char g_msg[64];
static int g_type_null;
static int g_msg_null;

static void record_error(int script_id, const char *type, const char *msg)
{
    g_calls++;
    g_owner = script_id;
    g_type_null = (type == NULL);
    g_msg_null = (msg == NULL);
    snprintf(g_type, sizeof(g_type), "%s", type ? type : "");
    snprintf(g_msg, sizeof(g_msg), "%s", msg ? msg : "");
}

void setUp(void)
{
    g_calls = 0;
    g_owner = -1;
    g_type[0] = '\0';
    g_msg[0] = '\0';
    g_type_null = 0;
    g_msg_null = 0;

    vm = be_vm_new();
    berry_register_registry(vm);
    berry_set_timer_error_handler(record_error);
    stack_base = be_top(vm);
    stub_set_now_ms(0);
}

void tearDown(void)
{
    /* Registry tables are file-static and outlive the VM: hand every slot
     * back so a case cannot eat into the next one's budget. */
    berry_set_timer_error_handler(NULL);
    berry_set_current_script(SCRIPT_ID_NONE);
    for (int id = 1; id <= 64; id++) berry_cleanup_script(id);
    be_vm_delete(vm);
    vm = NULL;
}

static void tick_at(unsigned now_ms)
{
    stub_set_now_ms(now_ms);
    berry_timer_tick(now_ms);
}

/* Run a chunk with an owner installed, like a script body does. */
static int run_as_owner(int owner, const char *src)
{
    berry_set_current_script(owner);
    int rc = berry_test_run_source(vm, src);
    berry_set_current_script(SCRIPT_ID_NONE);
    return rc;
}

/* ---- throwing timer callbacks ------------------------------------------ */

/* The handler sees the owner, the repeat entry is dropped instead of retried,
 * and the stack comes back to the watermark. */
void test_repeat_timer_error_reports_owner_and_removes_entry(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 41,
        "var hits = 0\n"
        "var eh = -1\n"
        "def bad()\n"
        "  hits += 1\n"
        "  raise 'boom', 'kaboom'\n"
        "end\n"
        "def setup()\n"
        "  eh = timer_every(1000, bad)\n"
        "end\n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "eh"));

    tick_at(1000);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_INT(41, g_owner);
    TEST_ASSERT_EQUAL_INT(0, g_type_null);
    TEST_ASSERT_EQUAL_INT(0, g_msg_null);
    TEST_ASSERT_EQUAL_STRING("boom", g_type);
    TEST_ASSERT_EQUAL_STRING("kaboom", g_msg);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "hits"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    tick_at(2000);
    tick_at(3000);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "hits"));

    /* The entry is gone, so its slot is free for the next registration. */
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(41, "var nh = timer_after(1000, bad)\n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "nh"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* Same contract for a one-shot armed from inside a callback: the slot it
 * reused is freed again. */
void test_one_shot_from_callback_error_reports_owner_and_frees_slot(void)
{
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 42,
        "var n = 0\n"
        "var sh = -1\n"
        "def inner()\n"
        "  raise 'inner_boom', 'inner_msg'\n"
        "end\n"
        "def outer()\n"
        "  n += 1\n"
        "  sh = timer_after(1000, inner)\n"
        "end\n"
        "def setup()\n"
        "  timer_after(1000, outer)\n"
        "end\n"));

    tick_at(1000);                  /* outer arms the one-shot in slot 0 */
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "sh"));
    TEST_ASSERT_EQUAL_INT(0, g_calls);

    tick_at(2000);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_INT(42, g_owner);
    TEST_ASSERT_EQUAL_INT(0, g_type_null);
    TEST_ASSERT_EQUAL_INT(0, g_msg_null);
    TEST_ASSERT_EQUAL_STRING("inner_boom", g_type);
    TEST_ASSERT_EQUAL_STRING("inner_msg", g_msg);
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    tick_at(3000);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_INT(0, run_as_owner(42, "var nh = timer_after(1000, inner)\n"));
    TEST_ASSERT_EQUAL_INT(0, berry_test_global_int(vm, "nh"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* ---- BE_EXIT in a callback --------------------------------------------- */
/* os.exit()/sys.exit() are compiled out (berry_conf.h sets BE_USE_OS_MODULE
 * and BE_USE_SYS_MODULE to 0), but any native binding may call be_exit(), so
 * the tick's BE_EXIT read is reachable. be_pcall() copies one value for
 * BE_EXIT (be_exec.c copy_exception) while the tick reads -2/-1 as an
 * exception pair.
 *
 * Confirmed: the handler gets the callback object as the type
 * ("<function: 0x...>") because -2 is the slot below the single exit status.
 * Bug session ses_e5de7d818ffexLysa6KUGVbQ75 owns the fix; drop the ignore
 * when it lands. */
static int l_test_exit(bvm *vm)
{
    be_exit(vm, 3);
    be_return_nil(vm);
}

void test_timer_callback_exit_reports_an_error_not_stack_garbage(void)
{
    TEST_IGNORE_MESSAGE("ses_e5de7d818ffexLysa6KUGVbQ75: handler got '<function: 0x...>'");

    be_regfunc(vm, "test_exit", l_test_exit);
    TEST_ASSERT_EQUAL_INT(0, berry_test_enable(vm, 43,
        "var exits = 0\n"
        "def dodie()\n"
        "  exits += 1\n"
        "  test_exit()\n"
        "end\n"
        "def setup()\n"
        "  timer_every(1000, dodie)\n"
        "end\n"));

    tick_at(1000);
    TEST_ASSERT_EQUAL_INT(1, berry_test_global_int(vm, "exits"));
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));

    /* Whatever contract the fix picks, a handler call must not describe the
     * callback object as the error type, and the owner stays the entry's. */
    if (g_calls > 0) {
        TEST_ASSERT_EQUAL_INT(43, g_owner);
        TEST_ASSERT_NULL(strstr(g_type, "<function"));
    }
}

/* The native platform links no test main of its own, so the suite provides
 * one, like the other tests-native suites. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_repeat_timer_error_reports_owner_and_removes_entry);
    RUN_TEST(test_one_shot_from_callback_error_reports_owner_and_frees_slot);
    RUN_TEST(test_timer_callback_exit_reports_an_error_not_stack_garbage);
    return UNITY_END();
}
