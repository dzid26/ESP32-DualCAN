#include "scripting/script_loader.h"
#include "scripting/berry_bindings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

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
        mkdir(SCRIPT_DIR, 0755);
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
    if (fsize <= 0 || fsize > 32 * 1024) {
        fclose(f);
        snprintf(s->error, sizeof(s->error), "file too large or empty");
        s->errored = true;
        return -1;
    }

    code = malloc(fsize + 1);
    if (!code) { fclose(f); return -1; }
    fread(code, 1, fsize, f);
    code[fsize] = '\0';
    fclose(f);

    bvm *vm = loader->vm;

    /* Tag every binding registration during load + setup with this id. */
    s->script_id = s_next_script_id++;
    berry_set_current_script(s->script_id);

    /* Execute the script (defines its functions). */
    if (be_loadbuffer(vm, path, code, strlen(code)) != 0 || be_pcall(vm, 0) != 0) {
        const char *msg = be_tostring(vm, -1);
        int line = berry_error_line();
        if (line > 0 && strncmp(msg, s->filename, strlen(s->filename)) != 0) {
            snprintf(s->error, sizeof(s->error), "%s:%d: %s",
                     s->filename, line, msg);
        } else {
            snprintf(s->error, sizeof(s->error), "%.*s",
                     (int)(sizeof(s->error) - 1), strip_script_dir(msg));
        }
        be_pop(vm, 1);
        s->errored = true;
        ESP_LOGE(TAG, "%s", s->error);
        berry_set_current_script(0);
        berry_cleanup_script(s->script_id);
        free(code);
        return -1;
    }
    be_pop(vm, 1);
    free(code);

    /* Capture setup/teardown into refs and clear the globals. This isolates
     * scripts from each other (the global namespace is shared by all). */
    s->setup_ref    = berry_capture_global(vm, "setup");
    s->teardown_ref = berry_capture_global(vm, "teardown");

    /* The legacy `loop` global is no longer polled — warn if a script defines
     * it, since the user probably intended timer_every. Clear it either way. */
    int loop_ref = berry_capture_global(vm, "loop");
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

    ESP_LOGI(TAG, "Enabled: %s (script_id=%d)", s->filename, s->script_id);

    /* Call setup() via captured ref. */
    if (s->setup_ref >= 0 && berry_call_ref(vm, s->setup_ref, s->error, sizeof(s->error)) == -2) {
        int line = berry_error_line();
        if (line > 0) {
            /* Prepend filename:line: to the error message from Berry. */
            char buf[256];
            snprintf(buf, sizeof(buf), "%s:%d: %s", s->filename, line, s->error);
            snprintf(s->error, sizeof(s->error), "%s", buf);
        }
        s->errored = true;
        berry_set_current_script(0);
        berry_release_ref(vm, s->setup_ref);
        berry_release_ref(vm, s->teardown_ref);
        s->setup_ref = -1;
        s->teardown_ref = -1;
        berry_cleanup_script(s->script_id);
        return -1;
    }

    berry_set_current_script(0);
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
     * still get tagged with this script's id so cleanup catches them). */
    berry_set_current_script(s->script_id);
    if (s->teardown_ref >= 0) berry_call_ref(vm, s->teardown_ref, NULL, 0);

    /* Revoke every can.on / timer / action tagged to this script. */
    berry_cleanup_script(s->script_id);
    berry_release_ref(vm, s->setup_ref);
    berry_release_ref(vm, s->teardown_ref);
    s->setup_ref = -1;
    s->teardown_ref = -1;
    berry_set_current_script(0);

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
