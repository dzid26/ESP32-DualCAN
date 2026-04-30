/* Standalone Berry parse-check for example scripts.
 *
 * Loads each .be file via be_loadbuffer (full compile, no execution). Returns
 * 0 if every file parses, 1 otherwise. Used by parse_check_examples.sh as a
 * quick sanity test before bundling examples into firmware/webui.
 *
 * Berry's compiler enforces strict-globals: references to undefined globals
 * are a compile error. We pre-register every binding we expose at runtime
 * as a nil global so the parser accepts them. Keep this list in sync with
 * be_regfunc() calls in firmware/src/scripting/berry_bindings.c. */

#include "berry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *BINDING_NAMES[] = {
    "can_send", "can_receive", "can_signal", "can_on",
    "can_msg_draft", "can_msg_set", "can_msg_send",
    "led_set", "led_off",
    "state_set", "state_get", "state_remove",
    "timer_after", "timer_every", "timer_cancel",
    "action_register", "action_invoke", "action_list",
    "millis",
    /* `print` is built into Berry; we only override it on-device. */
    NULL,
};

static void declare_bindings(bvm *vm)
{
    for (int i = 0; BINDING_NAMES[i]; i++) {
        be_pushnil(vm);
        be_setglobal(vm, BINDING_NAMES[i]);
        be_pop(vm, 1);
    }
}

static char *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)n, f);
    buf[n] = '\0';
    fclose(f);
    if (len_out) *len_out = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <file.be>...\n", argv[0]); return 2; }

    int failures = 0;
    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
        size_t len = 0;
        char *src = slurp(path, &len);
        if (!src) { fprintf(stderr, "  FAIL  %s (read error)\n", path); failures++; continue; }

        bvm *vm = be_vm_new();
        declare_bindings(vm);

        int rc = be_loadbuffer(vm, path, src, len);
        if (rc != 0) {
            const char *msg = be_tostring(vm, -1);
            fprintf(stderr, "  FAIL  %s: %s\n", path, msg ? msg : "(no message)");
            failures++;
        } else {
            fprintf(stdout, "  OK    %s\n", path);
        }
        be_vm_delete(vm);
        free(src);
    }

    if (failures) fprintf(stderr, "%d file(s) failed to parse\n", failures);
    return failures == 0 ? 0 : 1;
}
