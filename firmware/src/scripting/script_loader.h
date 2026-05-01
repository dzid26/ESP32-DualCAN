#pragma once

#include <stdbool.h>

#include "berry.h"

#define SCRIPT_MAX_COUNT    16
#define SCRIPT_MAX_NAME     64
#define SCRIPT_DIR          "/littlefs/scripts"

typedef struct {
    char    filename[SCRIPT_MAX_NAME];  /* e.g. "fold_mirrors.be" */
    bool    enabled;
    bool    loaded;                     /* true if setup() has been called */
    bool    errored;                    /* true if last load/run threw */
    char    error[128];                 /* last error message */
    int     script_id;                  /* unique tag, 0 = unloaded (reserved) */
    int     setup_ref;                  /* refs[] index of setup fn, -1 if none */
    int     teardown_ref;               /* refs[] index of teardown fn, -1 if none */
} script_entry_t;

typedef struct {
    script_entry_t scripts[SCRIPT_MAX_COUNT];
    int            count;
    bvm           *vm;
} script_loader_t;

/* Scan SCRIPT_DIR for .be files and populate the script list.
 * Does NOT load or run any scripts. */
int script_loader_scan(script_loader_t *loader, bvm *vm);

/* Enable a script by index: reads the file, executes it (which defines
 * functions), then calls setup() if it exists. */
int script_loader_enable(script_loader_t *loader, int idx);

/* Disable a script by index: calls teardown() if it exists, then
 * clears the script's registrations. */
int script_loader_disable(script_loader_t *loader, int idx);

/* Write a script file to SCRIPT_DIR. Creates the file if it doesn't exist. */
int script_loader_write(const char *filename, const char *code, size_t len);

/* Read a script file from SCRIPT_DIR into a caller-provided buffer.
 * Returns the number of bytes read, or -1 on error. */
int script_loader_read(const char *filename, char *buf, size_t buf_size);

/* Delete a script file from SCRIPT_DIR. Disables it first if loaded. */
int script_loader_delete(script_loader_t *loader, const char *filename);

/* Persist the set of currently-enabled scripts to SCRIPT_DIR/.enabled.
 * Call after any enable or disable operation. */
void script_loader_save_enabled(const script_loader_t *loader);

/* Read SCRIPT_DIR/.enabled and enable each listed script.
 * Call once after script_loader_scan() on boot. */
void script_loader_restore_enabled(script_loader_t *loader);
