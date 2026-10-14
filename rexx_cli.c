/*
 * Native command-line wrapper for the in-tree guideXOS REXX interpreter.
 *
 * This translation unit uses the repository's lexer/parser/compiler/VM
 * modules and the guideXOS OS I/O adapter for bounded script loading.
 */

#include "rexx_cli.h"
#include "rexx_os_io.h"

#include "rexx_compiler.h"
#include "rexx_lexer.h"
#include "rexx_parser.h"
#include "rexx_vm.h"

#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

/* Read through the OS adapter and reject binary input the lexer cannot use. */
static int rexx_cli_read_source(const char *path,
                                char **source_out,
                                size_t *length_out)
{
    RexxOSIOStatus status;

    if (source_out == NULL || length_out == NULL || path == NULL ||
        path[0] == '\0') {
        return 0;
    }
    *source_out = NULL;
    *length_out = 0u;
    status = rexx_os_read_file_ex(path, source_out, length_out);
    if (status != REXX_OS_IO_OK) {
        (void)fprintf(stderr, "rexx: %s: %s\n", path,
                      rexx_os_io_status_name(status));
        return 0;
    }
    if (*length_out != 0u &&
        memchr(*source_out, '\0', *length_out) != NULL) {
        (void)fprintf(stderr, "rexx: %s: script contains a NUL byte\n", path);
        rexx_os_free_file(*source_out);
        *source_out = NULL;
        *length_out = 0u;
        return 0;
    }
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

/* Map an optional REXX EXIT result to a host process status. The VM retains
 * the full RexxValue for embedders; this CLI boundary accepts integral values
 * representable by int32_t, which is the shell bridge's status type. */
static int rexx_cli_exit_status(const RexxVM *vm, int *status_out)
{
    double number;

    if (vm == NULL || status_out == NULL || !vm->exit_requested)
        return 0;
    if (!vm->exit_has_value) {
        *status_out = REXX_CLI_EXIT_SUCCESS;
        return 1;
    }

    if (vm->exit_value.type == REXX_VALUE_NUMBER) {
        number = vm->exit_value.as.number;
    } else if (vm->exit_value.type == REXX_VALUE_BOOLEAN) {
        number = vm->exit_value.as.boolean ? 1.0 : 0.0;
    } else if (vm->exit_value.type == REXX_VALUE_STRING) {
        char *copy;
        char *end;
        size_t length = vm->exit_value.as.string.length;

        if (vm->exit_value.as.string.bytes == NULL || length == SIZE_MAX ||
            memchr(vm->exit_value.as.string.bytes, '\0', length) != NULL)
            return 0;
        copy = (char *)malloc(length + 1u);
        if (copy == NULL) return -1;
        memcpy(copy, vm->exit_value.as.string.bytes, length);
        copy[length] = '\0';
        errno = 0;
        number = strtod(copy, &end);
        if (errno == ERANGE || end == copy) {
            free(copy);
            return 0;
        }
        while (*end == ' ' || *end == '\t' || *end == '\r' ||
               *end == '\n' || *end == '\f' || *end == '\v') {
            ++end;
        }
        if (*end != '\0') {
            free(copy);
            return 0;
        }
        free(copy);
    } else {
        return 0;
    }

    if (!isfinite(number) || number < (double)INT32_MIN ||
        number > (double)INT32_MAX || number != (double)(int32_t)number)
        return 0;
    *status_out = (int)(int32_t)number;
    return 1;
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

    /* The OS hook is a safe RC 127 stub until the shell installs live routes. */
    rexx_vm_set_address_dispatcher(&vm, rexx_os_vm_address_dispatcher, NULL);
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

    if (vm.exit_requested) {
        int exit_status = REXX_CLI_EXIT_FAILURE;
        int exit_status_result = rexx_cli_exit_status(&vm, &exit_status);
        if (exit_status_result < 0) {
            (void)fprintf(stderr,
                          "rexx: %s: out of memory while reading EXIT value\n",
                          path);
            goto cleanup;
        }
        if (exit_status_result == 0) {
            (void)fprintf(stderr,
                          "rexx: %s: EXIT value is not an integer process status\n",
                          path);
            goto cleanup;
        }
        result = exit_status;
    } else {
        result = REXX_CLI_EXIT_SUCCESS;
    }

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
    rexx_os_free_file(source);
    return result;
}
