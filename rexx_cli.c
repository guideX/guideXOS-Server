/*
 * Native command-line wrapper for the in-tree guideXOS REXX interpreter.
 *
 * File access remains behind rexx_os_io. Output is supplied per invocation,
 * so native shells can route SAY and diagnostics to the invoking Console.
 */

#include "rexx_cli.h"
#include "rexx_os_io.h"

#include "rexx_compiler.h"
#include "rexx_lexer.h"
#include "rexx_numeric.h"
#include "rexx_output.h"
#include "rexx_parser.h"
#include "rexx_vm.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    REXX_CLI_EXIT_SUCCESS = 0,
    REXX_CLI_EXIT_FAILURE = 1,
    REXX_CLI_EXIT_USAGE = 2
};

static int rexx_cli_vm_output(void *context,
                              const char *bytes,
                              size_t length)
{
    return rexx_output_write((RexxOutputContext *)context,
                             REXX_OUTPUT_STDOUT, bytes, length);
}

/* Print one consistent usage form for both help and argument errors. */
static int rexx_cli_print_usage(RexxOutputContext *output,
                                RexxOutputStream stream,
                                const char *program_name)
{
    const char *name = (program_name != NULL && program_name[0] != '\0')
                           ? program_name : "rexx";
    return rexx_output_format(output, stream,
                              "Usage: %s <script.rx> [arguments...]\n"
                              "       %s --help\n"
                              "       %s -- <script-path> [arguments...]\n",
                              name, name, name);
}

/* Read through the OS adapter and reject binary input the lexer cannot use. */
static int rexx_cli_read_source(RexxOutputContext *output,
                                const char *path,
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
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: %s\n", path,
                                 rexx_os_io_status_name(status));
        return 0;
    }
    if (*length_out != 0u &&
        memchr(*source_out, '\0', *length_out) != NULL) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: script contains a NUL byte\n",
                                 path);
        rexx_os_free_file(*source_out);
        *source_out = NULL;
        *length_out = 0u;
        return 0;
    }
    return 1;
}

static int rexx_cli_join_arguments(int argc,
                                   char **argv,
                                   int first_argument,
                                   char **bytes_out,
                                   size_t *length_out)
{
    size_t total = 0u;
    int index;
    char *joined;
    size_t offset = 0u;

    if (bytes_out == NULL || length_out == NULL || argv == NULL ||
        first_argument < 0 || first_argument > argc) return 0;
    *bytes_out = NULL;
    *length_out = 0u;
    for (index = first_argument; index < argc; ++index) {
        size_t part_length;
        if (argv[index] == NULL) return 0;
        part_length = strlen(argv[index]);
        if (part_length > (size_t)-1 - total ||
            (index != first_argument && total == (size_t)-1)) return 0;
        total += part_length;
        if (index != first_argument) ++total;
        if (total > REXX_VM_MAX_PARSE_SOURCE_LENGTH) return 0;
    }
    if (total == (size_t)-1) return 0;
    joined = (char *)malloc(total + 1u);
    if (joined == NULL) return 0;
    for (index = first_argument; index < argc; ++index) {
        size_t part_length = strlen(argv[index]);
        if (index != first_argument) joined[offset++] = ' ';
        if (part_length != 0u) {
            memcpy(joined + offset, argv[index], part_length);
            offset += part_length;
        }
    }
    joined[offset] = '\0';
    *bytes_out = joined;
    *length_out = total;
    return 1;
}

/* Emit a parser diagnostic with its best available source coordinates. */
static void rexx_cli_report_parse_error(RexxOutputContext *output,
                                        const char *path,
                                        const RexxParserError *error,
                                        RexxParserStatus status)
{
    const char *message = error != NULL && error->message != NULL
                              ? error->message : "invalid REXX syntax";
    size_t line = error != NULL ? error->token.line : 0u;
    size_t column = error != NULL ? error->token.column : 0u;

    if (status == REXX_PARSER_STATUS_OUT_OF_MEMORY) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: out of memory while parsing script\n",
                                 path);
    } else if (status == REXX_PARSER_STATUS_RESOURCE_LIMIT) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: parser resource limit exceeded\n",
                                 path);
    } else if (line != 0u && column != 0u &&
               status == REXX_PARSER_STATUS_LEXER_ERROR) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s:%lu:%lu: lexical error: %s\n",
                                 path, (unsigned long)line,
                                 (unsigned long)column, message);
    } else if (line != 0u && column != 0u) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s:%lu:%lu: syntax error: %s\n",
                                 path, (unsigned long)line,
                                 (unsigned long)column, message);
    } else {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: syntax error: %s\n", path,
                                 message);
    }
}

/* Map an optional REXX EXIT result to a host process status. The VM retains
 * the full RexxValue for embedders; this CLI boundary accepts integral values
 * representable by int32_t, which is the shell bridge's status type. */
static int rexx_cli_exit_status(const RexxVM *vm, int *status_out)
{
    RexxDecimal number;
    int32_t integer;
    const char *bytes = NULL;
    size_t length = 0u;
    char formatted[64];
    int written;
    RexxNumericStatus numeric_status;

    if (vm == NULL || status_out == NULL || !vm->exit_requested)
        return 0;
    if (!vm->exit_has_value) {
        *status_out = REXX_CLI_EXIT_SUCCESS;
        return 1;
    }

    if (vm->exit_value.type == REXX_VALUE_NUMBER) {
        written = rexx_numeric_format(formatted, sizeof(formatted),
                                      vm->exit_value.as.number);
        if (written < 0) return 0;
        bytes = formatted;
        length = (size_t)written;
    } else if (vm->exit_value.type == REXX_VALUE_BOOLEAN) {
        bytes = vm->exit_value.as.boolean ? "1" : "0";
        length = 1u;
    } else if (vm->exit_value.type == REXX_VALUE_STRING) {
        if (vm->exit_value.as.string.bytes == NULL ||
            memchr(vm->exit_value.as.string.bytes, '\0',
                   vm->exit_value.as.string.length) != NULL)
            return 0;
        bytes = vm->exit_value.as.string.bytes;
        length = vm->exit_value.as.string.length;
    } else {
        return 0;
    }
    numeric_status = rexx_decimal_parse(bytes, length, &number);
    if (numeric_status != REXX_NUMERIC_OK ||
        rexx_decimal_to_int32(&number, &integer) != REXX_NUMERIC_OK)
        return 0;
    *status_out = (int)integer;
    return 1;
}

/* Run the interpreter pipeline and release every owned object on all exits. */
static int rexx_cli_execute_source(RexxOutputContext *output,
                                   const char *path,
                                   const char *source,
                                   size_t source_length,
                                   const char *argument_string,
                                   size_t argument_string_length,
                                   int argument_string_present,
                                   RexxVMClockReader clock_reader,
                                   void *clock_user_data,
                                   const RexxVMByteStreamProvider *stream_provider,
                                   void *stream_user_data)
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
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: lexer initialization failed (%d)\n",
                                 path, (int)lexer_status);
        goto cleanup;
    }

    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_cli_report_parse_error(output, path,
                                    rexx_parser_get_error(&parser),
                                    parser_status);
        goto cleanup;
    }

    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_cli_report_parse_error(output, path,
                                    rexx_parser_get_error(&parser),
                                    parser_status);
        goto cleanup;
    }

    compiler_status = rexx_compiler_compile(program, &chunk, &compiler_error);
    if (compiler_status != REXX_COMPILER_STATUS_OK) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s:%lu:%lu: compile error: %s (%s)\n",
                                 path,
                                 (unsigned long)compiler_error.token.line,
                                 (unsigned long)compiler_error.token.column,
                                 compiler_error.message != NULL
                                     ? compiler_error.message
                                     : "unknown compiler error",
                                 rexx_compiler_status_name(compiler_status));
        goto cleanup;
    }

    vm_status = rexx_vm_init(&vm, 0u);
    if (vm_status != REXX_VM_STATUS_OK) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: VM initialization failed: %s\n",
                                 path, rexx_vm_status_name(vm_status));
        goto cleanup;
    }
    if (stream_provider != NULL) {
        vm_status = rexx_vm_set_byte_stream_provider(
            &vm, stream_provider, stream_user_data);
        if (vm_status != REXX_VM_STATUS_OK) {
            (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                     "rexx: %s: byte-stream provider installation failed: %s\n",
                                     path, rexx_vm_status_name(vm_status));
            goto cleanup;
        }
    }
    if (argument_string_present) {
        vm_status = rexx_vm_set_argument_string(
            &vm, argument_string, argument_string_length);
        if (vm_status != REXX_VM_STATUS_OK) {
            (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                     "rexx: %s: could not prepare PARSE ARG input: %s\n",
                                     path, rexx_vm_status_name(vm_status));
            goto cleanup;
        }
    }

    rexx_vm_set_output_writer(&vm, rexx_cli_vm_output, output);
    rexx_vm_set_address_dispatcher(&vm, rexx_os_vm_address_dispatcher, NULL);
    rexx_vm_set_clock_reader(&vm, clock_reader, clock_user_data);
    vm_status = rexx_vm_execute(&vm, chunk);
    if (vm_status != REXX_VM_STATUS_OK) {
        const RexxVMError *error = &vm.error;
        (void)rexx_output_flush(output, REXX_OUTPUT_STDOUT);
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: runtime error at instruction %lu: %s\n",
                                 path, (unsigned long)error->pc,
                                 error->message != NULL
                                     ? error->message
                                     : rexx_vm_status_name(vm_status));
        goto cleanup;
    }

    if (!rexx_output_flush(output, REXX_OUTPUT_STDOUT)) {
        (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: error writing standard output\n",
                                 path);
        goto cleanup;
    }

    if (vm.exit_requested) {
        int exit_status = REXX_CLI_EXIT_FAILURE;
        int exit_status_result = rexx_cli_exit_status(&vm, &exit_status);
        if (exit_status_result < 0) {
            (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                     "rexx: %s: out of memory while reading EXIT value\n",
                                     path);
            goto cleanup;
        }
        if (exit_status_result == 0) {
            (void)rexx_output_format(output, REXX_OUTPUT_STDERR,
                                     "rexx: %s: EXIT value is not an integer process status\n",
                                     path);
            goto cleanup;
        }
        result = exit_status;
    } else {
        result = REXX_CLI_EXIT_SUCCESS;
    }

cleanup:
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return result;
}

/* Explicit output host used by a kernel shell or another embedding Console. */
int guide_rexx_main_with_stream_provider(
    int argc, char **argv, const RexxOutputHost *host,
    RexxVMClockReader clock_reader, void *clock_user_data,
    const RexxVMByteStreamProvider *stream_provider,
    void *stream_user_data)
{
    const char *program_name = "rexx";
    const char *script_path = NULL;
    char *source = NULL;
    char *argument_string = NULL;
    size_t source_length = 0u;
    size_t argument_string_length = 0u;
    int index;
    int positional_only = 0;
    int script_argument_start = argc;
    int result;
    RexxOutputContext output;

    rexx_output_init(&output, host);
    if (argc > 0 && argv != NULL && argv[0] != NULL && argv[0][0] != '\0') {
        program_name = argv[0];
    }

    if (argc < 0 || argv == NULL) {
        (void)rexx_cli_print_usage(&output, REXX_OUTPUT_STDERR, program_name);
        return REXX_CLI_EXIT_USAGE;
    }

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (argument == NULL) {
            (void)rexx_output_format(&output, REXX_OUTPUT_STDERR,
                                     "rexx: invalid NULL argument at index %d\n",
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
                (void)rexx_cli_print_usage(&output, REXX_OUTPUT_STDERR,
                                           program_name);
                return REXX_CLI_EXIT_USAGE;
            }
            return rexx_cli_print_usage(&output, REXX_OUTPUT_STDOUT,
                                        program_name)
                       ? REXX_CLI_EXIT_SUCCESS : REXX_CLI_EXIT_FAILURE;
        }

        if (!positional_only && argument[0] == '-') {
            (void)rexx_output_format(&output, REXX_OUTPUT_STDERR,
                                     "rexx: unknown option: %s\n", argument);
            (void)rexx_cli_print_usage(&output, REXX_OUTPUT_STDERR,
                                       program_name);
            return REXX_CLI_EXIT_USAGE;
        }

        script_path = argument;
        script_argument_start = index + 1;
        break;
    }

    if (script_path == NULL || script_path[0] == '\0') {
        (void)rexx_cli_print_usage(&output, REXX_OUTPUT_STDERR, program_name);
        return REXX_CLI_EXIT_USAGE;
    }

    if (!rexx_cli_read_source(&output, script_path, &source, &source_length)) {
        return REXX_CLI_EXIT_FAILURE;
    }

    if (!rexx_cli_join_arguments(argc, argv, script_argument_start,
                                 &argument_string,
                                 &argument_string_length)) {
        (void)rexx_output_format(&output, REXX_OUTPUT_STDERR,
                                 "rexx: %s: command arguments exceed the PARSE ARG limit or could not be allocated\n",
                                 script_path);
        rexx_os_free_file(source);
        return REXX_CLI_EXIT_FAILURE;
    }

    result = rexx_cli_execute_source(&output, script_path, source,
                                     source_length, argument_string,
                                     argument_string_length,
                                     script_argument_start < argc,
                                     clock_reader, clock_user_data,
                                     stream_provider, stream_user_data);
    free(argument_string);
    rexx_os_free_file(source);
    return result;
}

int guide_rexx_main_with_services(int argc,
                                  char **argv,
                                  const RexxOutputHost *host,
                                  RexxVMClockReader clock_reader,
                                  void *clock_user_data)
{
    return guide_rexx_main_with_stream_provider(
        argc, argv, host, clock_reader, clock_user_data, NULL, NULL);
}

int guide_rexx_main_with_output(int argc,
                                char **argv,
                                const RexxOutputHost *host)
{
    return guide_rexx_main_with_services(argc, argv, host, NULL, NULL);
}

/* Backwards-compatible hosted CLI entry point. Native shells should call
 * guide_rexx_main_with_output() with their invocation-local output host. */
int guide_rexx_main(int argc, char **argv)
{
    return guide_rexx_main_with_output(argc, argv, NULL);
}
