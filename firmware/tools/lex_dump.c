/*
 * Dump the vendored Berry lexer's token stream for the web UI conformance test.
 *
 * Usage:
 *   lex_dump --tokens <file>   dump one file
 *   lex_dump --probe <source>  dump an inline source string
 *
 * The output is one line per token: numeric token type, value, line. The value
 * is lowercase hex of the UTF-8 bytes, so tabs, newlines, and NULs in string
 * values cannot break the line format. Numbers use their parsed value.
 * Keywords and operators use - because their identity is the type.
 *
 * The line is the lexer's current line after the token. For every token
 * without an embedded newline that is the token's start line, which is what
 * the TypeScript spans record. `lastline` is deliberately not used: Berry
 * assigns it before skipping trivia, so it reports the line where a preceding
 * comment started.
 */
#include "berry.h"
#include "be_lexer.h"
#include "be_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *source;
    size_t length;
    int done;
} source_t;

static const char* read_source(blexer *lexer, void *data, size_t *size)
{
    source_t *src = (source_t *)data;
    (void)lexer;
    if (src->done) {
        *size = 0;
        return NULL;
    }
    src->done = 1;
    *size = src->length;
    return src->source;
}

static char* slurp(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *buffer;
    size_t read;

    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    *length = read;
    return buffer;
}

static void print_hex(const char *data, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++) {
        printf("%02x", (unsigned char)data[i]);
    }
}

static int run_source(const char *name, const char *source, size_t length)
{
    bvm *vm = be_vm_new();
    blexer lexer;
    source_t src;

    src.source = source;
    src.length = length;
    src.done = 0;

    be_lexer_init(&lexer, vm, name, read_source, &src);
    while (be_lexer_scan_next(&lexer)) {
        btoken *token = &lexer.token;
        char number[64];
        const char *value = NULL;
        size_t value_length = 0;

        /* Trailing trivia makes lexer_next return EOS as a token. */
        if (token->type == TokenEOS) break;

        printf("%d\t", token->type);
        if (token->type == TokenString || token->type == TokenId) {
            value = str(token->u.s);
            value_length = str_len(token->u.s);
        } else if (token->type == TokenInteger) {
            snprintf(number, sizeof(number), BE_INT_FORMAT, token->u.i);
            value = number;
            value_length = strlen(number);
        } else if (token->type == TokenReal) {
            snprintf(number, sizeof(number), "%.9g", (double)token->u.r);
            value = number;
            value_length = strlen(number);
        }

        if (value) {
            print_hex(value, value_length);
        } else {
            putchar('-');
        }
        printf("\t%d\n", lexer.linenumber);
    }
    be_lexer_deinit(&lexer);
    be_vm_delete(vm);
    return 0;
}

static int dump_file(const char *path)
{
    size_t length = 0;
    char *source = slurp(path, &length);
    int result;

    if (!source) {
        fprintf(stderr, "read error: %s\n", path);
        return 1;
    }
    result = run_source(path, source, length);
    free(source);
    return result;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s --tokens <file> | --probe <source>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "--probe") == 0 && argc >= 3) {
        return run_source("<probe>", argv[2], strlen(argv[2]));
    }
    if (strcmp(argv[1], "--tokens") == 0 && argc >= 3) {
        return dump_file(argv[2]);
    }
    return dump_file(argv[1]);
}
