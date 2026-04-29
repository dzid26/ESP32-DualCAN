#pragma once

#include "berry.h"
#include "can/can.h"

void berry_set_buses(can_t *bus0, can_t *bus1);
void berry_register_bindings(bvm *vm);

/* ---- Per-script context ----
 * Bindings that register callbacks (can.on, timer.*, actions.register) tag
 * each entry with the "current script id". The script_loader sets this
 * before calling a script's setup() and clears it after. On disable, the
 * loader calls berry_cleanup_script(id) to revoke every tagged registration.
 *
 * Use 0 for "no script context" (e.g. registrations from C). 0 is reserved
 * and is never cleaned up. */
void berry_set_current_script(int script_id);
int  berry_current_script(void);

/* Revoke every can_on / timer / action registered with this script_id. */
void berry_cleanup_script(int script_id);

/* ---- Function ref helpers (used by script_loader) ---- */

/* If a global of the given name is a function, store it in the refs list,
 * clear the global, and return its ref id. Returns -1 if not a function. */
int  berry_capture_global(bvm *vm, const char *global_name);

/* Release a previously captured ref (no-op if ref < 0). */
void berry_release_ref(bvm *vm, int ref);

/* Call a captured ref with no args. Returns 0 on success, -1 if ref is invalid,
 * -2 if the call errored (logged). */
int  berry_call_ref(bvm *vm, int ref);

/* Poll all timers; fire any whose next_fire_ms has passed. Call once per
 * main-loop iteration. */
void berry_timer_tick(uint32_t now_ms);
