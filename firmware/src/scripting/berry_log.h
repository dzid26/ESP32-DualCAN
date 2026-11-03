#pragma once

/* ---- Log streaming ---- */

/* Callback type for Berry print() output. msg is a null-terminated string. */
typedef void (*berry_log_handler_t)(const char *msg);

/* Register (or clear, pass NULL) a handler for Berry print() calls. */
void berry_set_log_handler(berry_log_handler_t fn);

/* Push a message through the registered log handler. Lets non-Berry code
 * (e.g. the script loader) surface diagnostics to the UI log panel. No-op
 * if no handler is registered. */
void berry_log_push(const char *msg);
