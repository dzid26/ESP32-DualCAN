#pragma once

#include <stddef.h>

#include "berry.h"

/* ---- Per-script context ----
 * Bindings that register callbacks (timer.*, actions.register) tag each entry
 * with the "current script id". The script_loader sets this before calling a
 * script's setup() and restores it after. On disable, the loader calls
 * berry_cleanup_script(id) to revoke every tagged registration.
 *
 * Stored callbacks re-install their owner's context while they run, so a
 * timer/action registered from inside a callback inherits the same owner and
 * is revoked with it.
 *
 * SCRIPT_ID_NONE means "no script context". It is shared by every ownerless
 * registration and is never reclaimed, so registering with no owner logs a
 * warning: such an entry would outlive its script. */
#define SCRIPT_ID_NONE 0
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
 * -2 if the call failed; the failure is reported through err_out/type_out.
 * Does not install a script context: a caller invoking a stored script callback
 * must set the owner first.
 * A BE_EXIT is a failure too: type_out gets "exit" and err_out the status as a
 * decimal string. Neither output ever describes the callee as the error. */
int  berry_call_ref(bvm *vm, int ref, char *err_out, size_t err_size, char *type_out, size_t type_size);

/* Restore the VM value stack to a be_top() watermark captured before a call
 * into Berry. */
void berry_stack_restore(bvm *vm, int base);

/* Poll all timers; fire any whose next_fire_ms has passed. Call once per
 * main-loop iteration. */
void berry_timer_tick(uint32_t now_ms);

/* Register the timer/action bindings on the VM. */
void berry_register_registry(bvm *vm);

/* ---- Actions ---- */

int  berry_actions_snapshot(const char **out_names, int max);
int  berry_action_invoke(const char *name);

/* ---- Error reporting ---- */

/* Decode the failure be_pcall()/be_loadbuffer() reported as `status`, while
 * its values are still on the stack (call before berry_stack_restore).
 * BE_EXCEPTION reads the type/message pair; BE_EXIT reports type "exit" with
 * the status as the decimal message; other statuses never read the value
 * stack. Any output buffer may be NULL. */
void berry_decode_call_error(bvm *vm, int status,
                             char *type_out, size_t type_size,
                             char *msg_out, size_t msg_size);

int  berry_error_line(const char **name_out);
int  berry_format_error(char *buf, size_t bufsize,
                        const char *filename, int line,
                        const char *funcname,
                        const char *type, const char *msg);

/* Called when a Berry timer callback throws. A BE_EXIT (a native binding
 * calling be_exit()) arrives as type "exit" with msg = the exit status. */
typedef void (*berry_timer_error_handler_t)(int script_id, const char *error_type, const char *msg);
void berry_set_timer_error_handler(berry_timer_error_handler_t fn);

/* Resolves script_id -> filename for action error messages. */
typedef const char *(*berry_script_name_cb_t)(int script_id);
void berry_set_script_name_callback(berry_script_name_cb_t fn);

/* Argument checks shared by the native bindings. */
#define CHECK_TYPE(vm, n, pred, expected) \
    if (!(pred)) { \
        be_raise(vm, "type_error", be_pushfstring(vm, \
            "argument %d: expected %s, got %s", \
            (int)(n), (expected), be_typename(vm, (n)))); \
    }

#define CHECK_ARITY(vm, n) \
    if (be_top(vm) < (n)) { \
        be_raise(vm, "type_error", be_pushfstring(vm, \
            "expected %d argument(s), got %d", \
            (int)(n), (int)be_top(vm))); \
    }
