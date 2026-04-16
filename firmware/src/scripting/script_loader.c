#include "scripting/script_loader.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"

static const char *TAG = "scripts";

int script_loader_scan(script_loader_t *loader, bvm *vm)
{
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
        memset(s, 0, sizeof(*s));
        strncpy(s->filename, ent->d_name, SCRIPT_MAX_NAME - 1);
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
    snprintf(path, sizeof(path), "%s/%s", SCRIPT_DIR, s->filename);

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

    /* Execute the script (defines its functions). */
    if (be_loadstring(vm, code) != 0 || be_pcall(vm, 0) != 0) {
        snprintf(s->error, sizeof(s->error), "%.*s",
                 (int)(sizeof(s->error) - 1), be_tostring(vm, -1));
        be_pop(vm, 1);
        s->errored = true;
        ESP_LOGE(TAG, "%s load error: %s", s->filename, s->error);
        free(code);
        return -1;
    }
    be_pop(vm, 1);
    free(code);

    /* Call setup() if defined. */
    if (be_getglobal(vm, "setup")) {
        if (be_pcall(vm, 0) != 0) {
            snprintf(s->error, sizeof(s->error), "%.*s",
                     (int)(sizeof(s->error) - 1), be_tostring(vm, -1));
            be_pop(vm, 1);
            s->errored = true;
            ESP_LOGE(TAG, "%s setup() error: %s", s->filename, s->error);
            return -1;
        }
        be_pop(vm, 1);
    }

    s->loaded = true;
    s->enabled = true;
    s->errored = false;
    ESP_LOGI(TAG, "Enabled: %s", s->filename);
    return 0;
}

int script_loader_disable(script_loader_t *loader, int idx)
{
    if (idx < 0 || idx >= loader->count) return -1;
    script_entry_t *s = &loader->scripts[idx];
    if (!s->loaded) return 0;

    bvm *vm = loader->vm;

    /* Call teardown() if defined. */
    if (be_getglobal(vm, "teardown")) {
        if (be_pcall(vm, 0) != 0) {
            ESP_LOGW(TAG, "%s teardown() error: %s",
                     s->filename, be_tostring(vm, -1));
            be_pop(vm, 1);
        }
        be_pop(vm, 1);
    }

    s->loaded = false;
    s->enabled = false;
    ESP_LOGI(TAG, "Disabled: %s", s->filename);
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
    ESP_LOGI(TAG, "Wrote %zu bytes to %s", len, path);
    return 0;
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
