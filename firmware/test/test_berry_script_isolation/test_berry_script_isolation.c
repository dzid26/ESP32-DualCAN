/*
 * Host test: scripts must not share mutable state through Berry globals.
 *
 * Runs the real script_loader (scan/enable/timers) against the real Berry
 * bindings. The CAN surface is a test double: frames are pushed into the
 * cache can_read() serves, and can_send() records what the scripts transmit.
 *
 * The first test uses the two scripts from the 0x334 flood (pedal stopping
 * mode and track mode, preprocessed to .bep) and asserts one 0x334 send per
 * new car counter. On the old loader both scripts share the global
 * `last_counter`, so they defeat each other's guard and send every poll.
 */
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "berry_test.h"
#include "scripting/berry_bindings.h"
#include "scripting/script_loader.h"

#define FIXTURE_DIR "test/test_berry_script_isolation/fixtures"
#define RUN_DIR     SCRIPT_DIR

static bvm *vm;
static int stack_base;
static script_loader_t loader;
static can_t bus0;
static can_t bus1;
static int timer_errors;
static char created[8][SCRIPT_MAX_NAME];
static int created_count;

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return false; }
    }
    fclose(in);
    fclose(out);
    return true;
}

static void remember(const char *name)
{
    if (created_count < (int)(sizeof(created) / sizeof(created[0]))) {
        snprintf(created[created_count++], SCRIPT_MAX_NAME, "%s", name);
    }
}

/* Install a fixture pair as <name>.be + <name>.bep so the real scan/enable
 * path is exercised. The .be is only there to be discovered; enable picks
 * the runtime file. */
static int install_fixture(const char *name)
{
    char bep_src[256], bep_dst[256], be_dst[256];
    snprintf(bep_src, sizeof(bep_src), "%s/%s.bep", FIXTURE_DIR, name);
    snprintf(bep_dst, sizeof(bep_dst), "%s/%s.bep", RUN_DIR, name);
    snprintf(be_dst, sizeof(be_dst), "%s/%s.be", RUN_DIR, name);

    script_loader_scan(&loader, vm);   /* creates RUN_DIR on a fresh checkout */
    TEST_ASSERT_TRUE_MESSAGE(copy_file(bep_src, bep_dst), bep_src);
    remember(name);
    FILE *f = fopen(be_dst, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fputs("# fixture; runtime lives in the .bep", f);
    fclose(f);

    script_loader_scan(&loader, vm);
    char candidate[SCRIPT_MAX_NAME];
    snprintf(candidate, sizeof(candidate), "%s.be", name);
    for (int i = 0; i < loader.count; i++) {
        if (strcmp(loader.scripts[i].filename, candidate) == 0) return i;
    }
    return -1;
}

static void install_source(const char *name, const char *source)
{
    script_loader_scan(&loader, vm);
    remember(name);
    TEST_ASSERT_EQUAL_INT(0, script_loader_write(name, source, strlen(source)));
    script_loader_scan(&loader, vm);
}

static int find_script(const char *be_name)
{
    for (int i = 0; i < loader.count; i++) {
        if (strcmp(loader.scripts[i].filename, be_name) == 0) return i;
    }
    return -1;
}

static void tick_ms(unsigned ms)
{
    stub_set_now_ms(ms);
    berry_timer_tick(ms);
}

static void timer_error(int script_id, const char *type, const char *msg)
{
    (void)script_id; (void)type; (void)msg;
    timer_errors++;
}

void setUp(void)
{
    vm = be_vm_new();
    berry_register_bindings(vm);
    berry_set_timer_error_handler(timer_error);
    memset(&bus0, 0, sizeof(bus0));
    memset(&bus1, 0, sizeof(bus1));
    bus0.bus_id = 0;
    bus1.bus_id = 1;
    berry_set_buses(&bus0, &bus1);
    stack_base = be_top(vm);
    stub_set_now_ms(0);
    stub_can_reset();

    memset(&loader, 0, sizeof(loader));
    loader.vm = vm;
    created_count = 0;
}

void tearDown(void)
{
    if (vm) {
        berry_set_current_script(SCRIPT_ID_NONE);
        for (int i = 0; i < loader.count; i++) {
            if (loader.scripts[i].loaded) script_loader_disable(&loader, i);
        }
        for (int i = 0; i < created_count; i++) {
            char path[192];
            snprintf(path, sizeof(path), "%s/%s.be", RUN_DIR, created[i]);
            remove(path);
            snprintf(path, sizeof(path), "%s/%s.bep", RUN_DIR, created[i]);
            remove(path);
        }
        TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
    }
    berry_set_current_script(SCRIPT_ID_NONE);
    be_vm_delete(vm);
    vm = NULL;
    memset(&loader, 0, sizeof(loader));
}

/* ---- The 0x334 flood repro: two real device scripts ---- */

void test_two_scripts_get_one_send_per_car_counter(void)
{
    uint8_t f334[8] = {0};
    f334[6] = 5 << 4;            /* pedal's 0x334 counter = 5 (bits 52-55) */
    stub_can_push(0, 0x334, f334, 8);

    uint8_t f118[8] = {0};       /* pedal 0, track state 0 */
    stub_can_push(0, 0x118, f118, 8);

    uint8_t f313[8] = {0};
    f313[6] = 9 << 4;            /* track mode's 0x313 counter = 9 */
    stub_can_push(0, 0x313, f313, 8);

    uint8_t peek[8] = {0};
    uint8_t peek_dlc = 0;
    TEST_ASSERT_EQUAL_INT(0, can_read(&bus0, 0x334, peek, &peek_dlc, 0));
    TEST_ASSERT_EQUAL_INT(8, peek_dlc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(5 << 4, peek[6], "stub cache must serve the pushed frame");

    int pedal = install_fixture("pedal_stopping_mode");
    TEST_ASSERT_TRUE(pedal >= 0);
    int pedal_rc = script_loader_enable(&loader, pedal);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pedal_rc, loader.scripts[pedal].error);
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, berry_actions_snapshot(NULL, 0),
                                  "pedal setup must register its four tiles");

    /* RELEASE is the power-on mode: no frame is sent until a tile is tapped. */
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("stopping: CREEP"));

    unsigned now = 0;
    for (int i = 0; i < 20; i++) { now += 10; tick_ms(now); }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, timer_errors, "pedal poll must not throw");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stub_can_total_sends(0),
                                  "pedal alone must transmit one frame");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stub_can_send_count(0, 0x334),
                                  "pedal alone must answer each car counter once");

    int track = install_fixture("track_mode_on_full_throttle");
    TEST_ASSERT_TRUE(track >= 0);
    int track_rc = script_loader_enable(&loader, track);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, track_rc, loader.scripts[track].error);
    TEST_ASSERT_TRUE_MESSAGE(loader.scripts[track].loaded, "track must be loaded");
    TEST_ASSERT_TRUE_MESSAGE(loader.scripts[track].setup_ref >= 0, "track setup ref");

    /* 20 more polls while both car counters stay put. */
    for (int i = 0; i < 20; i++) {
        now += 10;
        tick_ms(now);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, timer_errors, "timer callbacks must not throw");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stub_can_send_count(0, 0x334),
                                  "a second script must not defeat pedal's counter guard");
    be_getglobal(vm, "last_counter");
    TEST_ASSERT_TRUE_MESSAGE(be_isnil(vm, -1),
                             "scripts must not create top-level globals");
    be_pop(vm, 1);
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

/* ---- Binding sanity: the pushed frame's counter reaches Berry ---- */

static const char *COUNTER_READER =
    "var m = can_msg_get(0, 0x334) "
    "def setup() "
    "  var b = bytes() "
    "  b.add(m[\"data\"][6]) "
    "  b.add(int(msg_sig_get(m, 52, 4, false, false, 1, 0))) "
    "  can_send_raw(0, 0x200, b) "
    "end";

void test_binding_reads_pushed_counter(void)
{
    uint8_t f334[8] = {0};
    f334[6] = 5 << 4;
    stub_can_push(0, 0x334, f334, 8);

    install_source("counter_reader.be", COUNTER_READER);
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("counter_reader.be")));

    uint8_t data[8];
    TEST_ASSERT_TRUE(stub_can_last_data(0, 0x200, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(5 << 4, data[0], "can_msg_get must copy the frame bytes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, data[1], "msg_sig_get must decode the pushed counter");
}

/* ---- Minimal multi-upvalue check ---- */

static const char *MULTI_UPVAL_SCRIPT =
    "var a = 1 "
    "var x = 2 "
    "def f() "
    "  var y = a + x "
    "  x = 3 "
    "end "
    "def probe() "
    "  var b = bytes() b.add(int(x)) can_send_raw(0, 0x306, b) "
    "end "
    "action_register(\"f\", f) "
    "action_register(\"probe\", probe) "
    "def setup() end";

void test_multi_upvalue(void)
{
    install_source("multi_upval.be", MULTI_UPVAL_SCRIPT);
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("multi_upval.be")));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("f"));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("probe"));
    uint8_t data[8];
    TEST_ASSERT_TRUE(stub_can_last_data(0, 0x306, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, data[0], "assignment must hit the second upvalue");
}

/* ---- Minimal if-block upvalue check ---- */

static const char *MIN_IF_SCRIPT =
    "var MODE_CREEP = 1 "
    "var MODE_HOLD = 2 "
    "var x = 2 "
    "def f(mode, label) "
    "  if mode == MODE_CREEP && x == MODE_HOLD "
    "    mode = 0 "
    "    label = \"ROLL\" "
    "  end "
    "  x = mode "
    "end "
    "def probe() "
    "  var b = bytes() b.add(int(x)) can_send_raw(0, 0x305, b) "
    "end "
    "action_register(\"f\", /-> f(1, \"x\")) "
    "action_register(\"probe\", probe) "
    "def setup() end";

void test_min_if_upvalue(void)
{
    install_source("min_if.be", MIN_IF_SCRIPT);
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("min_if.be")));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("f"));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("probe"));
    uint8_t data[8];
    TEST_ASSERT_TRUE(stub_can_last_data(0, 0x305, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, data[0], "assignment after an if block must hit the upvalue");
}

/* ---- Exact pedal shape ---- */

static const char *PEDAL_SHAPE_SCRIPT =
    "var MODE_ROLL = 0 "
    "var MODE_CREEP = 1 "
    "var MODE_HOLD = 2 "
    "var want_mode = -1 "
    "var last_counter = -1 "
    "def checksum(payload) "
    "  var s = 0 "
    "  for i : 0..payload.size() - 1 "
    "    s += payload[i] "
    "  end "
    "  return s "
    "end "
    "def try_send(m) "
    "  last_counter = int(msg_sig_get(m, 52, 4, false, false, 1, 0)) "
    "  if want_mode < 0 return end "
    "end "
    "def set_mode(mode, label) "
    "  if mode == MODE_CREEP && want_mode == MODE_HOLD "
    "    mode = MODE_ROLL "
    "    label = \"ROLL (exit HOLD)\" "
    "  end "
    "  want_mode = mode "
    "  print(\"label=\" .. label) "
    "end "
    "def poll() "
    "  var m = can_msg_get(0, 0x334) "
    "  if m != nil "
    "    var c = int(msg_sig_get(m, 52, 4, false, false, 1, 0)) "
    "    if c != last_counter last_counter = c end "
    "  end "
    "end "
    "def probe() "
    "  var b = bytes() b.add(int(want_mode)) can_send_raw(0, 0x303, b) "
    "end "
    "action_register(\"probe\", probe) "
    "action_register(\"set\", /-> set_mode(1, \"x\")) "
    "def setup() timer_every(10, poll) end";

void test_pedal_shape_upvalue(void)
{
    uint8_t f334[8] = {0};
    f334[6] = 5 << 4;
    stub_can_push(0, 0x334, f334, 8);

    install_source("pedal_shape.be", PEDAL_SHAPE_SCRIPT);
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("pedal_shape.be")));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("set"));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("probe"));
    uint8_t data[8];
    TEST_ASSERT_TRUE(stub_can_last_data(0, 0x303, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, data[0], "set_mode must reach probe's upvalue");
}

/* ---- can_msg_get -> msg_sig_set -> can_msg_send from a timer ---- */

static const char *MIRROR_SCRIPT =
    "def tick() "
    "  var m = can_msg_get(0, 0x334) "
    "  if m != nil "
    "    msg_sig_set(m, 26, 2, false, false, 1, 0, 1) "
    "    can_msg_send(0, m) "
    "  end "
    "end "
    "def setup() timer_every(10, tick) end";

void test_mirror_send_from_timer(void)
{
    uint8_t f334[8] = {0};
    f334[6] = 5 << 4;
    stub_can_push(0, 0x334, f334, 8);

    install_source("mirror_timer.be", MIRROR_SCRIPT);
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("mirror_timer.be")));
    tick_ms(10);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stub_can_send_count(0, 0x334),
                                  "can_msg_send must transmit the draft");
}

/* ---- Sibling closures must share the chunk's upvalue ---- */

static const char *SHARED_UPVALUE_SCRIPT =
    "var want = -1 "
    "var n = 0 "
    "def set_mode(mode) want = mode end "
    "def poll() "
    "  if want >= 0 "
    "    n += 1 "
    "    var b = bytes() b.add(n) can_send_raw(0, 0x302, b) "
    "  end "
    "end "
    "def setup() "
    "  action_register(\"set\", /-> set_mode(1)) "
    "  timer_every(10, poll) "
    "end";

void test_sibling_closures_share_upvalue(void)
{
    install_source("shared_upvalue.be", SHARED_UPVALUE_SCRIPT);
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("shared_upvalue.be")));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("set"));
    tick_ms(10);
    uint8_t data[8];
    TEST_ASSERT_TRUE(stub_can_last_data(0, 0x302, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, data[0], "set_mode must reach poll's upvalue");
}

/* ---- Action + upvalue sanity ---- */

static const char *ACTION_SCRIPT =
    "var n = 0 "
    "def bump() "
    "  n += 1 "
    "  var b = bytes() b.add(n) can_send_raw(0, 0x300, b) "
    "end "
    "def setup() action_register(\"bump\", bump) end";

void test_action_upvalue_mutation(void)
{
    install_source("action_bump.be", ACTION_SCRIPT);
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("action_bump.be")));
    TEST_ASSERT_EQUAL_INT(0, berry_action_invoke("bump"));
    uint8_t data[8];
    TEST_ASSERT_TRUE(stub_can_last_data(0, 0x300, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, data[0], "action must mutate the chunk's local");
}

/* ---- Mechanism check: same top-level name in two scripts ---- */
static const char *SCRIPT_A =
    "var shared = 1 "
    "def tick() "
    "  var b = bytes() b.add(shared) can_send_raw(0, 0x100, b) "
    "end "
    "def setup() timer_every(10, tick) end";

static const char *SCRIPT_B =
    "var shared = 2 "
    "def tick() "
    "  var b = bytes() b.add(shared) can_send_raw(1, 0x101, b) "
    "end "
    "def setup() timer_every(10, tick) end";

void test_same_top_level_name_stays_private(void)
{
    install_source("globals_a.be", SCRIPT_A);
    install_source("globals_b.be", SCRIPT_B);

    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("globals_a.be")));
    TEST_ASSERT_EQUAL_INT(0, script_loader_enable(&loader, find_script("globals_b.be")));

    tick_ms(10);

    uint8_t data[8];
    TEST_ASSERT_TRUE(stub_can_last_data(0, 0x100, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, data[0], "script A's shared must be its own");
    TEST_ASSERT_TRUE(stub_can_last_data(1, 0x101, data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, data[0], "script B's shared must be its own");

    be_getglobal(vm, "shared");
    TEST_ASSERT_TRUE_MESSAGE(be_isnil(vm, -1), "a script must not leave top-level names global");
    be_pop(vm, 1);
    TEST_ASSERT_EQUAL_INT(stack_base, be_top(vm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_two_scripts_get_one_send_per_car_counter);
    RUN_TEST(test_binding_reads_pushed_counter);
    RUN_TEST(test_multi_upvalue);
    RUN_TEST(test_min_if_upvalue);
    RUN_TEST(test_pedal_shape_upvalue);
    RUN_TEST(test_mirror_send_from_timer);
    RUN_TEST(test_sibling_closures_share_upvalue);
    RUN_TEST(test_action_upvalue_mutation);
    RUN_TEST(test_same_top_level_name_stays_private);
    return UNITY_END();
}




