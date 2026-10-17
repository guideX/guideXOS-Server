#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct Rx9Capture {
    char output[8192];
    size_t output_length;
    size_t command_count;
    char last_command[128];
    size_t last_command_length;
} Rx9Capture;

static size_t assertions;

enum {
    RX9_TEST_CONDITION_OFF = 0u,
    RX9_TEST_CONDITION_ON = 1u,
    RX9_TEST_CONDITION_DELAY = 2u,
    RX9_TEST_INVOCATION_SIGNAL = 0u,
    RX9_TEST_INVOCATION_CALL = 1u
};

static int check(int passed, const char *description)
{
    ++assertions;
    if (!passed) fprintf(stderr, "FAIL: %s\n", description);
    return passed ? 0 : 1;
}

static int output_writer(void *user_data, const char *bytes, size_t length)
{
    Rx9Capture *capture = (Rx9Capture *)user_data;
    if (length > sizeof(capture->output) - capture->output_length) return 0;
    memcpy(capture->output + capture->output_length, bytes, length);
    capture->output_length += length;
    return 1;
}

static int bytes_equal(const char *left, size_t left_length,
                       const char *right)
{
    size_t right_length = strlen(right);
    return left_length == right_length &&
           memcmp(left, right, right_length) == 0;
}

static int vm_local_number_equals(const RexxVM *vm, const char *name,
                                  double value)
{
    size_t index;
    for (index = 0u; index < vm->local_count; ++index) {
        const RexxVMEntry *entry = &vm->locals[index];
        if (bytes_equal(entry->name, entry->name_length, name) &&
            entry->value.type == REXX_VALUE_NUMBER &&
            entry->value.as.number == value)
            return 1;
    }
    return 0;
}

static int32_t address_dispatcher(void *user_data,
                                  const char *environment,
                                  size_t environment_length,
                                  const char *command,
                                  size_t command_length)
{
    Rx9Capture *capture = (Rx9Capture *)user_data;
    size_t copied = command_length;
    (void)environment;
    (void)environment_length;
    ++capture->command_count;
    if (copied >= sizeof(capture->last_command))
        copied = sizeof(capture->last_command) - 1u;
    if (copied != 0u) memcpy(capture->last_command, command, copied);
    capture->last_command[copied] = '\0';
    capture->last_command_length = copied;
    if (bytes_equal(command, command_length, "positive")) return 5;
    if (bytes_equal(command, command_length, "negative")) return -7;
    if (bytes_equal(command, command_length, "zero")) return 0;
    return 127;
}

static int compile_source(const char *source, RexxAstProgram **program,
                          RexxLexer *lexer, RexxParser *parser,
                          RexxBytecodeChunk **chunk,
                          RexxParserStatus *parser_status)
{
    RexxCompilerError compiler_error;
    *program = NULL;
    *chunk = NULL;
    memset(lexer, 0, sizeof(*lexer));
    memset(parser, 0, sizeof(*parser));
    if (rexx_lexer_init(lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK)
        return 0;
    if (rexx_parser_init(parser, lexer) != REXX_PARSER_STATUS_OK) return 0;
    *parser_status = rexx_parser_parse(parser, program);
    if (*parser_status != REXX_PARSER_STATUS_OK) return 0;
    if (rexx_compiler_compile(*program, chunk, &compiler_error) !=
        REXX_COMPILER_STATUS_OK) {
        rexx_parser_free_ast(*program);
        *program = NULL;
        return 0;
    }
    return 1;
}

static RexxVMStatus run_source(RexxVM *vm, const char *source,
                               RexxParserStatus *parser_status)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxVMStatus status;
    if (!compile_source(source, &program, &lexer, &parser, &chunk,
                        parser_status))
        return REXX_VM_STATUS_INVALID_BYTECODE;
    status = rexx_vm_execute(vm, chunk);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return status;
}

static int expect_output(RexxVM *vm, Rx9Capture *capture,
                         const char *source, const char *expected,
                         const char *description)
{
    RexxParserStatus parser_status = REXX_PARSER_STATUS_OK;
    RexxVMStatus status;
    size_t expected_length = strlen(expected);
    capture->output_length = 0u;
    capture->command_count = 0u;
    capture->last_command_length = 0u;
    status = run_source(vm, source, &parser_status);
    if (status != REXX_VM_STATUS_OK) {
        fprintf(stderr, "FAIL: %s returned %s\n", description,
                rexx_vm_status_name(status));
        ++assertions;
        return 1;
    }
    if (capture->output_length != expected_length ||
        memcmp(capture->output, expected, expected_length) != 0) {
        fprintf(stderr, "FAIL: %s output expected [%.*s], got [%.*s]\n",
                description, (int)expected_length, expected,
                (int)capture->output_length, capture->output);
    }
    return check(capture->output_length == expected_length &&
                 memcmp(capture->output, expected, expected_length) == 0,
                 description);
}

static int expect_parse_error(const char *source, const char *diagnostic,
                              const char *description)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxParserStatus status;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    if (rexx_lexer_init(&lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK ||
        rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK)
        return check(0, description);
    status = rexx_parser_parse(&parser, &program);
    rexx_parser_free_ast(program);
    return check(status == REXX_PARSER_STATUS_SYNTAX_ERROR &&
                 parser.error.message != NULL &&
                 strstr(parser.error.message, diagnostic) != NULL,
                 description);
}

static int qualify_synthetic_halt_notready_dispatch(void)
{
    static const struct {
        const char *source;
        RexxConditionType condition;
        int call_action;
    } cases[] = {
        {"call on halt name haltHandler\n"
         "exit 0\n"
         "haltHandler: return\n", REXX_CONDITION_HALT, 1},
        {"signal on halt name haltSignal\n"
         "exit 0\n"
         "haltSignal: exit 0\n", REXX_CONDITION_HALT, 0},
        {"call on notready name notreadyHandler\n"
         "exit 0\n"
         "notreadyHandler: return\n", REXX_CONDITION_NOTREADY, 1},
        {"signal on notready name notreadySignal\n"
         "exit 0\n"
         "notreadySignal: exit 0\n", REXX_CONDITION_NOTREADY, 0}
    };
    size_t index;
    int failures = 0;
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RexxLexer lexer;
        RexxParser parser;
        RexxAstProgram *program = NULL;
        RexxBytecodeChunk *chunk = NULL;
        RexxVM vm;
        RexxParserStatus parser_status = REXX_PARSER_STATUS_OK;
        RexxVMStatus status;
        int transferred = 0;
        int compiled = compile_source(cases[index].source, &program, &lexer,
                                      &parser, &chunk, &parser_status);
        failures += check(compiled,
                          "HALT/NOTREADY synthetic registration compiles");
        if (!compiled) continue;
        memset(&vm, 0, sizeof(vm));
        failures += check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
                          "HALT/NOTREADY synthetic VM initializes");
        if (!vm.initialized) {
            rexx_bytecode_chunk_free(chunk);
            rexx_parser_free_ast(program);
            continue;
        }
        failures += check(rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK &&
                          vm.condition_states[cases[index].condition] ==
                              RX9_TEST_CONDITION_ON,
                          "HALT/NOTREADY registration remains armed");
        status = rexx_vm_test_raise_condition(
            &vm, chunk, cases[index].condition, "synthetic event", 15u, 77u,
            &transferred);
        failures += check(status == REXX_VM_STATUS_CONTROL_TRANSFER &&
                          transferred && vm.condition_information_active &&
                          vm.current_condition ==
                              (uint32_t)cases[index].condition &&
                          bytes_equal(vm.condition_description,
                                      vm.condition_description_length,
                                      "synthetic event"),
                          "synthetic event records condition information");
        if (cases[index].call_action) {
            failures += check(vm.condition_states[cases[index].condition] ==
                                  RX9_TEST_CONDITION_DELAY,
                              "synthetic CALL event leaves condition in DELAY");
            failures += check(vm.condition_invocation ==
                                  RX9_TEST_INVOCATION_CALL,
                              "synthetic CALL event records CALL invocation");
            failures += check(vm.routine_depth == 1u,
                              "synthetic CALL event creates a handler frame");
            failures += check(vm.pc < chunk->instruction_count &&
                              chunk->instructions[vm.pc].opcode == OP_RETURN,
                              "synthetic CALL event targets the handler body");
            failures += check(vm_local_number_equals(&vm, "SIGL", 77.0),
                              "synthetic CALL event sets SIGL");
        } else {
            failures += check(vm.condition_states[cases[index].condition] ==
                                  RX9_TEST_CONDITION_OFF,
                              "synthetic SIGNAL event turns condition OFF");
            failures += check(vm.condition_invocation ==
                                  RX9_TEST_INVOCATION_SIGNAL,
                              "synthetic SIGNAL event records SIGNAL invocation");
            failures += check(vm.routine_depth == 0u,
                              "synthetic SIGNAL event creates no handler frame");
            failures += check(vm.pc > 0u &&
                              vm.pc < chunk->instruction_count &&
                              chunk->instructions[vm.pc - 1u].opcode == OP_LABEL &&
                              chunk->instructions[vm.pc].source_line == 3u,
                              "synthetic SIGNAL event targets the handler body");
            failures += check(vm_local_number_equals(&vm, "SIGL", 77.0),
                              "synthetic SIGNAL event sets SIGL");
        }
        rexx_vm_free(&vm);
        failures += check(rexx_vm_test_allocator_live_blocks() == 0u,
                          "synthetic event state releases all VM allocations");
        rexx_bytecode_chunk_free(chunk);
        rexx_parser_free_ast(program);
    }
    return failures;
}

static int qualify_semantics(void)
{
    static const char registration_source[] =
        "signal on error name signalError\n"
        "signal off error\n"
        "signal on failure\n"
        "signal off failure\n"
        "signal on halt name haltHandler\n"
        "signal off halt\n"
        "signal on notready\n"
        "signal off notready\n"
        "signal on novalue\n"
        "signal off novalue\n"
        "signal on syntax\n"
        "signal off syntax\n"
        "call on error name callError\n"
        "call off error\n"
        "call on failure\n"
        "call off failure\n"
        "call on halt\n"
        "call off halt\n"
        "call on notready\n"
        "call off notready\n"
        "exit 0\n"
        "signalError: exit 1\n"
        "haltHandler: exit 1\n"
        "callError: return\n";
    static const char no_condition_source[] =
        "say length(condition())\n"
        "say length(condition('S'))\n"
        "exit 0\n";
    static const char signal_novalue_source[] =
        "signal on novalue name missingValue\n"
        "a. = 0\n"
        "say a.missing; say a.uninitializedTail\n"
        "say missing\n"
        "say 'not executed'\n"
        "exit 0\n"
        "missingValue:\n"
        "say condition('C')\n"
        "say condition('D')\n"
        "say condition('I')\n"
        "say condition('S')\n"
        "say sigl\n"
        "exit 0\n";
    static const char compound_novalue_source[] =
        "signal on novalue\n"
        "tail = 'Z'\n"
        "say b.tail\n"
        "exit 0\n"
        "novalue: say condition('D'); exit 0\n";
    static const char signal_error_source[] =
        "signal on error name trapped\n"
        "address KERNEL 'positive'\n"
        "say 'not executed'\n"
        "exit 0\n"
        "trapped:\n"
        "say condition('C')\n"
        "say condition('I')\n"
        "say condition('S')\n"
        "say rc\n"
        "say sigl\n"
        "exit 0\n";
    static const char syntax_source[] =
        "signal on syntax\n"
        "x = 1 / 0\n"
        "say 'not executed'\n"
        "exit 0\n"
        "syntax:\n"
        "say condition('C')\n"
        "say rc\n"
        "say sigl\n"
        "say condition('D')\n"
        "exit 0\n";
    static const char invalid_bif_source[] =
        "signal on syntax\n"
        "x = condition('bad')\n"
        "exit 0\n"
        "syntax: say condition('C'); say rc; exit 0\n";
    static const char syntax_handler_error_source[] =
        "signal on syntax\n"
        "x = 1 / 0\n"
        "syntax: y = 'bad' + 1\n";
    static const char syntax_missing_handler_source[] =
        "signal on syntax name absentHandler\n"
        "x = 'bad' + 1\n";
    static const char syntax_recovery_command_source[] =
        "signal on syntax\n"
        "x = 1 / 0\n"
        "exit 1\n"
        "syntax:\n"
        "say condition('D')\n"
        "address KERNEL 'zero'\n"
        "say rc\n"
        "say 'recovered'\n"
        "exit 0\n";
    static const char call_error_source[] =
        "call seed\n"
        "call on error name handleError\n"
        "call on failure name handleFailure\n"
        "address KERNEL 'positive'\n"
        "say 'resumed'\n"
        "say seenC\n"
        "say seenD\n"
        "say seenI\n"
        "say seenS\n"
        "say seenRC\n"
        "say seenLine\n"
        "say seenAfterNested\n"
        "say result\n"
        "say length(condition('C'))\n"
        "exit 0\n"
        "seed: return 'OLD'\n"
        "handleError:\n"
        "seenC = condition('C')\n"
        "seenD = condition('D')\n"
        "seenI = condition('I')\n"
        "seenS = condition('S')\n"
        "seenRC = rc\n"
        "seenLine = sigl\n"
        "address KERNEL 'positive'\n"
        "address KERNEL 'negative'\n"
        "seenAfterNested = condition('C')\n"
        "return 'IGNORED'\n"
        "handleFailure: say condition('C'); return\n";
    static const char call_failure_delay_source[] =
        "call on failure name handleFailure\n"
        "address KERNEL 'negative'\n"
        "say 'resumed'\n"
        "say rc\n"
        "exit 0\n"
        "handleFailure: say condition('S'); address KERNEL 'negative'; return\n";
    static const char call_context_restore_source[] =
        "numeric digits 5\n"
        "call on error name handleError\n"
        "address KERNEL 'positive'\n"
        "exit 0\n"
        "handleError: numeric digits 2; address OTHER; return\n";
    static const char call_trap_state_restore_source[] =
        "hits = 0\n"
        "call on error name handleError\n"
        "address KERNEL 'positive'\n"
        "address KERNEL 'positive'\n"
        "say hits\n"
        "exit 0\n"
        "handleError: hits = hits + 1; call off error; say condition('S'); return\n";
    static const char missing_signal_handler_source[] =
        "signal on error name absentLabel\n"
        "address KERNEL 'positive'\n";
    static const char missing_call_handler_source[] =
        "call on error name absentRoutine\n"
        "address KERNEL 'positive'\n";
    static const char failure_precedence_source[] =
        "gotError = 'NONE'\n"
        "gotFailure = 'NONE'\n"
        "call on error name errorHandler\n"
        "call on failure name failureHandler\n"
        "address KERNEL 'negative'\n"
        "say gotError\n"
        "say gotFailure\n"
        "say rc\n"
        "exit 0\n"
        "errorHandler: gotError = 'ERROR'; return\n"
        "failureHandler: gotFailure = condition('C'); return\n";
    static const char error_fallback_source[] =
        "call on error name handler\n"
        "address KERNEL 'negative'\n"
        "say trapped\n"
        "say rc\n"
        "exit 0\n"
        "handler: trapped = condition('C'); return\n";
    static const char routine_restore_source[] =
        "signal on novalue name missingValue\n"
        "call worker\n"
        "say missingAfterReturn\n"
        "say 'not executed'\n"
        "exit 0\n"
        "worker: procedure\n"
        "signal off novalue\n"
        "return\n"
        "missingValue: say condition('S'); say sigl; exit 0\n";
    static const char info_restore_source[] =
        "call on error name handler\n"
        "address KERNEL 'positive'\n"
        "say length(condition('C'))\n"
        "exit 0\n"
        "handler: call inspectInfo; say condition('C'); say condition('D'); return\n"
        "inspectInfo: say condition('C'); say condition('D'); return\n";
    static const char recovery_source[] =
        "say 'recovered'\nexit 0\n";
    Rx9Capture capture;
    RexxVM vm;
    RexxParserStatus parser_status;
    int failures = 0;
    static const char no_condition_expected[] = "0\n0\n";

    memset(&capture, 0, sizeof(capture));
    memset(&vm, 0, sizeof(vm));
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) return 1;
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);

    failures += check(run_source(&vm, registration_source, &parser_status) ==
                          REXX_VM_STATUS_OK,
                      "all six condition registrations and OFF forms");
    failures += expect_output(&vm, &capture, no_condition_source,
                              no_condition_expected,
                              "CONDITION returns null without active condition");
    failures += expect_output(&vm, &capture, signal_novalue_source,
                              "0\n0\nNOVALUE\nMISSING\nSIGNAL\nOFF\n4\n",
                              "SIGNAL ON NOVALUE and compound default rule");
    failures += expect_output(&vm, &capture, compound_novalue_source,
                              "B.Z\n",
                              "NOVALUE description uses the resolved compound name");
    failures += expect_output(&vm, &capture, signal_error_source,
                              "ERROR\nSIGNAL\nOFF\n5\n2\n",
                              "SIGNAL ON ERROR nonreturning transfer and RC/SIGL");
    failures += check(capture.command_count == 1u &&
                      bytes_equal(capture.last_command,
                                  capture.last_command_length, "positive"),
                      "safe positive host command reached dispatcher once");
    failures += expect_output(&vm, &capture, syntax_source,
                              "SYNTAX\n42\n2\nArithmetic overflow/underflow\n",
                              "runtime SYNTAX transfer sets classic RC and SIGL");
    failures += expect_output(&vm, &capture, invalid_bif_source,
                              "SYNTAX\n40\n",
                              "invalid CONDITION option is catchable runtime SYNTAX");
    failures += check(run_source(&vm, syntax_handler_error_source,
                                 &parser_status) ==
                          REXX_VM_STATUS_NUMERIC_ERROR,
                      "SYNTAX trap disables itself before handler errors");
    failures += check(run_source(&vm, syntax_missing_handler_source,
                                 &parser_status) ==
                          REXX_VM_STATUS_UNKNOWN_LABEL,
                      "undefined SYNTAX handler is not recursively trapped");
    failures += expect_output(
        &vm, &capture, syntax_recovery_command_source,
        "Arithmetic overflow/underflow\n0\nrecovered\n",
        "SYNTAX handler can run a successful recovery command");
    failures += check(capture.command_count == 1u &&
                      bytes_equal(capture.last_command,
                                  capture.last_command_length, "zero"),
                      "recovery command uses the deterministic safe dispatcher");
    failures += expect_output(
        &vm, &capture, call_error_source,
        "FAILURE\nresumed\nERROR\npositive\nCALL\nDELAY\n5\n4\nERROR\nOLD\n0\n",
        "nested CALL ON conditions restore DELAY, info and ignore results");
    failures += check(capture.command_count == 3u,
                      "repeated ERROR in DELAY is ignored; FAILURE nests once");
    failures += expect_output(
        &vm, &capture, call_failure_delay_source,
        "DELAY\nresumed\n-7\n",
        "repeated FAILURE in DELAY is ignored and CALL resumes");
    failures += check(capture.command_count == 2u,
                      "repeated FAILURE while DELAY does not recurse");
    failures += check(run_source(&vm, call_context_restore_source,
                                 &parser_status) == REXX_VM_STATUS_OK &&
                      vm.numeric_digits == 5u &&
                      vm.address_environment == NULL,
                      "CALL ON restores NUMERIC and ADDRESS state");
    failures += expect_output(
        &vm, &capture, call_trap_state_restore_source,
        "OFF\nOFF\n2\n",
        "explicit CALL OFF changes DELAY locally and caller registration restores");
    failures += check(run_source(&vm, missing_signal_handler_source,
                                 &parser_status) ==
                          REXX_VM_STATUS_UNKNOWN_LABEL,
                      "missing SIGNAL condition handler fails as unknown label");
    failures += check(run_source(&vm, missing_call_handler_source,
                                 &parser_status) ==
                          REXX_VM_STATUS_UNKNOWN_ROUTINE,
                      "missing CALL condition handler fails as unknown routine");
    failures += expect_output(&vm, &capture, failure_precedence_source,
                              "NONE\nFAILURE\n-7\n",
                              "FAILURE takes precedence over ERROR for negative RC");
    failures += expect_output(&vm, &capture, error_fallback_source,
                              "ERROR\n-7\n",
                              "negative RC falls back to ERROR without FAILURE trap");
    failures += expect_output(&vm, &capture, routine_restore_source,
                              "OFF\n3\n",
                              "condition registrations restore after PROCEDURE return");
    failures += expect_output(&vm, &capture, info_restore_source,
                              "ERROR\npositive\nERROR\npositive\n0\n",
                              "condition information restores across nested calls");

    failures += expect_parse_error("call on novalue\n",
                                   "CALL ON NOVALUE and CALL ON SYNTAX",
                                   "CALL ON NOVALUE is rejected by parser");
    failures += expect_parse_error("call off syntax\n",
                                   "CALL ON NOVALUE and CALL ON SYNTAX",
                                   "CALL OFF SYNTAX is rejected by parser");
    failures += expect_parse_error("signal on bogus\n",
                                   "invalid REXX condition name",
                                   "unknown condition is rejected by parser");
    failures += expect_parse_error("signal on error name\n",
                                   "constant handler name",
                                   "missing NAME handler is rejected by parser");

    {
        RexxVMStatus recovery_status;
        capture.output_length = 0u;
        recovery_status = run_source(&vm, recovery_source, &parser_status);
        if (recovery_status != REXX_VM_STATUS_OK ||
            vm.condition_information_active ||
            vm.condition_states[REXX_CONDITION_ERROR] != 0u ||
            vm.condition_states[REXX_CONDITION_SYNTAX] != 0u)
            fprintf(stderr,
                    "clean execute status=%s info=%d error=%u syntax=%u output=[%.*s]\n",
                    rexx_vm_status_name(recovery_status),
                    vm.condition_information_active,
                    (unsigned)vm.condition_states[REXX_CONDITION_ERROR],
                    (unsigned)vm.condition_states[REXX_CONDITION_SYNTAX],
                    (int)capture.output_length, capture.output);
        failures += check(recovery_status == REXX_VM_STATUS_OK &&
                          capture.output_length == strlen("recovered\n") &&
                          memcmp(capture.output, "recovered\n",
                                 strlen("recovered\n")) == 0 &&
                          !vm.condition_information_active &&
                          vm.condition_states[REXX_CONDITION_ERROR] == 0u &&
                          vm.condition_states[REXX_CONDITION_SYNTAX] == 0u,
                          "independent execute starts with clean trap state");
    }

    rexx_vm_free(&vm);
    return failures;
}

static int qualify_allocator_failures(void)
{
    static const char trapped_source[] =
        "call on error name handler\n"
        "exposedNames = 'shared'\n"
        "shared = 'ok'\n"
        "call exposeWorker\n"
        "separator = '|'\n"
        "parse value 'left|right' with first (separator) second\n"
        "dropNames = 'throwaway'\n"
        "throwaway = 'owned'\n"
        "drop (dropNames)\n"
        "address KERNEL 'positive'\n"
        "exit 0\n"
        "handler: call nested; return 'ignored'\n"
        "nested: say condition('D'); return\n"
        "exposeWorker: procedure expose (exposedNames); return\n";
    static const char recovery_source[] = "say 'recovered'\nexit 0\n";
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxVMStatus vm_status;
    RexxCompilerStatus compiler_status;
    size_t parser_attempts;
    size_t compiler_attempts;
    size_t vm_attempts;
    size_t failpoint;
    int failures = 0;
    RexxParserStatus parse_status;

    rexx_parser_test_allocator_fail_on(SIZE_MAX);
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    if (rexx_lexer_init(&lexer, trapped_source, strlen(trapped_source)) !=
            REXX_LEXER_STATUS_OK ||
        rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK) {
        rexx_parser_test_allocator_disable();
        return 1;
    }
    parser_attempts = rexx_parser_test_allocator_attempts();
    rexx_parser_test_allocator_disable();
    rexx_parser_free_ast(program);
    failures += check(rexx_parser_test_allocator_live_blocks() == 0u,
                      "RX9 parser baseline releases condition AST");
    for (failpoint = 1u; failpoint <= parser_attempts; ++failpoint) {
        memset(&lexer, 0, sizeof(lexer));
        memset(&parser, 0, sizeof(parser));
        program = NULL;
        rexx_parser_test_allocator_fail_on(failpoint);
        (void)rexx_lexer_init(&lexer, trapped_source,
                              strlen(trapped_source));
        (void)rexx_parser_init(&parser, &lexer);
        parse_status = rexx_parser_parse(&parser, &program);
        rexx_parser_test_allocator_disable();
        failures += check(parse_status == REXX_PARSER_STATUS_OUT_OF_MEMORY,
                          "RX9 parser allocation failure is diagnosed");
        rexx_parser_free_ast(program);
        failures += check(rexx_parser_test_allocator_live_blocks() == 0u,
                          "RX9 parser failure releases partial condition AST");
    }

    if (rexx_lexer_init(&lexer, trapped_source, strlen(trapped_source)) !=
            REXX_LEXER_STATUS_OK ||
        rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK)
        return failures + 1;
    rexx_compiler_test_allocator_fail_on(SIZE_MAX);
    if (rexx_compiler_compile(program, &chunk, &compiler_error) !=
        REXX_COMPILER_STATUS_OK) {
        rexx_compiler_test_allocator_disable();
        rexx_parser_free_ast(program);
        return failures + 1;
    }
    compiler_attempts = rexx_compiler_test_allocator_attempts();
    rexx_compiler_test_allocator_disable();
    rexx_bytecode_chunk_free(chunk);
    chunk = NULL;
    failures += check(rexx_compiler_test_allocator_live_blocks() == 0u,
                      "RX9 compiler baseline releases condition symbols");
    for (failpoint = 1u; failpoint <= compiler_attempts; ++failpoint) {
        chunk = NULL;
        rexx_compiler_test_allocator_fail_on(failpoint);
        compiler_status = rexx_compiler_compile(
            program, &chunk, &compiler_error);
        rexx_compiler_test_allocator_disable();
        failures += check(compiler_status ==
                              REXX_COMPILER_STATUS_OUT_OF_MEMORY &&
                          chunk == NULL,
                          "RX9 compiler allocation failure is diagnosed");
        rexx_bytecode_chunk_free(chunk);
        failures += check(rexx_compiler_test_allocator_live_blocks() == 0u,
                          "RX9 compiler failure releases condition symbols");
    }
    rexx_parser_free_ast(program);
    program = NULL;
    if (rexx_compiler_compile(NULL, &chunk, &compiler_error) ==
        REXX_COMPILER_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return failures + 1;
    }

    /* Rebuild both chunks without allocation-failure injection for VM runs. */
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    if (rexx_lexer_init(&lexer, trapped_source, strlen(trapped_source)) !=
            REXX_LEXER_STATUS_OK ||
        rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK ||
        rexx_compiler_compile(program, &chunk, &compiler_error) !=
            REXX_COMPILER_STATUS_OK)
        return failures + 1;
    {
        RexxLexer recovery_lexer;
        RexxParser recovery_parser;
        RexxAstProgram *recovery_program = NULL;
        RexxBytecodeChunk *recovery_chunk = NULL;
        RexxCompilerError recovery_error;
        Rx9Capture capture;
        RexxVM vm;
        memset(&recovery_lexer, 0, sizeof(recovery_lexer));
        memset(&recovery_parser, 0, sizeof(recovery_parser));
        memset(&capture, 0, sizeof(capture));
        memset(&vm, 0, sizeof(vm));
        if (rexx_lexer_init(&recovery_lexer, recovery_source,
                            strlen(recovery_source)) != REXX_LEXER_STATUS_OK ||
            rexx_parser_init(&recovery_parser, &recovery_lexer) !=
                REXX_PARSER_STATUS_OK ||
            rexx_parser_parse(&recovery_parser, &recovery_program) !=
                REXX_PARSER_STATUS_OK ||
            rexx_compiler_compile(recovery_program, &recovery_chunk,
                                  &recovery_error) != REXX_COMPILER_STATUS_OK)
            return failures + 1;
        if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK)
            return failures + 1;
        rexx_vm_set_output_writer(&vm, output_writer, &capture);
        rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);
        rexx_vm_test_allocator_fail_on(SIZE_MAX);
        vm_status = rexx_vm_execute(&vm, chunk);
        vm_attempts = rexx_vm_test_allocator_attempts();
        rexx_vm_test_allocator_disable();
        failures += check(vm_status == REXX_VM_STATUS_OK,
                          "RX9 VM condition-call baseline succeeds");
        rexx_vm_free(&vm);
        failures += check(rexx_vm_test_allocator_live_blocks() == 0u,
                          "RX9 VM baseline releases all allocations");
        for (failpoint = 1u; failpoint <= vm_attempts; ++failpoint) {
            memset(&capture, 0, sizeof(capture));
            memset(&vm, 0, sizeof(vm));
            if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
                ++failures;
                break;
            }
            rexx_vm_set_output_writer(&vm, output_writer, &capture);
            rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);
            rexx_vm_test_allocator_fail_on(failpoint);
            vm_status = rexx_vm_execute(&vm, chunk);
            rexx_vm_test_allocator_disable();
            if (vm_status != REXX_VM_STATUS_OUT_OF_MEMORY)
                fprintf(stderr, "VM failpoint %lu returned %s\n",
                        (unsigned long)failpoint,
                        rexx_vm_status_name(vm_status));
            failures += check(
                vm_status == REXX_VM_STATUS_OUT_OF_MEMORY,
                "RX9 VM injected condition and reference failure is terminal");
            memset(&capture, 0, sizeof(capture));
            failures += check(rexx_vm_execute(&vm, recovery_chunk) ==
                                  REXX_VM_STATUS_OK &&
                              capture.output_length ==
                                  strlen("recovered\n") &&
                              memcmp(capture.output, "recovered\n",
                                     strlen("recovered\n")) == 0,
                              "RX9 VM recovers after injected allocation failure");
            rexx_vm_free(&vm);
            failures += check(rexx_vm_test_allocator_live_blocks() == 0u,
                              "RX9 VM failpoint cleanup releases all allocations");
        }
        failures += check(rexx_vm_test_allocator_live_blocks() == 0u,
                          "RX9 VM condition allocations are fully released");
        rexx_bytecode_chunk_free(recovery_chunk);
        rexx_parser_free_ast(recovery_program);
    }
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    printf("RX9 allocator failpoints: parser %lu, compiler %lu, VM %lu\n",
           (unsigned long)parser_attempts, (unsigned long)compiler_attempts,
           (unsigned long)vm_attempts);
    return failures;
}

static int qualify_resource_boundary(void)
{
    static const char source[] =
        "signal on syntax\n"
        "do forever\n"
        "nop\n"
        "end\n";
    static const char numeric_work_source[] =
        "signal on syntax\n"
        "value = 99999999999999999 * 99999999999999999\n"
        "say 'not executed'\n";
    static const char recovery_source[] = "say 'recovered'\nexit 0\n";
    RexxVM vm;
    RexxParserStatus parser_status = REXX_PARSER_STATUS_OK;
    Rx9Capture capture;
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    memset(&capture, 0, sizeof(capture));
    memset(&vm, 0, sizeof(vm));
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    if (rexx_lexer_init(&lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK ||
        rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK ||
        rexx_compiler_compile(program, &chunk, &compiler_error) !=
            REXX_COMPILER_STATUS_OK)
        return 1;
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) return 1;
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    (void)rexx_vm_set_instruction_budget(&vm, 32u);
    {
        RexxVMStatus status = rexx_vm_execute(&vm, chunk);
        int failed = check(status == REXX_VM_STATUS_INSTRUCTION_LIMIT,
                           "SYNTAX trap cannot catch instruction budget exhaustion");
        vm.instruction_budget = REXX_VM_DEFAULT_INSTRUCTION_BUDGET;
        vm.numeric_work_budget = 1u;
        status = run_source(&vm, numeric_work_source, &parser_status);
        failed += check(status == REXX_VM_STATUS_RESOURCE_LIMIT,
                        "SYNTAX trap cannot catch numeric-work exhaustion");
        vm.numeric_work_budget = REXX_VM_DEFAULT_NUMERIC_WORK_BUDGET;
        capture.output_length = 0u;
        status = run_source(&vm, recovery_source, &parser_status);
        failed += check(status == REXX_VM_STATUS_OK &&
                        capture.output_length == strlen("recovered\n") &&
                        memcmp(capture.output, "recovered\n",
                               strlen("recovered\n")) == 0,
                        "VM can execute and recover after numeric-work failure");
        rexx_vm_free(&vm);
        rexx_bytecode_chunk_free(chunk);
        rexx_parser_free_ast(program);
        return failed;
    }
}

int main(void)
{
    int failures = qualify_semantics();
    failures += qualify_synthetic_halt_notready_dispatch();
    failures += qualify_allocator_failures();
    failures += qualify_resource_boundary();
    printf("RX9 condition assertions: %lu (%s)\n", (unsigned long)assertions,
           failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
