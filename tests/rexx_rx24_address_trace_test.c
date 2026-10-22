#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t assertions;
static size_t failures;

#define CHECK(condition, label) do { \
    ++assertions; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "[FAIL] %s (line %d)\n", label, __LINE__); \
    } \
} while (0)

typedef struct Capture {
    char output[4096];
    size_t output_length;
    char environment[64];
    size_t environment_length;
    char command[128];
    size_t command_length;
    size_t dispatches;
    int32_t return_code;
    int fail_output;
} Capture;

static RexxVMStatus compile_bytes(const char *source, size_t source_length,
                                 RexxBytecodeChunk **chunk_out)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxCompilerError error;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&error, 0, sizeof(error));
    *chunk_out = NULL;
    if (rexx_lexer_init(&lexer, source, source_length) !=
        REXX_LEXER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        if (parser_status == REXX_PARSER_STATUS_OUT_OF_MEMORY)
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        if (parser_status == REXX_PARSER_STATUS_RESOURCE_LIMIT)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        return REXX_VM_STATUS_DYNAMIC_SYNTAX;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    if (compiler_status == REXX_COMPILER_STATUS_OK)
        return REXX_VM_STATUS_OK;
    if (compiler_status == REXX_COMPILER_STATUS_OUT_OF_MEMORY)
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    if (compiler_status == REXX_COMPILER_STATUS_RESOURCE_LIMIT)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    return REXX_VM_STATUS_INVALID_ARGUMENT;
}

static RexxVMStatus execute_source(RexxVM *vm, const char *source)
{
    RexxBytecodeChunk *chunk = NULL;
    RexxVMStatus status = compile_bytes(source, strlen(source), &chunk);
    if (status == REXX_VM_STATUS_OK) status = rexx_vm_execute(vm, chunk);
    rexx_bytecode_chunk_free(chunk);
    return status;
}

static int bytes_equal(const RexxValue *value, const char *expected,
                       size_t expected_length)
{
    return value != NULL && value->type == REXX_VALUE_STRING &&
           value->as.string.bytes != NULL &&
           value->as.string.length == expected_length &&
           (expected_length == 0u ||
            memcmp(value->as.string.bytes, expected, expected_length) == 0);
}

static RexxVMStatus call_builtin(RexxVM *vm, const char *name,
                                const RexxValue *arguments,
                                size_t argument_count, RexxValue *result)
{
    return rexx_builtin_dispatch_with_vm(vm, name, strlen(name), arguments,
                                         argument_count, result);
}

static RexxVMStatus call_string_option(RexxVM *vm, const char *name,
                                       const char *option,
                                       RexxValue *result)
{
    RexxValue argument = REXX_VALUE_INIT;
    RexxVMStatus status = rexx_value_set_string(&argument, option,
                                                strlen(option));
    if (status == REXX_VM_STATUS_OK)
        status = call_builtin(vm, name, &argument, 1u, result);
    rexx_value_clear(&argument);
    return status;
}

static int32_t address_dispatcher(void *user_data, const char *environment,
                                 size_t environment_length,
                                 const char *command, size_t command_length)
{
    Capture *capture = (Capture *)user_data;
    size_t copied;
    ++capture->dispatches;
    copied = environment_length < sizeof(capture->environment)
                 ? environment_length : sizeof(capture->environment) - 1u;
    if (copied != 0u) memcpy(capture->environment, environment, copied);
    capture->environment[copied] = '\0';
    capture->environment_length = copied;
    copied = command_length < sizeof(capture->command)
                 ? command_length : sizeof(capture->command) - 1u;
    if (copied != 0u) memcpy(capture->command, command, copied);
    capture->command[copied] = '\0';
    capture->command_length = copied;
    if (command_length == 8u && memcmp(command, "negative", 8u) == 0)
        return -7;
    return capture->return_code;
}

static int output_writer(void *user_data, const char *bytes, size_t length)
{
    Capture *capture = (Capture *)user_data;
    if (capture->fail_output || length > sizeof(capture->output) -
        capture->output_length - 1u)
        return 0;
    if (length != 0u)
        memcpy(capture->output + capture->output_length, bytes, length);
    capture->output_length += length;
    capture->output[capture->output_length] = '\0';
    return 1;
}

static int test_address_builtin(void)
{
    RexxVM vm;
    RexxValue result = REXX_VALUE_INIT;
    RexxValue extra = REXX_VALUE_INIT;
    const char *environment = NULL;
    size_t environment_length = 0u;
    char padded_environment[] = "TEST   ";
    RexxVMStatus status;

    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "ADDRESS test VM initializes");
    status = call_builtin(&vm, "ADDRESS", NULL, 0u, &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "KERNEL", 6u),
          "ADDRESS returns the runtime's initial KERNEL environment");
    rexx_value_clear(&result);

    CHECK(rexx_value_set_string(&extra, "extra", 5u) == REXX_VM_STATUS_OK,
          "extra ADDRESS argument is prepared");
    status = call_builtin(&vm, "ADDRESS", &extra, 1u, &result);
    CHECK(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "ADDRESS rejects extra arguments");
    rexx_value_clear(&extra);

    vm.address_environment = padded_environment;
    vm.address_environment_length = sizeof(padded_environment) - 1u;
    status = call_builtin(&vm, "ADDRESS", NULL, 0u, &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "TEST", 4u),
          "ADDRESS removes trailing environment blanks");
    rexx_value_clear(&result);
    vm.address_environment = NULL;
    vm.address_environment_length = 1u;
    status = call_builtin(&vm, "ADDRESS", NULL, 0u, &result);
    CHECK(status == REXX_VM_STATUS_INVALID_ARGUMENT,
          "ADDRESS detects inconsistent null environment state");
    vm.address_environment_length = 0u;

    status = rexx_vm_get_address_environment(&vm, &environment,
                                             &environment_length);
    CHECK(status == REXX_VM_STATUS_OK && environment_length == 6u &&
          memcmp(environment, "KERNEL", 6u) == 0,
          "public environment view reports the same established default");
    rexx_vm_free(&vm);
    return 0;
}

static int test_address_instructions_and_frames(void)
{
    static const char source[] =
        "ADDRESS OUTER\n"
        "SAY ADDRESS()\n"
        "ADDRESS INNER 'temporary'\n"
        "SAY ADDRESS()\n"
        "SAY who()\n"
        "SAY ADDRESS()\n"
        "EXIT\n"
        "who: PROCEDURE\n"
        "ADDRESS NESTED\n"
        "RETURN ADDRESS()\n";
    static const char expected[] = "OUTER\nOUTER\nNESTED\nOUTER\n";
    RexxVM vm;
    RexxValue restored = REXX_VALUE_INIT;
    Capture capture;
    RexxVMStatus status;

    memset(&capture, 0, sizeof(capture));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "ADDRESS instruction VM initializes");
    rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    status = execute_source(&vm, source);
    CHECK(status == REXX_VM_STATUS_OK, "persistent and temporary ADDRESS execute");
    CHECK(capture.environment_length == 5u &&
          memcmp(capture.environment, "INNER", 5u) == 0 &&
          capture.command_length == 9u &&
          memcmp(capture.command, "temporary", 9u) == 0,
          "one-command ADDRESS selects its callback environment and command");
    CHECK(capture.dispatches == 1u,
          "only the one-command ADDRESS form reaches the mock dispatcher");
    CHECK(capture.output_length == sizeof(expected) - 1u &&
          memcmp(capture.output, expected, sizeof(expected) - 1u) == 0,
          "ADDRESS() observes persistent state and nested routine restoration");
    CHECK(vm.address_environment_length == 5u &&
          memcmp(vm.address_environment, "OUTER", 5u) == 0,
          "root environment remains active after internal function return");
    rexx_vm_free(&vm);

    memset(&capture, 0, sizeof(capture));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "ADDRESS unwind VM initializes");
    status = execute_source(&vm,
        "ADDRESS OUTER\nCALL broken\nEXIT\n"
        "broken: ADDRESS INNER\nvalue = UNKNOWN()\nRETURN\n");
    CHECK(status == REXX_VM_STATUS_UNKNOWN_FUNCTION,
          "unknown function provides an internal-routine unwind path");
    status = call_builtin(&vm, "ADDRESS", NULL, 0u, &restored);
    CHECK(status == REXX_VM_STATUS_OK && vm.address_environment_length == 5u &&
          memcmp(vm.address_environment, "OUTER", 5u) == 0 &&
          bytes_equal(&restored, "OUTER", 5u),
          "ADDRESS state is restored after exceptional routine unwind");
    rexx_value_clear(&restored);
    rexx_vm_free(&vm);
    return 0;
}

static int test_trace_options_and_allocation(void)
{
    RexxVM vm;
    RexxValue result = REXX_VALUE_INIT;
    RexxValue numeric = REXX_VALUE_INIT;
    RexxValue extra = REXX_VALUE_INIT;
    RexxVMStatus status;
    RexxVM vm_other;

    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "TRACE test VM initializes");
    status = call_builtin(&vm, "TRACE", NULL, 0u, &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "N", 1u),
          "TRACE() reports the operational default Normal action");
    rexx_value_clear(&result);

    rexx_vm_test_allocator_fail_on(1u);
    status = call_string_option(&vm, "TRACE", "O", &result);
    rexx_vm_test_allocator_disable();
    CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY && vm.trace_action == 'N',
          "TRACE return allocation failure leaves the active action unchanged");

    status = call_string_option(&vm, "TRACE", "Off", &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "N", 1u) &&
          vm.trace_action == 'O',
          "TRACE Off returns the previous action and disables tracing");
    rexx_value_clear(&result);
    status = call_string_option(&vm, "TRACE", "normal", &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "O", 1u) &&
          vm.trace_action == 'N',
          "TRACE Normal returns Off and enables operational Normal tracing");
    rexx_value_clear(&result);
    status = call_string_option(&vm, "TRACE", "No", &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "N", 1u) &&
          vm.trace_action == 'N',
          "TRACE accepts a documented case-insensitive option abbreviation");
    rexx_value_clear(&result);

    status = call_string_option(&vm, "TRACE", "Error", &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "N", 1u) &&
          vm.trace_action == 'E',
          "TRACE Error returns Normal and enables all nonzero command outcomes");
    rexx_value_clear(&result);
    status = call_string_option(&vm, "TRACE", "Failure", &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "E", 1u) &&
          vm.trace_action == 'F',
          "TRACE Failure returns Error and enables negative command outcomes");
    rexx_value_clear(&result);
    status = call_string_option(&vm, "TRACE", "N", &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "F", 1u) &&
          vm.trace_action == 'N',
          "TRACE N returns Failure and selects negative command outcomes");
    rexx_value_clear(&result);

    status = call_string_option(&vm, "TRACE", "R", &result);
    CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY &&
          vm.trace_action == 'N',
          "valid Results action is explicitly rejected without state mutation");
    status = call_string_option(&vm, "TRACE", "?N", &result);
    CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY &&
          vm.trace_action == 'N',
          "interactive prefix is explicitly rejected without enabling a pause");
    status = call_string_option(&vm, "TRACE", "!N", &result);
    CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY &&
          vm.trace_action == 'N',
          "command-inhibition prefix is explicitly rejected without mutation");
    status = call_string_option(&vm, "TRACE", "?!Normal", &result);
    CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY &&
          vm.trace_action == 'N',
          "combined prefixes do not toggle unsupported debugger controls");
    status = call_string_option(&vm, "TRACE", "?", &result);
    CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY &&
          vm.trace_action == 'N', "lone interactive prefix is unsupported");
    status = call_string_option(&vm, "TRACE", "!", &result);
    CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY &&
          vm.trace_action == 'N', "lone inhibition prefix is unsupported");
    status = call_string_option(&vm, "TRACE", "S", &result);
    CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY,
          "Scan is explicitly rejected until scan execution exists");
    {
        static const char *const unsupported[] = { "A", "C", "I", "L" };
        size_t index;
        for (index = 0u; index < sizeof(unsupported) / sizeof(unsupported[0]);
             ++index) {
            status = call_string_option(&vm, "TRACE", unsupported[index],
                                        &result);
            CHECK(status == REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY &&
                  vm.trace_action == 'N',
                  "unimplemented clause and expression modes are rejected explicitly");
        }
    }

    status = call_string_option(&vm, "TRACE", "Z", &result);
    CHECK(status == REXX_VM_STATUS_INVALID_TRACE_REQUEST &&
          vm.trace_action == 'N', "invalid TRACE letter is rejected");
    status = call_string_option(&vm, "TRACE", "Nonsense", &result);
    CHECK(status == REXX_VM_STATUS_OK && bytes_equal(&result, "N", 1u) &&
          vm.trace_action == 'N',
          "TRACE uses the first alphabetic option character per the specification");
    rexx_value_clear(&result);
    status = call_string_option(&vm, "TRACE", "", &result);
    CHECK(status == REXX_VM_STATUS_INVALID_TRACE_REQUEST,
          "empty TRACE option is rejected");
    status = call_string_option(&vm, "TRACE", "N?", &result);
    CHECK(status == REXX_VM_STATUS_INVALID_TRACE_REQUEST,
          "malformed TRACE option is rejected");
    CHECK(rexx_value_set_number(&numeric, 1.0) == REXX_VM_STATUS_OK,
          "numeric TRACE option is prepared");
    status = call_builtin(&vm, "TRACE", &numeric, 1u, &result);
    CHECK(status == REXX_VM_STATUS_INVALID_TRACE_REQUEST &&
          vm.trace_action == 'N',
          "numeric TRACE option is rejected without applying instruction semantics");
    rexx_value_clear(&numeric);
    CHECK(rexx_value_set_string(&extra, "O", 1u) == REXX_VM_STATUS_OK,
          "extra TRACE argument is prepared");
    status = call_builtin(&vm, "TRACE", &extra, 2u, &result);
    CHECK(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
          vm.trace_action == 'N', "TRACE rejects extra arguments");
    rexx_value_clear(&extra);

    CHECK(rexx_vm_init(&vm_other, 0u) == REXX_VM_STATUS_OK,
          "second TRACE VM initializes");
    status = call_string_option(&vm, "TRACE", "O", &result);
    rexx_value_clear(&result);
    CHECK(status == REXX_VM_STATUS_OK && vm.trace_action == 'O' &&
          vm_other.trace_action == 'N',
          "TRACE state is isolated between VM instances");

    rexx_vm_free(&vm_other);
    rexx_vm_free(&vm);
    return 0;
}

static int test_trace_output_and_routine_restore(void)
{
    static const char trace_source[] =
        "SAY TRACE()\n"
        "old = TRACE('Normal')\n"
        "cmd = 'first'\n"
        "cmd\n"
        "old = TRACE('Off')\n"
        "SAY old\n"
        "cmd = 'second'\n"
        "cmd\n"
        "old = TRACE('Normal')\n"
        "cmd = 'third'\n"
        "cmd\n"
        "SAY TRACE()\n"
        "EXIT\n";
    static const char trace_expected[] =
        "N\n"
        "TRACE N: line 4: first RC(-7)\n"
        "N\n"
        "TRACE N: line 11: third RC(-7)\n"
        "N\n";
    static const char routine_source[] =
        "old = TRACE('N')\n"
        "SAY recurse(2)\n"
        "SAY TRACE()\n"
        "EXIT\n"
        "recurse: PROCEDURE\n"
        "PARSE ARG n\n"
        "IF n = 0 THEN DO\n"
        "  old = TRACE('O')\n"
        "  RETURN TRACE()\n"
        "END\n"
        "child = recurse(n - 1)\n"
        "RETURN child\n";
    static const char routine_expected[] = "O\nN\n";
    RexxVM vm;
    Capture capture;
    RexxVMStatus status;

    memset(&capture, 0, sizeof(capture));
    capture.return_code = -7;
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "trace output VM initializes");
    rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    status = execute_source(&vm, trace_source);
    if (status != REXX_VM_STATUS_OK)
        fprintf(stderr, "trace source status: %s\n", rexx_vm_status_name(status));
    CHECK(status == REXX_VM_STATUS_OK,
          "Normal and Off trace modes execute with failing mock commands");
    CHECK(capture.dispatches == 3u,
          "Normal trace leaves command dispatch active");
    CHECK(capture.output_length == sizeof(trace_expected) - 1u &&
          memcmp(capture.output, trace_expected, sizeof(trace_expected) - 1u) == 0,
          "Normal traces negative command RC; Off suppresses it");
    rexx_vm_free(&vm);

    memset(&capture, 0, sizeof(capture));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "trace frame VM initializes");
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    status = execute_source(&vm, routine_source);
    if (status != REXX_VM_STATUS_OK)
        fprintf(stderr, "routine source status: %s\n", rexx_vm_status_name(status));
    CHECK(status == REXX_VM_STATUS_OK,
          "recursive internal functions can change trace action");
    if (capture.output_length != sizeof(routine_expected) - 1u ||
        memcmp(capture.output, routine_expected,
               sizeof(routine_expected) - 1u) != 0 || vm.trace_action != 'N')
        fprintf(stderr, "routine output=[%.*s], action=%c\n",
                (int)capture.output_length, capture.output, vm.trace_action);
    CHECK(capture.output_length == sizeof(routine_expected) - 1u &&
          memcmp(capture.output, routine_expected,
                 sizeof(routine_expected) - 1u) == 0 &&
          vm.trace_action == 'N' && vm.trace_enabled != 0u,
          "recursive returns restore each caller's trace action");
    rexx_vm_free(&vm);
    return 0;
}

static int test_trace_error_and_output_recovery(void)
{
    RexxVM vm;
    RexxValue result = REXX_VALUE_INIT;
    Capture capture;
    RexxVMStatus status;

    memset(&capture, 0, sizeof(capture));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "trace error VM initializes");
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    status = execute_source(&vm,
        "old = TRACE('N')\nCALL broken\nEXIT\n"
        "broken: old = TRACE('O')\n"
        "value = UNKNOWN()\nRETURN\n");
    CHECK(status == REXX_VM_STATUS_UNKNOWN_FUNCTION &&
          vm.routine_depth == 0u && vm.trace_action == 'N' &&
          vm.trace_enabled != 0u,
          "unhandled internal error unwinds and restores the caller trace action");

    capture.fail_output = 1;
    capture.return_code = -1;
    rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);
    status = execute_source(&vm, "cmd = 'fails'\ncmd\n");
    if (status != REXX_VM_STATUS_IO_ERROR)
        fprintf(stderr, "failed-output source status: %s\n", rexx_vm_status_name(status));
    CHECK(status == REXX_VM_STATUS_IO_ERROR && vm.trace_action == 'N' &&
          capture.dispatches == 1u,
          "failed trace output reports I/O error after real dispatch");
    capture.fail_output = 0;
    status = execute_source(&vm, "cmd = 'works'\ncmd\n");
    if (status != REXX_VM_STATUS_OK)
        fprintf(stderr, "recovery source status: %s\n", rexx_vm_status_name(status));
    CHECK(status == REXX_VM_STATUS_OK && vm.trace_action == 'N' &&
          capture.dispatches == 2u,
          "VM recovers after a trace-output failure");

    rexx_vm_test_allocator_fail_on(1u);
    status = call_builtin(&vm, "ADDRESS", NULL, 0u, &result);
    rexx_vm_test_allocator_disable();
    CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
          vm.trace_action == 'N' && vm.address_environment == NULL,
          "ADDRESS result allocation failure preserves environment and trace state");
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
    return 0;
}

static int test_trace_error_failure_and_normal_modes(void)
{
    static const char source[] =
        "old = TRACE('F')\n"
        "cmd = 'positive'\n"
        "cmd\n"
        "cmd = 'negative'\n"
        "cmd\n"
        "old = TRACE('E')\n"
        "cmd = 'again'\n"
        "cmd\n"
        "cmd = 'negative'\n"
        "cmd\n"
        "old = TRACE('O')\n"
        "cmd = 'quiet'\n"
        "cmd\n"
        "SAY TRACE()\n"
        "EXIT\n";
    static const char expected[] =
        "TRACE F: line 5: negative RC(-7)\n"
        "TRACE E: line 8: again RC(127)\n"
        "TRACE E: line 10: negative RC(-7)\n"
        "O\n";
    RexxVM vm;
    Capture capture;
    RexxVMStatus status;

    memset(&capture, 0, sizeof(capture));
    capture.return_code = 127;
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "Error/Failure trace VM initializes");
    rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    status = execute_source(&vm, source);
    if (status != REXX_VM_STATUS_OK || capture.dispatches != 5u ||
        capture.output_length != sizeof(expected) - 1u ||
        memcmp(capture.output, expected, sizeof(expected) - 1u) != 0)
        fprintf(stderr, "E/F status=%s dispatches=%lu output=[%.*s]\n",
                rexx_vm_status_name(status),
                (unsigned long)capture.dispatches,
                (int)capture.output_length, capture.output);
    CHECK(status == REXX_VM_STATUS_OK && capture.dispatches == 5u,
          "Error, Failure, and Off actions leave command dispatch operational");
    CHECK(capture.output_length == sizeof(expected) - 1u &&
          memcmp(capture.output, expected, sizeof(expected) - 1u) == 0,
          "Error traces both outcomes, Failure traces negative RC, and Off is quiet");
    rexx_vm_free(&vm);
    return 0;
}

static int test_subroutine_and_interpret_state(void)
{
    static const char source[] =
        "ADDRESS OUTER\n"
        "old = TRACE('N')\n"
        "CALL change_state\n"
        "SAY ADDRESS()\n"
        "SAY TRACE()\n"
        "INTERPRET \"ADDRESS DYNAMIC\"\n"
        "INTERPRET \"old = TRACE('E')\"\n"
        "SAY ADDRESS()\n"
        "SAY TRACE()\n"
        "EXIT\n"
        "change_state: PROCEDURE\n"
        "ADDRESS INNER\n"
        "old = TRACE('O')\n"
        "RETURN\n";
    static const char expected[] = "OUTER\nN\nDYNAMIC\nE\n";
    RexxVM vm;
    Capture capture;
    RexxVMStatus status;

    memset(&capture, 0, sizeof(capture));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "subroutine and INTERPRET VM initializes");
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    status = execute_source(&vm, source);
    CHECK(status == REXX_VM_STATUS_OK,
          "CALL and INTERPRET can execute ADDRESS and TRACE built-ins");
    CHECK(capture.output_length == sizeof(expected) - 1u &&
          memcmp(capture.output, expected, sizeof(expected) - 1u) == 0,
          "PROCEDURE subroutine restores caller state and INTERPRET updates its VM");
    CHECK(vm.address_environment_length == 7u &&
          memcmp(vm.address_environment, "DYNAMIC", 7u) == 0 &&
          vm.trace_action == 'E' && vm.trace_enabled != 0u,
          "ADDRESS and TRACE state changes in INTERPRET remain active");
    rexx_vm_free(&vm);
    return 0;
}

int main(void)
{
    (void)test_address_builtin();
    (void)test_address_instructions_and_frames();
    (void)test_trace_options_and_allocation();
    (void)test_trace_output_and_routine_restore();
    (void)test_trace_error_failure_and_normal_modes();
    (void)test_trace_error_and_output_recovery();
    (void)test_subroutine_and_interpret_state();
    printf("RX24 ADDRESS/TRACE assertions: %lu, failures: %lu\n",
           (unsigned long)assertions, (unsigned long)failures);
    return failures == 0u ? 0 : 1;
}
