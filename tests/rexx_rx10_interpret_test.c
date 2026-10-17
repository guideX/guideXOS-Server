#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_compiler.h"
#include "../rexx_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t assertions;
static char captured_output[8192];
static size_t captured_length;
static size_t address_calls;
static size_t vm_dynamic_failpoints;
static size_t vm_syntax_transfer_failpoints;
static size_t parser_dynamic_failpoints;
static size_t compiler_dynamic_failpoints;
static int address_environment_ok;
static int address_command_ok;

static int check(int condition, const char *description)
{
    ++assertions;
    if (!condition) fprintf(stderr, "FAIL: %s\n", description);
    return condition ? 0 : 1;
}

static int32_t capture_output(void *user_data, const char *bytes, size_t length)
{
    (void)user_data;
    if (length > sizeof(captured_output) - captured_length) return -1;
    if (length != 0u)
        memcpy(captured_output + captured_length, bytes, length);
    captured_length += length;
    return 1;
}

static int32_t test_address(void *user_data, const char *environment,
                            size_t environment_length, const char *command,
                            size_t command_length)
{
    (void)user_data;
    ++address_calls;
    address_environment_ok = environment_length == 6u &&
        memcmp(environment, "REMOTE", 6u) == 0;
    address_command_ok = command_length == 5u &&
        memcmp(command, "probe", 5u) == 0;
    if (command_length == 13u && memcmp(command, "command fails", 13u) == 0)
        return 7;
    return 3;
}

static void reset_output(void)
{
    captured_length = 0u;
    memset(captured_output, 0, sizeof(captured_output));
    address_calls = 0u;
    address_environment_ok = 0;
    address_command_ok = 0;
}

static RexxBytecodeChunk *compile_source(const char *source)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError error;
    if (source == NULL || rexx_lexer_init(&lexer, source, strlen(source)) !=
            REXX_LEXER_STATUS_OK ||
        rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK ||
        rexx_compiler_compile(program, &chunk, &error) !=
            REXX_COMPILER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return NULL;
    }
    rexx_parser_free_ast(program);
    return chunk;
}

static RexxVMStatus execute_text(RexxVM *vm, const char *source)
{
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVMStatus status;
    if (chunk == NULL) return REXX_VM_STATUS_INVALID_BYTECODE;
    status = rexx_vm_execute(vm, chunk);
    rexx_bytecode_chunk_free(chunk);
    return status;
}

static int init_vm(RexxVM *vm)
{
    if (rexx_vm_init(vm, 0u) != REXX_VM_STATUS_OK) return 0;
    rexx_vm_set_output_writer(vm, capture_output, NULL);
    rexx_vm_set_address_dispatcher(vm, test_address, NULL);
    return 1;
}

static int test_dynamic_language(void)
{
    static const char basic[] =
        "code = 'a = 5; b = 7; say a + b'\n"
        "interpret code\n"
        "say a\n"
        "exit 0\n";
    static const char controls[] =
        "code = 'if 1 then say \"then\"; else say \"no\"'\n"
        "interpret code\n"
        "code = 'do i = 1 to 3; say i; end'\n"
        "interpret code\n"
        "code = 'select; when 1 = 2 then say \"wrong\"; when 2 = 2 then say \"selected\"; otherwise say \"otherwise\"; end'\n"
        "interpret code\n"
        "code = 'do i = 1 to 5; if i = 3 then leave; say i; end; say \"after inner leave\"'\n"
        "interpret code\n"
        "exit 0\n";
    static const char nested[] =
        "inner = 'say \"nested\"'\n"
        "outer = 'interpret inner'\n"
        "interpret outer\n"
        "exit 0\n";
    static const char roundtrip[] =
        "code = 'call helper 5; say result'\n"
        "interpret code\n"
        "exit 0\n"
        "helper:\n"
        "parse arg number\n"
        "return number * 2\n";
    static const char expose[] =
        "shared = 'before'\n"
        "call helper 5\n"
        "say shared\n"
        "say result\n"
        "exit 0\n"
        "helper: procedure expose shared\n"
        "parse arg number\n"
        "code = 'shared = \"after\"; say arg(1)'\n"
        "interpret code\n"
        "return number\n";
    static const char numeric[] =
        "code = 'numeric digits 4; numeric fuzz 1; numeric form engineering'\n"
        "interpret code\n"
        "say digits()\n"
        "say fuzz()\n"
        "say form()\n"
        "exit 0\n";
    static const char address[] =
        "code = 'address REMOTE \"probe\"'\n"
        "interpret code\n"
        "say rc\n"
        "exit 0\n";
    static const char signal[] =
        "interpret 'signal finished'\n"
        "say 'bad'\n"
        "finished: say 'left'\n"
        "exit 0\n";
    RexxVM vm;
    int failures = 0;
    if (!init_vm(&vm)) return 1;

    reset_output();
    failures += check(execute_text(&vm, basic) == REXX_VM_STATUS_OK,
                      "dynamic assignment and multiple clauses execute");
    failures += check(captured_length == 5u &&
                      memcmp(captured_output, "12\n5\n", 5u) == 0,
                      "dynamic assignments update the live variable generation");
    failures += check(vm.interpret_depth == 0u && vm.dynamic_source_bytes == 0u &&
                      vm.dynamic_bytecode_bytes == 0u &&
                      vm.dynamic_instruction_count == 0u,
                      "completed dynamic code releases all context accounting");

    reset_output();
    failures += check(execute_text(&vm, controls) == REXX_VM_STATUS_OK,
                      "dynamic IF/ELSE and complete DO loop execute");
    failures += check(strcmp(captured_output,
                             "then\n1\n2\n3\nselected\n1\n2\nafter inner leave\n") == 0,
                      "dynamic structured clauses and local LEAVE stay in fragment");

    reset_output();
    failures += check(execute_text(&vm, nested) == REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "nested\n") == 0,
                      "nested INTERPRET restores the parent chunk");

    reset_output();
    failures += check(execute_text(&vm, roundtrip) == REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "10\n") == 0,
                      "dynamic CALL returns to the interpreted continuation");

    reset_output();
    failures += check(execute_text(&vm, expose) == REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "5\nafter\n5\n") == 0,
                      "INTERPRET uses PROCEDURE EXPOSE variables and ARG values");

    reset_output();
    failures += check(execute_text(&vm, numeric) == REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "4\n1\nENGINEERING\n") == 0,
                      "dynamic NUMERIC settings persist in the current routine");

    reset_output();
    failures += check(execute_text(&vm, address) == REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "3\n") == 0 && address_calls == 1u &&
                      address_environment_ok && address_command_ok,
                      "dynamic ADDRESS dispatch preserves the active environment");

    reset_output();
    failures += check(execute_text(&vm, signal) == REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "left\n") == 0,
                      "dynamic SIGNAL abandons its fragment and finds original labels");

    rexx_vm_free(&vm);
    return failures;
}

static int test_transfers_and_budgets(void)
{
    static const char return_source[] =
        "call helper\n"
        "say result\n"
        "exit 0\n"
        "helper:\n"
        "interpret 'return \"from dynamic\"'\n"
        "say 'bad'\n";
    static const char exit_source[] =
        "interpret 'exit 7'\n"
        "say 'bad'\n";
    static const char nested_source[] =
        "code = 'interpret code'\n"
        "interpret code\n"
        "exit 0\n";
    static const char instruction_source[] =
        "interpret 'do forever; nop; end'\n"
        "exit 0\n";
    static const char outer_leave_source[] =
        "do forever; interpret 'leave'; end\n";
    static const char outer_iterate_source[] =
        "do forever; interpret 'iterate'; end\n";
    static const char recovery_source[] = "say 'recovered'\nexit 0\n";
    RexxVM vm;
    RexxValue large_value = REXX_VALUE_INIT;
    char *large_source;
    size_t i;
    int failures = 0;

    if (!init_vm(&vm)) return 1;
    reset_output();
    failures += check(execute_text(&vm, return_source) == REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "from dynamic\n") == 0,
                      "dynamic RETURN ends the active routine");

    reset_output();
    failures += check(execute_text(&vm, exit_source) == REXX_VM_STATUS_OK &&
                      vm.exit_requested && vm.exit_has_value &&
                      vm.exit_value.type == REXX_VALUE_STRING &&
                      vm.exit_value.as.string.length == 1u &&
                      vm.exit_value.as.string.bytes[0] == '7' &&
                      captured_length == 0u,
                      "dynamic EXIT terminates the whole script with its value");

    reset_output();
    failures += check(execute_text(&vm, nested_source) ==
                          REXX_VM_STATUS_RESOURCE_LIMIT &&
                      vm.interpret_depth == 0u && vm.active_chunk == NULL,
                      "excessive nested INTERPRET fails at the depth bound");
    reset_output();
    failures += check(execute_text(&vm, recovery_source) ==
                          REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "recovered\n") == 0,
                      "VM executes a new script after nesting exhaustion");

    reset_output();
    failures += check(execute_text(&vm, outer_leave_source) ==
                          REXX_VM_STATUS_DYNAMIC_SYNTAX &&
                      vm.interpret_depth == 0u && vm.do_depth == 0u,
                      "dynamic LEAVE cannot escape to a caller DO loop");
    reset_output();
    failures += check(execute_text(&vm, outer_iterate_source) ==
                          REXX_VM_STATUS_DYNAMIC_SYNTAX &&
                      vm.interpret_depth == 0u && vm.do_depth == 0u,
                      "dynamic ITERATE cannot target a caller DO loop");

    failures += check(rexx_vm_set_instruction_budget(&vm, 64u) ==
                          REXX_VM_STATUS_OK,
                      "small shared instruction budget is configurable");
    reset_output();
    failures += check(execute_text(&vm, instruction_source) ==
                          REXX_VM_STATUS_INSTRUCTION_LIMIT &&
                      vm.interpret_depth == 0u && vm.dynamic_instruction_count == 0u,
                      "interpreted loop consumes the shared instruction budget");
    failures += check(rexx_vm_set_instruction_budget(
                          &vm, REXX_VM_DEFAULT_INSTRUCTION_BUDGET) ==
                          REXX_VM_STATUS_OK,
                      "instruction budget can be restored after exhaustion");
    reset_output();
    failures += check(execute_text(&vm, recovery_source) ==
                          REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "recovered\n") == 0,
                      "VM recovers after interpreted instruction exhaustion");

    vm.interpret_compile_budget = 2u;
    failures += check(execute_text(&vm, "interpret 'nop'\nexit 0\n") ==
                          REXX_VM_STATUS_RESOURCE_LIMIT,
                      "runtime parse/compile work has a shared bounded budget");
    vm.interpret_compile_budget = REXX_VM_DEFAULT_INTERPRET_COMPILE_BUDGET;

    large_source = (char *)malloc(REXX_VM_MAX_INTERPRET_SOURCE_LENGTH + 2u);
    if (large_source == NULL) {
        failures += check(0, "oversized source test allocation succeeds");
    } else {
        for (i = 0u; i < REXX_VM_MAX_INTERPRET_SOURCE_LENGTH + 1u; ++i)
            large_source[i] = ' ';
        large_source[REXX_VM_MAX_INTERPRET_SOURCE_LENGTH + 1u] = '\0';
        failures += check(rexx_value_set_string(
                              &large_value, large_source,
                              REXX_VM_MAX_INTERPRET_SOURCE_LENGTH + 1u) ==
                              REXX_VM_STATUS_OK &&
                          rexx_vm_set_local(&vm, "CODE", 4u, &large_value) ==
                              REXX_VM_STATUS_OK,
                          "oversized interpreted source is staged in the VM");
        failures += check(execute_text(&vm, "interpret code\nexit 0\n") ==
                              REXX_VM_STATUS_RESOURCE_LIMIT,
                          "oversized interpreted source is rejected");
        free(large_source);
    }
    rexx_value_clear(&large_value);
    rexx_vm_free(&vm);
    return failures;
}

static RexxVMStatus execute_chunk_with_allocator(RexxVM *vm,
                                                RexxBytecodeChunk *chunk)
{
    reset_output();
    return rexx_vm_execute(vm, chunk);
}

static int test_allocation_failures(void)
{
    static const char dynamic_source[] =
        "call on error name handleError\n"
        "inner = 'address KERNEL \"command fails\"; say \"dynamic allocation path\"'\n"
        "code = 'interpret inner'\n"
        "interpret code\n"
        "exit 0\n"
        "handleError:\n"
        "return\n";
    static const char recovery_source[] = "say 'recovered'\nexit 0\n";
    RexxBytecodeChunk *dynamic_chunk = compile_source(dynamic_source);
    RexxBytecodeChunk *recovery_chunk = compile_source(recovery_source);
    RexxVM vm;
    size_t attempts;
    size_t failure;
    size_t baseline_live;
    int failures = 0;

    if (dynamic_chunk == NULL || recovery_chunk == NULL || !init_vm(&vm)) {
        rexx_bytecode_chunk_free(dynamic_chunk);
        rexx_bytecode_chunk_free(recovery_chunk);
        return 1;
    }
    rexx_vm_test_allocator_fail_on(SIZE_MAX);
    failures += check(execute_chunk_with_allocator(&vm, dynamic_chunk) ==
                          REXX_VM_STATUS_OK &&
                      strcmp(captured_output, "dynamic allocation path\n") == 0 &&
                      address_calls == 1u,
                      "nested condition-call allocation baseline succeeds");
    attempts = rexx_vm_test_allocator_attempts();
    vm_dynamic_failpoints = attempts;
    rexx_vm_test_allocator_disable();
    failures += check(attempts != 0u,
                      "dynamic path exercises VM-owned allocations");
    rexx_vm_free(&vm);
    baseline_live = rexx_vm_test_allocator_live_blocks();

    for (failure = 1u; failure <= attempts; ++failure) {
        RexxVM failed_vm;
        if (!init_vm(&failed_vm)) {
            failures += check(0, "VM initializes for VM allocation failpoint");
            break;
        }
        rexx_vm_test_allocator_fail_on(failure);
        if (rexx_vm_execute(&failed_vm, dynamic_chunk) !=
            REXX_VM_STATUS_OUT_OF_MEMORY)
            failures += check(0, "VM allocation failpoint returns out of memory");
        else
            failures += check(1, "VM allocation failpoint returns out of memory");
        rexx_vm_test_allocator_disable();
        failures += check(failed_vm.interpret_depth == 0u &&
                          failed_vm.dynamic_source_bytes == 0u &&
                          failed_vm.dynamic_bytecode_bytes == 0u,
                          "VM allocation failure releases dynamic contexts");
        reset_output();
        failures += check(rexx_vm_execute(&failed_vm, recovery_chunk) ==
                              REXX_VM_STATUS_OK &&
                          strcmp(captured_output, "recovered\n") == 0,
                          "VM recovers after dynamic VM allocation failure");
        rexx_vm_free(&failed_vm);
        failures += check(rexx_vm_test_allocator_live_blocks() == baseline_live,
                          "VM failpoint leaves no VM allocation leak");
    }

    rexx_parser_test_allocator_fail_on(SIZE_MAX);
    if (!init_vm(&vm)) failures += check(0, "VM initializes for parser baseline");
    else {
        failures += check(rexx_vm_execute(&vm, dynamic_chunk) ==
                              REXX_VM_STATUS_OK,
                          "dynamic parser allocation baseline succeeds");
        attempts = rexx_parser_test_allocator_attempts();
        parser_dynamic_failpoints = attempts;
        rexx_parser_test_allocator_disable();
        failures += check(attempts != 0u,
                          "dynamic source exercises parser allocations");
        rexx_vm_free(&vm);
        for (failure = 1u; failure <= attempts; ++failure) {
            RexxVM failed_vm;
            if (!init_vm(&failed_vm)) {
                failures += check(0, "VM initializes for parser failpoint");
                break;
            }
            rexx_parser_test_allocator_fail_on(failure);
            failures += check(rexx_vm_execute(&failed_vm, dynamic_chunk) ==
                                  REXX_VM_STATUS_OUT_OF_MEMORY,
                              "runtime parser failpoint is terminal OOM");
            rexx_parser_test_allocator_disable();
            reset_output();
            failures += check(rexx_vm_execute(&failed_vm, recovery_chunk) ==
                                  REXX_VM_STATUS_OK,
                              "VM recovers after runtime parser allocation failure");
            rexx_vm_free(&failed_vm);
        }
    }

    rexx_compiler_test_allocator_fail_on(SIZE_MAX);
    if (!init_vm(&vm)) failures += check(0, "VM initializes for compiler baseline");
    else {
        failures += check(rexx_vm_execute(&vm, dynamic_chunk) ==
                              REXX_VM_STATUS_OK,
                          "dynamic compiler allocation baseline succeeds");
        attempts = rexx_compiler_test_allocator_attempts();
        compiler_dynamic_failpoints = attempts;
        rexx_compiler_test_allocator_disable();
        failures += check(attempts != 0u,
                          "dynamic source exercises compiler allocations");
        rexx_vm_free(&vm);
        for (failure = 1u; failure <= attempts; ++failure) {
            RexxVM failed_vm;
            if (!init_vm(&failed_vm)) {
                failures += check(0, "VM initializes for compiler failpoint");
                break;
            }
            rexx_compiler_test_allocator_fail_on(failure);
            failures += check(rexx_vm_execute(&failed_vm, dynamic_chunk) ==
                                  REXX_VM_STATUS_OUT_OF_MEMORY,
                              "runtime compiler failpoint is terminal OOM");
            rexx_compiler_test_allocator_disable();
            reset_output();
            failures += check(rexx_vm_execute(&failed_vm, recovery_chunk) ==
                                  REXX_VM_STATUS_OK,
                              "VM recovers after runtime compiler allocation failure");
            rexx_vm_free(&failed_vm);
        }
    }
    rexx_vm_test_allocator_disable();
    rexx_parser_test_allocator_disable();
    rexx_compiler_test_allocator_disable();
    rexx_bytecode_chunk_free(dynamic_chunk);
    rexx_bytecode_chunk_free(recovery_chunk);
    return failures;
}

static int test_syntax_transfer_allocation_failures(void)
{
    static const char source[] =
        "signal on syntax name syntaxHandler\n"
        "interpret 'numeric form value invalid'\n"
        "exit 0\n"
        "syntaxHandler:\n"
        "exit 0\n";
    static const char recovery_source[] = "say 'recovered'\nexit 0\n";
    RexxBytecodeChunk *source_chunk = compile_source(source);
    RexxBytecodeChunk *recovery_chunk = compile_source(recovery_source);
    RexxVM vm;
    size_t attempts;
    size_t failure;
    size_t live_before;
    int failures = 0;

    if (source_chunk == NULL || recovery_chunk == NULL || !init_vm(&vm)) {
        rexx_bytecode_chunk_free(source_chunk);
        rexx_bytecode_chunk_free(recovery_chunk);
        return 1;
    }
    rexx_vm_test_allocator_fail_on(SIZE_MAX);
    failures += check(rexx_vm_execute(&vm, source_chunk) ==
                          REXX_VM_STATUS_OK && vm.interpret_depth == 0u,
                      "runtime SYNTAX SIGNAL exits and cleans dynamic contexts");
    attempts = rexx_vm_test_allocator_attempts();
    vm_syntax_transfer_failpoints = attempts;
    rexx_vm_test_allocator_disable();
    rexx_vm_free(&vm);
    live_before = rexx_vm_test_allocator_live_blocks();

    for (failure = 1u; failure <= attempts; ++failure) {
        RexxVM failed_vm;
        if (!init_vm(&failed_vm)) {
            failures += check(0, "VM initializes for SYNTAX transfer failpoint");
            break;
        }
        rexx_vm_test_allocator_fail_on(failure);
        failures += check(rexx_vm_execute(&failed_vm, source_chunk) ==
                              REXX_VM_STATUS_OUT_OF_MEMORY,
                          "SYNTAX transfer allocation failpoint is fatal OOM");
        rexx_vm_test_allocator_disable();
        failures += check(failed_vm.interpret_depth == 0u &&
                          failed_vm.dynamic_source_bytes == 0u &&
                          failed_vm.dynamic_bytecode_bytes == 0u,
                          "SYNTAX transfer failure releases dynamic contexts");
        reset_output();
        failures += check(rexx_vm_execute(&failed_vm, recovery_chunk) ==
                              REXX_VM_STATUS_OK &&
                          strcmp(captured_output, "recovered\n") == 0,
                          "VM recovers after SYNTAX transfer allocation failure");
        rexx_vm_free(&failed_vm);
        failures += check(rexx_vm_test_allocator_live_blocks() == live_before,
                          "SYNTAX transfer failpoint leaves no VM leak");
    }
    rexx_vm_test_allocator_disable();
    rexx_bytecode_chunk_free(source_chunk);
    rexx_bytecode_chunk_free(recovery_chunk);
    return failures;
}

int main(void)
{
    int failures = 0;
    failures += test_dynamic_language();
    failures += test_transfers_and_budgets();
    failures += test_allocation_failures();
    failures += test_syntax_transfer_allocation_failures();
    if (failures == 0)
        printf("RX10 INTERPRET assertions passed: %lu; allocation failpoints "
               "VM nested=%lu VM SYNTAX=%lu parser=%lu compiler=%lu\n",
               (unsigned long)assertions,
               (unsigned long)vm_dynamic_failpoints,
               (unsigned long)vm_syntax_transfer_failpoints,
               (unsigned long)parser_dynamic_failpoints,
               (unsigned long)compiler_dynamic_failpoints);
    else
        fprintf(stderr, "RX10 INTERPRET assertions failed: %d/%lu\n",
                failures, (unsigned long)assertions);
    return failures == 0 ? 0 : 1;
}
