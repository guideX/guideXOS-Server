/*
 * Native command-line wrapper for the in-tree guideXOS REXX interpreter.
 *
 * This translation unit deliberately depends only on the C runtime and the
 * repository's lexer/parser/compiler/VM modules. Script input is read in
 * bounded chunks rather than by trusting a file-size result, which also
 * handles files whose size cannot be determined in advance.
 */

#include "rexx_cli.h"

#include "rexx_compiler.h"
#include "rexx_lexer.h"
#include "rexx_parser.h"
#include "rexx_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Bound script input so a malformed or unexpectedly large file cannot cause
 * unbounded allocations before the parser's own AST limits take effect.
 * Keep this limit below SIZE_MAX so the trailing NUL byte is always safe.
 */
#define REXX_CLI_MAX_SOURCE_BYTES ((size_t)16777216u) /* 16 MiB */
#define REXX_CLI_READ_CHUNK       ((size_t)4096u)

enum {
    REXX_CLI_EXIT_SUCCESS = 0,
    REXX_CLI_EXIT_FAILURE = 1,
    REXX_CLI_EXIT_USAGE = 2
};

/* Print one consistent usage form for both help and argument errors. */
static void rexx_cli_print_usage(FILE *stream, const char *program_name)
{
    const char *name = (program_name != NULL && program_name[0] != '\0')
                           ? program_name : "rexx";

    (void)fprintf(stream,
                  "Usage: %s <script.rx>\n"
                  "       %s --help\n"
                  "       %s -- <script-path>\n",
                  name, name, name);
}

/*
 * Grow the input buffer without overflowing size_t or exceeding the configured
 * source limit. On failure, the original allocation remains owned by caller.
 */
static int rexx_cli_grow_source(char **buffer,
                                size_t *capacity,
                                size_t minimum_capacity)
{
    size_t next_capacity;
    char *grown;

    if (buffer == NULL || capacity == NULL || minimum_capacity == 0u ||
        minimum_capacity > REXX_CLI_MAX_SOURCE_BYTES) {
        return 0;
    }

    next_capacity = *capacity;
    if (next_capacity == 0u) {
        next_capacity = REXX_CLI_READ_CHUNK;
        if (next_capacity > REXX_CLI_MAX_SOURCE_BYTES) {
            next_capacity = REXX_CLI_MAX_SOURCE_BYTES;
        }
    }

    while (next_capacity < minimum_capacity) {
        if (next_capacity > REXX_CLI_MAX_SOURCE_BYTES / 2u) {
            next_capacity = REXX_CLI_MAX_SOURCE_BYTES;
        } else {
            next_capacity *= 2u;
        }
    }

    if (next_capacity < minimum_capacity ||
        next_capacity > REXX_CLI_MAX_SOURCE_BYTES) {
        return 0;
    }

    grown = (char *)realloc(*buffer, next_capacity);
    if (grown == NULL) {
        return 0;
    }

    *buffer = grown;
    *capacity = next_capacity;
    return 1;
}

/*
 * Read a script as bytes, append a NUL terminator for convenience, and reject
 * embedded NULs because the lexer and downstream diagnostics treat source as
 * text. On failure, this function frees any partial buffer and leaves outputs
 * in their initialized empty state.
 */
static int rexx_cli_read_source(const char *path,
                                char **source_out,
                                size_t *length_out)
{
    FILE *file = NULL;
    char *source = NULL;
    size_t capacity = 0u;
    size_t length = 0u;
    int failed = 0;

    if (source_out == NULL || length_out == NULL || path == NULL ||
        path[0] == '\0') {
        return 0;
    }

    *source_out = NULL;
    *length_out = 0u;

    file = fopen(path, "rb");
    if (file == NULL) {
        (void)fprintf(stderr,
                      "rexx: %s: cannot open script (not found or inaccessible)\n",
                      path);
        return 0;
    }

    if (!rexx_cli_grow_source(&source, &capacity, REXX_CLI_READ_CHUNK)) {
        (void)fprintf(stderr, "rexx: %s: out of memory while loading script\n",
                      path);
        failed = 1;
    }

    while (!failed) {
        size_t bytes_read;

        /*
         * If the limit is exactly reached, inspect one extra byte to
         * distinguish a valid maximum-size input from a truncated oversized
         * file. fgetc's int return keeps every byte value distinct from EOF.
         */
        if (length == REXX_CLI_MAX_SOURCE_BYTES) {
            int extra_byte = fgetc(file);
            if (extra_byte != EOF) {
                (void)fprintf(stderr,
                              "rexx: %s: script exceeds the %lu-byte limit\n",
                              path,
                              (unsigned long)REXX_CLI_MAX_SOURCE_BYTES);
                failed = 1;
            } else if (ferror(file)) {
                (void)fprintf(stderr, "rexx: %s: error reading script\n", path);
                failed = 1;
            }
            break;
        }

        if (length == capacity) {
            size_t next_capacity = capacity;
            if (next_capacity > REXX_CLI_MAX_SOURCE_BYTES / 2u) {
                next_capacity = REXX_CLI_MAX_SOURCE_BYTES;
            } else {
                next_capacity *= 2u;
            }
            if (next_capacity <= capacity ||
                !rexx_cli_grow_source(&source, &capacity, next_capacity)) {
                (void)fprintf(stderr,
                              "rexx: %s: out of memory while loading script\n",
                              path);
                failed = 1;
                break;
            }
        }

        bytes_read = fread(source + length, 1u, capacity - length, file);
        length += bytes_read;

        if (bytes_read == 0u) {
            if (ferror(file)) {
                (void)fprintf(stderr, "rexx: %s: error reading script\n", path);
                failed = 1;
            } else if (!feof(file)) {
                /* A zero-byte read without EOF or an error cannot make progress. */
                (void)fprintf(stderr, "rexx: %s: incomplete read of script\n", path);
                failed = 1;
            }
            break;
        }
    }

    if (fclose(file) != 0 && !failed) {
        (void)fprintf(stderr, "rexx: %s: error closing script after read\n", path);
        failed = 1;
    }

    if (!failed && length != 0u && memchr(source, '\0', length) != NULL) {
        (void)fprintf(stderr, "rexx: %s: script contains a NUL byte\n", path);
        failed = 1;
    }

    if (!failed && length < capacity) {
        source[length] = '\0';
    } else if (!failed) {
        char *terminated_source;

        /* At the exact size limit, reserve one extra byte for the terminator. */
        terminated_source = (char *)realloc(source, length + 1u);
        if (terminated_source == NULL) {
            (void)fprintf(stderr,
                          "rexx: %s: out of memory while finalizing script\n",
                          path);
            failed = 1;
        } else {
            source = terminated_source;
            source[length] = '\0';
        }
    }

    if (failed) {
        free(source);
        return 0;
    }

    *source_out = source;
    *length_out = length;
    return 1;
}

/* Emit a parser diagnostic with its best available source coordinates. */
static void rexx_cli_report_parse_error(const char *path,
                                        const RexxParserError *error,
                                        RexxParserStatus status)
{
    const char *message = error != NULL && error->message != NULL
                              ? error->message : "invalid REXX syntax";
    size_t line = error != NULL ? error->token.line : 0u;
    size_t column = error != NULL ? error->token.column : 0u;

    if (status == REXX_PARSER_STATUS_OUT_OF_MEMORY) {
        (void)fprintf(stderr, "rexx: %s: out of memory while parsing script\n",
                      path);
    } else if (status == REXX_PARSER_STATUS_RESOURCE_LIMIT) {
        (void)fprintf(stderr, "rexx: %s: parser resource limit exceeded\n", path);
    } else if (line != 0u && column != 0u &&
               status == REXX_PARSER_STATUS_LEXER_ERROR) {
        (void)fprintf(stderr, "rexx: %s:%lu:%lu: lexical error: %s\n",
                      path, (unsigned long)line, (unsigned long)column, message);
    } else if (line != 0u && column != 0u) {
        (void)fprintf(stderr, "rexx: %s:%lu:%lu: syntax error: %s\n",
                      path, (unsigned long)line, (unsigned long)column, message);
    } else {
        (void)fprintf(stderr, "rexx: %s: syntax error: %s\n", path, message);
    }
}

/* Run the interpreter pipeline and release every owned object on all exits. */
static int rexx_cli_execute_source(const char *path,
                                  const char *source,
                                  size_t source_length)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxVM vm;
    RexxLexerStatus lexer_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;
    RexxVMStatus vm_status;
    int result = REXX_CLI_EXIT_FAILURE;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&vm, 0, sizeof(vm));
    memset(&compiler_error, 0, sizeof(compiler_error));

    lexer_status = rexx_lexer_init(&lexer, source, source_length);
    if (lexer_status != REXX_LEXER_STATUS_OK) {
        (void)fprintf(stderr, "rexx: %s: lexer initialization failed (%d)\n",
                      path, (int)lexer_status);
        goto cleanup;
    }

    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_cli_report_parse_error(path, rexx_parser_get_error(&parser),
                                    parser_status);
        goto cleanup;
    }

    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_cli_report_parse_error(path, rexx_parser_get_error(&parser),
                                    parser_status);
        goto cleanup;
    }

    compiler_status = rexx_compiler_compile(program, &chunk, &compiler_error);
    if (compiler_status != REXX_COMPILER_STATUS_OK) {
        (void)fprintf(stderr,
                      "rexx: %s:%lu:%lu: compile error: %s (%s)\n",
                      path,
                      (unsigned long)compiler_error.token.line,
                      (unsigned long)compiler_error.token.column,
                      compiler_error.message != NULL
                          ? compiler_error.message : "unknown compiler error",
                      rexx_compiler_status_name(compiler_status));
        goto cleanup;
    }

    vm_status = rexx_vm_init(&vm, 0u);
    if (vm_status != REXX_VM_STATUS_OK) {
        (void)fprintf(stderr, "rexx: %s: VM initialization failed: %s\n",
                      path, rexx_vm_status_name(vm_status));
        goto cleanup;
    }

    /*
     * The VM's built-in ADDRESS dispatcher is intentionally safe: it returns
     * RC 127 and never forwards text to a host shell. A native shell embedding
     * may set its own dispatcher before VM execution in a future integration.
     */
    vm_status = rexx_vm_execute(&vm, chunk);
    if (vm_status != REXX_VM_STATUS_OK) {
        const RexxVMError *error = &vm.error;
        (void)fflush(stdout);
        (void)fprintf(stderr,
                      "rexx: %s: runtime error at instruction %lu: %s\n",
                      path, (unsigned long)error->pc,
                      error->message != NULL
                          ? error->message : rexx_vm_status_name(vm_status));
        goto cleanup;
    }

    if (fflush(stdout) != 0) {
        (void)fprintf(stderr, "rexx: %s: error writing standard output\n", path);
        goto cleanup;
    }

    result = REXX_CLI_EXIT_SUCCESS;

cleanup:
    /* All release helpers accept zero-initialized/partially initialized state. */
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return result;
}

/* Public shell entry point. It intentionally has no standalone main(). */
int guide_rexx_main(int argc, char **argv)
{
    const char *program_name = "rexx";
    const char *script_path = NULL;
    char *source = NULL;
    size_t source_length = 0u;
    int index;
    int positional_only = 0;
    int result;

    if (argc > 0 && argv != NULL && argv[0] != NULL && argv[0][0] != '\0') {
        program_name = argv[0];
    }

    if (argc < 0 || argv == NULL) {
        rexx_cli_print_usage(stderr, program_name);
        return REXX_CLI_EXIT_USAGE;
    }

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (argument == NULL) {
            (void)fprintf(stderr, "rexx: invalid NULL argument at index %d\n",
                          index);
            return REXX_CLI_EXIT_USAGE;
        }

        if (!positional_only && strcmp(argument, "--") == 0) {
            positional_only = 1;
            continue;
        }

        if (!positional_only &&
            (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0)) {
            if (argc != 2) {
                rexx_cli_print_usage(stderr, program_name);
                return REXX_CLI_EXIT_USAGE;
            }
            rexx_cli_print_usage(stdout, program_name);
            return REXX_CLI_EXIT_SUCCESS;
        }

        if (!positional_only && argument[0] == '-') {
            (void)fprintf(stderr, "rexx: unknown option: %s\n", argument);
            rexx_cli_print_usage(stderr, program_name);
            return REXX_CLI_EXIT_USAGE;
        }

        if (script_path != NULL) {
            (void)fputs("rexx: only one script path may be specified\n", stderr);
            rexx_cli_print_usage(stderr, program_name);
            return REXX_CLI_EXIT_USAGE;
        }
        script_path = argument;
    }

    if (script_path == NULL || script_path[0] == '\0') {
        rexx_cli_print_usage(stderr, program_name);
        return REXX_CLI_EXIT_USAGE;
    }

    if (!rexx_cli_read_source(script_path, &source, &source_length)) {
        return REXX_CLI_EXIT_FAILURE;
    }

    result = rexx_cli_execute_source(script_path, source, source_length);
    free(source);
    return result;
}
