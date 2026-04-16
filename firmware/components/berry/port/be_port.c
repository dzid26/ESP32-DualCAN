/*
 * Berry platform port for ESP-IDF (Dorky Commander).
 *
 * Provides the I/O hooks Berry requires. Filesystem calls are stubbed
 * because BE_USE_FILE_SYSTEM=0 in berry_conf.h — scripts are loaded
 * from C strings via be_loadstring().
 */
#include "berry.h"
#include "be_mem.h"
#include "be_sys.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "berry";

BERRY_API void be_writebuffer(const char *buffer, size_t length)
{
    /* Berry's print() calls here. Route to ESP_LOG so output appears
     * on the USB-Serial-JTAG console alongside other firmware logs. */
    ESP_LOGI(TAG, "%.*s", (int)length, buffer);
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
