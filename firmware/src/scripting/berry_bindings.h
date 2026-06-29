#pragma once

/* Bump when the Berry C binding set changes incompatibly (add/remove/change
 * a be_regfunc call in berry_bindings.c). The web UI warns before uploading
 * scripts validated against a different version. */
#define SCRIPTING_API_VERSION 1

#include "berry.h"
#include "can/can.h"

void berry_set_buses(can_t *bus0, can_t *bus1);
/* Returns NULL for an out-of-range or unconfigured index. */
can_t *berry_get_bus(int bus);
void berry_register_bindings(bvm *vm);

/* ---- Per-script context ----
 * Bindings that register callbacks (timer.*, actions.register) tag each
 * entry with the "current script id". The script_loader sets this before
 * calling a script's setup() and clears it after. On disable, the loader
 * calls berry_cleanup_script(id) to revoke every tagged registration.
 *
 * Use 0 for "no script context" (e.g. registrations from C). 0 is reserved
 * and is never cleaned up. */
void berry_set_current_script(int script_id);
int  berry_current_script(void);

/* Revoke every timer / action registered with this script_id. */
void berry_cleanup_script(int script_id);

/* ---- Function ref helpers (used by script_loader) ---- */

/* If a global of the given name is a function, store it in the refs list,
 * clear the global, and return its ref id. Returns -1 if not a function. */
int  berry_capture_global(bvm *vm, const char *global_name);

/* Release a previously captured ref (no-op if ref < 0). */
void berry_release_ref(bvm *vm, int ref);

/* Call a captured ref with no args. Returns 0 on success, -1 if ref is invalid,
 * -2 if the call errored (logged). */
int  berry_call_ref(bvm *vm, int ref, char *err_out, size_t err_size, char *type_out, size_t type_size);

/* Poll all timers; fire any whose next_fire_ms has passed. Call once per
 * main-loop iteration. */
void berry_timer_tick(uint32_t now_ms);

/* ---- Log streaming ---- */

/* Callback type for Berry print() output. msg is a null-terminated string. */
typedef void (*berry_log_handler_t)(const char *msg);

/* Register (or clear, pass NULL) a handler for Berry print() calls. */
void berry_set_log_handler(berry_log_handler_t fn);

/* Push a message through the registered log handler. Lets non-Berry code
 * (e.g. the script loader) surface diagnostics to the UI log panel. No-op
 * if no handler is registered. */
void berry_log_push(const char *msg);

/* Callback type for looking up a script name by script_id. Returns a pointer
 * to the filename (owned by the caller's script_loader). */
typedef const char *(*berry_script_name_cb_t)(int script_id);

/* Register (or clear, pass NULL) a callback to resolve script names. */
void berry_set_script_name_callback(berry_script_name_cb_t fn);

/* Returns the line number of the innermost tracestack frame (where the
 * error occurred), or 0 if no tracestack or line info is available.
 * If name_out is non-NULL, it is set to the Berry function name (or NULL). */
int berry_error_line(const char **name_out);

/* Format a Berry error message into buf.
 *
 * Produces:  file:line: funcname(...): [type: ]msg
 *
 * - filename  may be NULL (omits "file:")
 * - line <= 0 omits "file:line: ".  line > 0 with NULL filename produces ":N:"
 * - funcname  may be NULL (omits "funcname(...): ")
 * - type      may be NULL (omits "type: ").  Non-NULL appends ": "
 * - msg       may be NULL (prints "unknown error")
 *
 * When line <= 0 AND funcname is NULL AND type is NULL, just returns msg
 * (or "unknown error").
 *
 * Returns the number of bytes written (like snprintf). */
int berry_format_error(char *buf, size_t bufsize,
                       const char *filename, int line,
                       const char *funcname,
                       const char *type, const char *msg);

/* ---- Timer error handling ---- */

/* Called when a Berry timer callback throws an error.
 * script_id identifies the script; error_type is e.g. "type_error";
 * msg is the error message. */
typedef void (*berry_timer_error_handler_t)(int script_id, const char *error_type, const char *msg);

/* Register (or clear, pass NULL) a handler for timer errors.
 * The script_loader / protocol layer uses this to disable the offending script. */
void berry_set_timer_error_handler(berry_timer_error_handler_t fn);

/* ---- Action invocation ---- */

/* Snapshot the registered action names. `out_names` must have room for
 * `max` pointers; returned strings are owned by the registry (do not free).
 * Returns the actual count (may be > max if truncated). */
int  berry_actions_snapshot(const char **out_names, int max);

/* Invoke a registered action by name with no arg. Returns 0 on success,
 * -1 if not found, -2 if Berry call failed (error logged). */
int  berry_action_invoke(const char *name);
