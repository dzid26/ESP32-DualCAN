#include "scripting/berry_registry.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

/* Berry internals for tracestack inspection. */
#include "be_vm.h"
#include "be_var.h"
#include "be_vector.h"
#include "be_string.h"
#include "be_debug.h"

static const char *TAG_BB = "berry";

/* ---- Limits ---- */
#define MAX_TIMERS    16    /* max concurrent timer.after/every */
#define MAX_ACTIONS   32    /* max actions registered at once */
#define ACTION_NAME_MAX 40  /* max length of action name */

static bvm   *s_vm;
static int    s_current_script_id;
static berry_timer_error_handler_t s_timer_error_handler;
static berry_script_name_cb_t s_script_name_cb;

void berry_set_current_script(int script_id) { s_current_script_id = script_id; }
int  berry_current_script(void)              { return s_current_script_id; }

/* Install the owning script around a stored callback, so anything the
 * callback registers inherits the same owner. */
static int script_ctx_enter(int owner)
{
    int prev = s_current_script_id;
    s_current_script_id = owner;
    return prev;
}

static void script_ctx_leave(int prev)
{
    s_current_script_id = prev;
}

/* ---- Berry function ref storage ----
 * Keeps a hidden global list ".refs" on the VM containing function references
 * that C code holds across script invocations (timers, actions). Each ref is
 * just the list index. When the function is no longer needed, the list slot
 * is set to nil (refs_free). This prevents GC from collecting the closures. */

static int refs_alloc(bvm *vm, int fn_index)
{
    if (fn_index < 0) fn_index = be_top(vm) + fn_index + 1;

    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) {
        be_pop(vm, 1);
        be_newlist(vm);
        be_setglobal(vm, ".refs");   /* leaves the list on the stack */
        be_pop(vm, 1);
        be_getglobal(vm, ".refs");
    }
    be_pushvalue(vm, fn_index);
    be_data_push(vm, -2);
    be_pop(vm, 1);
    int ref = be_data_size(vm, -1) - 1;
    be_pop(vm, 1);
    return ref;
}

static void refs_free(bvm *vm, int ref)
{
    if (ref < 0) return;
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) { be_pop(vm, 1); return; }
    be_pushint(vm, ref);
    be_pushnil(vm);
    be_setindex(vm, -3);
    be_pop(vm, 3);
}

static bool refs_push(bvm *vm, int ref)
{
    be_getglobal(vm, ".refs");
    if (be_isnil(vm, -1)) { be_pop(vm, 1); return false; }
    be_pushint(vm, ref);
    be_getindex(vm, -2);
    be_remove(vm, -2);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 2);
        return false;
    }
    return true;
}

int berry_capture_global(bvm *vm, const char *global_name)
{
    be_getglobal(vm, global_name);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 1);
        return -1;
    }
    int ref = refs_alloc(vm, -1);
    be_pop(vm, 1);
    be_pushnil(vm);
    be_setglobal(vm, global_name);   /* leaves the value on the stack */
    be_pop(vm, 1);
    return ref;
}

void berry_release_ref(bvm *vm, int ref)
{
    refs_free(vm, ref);
}

/* Restore the VM value stack to `base`, captured with be_top() before a call
 * into Berry. Use at call boundaries; be_pop() for values you pushed. */
void berry_stack_restore(bvm *vm, int base)
{
    int extra = be_top(vm) - base;
    be_assert(extra >= 0);
    if (extra > 0) be_pop(vm, extra);
}

/* Decode what be_pcall() left on the VM stack after a failure.
 *
 * be_pcall() returns the thrown status, not a value count. copy_exception()
 * (be_exec.c) copies two values for BE_EXCEPTION (type, message) but only one
 * for BE_EXIT (the integer status). For BE_EXIT the slot below the status is
 * whatever the callee left there - often the callback itself - so -2 must
 * never be read as a type. Other statuses (e.g. BE_MALLOC_FAIL) copy nothing.
 *
 * Contract: an exit is reported as type "exit" and msg = the exit status as a
 * decimal string, so callers get a defined pair instead of stack garbage.
 * Must be called before berry_stack_restore(), while the status values are
 * still on the stack. Any output buffer may be NULL. */
void berry_decode_call_error(bvm *vm, int status,
                             char *type_out, size_t type_size,
                             char *msg_out, size_t msg_size)
{
    const char *type = "error";
    const char *msg = "unknown error";
    char fallback[32];

    if (status == BE_EXCEPTION) {
        const char *t = be_tostring(vm, -2);
        const char *m = be_tostring(vm, -1);
        if (t) type = t;
        if (m) msg = m;
    } else if (status == BE_EXIT) {
        snprintf(fallback, sizeof(fallback), "%d", (int)be_toint(vm, -1));
        type = "exit";
        msg = fallback;
    } else {
        snprintf(fallback, sizeof(fallback), "be_pcall status %d", status);
        msg = fallback;
    }

    if (type_out && type_size > 0) snprintf(type_out, type_size, "%s", type);
    if (msg_out && msg_size > 0) snprintf(msg_out, msg_size, "%s", msg);
}

int berry_call_ref(bvm *vm, int ref, char *err_out, size_t err_size, char *type_out, size_t type_size)
{
    if (ref < 0) return -1;
    int base = be_top(vm);
    if (!refs_push(vm, ref)) return -1;
    int status = be_pcall(vm, 0);
    if (status != 0) {
        berry_decode_call_error(vm, status, type_out, type_size, err_out, err_size);
        berry_stack_restore(vm, base);
        return -2;
    }
    berry_stack_restore(vm, base);
    return 0;
}

/* ---- timer.* (polled from main loop via berry_timer_tick) ----
 * All timers are polled (not interrupt-driven). Each timer_after/every call
 * allocates a slot in s_timers[] and stores a Berry function ref. The main
 * loop calls berry_timer_tick() which walks the table and fires due timers. */

typedef struct {
    bool     in_use;
    int      ref;
    int      script_id;
    uint32_t next_fire_ms;
    uint32_t period_ms;     /* 0 = one-shot */
} timer_entry_t;

/* Registry of all active timers. Index == handle returned to Berry. */
static timer_entry_t s_timers[MAX_TIMERS];

static int alloc_timer(int ref, const char *binding, uint32_t period_ms, uint32_t now_ms)
{
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!s_timers[i].in_use) {
            if (s_current_script_id == SCRIPT_ID_NONE)
                ESP_LOGW(TAG_BB, "%s: no script owner - not revoked on disable", binding);
            s_timers[i] = (timer_entry_t){
                .in_use = true, .ref = ref,
                .script_id = s_current_script_id,
                .next_fire_ms = now_ms + period_ms,
                .period_ms = period_ms,
            };
            return i;
        }
    }
    return -1;
}

/* timer_after(ms:int, fn:function) -> handle:int
 * Schedule a one-shot callback. fn is called once after ms milliseconds.
 * Returns an integer handle (pass to timer_cancel). */
static int l_timer_after(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isfunction(vm, 2), "function");
    uint32_t ms = (uint32_t)be_toint(vm, 1);
    int ref = refs_alloc(vm, 2);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    int h = alloc_timer(ref, "timer.after", ms, now);
    if (h < 0) {
        refs_free(vm, ref);
        ESP_LOGE(TAG_BB, "timer.after: registry full");
        be_return_nil(vm);
    }
    s_timers[h].period_ms = 0;   /* one-shot */
    be_pushint(vm, h);
    be_return(vm);
}

/* timer_every(ms:int, fn:function) -> handle:int
 * Schedule a repeating callback. fn is called every ms milliseconds.
 * Returns an integer handle (pass to timer_cancel). */
static int l_timer_every(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isfunction(vm, 2), "function");
    uint32_t ms = (uint32_t)be_toint(vm, 1);
    int ref = refs_alloc(vm, 2);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    int h = alloc_timer(ref, "timer.every", ms, now);
    if (h < 0) {
        refs_free(vm, ref);
        ESP_LOGE(TAG_BB, "timer.every: registry full");
        be_return_nil(vm);
    }
    be_pushint(vm, h);
    be_return(vm);
}

/* timer_cancel(handle:int) -> nil
 * Cancel a pending timer. No-op if the handle is invalid or already fired. */
static int l_timer_cancel(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    int h = be_toint(vm, 1);
    if (h >= 0 && h < MAX_TIMERS && s_timers[h].in_use) {
        refs_free(vm, s_timers[h].ref);
        s_timers[h].in_use = false;
    }
    be_return_nil(vm);
}

void berry_timer_tick(uint32_t now_ms)
{
    if (!s_vm) return;
    for (int i = 0; i < MAX_TIMERS; i++) {
        timer_entry_t *t = &s_timers[i];
        if (!t->in_use) continue;
        if ((int32_t)(now_ms - t->next_fire_ms) < 0) continue;

        /* Snapshot owner/ref before the call: a one-shot frees its slot and
         * the callback may reuse it. */
        int ref = t->ref;
        int owner = t->script_id;
        bool one_shot = (t->period_ms == 0);
        if (one_shot) {
            t->in_use = false;
        } else {
            t->next_fire_ms = now_ms + t->period_ms;
        }

        char err_type[32], err_msg[128];
        int prev_ctx = script_ctx_enter(owner);
        int rc = berry_call_ref(s_vm, ref, err_msg, sizeof(err_msg),
                                err_type, sizeof(err_type));
        script_ctx_leave(prev_ctx);
        if (rc == -2) {
            if (!one_shot) t->in_use = false;
            refs_free(s_vm, ref);
            ref = -1;
            if (s_timer_error_handler) {
                s_timer_error_handler(owner, err_type, err_msg);
            }
        }

        if (one_shot && ref >= 0) refs_free(s_vm, ref);
    }
}

/* ---- actions.register / actions.invoke ----
 * Dashboard actions are named Berry closures callable from the web UI or
 * from other scripts. Each entry stores the name string and a Berry ref.
 * Berry names the function "action_register"/"action_invoke" at the global
 * level; C uses "actions.register"/"actions.invoke" internally. */

typedef struct {
    bool in_use;
    char name[ACTION_NAME_MAX];
    int  ref;
    int  script_id;
} action_entry_t;

/* Registry of all registered actions. Name is the lookup key. */
static action_entry_t s_actions[MAX_ACTIONS];

static int find_action(const char *name)
{
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (s_actions[i].in_use && strcmp(s_actions[i].name, name) == 0) return i;
    }
    return -1;
}

/* action_register(name:str, fn:function) -> nil
 * Register a named action visible on the Dashboard. If an action with the
 * same name already exists, it is replaced. */
static int l_action_register(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    CHECK_TYPE(vm, 2, be_isfunction(vm, 2), "function");
    const char *name = be_tostring(vm, 1);

    int existing = find_action(name);
    if (existing >= 0) {
        refs_free(vm, s_actions[existing].ref);
        s_actions[existing].in_use = false;
    }

    int slot = -1;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (!s_actions[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        ESP_LOGE(TAG_BB, "actions registry full");
        be_return_nil(vm);
    }
    if (s_current_script_id == SCRIPT_ID_NONE)
        ESP_LOGW(TAG_BB, "action_register: no script owner - not revoked on disable");

    int ref = refs_alloc(vm, 2);
    s_actions[slot].in_use = true;
    s_actions[slot].ref = ref;
    s_actions[slot].script_id = s_current_script_id;
    strncpy(s_actions[slot].name, name, ACTION_NAME_MAX - 1);
    s_actions[slot].name[ACTION_NAME_MAX - 1] = '\0';

    ESP_LOGI(TAG_BB, "action registered: %s (script_id=%d)", name, s_current_script_id);
    be_return_nil(vm);
}

/* action_invoke(name:str) -> nil
 * Programmatically invoke a registered action by name. */
static int l_action_invoke(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    berry_action_invoke(be_tostring(vm, 1));
    be_return_nil(vm);
}

int berry_actions_snapshot(const char **out_names, int max)
{
    int n = 0, found = 0;
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (!s_actions[i].in_use) continue;
        if (n < max) out_names[n++] = s_actions[i].name;
        found++;
    }
    return found;
}

int berry_action_invoke(const char *name)
{
    if (!s_vm || !name) return -1;
    int idx = find_action(name);
    if (idx < 0) return -1;

    /* The callback may re-register this action by name, replacing idx. */
    int ref   = s_actions[idx].ref;
    int owner = s_actions[idx].script_id;
    char err_type[32], err_msg[128];
    int prev_ctx = script_ctx_enter(owner);
    int rc = berry_call_ref(s_vm, ref, err_msg, sizeof(err_msg),
                            err_type, sizeof(err_type));
    script_ctx_leave(prev_ctx);
    if (rc == -2) {
        const char *func_name = NULL;
        int line = berry_error_line(&func_name);
        const char *fname = s_script_name_cb ? s_script_name_cb(owner) : NULL;
        if (fname) {
            char logbuf[256];
            berry_format_error(logbuf, sizeof(logbuf), fname, line, func_name, err_type, err_msg);
            ESP_LOGE("scripts", "%s", logbuf);
        } else if (line > 0) {
            ESP_LOGE("scripts", "action '%s' (line %d): %s", name, line, err_msg);
        } else {
            ESP_LOGE("scripts", "action '%s': %s", name, err_msg);
        }
        return -2;
    }
    return rc < 0 ? -1 : 0;
}

/* ---- Per-script cleanup ----
 * Called when a script is disabled. Releases all timers and actions tagged
 * with script_id. 0 means "no owner" and is never reclaimed, so registrations
 * must inherit an owner (see script_ctx_enter). */

void berry_cleanup_script(int script_id)
{
    if (script_id == SCRIPT_ID_NONE) return;

    /* Cancel timers */
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (s_timers[i].in_use && s_timers[i].script_id == script_id) {
            refs_free(s_vm, s_timers[i].ref);
            s_timers[i].in_use = false;
        }
    }

    /* Drop actions */
    for (int i = 0; i < MAX_ACTIONS; i++) {
        if (s_actions[i].in_use && s_actions[i].script_id == script_id) {
            refs_free(s_vm, s_actions[i].ref);
            s_actions[i].in_use = false;
        }
    }
}

void berry_register_registry(bvm *vm)
{
    s_vm = vm;
    be_regfunc(vm, "timer_after", l_timer_after);
    be_regfunc(vm, "timer_every", l_timer_every);
    be_regfunc(vm, "timer_cancel", l_timer_cancel);
    be_regfunc(vm, "action_register", l_action_register);
    be_regfunc(vm, "action_invoke",   l_action_invoke);
}

void berry_set_timer_error_handler(berry_timer_error_handler_t fn)
{
    s_timer_error_handler = fn;
}

void berry_set_script_name_callback(berry_script_name_cb_t fn)
{
    s_script_name_cb = fn;
}

/* Repair tracestack IPs after native-function errors.
 * repair_stack() is a copy from berry be_debug.c:228
 * Native frames don't save ip, but the recycled slot retains the
 * Berry closure's return address. This copies it to the closure frame. */
static void repair_stack(bvm *vm)
{
    bcallsnapshot *cf;
    bcallsnapshot *base = be_stack_base(&vm->tracestack);
    bcallsnapshot *top = be_stack_top(&vm->tracestack);
    /* Because the native function does not push `ip` to the
     * stack, the ip on the native function frame corresponds
     * to the previous Berry closure. */
    for (cf = top; cf >= base; --cf) {
        if (!var_isclosure(&cf->func)) {
            /* the last native function stack frame has the `ip` of
             * the previous Berry frame */
            binstruction *ip = cf->ip;
            /* skip native function stack frames */
            for (; cf >= base && !var_isclosure(&cf->func); --cf);
            /* fixed `ip` of Berry closure frame near native function frame */
            if (cf >= base) cf->ip = ip;
        }
    }
}

int berry_error_line(const char **name_out)
{
    if (name_out) *name_out = NULL;
    if (!s_vm) return 0;
    if (be_stack_isempty(&s_vm->tracestack)) return 0;

    repair_stack(s_vm);

    bcallsnapshot *base = be_stack_base(&s_vm->tracestack);
    bcallsnapshot *top = be_stack_top(&s_vm->tracestack);
    for (bcallsnapshot *cf = top; cf >= base; --cf) {
        if (!var_isclosure(&cf->func)) continue;
        bclosure *cl = var_toobj(&cf->func);
        if (!cl || !cl->proto) continue;
        bproto *proto = cl->proto;
        if (name_out && proto->name) *name_out = str(proto->name);
#if BE_DEBUG_RUNTIME_INFO
        if (proto->lineinfo && proto->nlineinfo) {
            int pc = (int)(cf->ip - proto->code - 1);
            if (pc < 0) pc = 0;
            blineinfo *it = proto->lineinfo;
            blineinfo *end = it + proto->nlineinfo;
            for (; it < end && pc > it->endpc; ++it) {}
            if (it < end) return it->linenumber;
        }
#else
        (void)proto;
#endif
        return 0;
    }
    return 0;
}

static int buprintf(char *buf, size_t size, int pos, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + pos, size - pos, fmt, args);
    va_end(args);
    if (n > 0) pos = ((size_t)(pos + n) < size) ? pos + n : (int)size - 1;
    return pos;
}

int berry_format_error(char *buf, size_t bufsize,
                       const char *filename, int line,
                       const char *funcname,
                       const char *type, const char *msg)
{
    if (!msg) msg = "unknown error";
    buf[0] = '\0';

    int pos = 0;
    if (line > 0 && filename)   pos = buprintf(buf, bufsize, pos, "%s:%d: ", filename, line);
    if (funcname)               pos = buprintf(buf, bufsize, pos, "%s(...): ", funcname);
    if (type)                   pos = buprintf(buf, bufsize, pos, "%s: ", type);
    pos = buprintf(buf, bufsize, pos, "%s", msg);
    return pos;
}
