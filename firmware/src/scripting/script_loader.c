#include "scripting/script_loader.h"
#include "scripting/berry_log.h"
#include "scripting/berry_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define fs_mkdir(path) _mkdir(path)
#else
#define fs_mkdir(path) mkdir((path), 0755)
#endif

#include "esp_log.h"

static const char *TAG = "scripts";

/* Strip SCRIPT_DIR prefix from a path for user-facing error messages. */
static const char *strip_script_dir(const char *err) {
    if (!err) return err;
    size_t dlen = strlen(SCRIPT_DIR);
    if (strncmp(err, SCRIPT_DIR, dlen) == 0 && err[dlen] == '/')
        return err + dlen + 1;
    return err;
}
#define ENABLED_FILE SCRIPT_DIR "/.enabled"
static script_entry_t s_scan_existing[SCRIPT_MAX_COUNT];
static int            s_next_script_id = 1;   /* 0 reserved for "no context" */

static bool source_to_runtime_path(const char *filename, char *path, size_t path_size)
{
    size_t nlen = filename ? strlen(filename) : 0;
    if (nlen < 4 || strcmp(filename + nlen - 3, ".be") != 0) return false;
    int n = snprintf(path, path_size, "%s/%s", SCRIPT_DIR, filename);
    if (n < 0 || (size_t)n >= path_size) return false;
    size_t len = strlen(path);
    if (len < 3 || len + 1 >= path_size) return false;
    path[len] = 'p'; /* .be -> .bep */
    path[len + 1] = '\0';
    return true;
}

/* Appended to every script before compilation. Because the chunk is parsed
 * with top-level names as locals, setup/teardown are locals that disappear
 * when the chunk returns; this epilogue hands them back in a map without
 * touching the shared global table. The try/except lets scripts omit either
 * hook (an undefined name raises). */
static const char LOADER_EPILOGUE[] =
    "\n"
    "var __dorky_script_api__ = {}\n"
    "try __dorky_script_api__[\"setup\"] = setup except .. end\n"
    "try __dorky_script_api__[\"teardown\"] = teardown except .. end\n"
    "try __dorky_script_api__[\"loop\"] = loop except .. end\n"
    "return __dorky_script_api__\n";

/* Drop a stale global before loading: the epilogue only wants this script's
 * local hooks, never a value left behind by another chunk. */
static void clear_global(bvm *vm, const char *name)
{
    be_pushnil(vm);
    be_setglobal(vm, name);
    be_pop(vm, 1);
}

static const script_entry_t *find_existing_script(
    const script_entry_t *scripts, int count, const char *filename)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(scripts[i].filename, filename) == 0) {
            return &scripts[i];
        }
    }
    return NULL;
}

int script_loader_scan(script_loader_t *loader, bvm *vm)
{
    int existing_count = loader->count;

    memcpy(s_scan_existing, loader->scripts, sizeof(s_scan_existing));
    loader->vm = vm;
    loader->count = 0;

    struct stat st;
    if (stat(SCRIPT_DIR, &st) != 0) {
        fs_mkdir(SCRIPT_DIR);
        ESP_LOGI(TAG, "Created %s", SCRIPT_DIR);
    }

    DIR *dir = opendir(SCRIPT_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open %s", SCRIPT_DIR);
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && loader->count < SCRIPT_MAX_COUNT) {
        size_t nlen = strlen(ent->d_name);
        if (nlen < 4 || strcmp(ent->d_name + nlen - 3, ".be") != 0) {
            continue;
        }
        script_entry_t *s = &loader->scripts[loader->count++];
        const script_entry_t *prev =
            find_existing_script(s_scan_existing, existing_count, ent->d_name);

        memset(s, 0, sizeof(*s));
        s->setup_ref = -1;
        s->teardown_ref = -1;
        if (prev) {
            *s = *prev;
        } else {
            strncpy(s->filename, ent->d_name, SCRIPT_MAX_NAME - 1);
        }
        ESP_LOGD(TAG, "  [%d] %s", loader->count - 1, s->filename);
    }
    closedir(dir);

    ESP_LOGI(TAG, "Found %d scripts in %s", loader->count, SCRIPT_DIR);
    return loader->count;
}

int script_loader_enable(script_loader_t *loader, int idx)
{
    if (idx < 0 || idx >= loader->count) return -1;
    script_entry_t *s = &loader->scripts[idx];
    if (s->loaded) return 0;

    char path[128];
    bool has_runtime = false;
    if (source_to_runtime_path(s->filename, path, sizeof(path))) {
        FILE *rf = fopen(path, "r");
        if (rf) {
            has_runtime = true;
            fclose(rf);
        }
    }
    if (!has_runtime) {
        snprintf(path, sizeof(path), "%s/%s", SCRIPT_DIR, s->filename);
    }

    char *code = NULL;
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        snprintf(s->error, sizeof(s->error), "cannot open file");
        s->errored = true;
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    const size_t epilogue_len = sizeof(LOADER_EPILOGUE) - 1;
    if (fsize <= 0 || (size_t)fsize + epilogue_len > 32 * 1024) {
        fclose(f);
        snprintf(s->error, sizeof(s->error), "file too large or empty");
        s->errored = true;
        return -1;
    }

    code = malloc((size_t)fsize + epilogue_len + 1);
    if (!code) { fclose(f); return -1; }
    fread(code, 1, fsize, f);
    memcpy(code + fsize, LOADER_EPILOGUE, epilogue_len + 1);
    fclose(f);

    bvm *vm = loader->vm;

    /* Tag every binding registration during load + setup with this id. */
    int prev_ctx = berry_current_script();
    s->script_id = s_next_script_id++;
    berry_set_current_script(s->script_id);

    /* Execute the script (defines its functions and locals), then collect
     * setup/teardown from the epilogue map. Parsing with islocal=true keeps
     * every top-level `var`, `def` and assignment private to this chunk. */
    int stack_base = be_top(vm);
    clear_global(vm, "setup");
    clear_global(vm, "teardown");
    clear_global(vm, "loop");
    int rc = be_loadbuffer_local(vm, path, code, strlen(code));
    if (rc == 0) rc = be_pcall(vm, 0);
    if (rc != 0) {
        const char *msg = be_tostring(vm, -1);
        const char *etype = be_tostring(vm, -2);
        const char *func_name = NULL;
        int line = berry_error_line(&func_name);
        if (line > 0 && strncmp(msg, s->filename, strlen(s->filename)) != 0) {
            berry_format_error(s->error, sizeof(s->error),
                               s->filename, line, func_name, etype, msg);
        } else {
            snprintf(s->error, sizeof(s->error), "%.*s",
                     (int)(sizeof(s->error) - 1), strip_script_dir(msg));
        }
        berry_stack_restore(vm, stack_base);
        s->errored = true;
        ESP_LOGE(TAG, "%s", s->error);
        berry_set_current_script(prev_ctx);
        berry_cleanup_script(s->script_id);
        free(code);
        return -1;
    }

    if (!be_ismapinstance(vm, -1)) {
        snprintf(s->error, sizeof(s->error),
                 "script ended before the loader epilogue (top-level return is not supported)");
        berry_stack_restore(vm, stack_base);
        s->errored = true;
        ESP_LOGE(TAG, "%s", s->error);
        berry_set_current_script(prev_ctx);
        berry_cleanup_script(s->script_id);
        free(code);
        return -1;
    }

    /* Capture setup/teardown into refs. They are locals of the chunk, kept
     * alive by the returned map and then by the refs list. */
    s->setup_ref    = berry_capture_map_function(vm, -1, "setup");
    s->teardown_ref = berry_capture_map_function(vm, -1, "teardown");

    /* The legacy `loop` global is no longer polled — warn if a script defines
     * it, since the user probably intended timer_every. */
    int loop_ref = berry_capture_map_function(vm, -1, "loop");
    if (loop_ref >= 0) {
        char warn[160];
        snprintf(warn, sizeof(warn),
                 "warning: %s defines loop() — it is no longer polled; "
                 "use timer_every(period_ms, fn) instead",
                 s->filename);
        ESP_LOGW(TAG, "%s", warn);
        berry_log_push(warn);
        berry_release_ref(vm, loop_ref);
    }

    berry_stack_restore(vm, stack_base);
    free(code);

    ESP_LOGI(TAG, "Enabled: %s (script_id=%d)", s->filename, s->script_id);

    /* Call setup() via captured ref. */
    if (s->setup_ref >= 0) {
        char errtype[32] = "";
        if (berry_call_ref(vm, s->setup_ref, s->error, sizeof(s->error), errtype, sizeof(errtype)) == -2) {
            const char *func_name = NULL;
            int line = berry_error_line(&func_name);
            if (line > 0) {
                char buf[256];
                berry_format_error(buf, sizeof(buf),
                                   s->filename, line, func_name,
                                   errtype[0] ? errtype : NULL, s->error);
                snprintf(s->error, sizeof(s->error), "%s", buf);
            }
            s->errored = true;
            berry_set_current_script(prev_ctx);
            berry_release_ref(vm, s->setup_ref);
            berry_release_ref(vm, s->teardown_ref);
            s->setup_ref = -1;
            s->teardown_ref = -1;
            berry_cleanup_script(s->script_id);
            return -1;
        }
    }

    berry_set_current_script(prev_ctx);
    s->loaded = true;
    s->enabled = true;
    s->errored = false;
    return 0;
}

int script_loader_disable(script_loader_t *loader, int idx)
{
    if (idx < 0 || idx >= loader->count) return -1;
    script_entry_t *s = &loader->scripts[idx];
    if (!s->loaded) return 0;

    bvm *vm = loader->vm;

    /* Call teardown via captured ref (any new registrations from teardown
     * still get tagged with this script's id so cleanup catches them).
     * disable() can run inside a tick, so restore the caller's context. */
    int prev_ctx = berry_current_script();
    berry_set_current_script(s->script_id);
    if (s->teardown_ref >= 0) berry_call_ref(vm, s->teardown_ref, NULL, 0, NULL, 0);

    /* Revoke every timer / action tagged to this script. */
    berry_cleanup_script(s->script_id);
    berry_release_ref(vm, s->setup_ref);
    berry_release_ref(vm, s->teardown_ref);
    s->setup_ref = -1;
    s->teardown_ref = -1;
    berry_set_current_script(prev_ctx);

    s->loaded = false;
    s->enabled = false;
    ESP_LOGD(TAG, "Disabled: %s", s->filename);
    return 0;
}

int script_loader_delete(script_loader_t *loader, const char *filename)
{
    /* Disable first if loaded. */
    for (int i = 0; i < loader->count; i++) {
        if (strcmp(loader->scripts[i].filename, filename) == 0) {
            script_loader_disable(loader, i);
            break;
        }
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SCRIPT_DIR, filename);
    if (remove(path) != 0) {
        ESP_LOGE(TAG, "Delete failed: %s", path);
        return -1;
    }
    ESP_LOGI(TAG, "Deleted: %s", path);
    if (source_to_runtime_path(filename, path, sizeof(path))) {
        if (remove(path) == 0) {
            ESP_LOGI(TAG, "Deleted: %s", path);
        }
    }
    script_loader_scan(loader, loader->vm);
    return 0;
}

int script_loader_write(const char *filename, const char *code, size_t len)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SCRIPT_DIR, filename);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fwrite(code, 1, len, f);
    fclose(f);
    ESP_LOGD(TAG, "Wrote %zu bytes to %s", len, path);
    return 0;
}

int script_loader_write_runtime(const char *filename, const char *code, size_t len)
{
    char path[128];
    if (!source_to_runtime_path(filename, path, sizeof(path))) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fwrite(code, 1, len, f);
    fclose(f);
    ESP_LOGD(TAG, "Wrote %zu bytes to %s", len, path);
    return 0;
}

void script_loader_delete_runtime(const char *filename)
{
    char path[128];
    if (!source_to_runtime_path(filename, path, sizeof(path))) return;
    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Deleted: %s", path);
    }
}

int script_loader_read(const char *filename, char *buf, size_t buf_size)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SCRIPT_DIR, filename);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 0 || (size_t)fsize >= buf_size) {
        fclose(f);
        return -1;
    }
    int nread = fread(buf, 1, fsize, f);
    buf[nread] = '\0';
    fclose(f);
    return nread;
}

void script_loader_save_enabled(const script_loader_t *loader)
{
    FILE *f = fopen(ENABLED_FILE, "w");
    if (!f) {
        ESP_LOGW(TAG, "cannot write %s", ENABLED_FILE);
        return;
    }
    for (int i = 0; i < loader->count; i++) {
        if (loader->scripts[i].enabled) {
            fprintf(f, "%s\n", loader->scripts[i].filename);
        }
    }
    fclose(f);
}

void script_loader_restore_enabled(script_loader_t *loader)
{
    FILE *f = fopen(ENABLED_FILE, "r");
    if (!f) return;   /* no saved state — first boot */

    char line[SCRIPT_MAX_NAME];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;
        for (int i = 0; i < loader->count; i++) {
            if (strcmp(loader->scripts[i].filename, line) == 0) {
                script_loader_enable(loader, i);
                break;
            }
        }
    }
    fclose(f);
}
