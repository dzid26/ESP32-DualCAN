#pragma once

/*
 * Shared helpers for the host Berry suites. Header-only: each suite is its
 * own binary, so the static functions are per-TU.
 */

#include <stdio.h>
#include <string.h>

#include "berry.h"
#include "scripting/berry_registry.h"

/* Implemented by berry_common/stubs.c; moves the clock the registry reads. */
void stub_set_now_ms(unsigned ms);

/* Load and run a chunk. On error prints the Berry message and restores the
 * value stack. Returns 0 or -1. */
static inline int berry_test_run_source(bvm *vm, const char *src)
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

/* Run a chunk and capture setup/teardown. Returns 0 or -1. */
static inline int berry_test_capture(bvm *vm, const char *src,
                                     int *setup_ref, int *teardown_ref)
{
    if (berry_test_run_source(vm, src) != 0) return -1;
    *setup_ref = berry_capture_global(vm, "setup");
    *teardown_ref = berry_capture_global(vm, "teardown");
    return 0;
}

/* Enable the way script_loader_enable does: run the chunk under the owner,
 * capture setup() and call it. Returns 0 or negative. */
static inline int berry_test_enable(bvm *vm, int owner, const char *src)
{
    berry_set_current_script(owner);
    if (berry_test_run_source(vm, src) != 0) {
        berry_set_current_script(SCRIPT_ID_NONE);
        return -1;
    }
    int setup_ref = berry_capture_global(vm, "setup");
    char err[128] = "";
    int rc = berry_call_ref(vm, setup_ref, err, sizeof(err), NULL, 0);
    berry_set_current_script(SCRIPT_ID_NONE);
    if (rc != 0) printf("setup error: %s\n", err);
    return rc;
}

static inline int berry_test_global_int(bvm *vm, const char *name)
{
    be_getglobal(vm, name);
    int value = (int)be_toint(vm, -1);
    be_pop(vm, 1);
    return value;
}
