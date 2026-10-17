#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

static const char allocation_source[] =
    "names = 'x y'\n"
    "x = 1\n"
    "y = 2\n"
    "call worker\n"
    "say 'Caller resumed'\n"
    "exit 0\n"
    "worker: procedure expose (names)\n"
    "  x = x + 1\n"
    "  y = y + 1\n"
    "  destination = 'FINISH'\n"
    "  signal value destination\n"
    "  say 'unreachable'\n"
    "  return\n"
    "finish: say x y\n"
    "exit 0\n";

typedef struct Capture {
    char bytes[128];
    size_t length;
} Capture;

typedef struct AddressCapture {
    char environment[32];
    size_t length;
    char first_environment[32];
    size_t first_length;
    size_t calls;
} AddressCapture;

static size_t assertion_count;

static int output_writer(void *user_data, const char *bytes, size_t length)
{
    Capture *capture = (Capture *)user_data;
    if (length > sizeof(capture->bytes) - capture->length) return 0;
    memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
}

static int32_t address_dispatcher(void *user_data, const char *environment,
                                  size_t environment_length,
                                  const char *command, size_t command_length)
{
    AddressCapture *capture = (AddressCapture *)user_data;
    size_t copied = environment_length;
    (void)command;
    (void)command_length;
    if (copied >= sizeof(capture->environment)) return 127;
    if (capture->calls == 0u) {
        memcpy(capture->first_environment, environment, copied);
        capture->first_environment[copied] = '\0';
        capture->first_length = copied;
    }
    memcpy(capture->environment, environment, copied);
    capture->environment[copied] = '\0';
    capture->length = copied;
    ++capture->calls;
    return 0;
}

static int check(int condition, const char *description)
{
    ++assertion_count;
    if (!condition) fprintf(stderr, "FAIL: %s\n", description);
    return condition ? 0 : 1;
}

static int parse_program(RexxAstProgram **program, RexxLexer *lexer,
                         RexxParser *parser)
{
    if (rexx_lexer_init(lexer, allocation_source,
                        sizeof(allocation_source) - 1u) != REXX_LEXER_STATUS_OK)
        return 0;
    if (rexx_parser_init(parser, lexer) != REXX_PARSER_STATUS_OK) return 0;
    return rexx_parser_parse(parser, program) == REXX_PARSER_STATUS_OK;
}

static int recovery_run(RexxVM *vm, RexxBytecodeChunk *chunk,
                        Capture *capture)
{
    static const char expected[] = "2 3\n";
    capture->length = 0u;
    return rexx_vm_execute(vm, chunk) == REXX_VM_STATUS_OK &&
           capture->length == sizeof(expected) - 1u &&
           memcmp(capture->bytes, expected, sizeof(expected) - 1u) == 0 &&
           vm->routine_depth == 0u && vm->do_depth == 0u && vm->sp == 0u &&
           vm->current_environment == vm->root_environment;
}

static int qualify_allocation_failures(void)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    size_t attempts;
    size_t failpoint;
    int failures = 0;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    rexx_parser_test_allocator_fail_on(SIZE_MAX);
    failures += check(parse_program(&program, &lexer, &parser),
                      "RX8 parser baseline");
    attempts = rexx_parser_test_allocator_attempts();
    rexx_parser_test_allocator_disable();
    rexx_parser_free_ast(program);
    failures += check(rexx_parser_test_allocator_live_blocks() == 0u,
                      "RX8 parser baseline cleanup");
    for (failpoint = 1u; failpoint <= attempts; ++failpoint) {
        memset(&lexer, 0, sizeof(lexer));
        memset(&parser, 0, sizeof(parser));
        program = NULL;
        rexx_parser_test_allocator_fail_on(failpoint);
        failures += check(!parse_program(&program, &lexer, &parser) &&
                          parser.error.status == REXX_PARSER_STATUS_OUT_OF_MEMORY,
                          "RX8 parser failure diagnostic");
        rexx_parser_test_allocator_disable();
        rexx_parser_free_ast(program);
        failures += check(rexx_parser_test_allocator_live_blocks() == 0u,
                          "RX8 parser failed-parse cleanup");
    }
    printf("RX8 parser allocation failpoints: %lu/%lu\n",
           (unsigned long)attempts, (unsigned long)attempts);

    if (!parse_program(&program, &lexer, &parser)) return failures + 1;
    rexx_compiler_test_allocator_fail_on(SIZE_MAX);
    failures += check(rexx_compiler_compile(program, &chunk, &compiler_error) ==
                          REXX_COMPILER_STATUS_OK,
                      "RX8 compiler baseline");
    attempts = rexx_compiler_test_allocator_attempts();
    rexx_compiler_test_allocator_disable();
    rexx_bytecode_chunk_free(chunk);
    chunk = NULL;
    failures += check(rexx_compiler_test_allocator_live_blocks() == 0u,
                      "RX8 compiler baseline cleanup");
    for (failpoint = 1u; failpoint <= attempts; ++failpoint) {
        chunk = NULL;
        rexx_compiler_test_allocator_fail_on(failpoint);
        failures += check(rexx_compiler_compile(program, &chunk,
                                               &compiler_error) ==
                              REXX_COMPILER_STATUS_OUT_OF_MEMORY &&
                          chunk == NULL,
                          "RX8 compiler failure diagnostic");
        rexx_compiler_test_allocator_disable();
        rexx_bytecode_chunk_free(chunk);
        failures += check(rexx_compiler_test_allocator_live_blocks() == 0u,
                          "RX8 compiler failed-build cleanup");
    }
    printf("RX8 compiler allocation failpoints: %lu/%lu\n",
           (unsigned long)attempts, (unsigned long)attempts);

    if (rexx_compiler_compile(program, &chunk, &compiler_error) !=
        REXX_COMPILER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return failures + 1;
    }
    rexx_parser_free_ast(program);

    {
        Capture capture;
        RexxVM vm;
        memset(&capture, 0, sizeof(capture));
        memset(&vm, 0, sizeof(vm));
        if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
            rexx_bytecode_chunk_free(chunk);
            return failures + 1;
        }
        rexx_vm_set_output_writer(&vm, output_writer, &capture);
        rexx_vm_test_allocator_fail_on(SIZE_MAX);
        failures += check(recovery_run(&vm, chunk, &capture),
                          "RX8 VM baseline SIGNAL and subsidiary alias run");
        attempts = rexx_vm_test_allocator_attempts();
        rexx_vm_test_allocator_disable();
        rexx_vm_free(&vm);
        for (failpoint = 1u; failpoint <= attempts; ++failpoint) {
            RexxVMStatus status;
            memset(&vm, 0, sizeof(vm));
            memset(&capture, 0, sizeof(capture));
            if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
                ++failures;
                break;
            }
            rexx_vm_set_output_writer(&vm, output_writer, &capture);
            rexx_vm_test_allocator_fail_on(failpoint);
            status = rexx_vm_execute(&vm, chunk);
            failures += check(status == REXX_VM_STATUS_OUT_OF_MEMORY,
                              "RX8 VM injected allocation failure");
            rexx_vm_test_allocator_disable();
            failures += check(vm.routine_depth == 0u && vm.do_depth == 0u &&
                              vm.sp == 0u && vm.current_environment ==
                                  vm.root_environment,
                              "RX8 VM failure unwinds transient state");
            failures += check(recovery_run(&vm, chunk, &capture),
                              "RX8 VM recovery after allocation failure");
            rexx_vm_free(&vm);
        }
        failures += check(rexx_vm_test_allocator_live_blocks() == 0u,
                          "RX8 VM allocations return to zero");
        printf("RX8 VM allocation failpoints: %lu/%lu\n",
               (unsigned long)attempts, (unsigned long)attempts);
    }
    rexx_bytecode_chunk_free(chunk);
    return failures;
}

static int compile_source(const char *source, RexxAstProgram **program,
                          RexxLexer *lexer, RexxParser *parser,
                          RexxBytecodeChunk **chunk)
{
    RexxCompilerError error;
    memset(lexer, 0, sizeof(*lexer));
    memset(parser, 0, sizeof(*parser));
    *program = NULL;
    *chunk = NULL;
    if (rexx_lexer_init(lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK ||
        rexx_parser_init(parser, lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(parser, program) != REXX_PARSER_STATUS_OK)
        return 0;
    return rexx_compiler_compile(*program, chunk, &error) ==
           REXX_COMPILER_STATUS_OK;
}

static int qualify_signal_context_restore(void)
{
    static const char source[] =
        "numeric digits 12\n"
        "numeric fuzz 1\n"
        "numeric form engineering\n"
        "address OUTER\n"
        "call worker\n"
        "say digits() fuzz() form()\n"
        "address 'root-probe'\n"
        "exit 0\n"
        "worker: procedure\n"
        "numeric digits 5\n"
        "numeric fuzz 0\n"
        "numeric form scientific\n"
        "address INNER\n"
        "signal after_transfer\n"
        "return\n"
        "after_transfer:\n"
        "say digits() fuzz() form()\n"
        "address 'callee-probe'\n"
        "return\n";
    static const char expected[] =
        "5 0 SCIENTIFIC\n12 1 ENGINEERING\n";
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    Capture output;
    AddressCapture address;
    int failures = 0;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&vm, 0, sizeof(vm));
    memset(&output, 0, sizeof(output));
    memset(&address, 0, sizeof(address));
    if (!compile_source(source, &program, &lexer, &parser, &chunk) ||
        rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_parser_free_ast(program);
        rexx_bytecode_chunk_free(chunk);
        return 1;
    }
    rexx_vm_set_output_writer(&vm, output_writer, &output);
    rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &address);
    failures += check(rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK &&
                      output.length == sizeof(expected) - 1u &&
                      memcmp(output.bytes, expected, sizeof(expected) - 1u) ==
                          0 &&
                      address.calls == 2u && address.first_length == 5u &&
                      memcmp(address.first_environment, "INNER", 5u) == 0 &&
                      address.length == 5u &&
                      memcmp(address.environment, "OUTER", 5u) == 0 &&
                      vm.numeric_digits == 12u && vm.numeric_fuzz == 1u &&
                      vm.numeric_form == REXX_NUMERIC_FORM_ENGINEERING &&
                      vm.routine_depth == 0u && vm.do_depth == 0u &&
                      vm.sp == 0u && vm.current_environment ==
                          vm.root_environment,
                      "RX8 SIGNAL restores numeric, ADDRESS, and routine state");
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return failures;
}

static int qualify_resource_recovery(void)
{
    static const char expose_source[] =
        "call worker\nexit 0\nworker: procedure expose (names)\nreturn\n";
    static const char loop_source[] = "again: signal again\n";
    static const char recovery_source[] = "say 'recovered'\nexit 0\n";
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxValue names = REXX_VALUE_INIT;
    Capture capture;
    int failures = 0;
    char oversized[REXX_VM_MAX_SUBSIDIARY_LIST_LENGTH + 2u];

    memset(&capture, 0, sizeof(capture));
    memset(&vm, 0, sizeof(vm));
    if (!compile_source(expose_source, &program, &lexer, &parser, &chunk) ||
        rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_parser_free_ast(program);
        rexx_bytecode_chunk_free(chunk);
        return 1;
    }
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    failures += check(rexx_value_set_string(&names, "a (b)", 5u) ==
                          REXX_VM_STATUS_OK &&
                      rexx_vm_set_local(&vm, "names", 5u, &names) ==
                          REXX_VM_STATUS_OK &&
                      rexx_vm_execute(&vm, chunk) ==
                          REXX_VM_STATUS_INVALID_EXPOSE_LIST &&
                      vm.routine_depth == 0u && vm.do_depth == 0u &&
                      vm.sp == 0u && vm.current_environment ==
                          vm.root_environment,
                      "RX8 invalid subsidiary symbol list");
    rexx_value_clear(&names);
    memset(oversized, 'x', sizeof(oversized) - 1u);
    oversized[sizeof(oversized) - 1u] = '\0';
    failures += check(rexx_value_set_string(
                          &names, oversized,
                          REXX_VM_MAX_SUBSIDIARY_LIST_LENGTH + 1u) ==
                          REXX_VM_STATUS_OK &&
                      rexx_vm_set_local(&vm, "names", 5u, &names) ==
                          REXX_VM_STATUS_OK &&
                      rexx_vm_execute(&vm, chunk) ==
                          REXX_VM_STATUS_RESOURCE_LIMIT &&
                      vm.routine_depth == 0u && vm.do_depth == 0u &&
                      vm.sp == 0u && vm.current_environment ==
                          vm.root_environment,
                      "RX8 oversized subsidiary list bound");
    rexx_value_clear(&names);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);

    program = NULL;
    chunk = NULL;
    if (!compile_source(loop_source, &program, &lexer, &parser, &chunk) ||
        rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_parser_free_ast(program);
        rexx_bytecode_chunk_free(chunk);
        return failures + 1;
    }
    memset(&capture, 0, sizeof(capture));
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    failures += check(rexx_vm_set_instruction_budget(&vm, 12u) ==
                          REXX_VM_STATUS_OK &&
                      rexx_vm_execute(&vm, chunk) ==
                          REXX_VM_STATUS_INSTRUCTION_LIMIT &&
                      vm.routine_depth == 0u && vm.do_depth == 0u &&
                      vm.sp == 0u,
                      "RX8 backward SIGNAL respects global instruction budget");
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    program = NULL;
    chunk = NULL;
    if (!compile_source(recovery_source, &program, &lexer, &parser, &chunk)) {
        rexx_vm_free(&vm);
        return failures + 1;
    }
    failures += check(rexx_vm_set_instruction_budget(&vm, 100u) ==
                          REXX_VM_STATUS_OK &&
                      rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK &&
                      capture.length == 10u &&
                      memcmp(capture.bytes, "recovered\n", 10u) == 0,
                      "RX8 VM recovers after SIGNAL budget exhaustion");
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return failures;
}

int main(void)
{
    int failures = qualify_allocation_failures();
    failures += qualify_resource_recovery();
    failures += qualify_signal_context_restore();
    if (failures == 0) {
        printf("RX8 C assertions passed: %lu/%lu\n",
               (unsigned long)assertion_count,
               (unsigned long)assertion_count);
        puts("RX8 semantic, allocation, and recovery assertions passed");
        return 0;
    }
    fprintf(stderr, "RX8 assertions failed: %d\n", failures);
    return 1;
}
