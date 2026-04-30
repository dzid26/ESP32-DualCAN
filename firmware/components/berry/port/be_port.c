/*
 * Berry platform port for ESP-IDF (Dorky Commander).
 *
 * Provides the I/O hooks Berry requires. Filesystem calls are stubbed
 * because BE_USE_FILE_SYSTEM=0 in berry_conf.h — scripts are loaded
 * from C strings via be_loadstring().
 *
 * Owns the Berry log handler (set by main / protocol layer) because
 * Berry's built-in print() routes its output through be_writebuffer —
 * any handler hooked anywhere else would never be called.
 */
#include "berry.h"
#include "be_mem.h"
#include "be_sys.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "berry";

typedef void (*berry_log_handler_t)(const char *msg);
static berry_log_handler_t s_log_handler;

void berry_set_log_handler(berry_log_handler_t fn)
{
    s_log_handler = fn;
}

void berry_log_push(const char *msg)
{
    if (s_log_handler && msg) s_log_handler(msg);
}

BERRY_API void be_writebuffer(const char *buffer, size_t length)
{
    /* Always log to serial so output is visible during local debugging. */
    ESP_LOGI(TAG, "%.*s", (int)length, buffer);

    /* Forward to the registered handler (BLE log push) as a null-terminated
     * string. Strip a trailing newline if Berry wrote one — print() emits
     * 'foo\n' which would be ugly in the UI log panel. */
    if (s_log_handler && length > 0) {
        char tmp[256];
        size_t n = length;
        if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
        memcpy(tmp, buffer, n);
        while (n > 0 && (tmp[n - 1] == '\n' || tmp[n - 1] == '\r')) n--;
        tmp[n] = '\0';
        if (n > 0) s_log_handler(tmp);
    }
}

BERRY_API char *be_readstring(char *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return NULL;
}

/* open() builtin is always in be_baselib's vartab but implemented
 * in be_filelib.c which we excluded.  Provide a stub that errors. */
int be_nfunc_open(bvm *vm)
{
    be_raise(vm, "io_error", "open() not supported (no filesystem)");
    return 0;
}

/* Stubbed file I/O — Berry's be_fwrite is used internally by
 * be_writebuffer when the default port uses stdout.  We still
 * need the symbol because some internal paths reference it. */
void *be_fopen(const char *filename, const char *modes)
{
    (void)filename;
    (void)modes;
    return NULL;
}

int be_fclose(void *hfile)
{
    (void)hfile;
    return -1;
}

size_t be_fwrite(void *hfile, const void *buffer, size_t length)
{
    (void)hfile;
    (void)buffer;
    (void)length;
    return 0;
}

size_t be_fread(void *hfile, void *buffer, size_t length)
{
    (void)hfile;
    (void)buffer;
    (void)length;
    return 0;
}

char *be_fgets(void *hfile, void *buffer, int size)
{
    (void)hfile;
    (void)buffer;
    (void)size;
    return NULL;
}

int be_fseek(void *hfile, long offset)
{
    (void)hfile;
    (void)offset;
    return -1;
}

long int be_ftell(void *hfile)
{
    (void)hfile;
    return -1;
}

long int be_fflush(void *hfile)
{
    (void)hfile;
    return -1;
}

size_t be_fsize(void *hfile)
{
    (void)hfile;
    return 0;
}
