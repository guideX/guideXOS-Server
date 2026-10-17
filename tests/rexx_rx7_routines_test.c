#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct Capture {
    char output[8192];
    size_t output_length;
    char last_environment[64];
    size_t last_environment_length;
    int32_t command_rc;
} Capture;

static int output_writer(void *user_data, const char *bytes, size_t length)
{
    Capture *capture = (Capture *)user_data;
    if (length > sizeof(capture->output) - capture->output_length)
        return 0;
    memcpy(capture->output + capture->output_length, bytes, length);
    capture->output_length += length;
    return 1;
}

static int32_t address_dispatcher(void *user_data, const char *environment,
                                  size_t environment_length,
                                  const char *command, size_t command_length)
{
    Capture *capture = (Capture *)user_data;
    size_t copied = environment_length;
    (void)command;
    (void)command_length;
    if (copied >= sizeof(capture->last_environment))
        copied = sizeof(capture->last_environment) - 1u;
    memcpy(capture->last_environment, environment, copied);
    capture->last_environment[copied] = '\0';
    capture->last_environment_length = copied;
    return capture->command_rc;
}

static int check(int condition, const char *name)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    return condition ? 0 : 1;
}

static RexxVMStatus run_source(RexxVM *vm, const char *source)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxVMStatus result;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    if (rexx_lexer_init(&lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return REXX_VM_STATUS_INVALID_BYTECODE;
    }
    if (rexx_compiler_compile(program, &chunk, &compiler_error) !=
        REXX_COMPILER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return REXX_VM_STATUS_INVALID_BYTECODE;
    }
    result = rexx_vm_execute(vm, chunk);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return result;
}

int main(void)
{
    static const char recursive_source[] =
        "say factorial(5)\n"
        "exit 0\n"
        "factorial: procedure\n"
        "  parse arg n\n"
        "  if n = 0 then return 1\n"
        "  return n * factorial(n - 1)\n";
    static const char nested_expose_source[] =
        "x = 1\n"
        "call outer\n"
        "say x\n"
        "exit 0\n"
        "outer: procedure expose x\n"
        "  x = x + 1\n"
        "  call inner\n"
        "  return\n"
        "inner: procedure expose x\n"
        "  x = x + 1\n"
        "  return\n";
    static const char argument_source[] =
        "call outer 'keep'\n"
        "exit 0\n"
        "outer: parse arg value\n"
        "  call inner 'replace'\n"
        "  say arg(1)\n"
        "  return\n"
        "inner: parse arg value\n"
        "  say arg(1)\n"
        "  say arg()\n"
        "  return\n";
    static const char state_source[] =
        "numeric digits 9\n"
        "address OUTER\n"
        "call change\n"
        "say digits()\n"
        "address 'probe'\n"
        "exit 0\n"
        "change: numeric digits 12\n"
        "  address INNER\n"
        "  return\n";
    static const char procedure_state_source[] =
        "numeric digits 9\n"
        "address OUTER\n"
        "call change\n"
        "say digits()\n"
        "address 'probe'\n"
        "exit 0\n"
        "change: procedure\n"
        "  numeric digits 12\n"
        "  address INNER\n"
        "  return\n";
    static const char result_source[] =
        "address KERNEL 'command'\n"
        "call withResult\n"
        "say rc\n"
        "say result\n"
        "call withoutResult\n"
        "say result\n"
        "exit 0\n"
        "withResult: return 42\n"
        "withoutResult: return\n";
    static const char depth_source[] =
        "call recur\n"
        "exit 0\n"
        "recur: procedure\n"
        "  call recur\n"
        "  return\n";
    static const char recovery_source[] = "say 'recovered'\nexit 0\n";
    static const char stack_limit_source[] =
        "say grow(10)\n"
        "exit 0\n"
        "grow: procedure\n"
        "  parse arg n\n"
        "  if n = 0 then return 0\n"
        "  return 1 + grow(n - 1)\n";
    static const char budget_source[] =
        "say grow(10)\n"
        "exit 0\n"
        "grow: procedure\n"
        "  parse arg n\n"
        "  if n = 0 then return 0\n"
        "  return 1 + grow(n - 1)\n";
    Capture capture;
    RexxVM vm;
    RexxVM small_vm;
    int failures = 0;
    memset(&capture, 0, sizeof(capture));
    memset(&vm, 0, sizeof(vm));
    memset(&small_vm, 0, sizeof(small_vm));
    capture.command_rc = 23;
    failures += check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
                      "VM initialization");
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    rexx_vm_set_address_dispatcher(&vm, address_dispatcher, &capture);

    failures += check(run_source(&vm, recursive_source) == REXX_VM_STATUS_OK &&
                      capture.output_length == 4u &&
                      memcmp(capture.output, "120\n", 4u) == 0,
                      "recursive internal function and expression stack");
    capture.output_length = 0u;
    failures += check(run_source(&vm, nested_expose_source) == REXX_VM_STATUS_OK &&
                      capture.output_length == 2u &&
                      memcmp(capture.output, "3\n", 2u) == 0,
                      "nested PROCEDURE EXPOSE aliases caller storage");
    capture.output_length = 0u;
    failures += check(run_source(&vm, argument_source) == REXX_VM_STATUS_OK &&
                      capture.output_length == 15u &&
                      memcmp(capture.output, "replace\n1\nkeep\n", 15u) == 0,
                      "nested calls restore routine arguments");
    capture.output_length = 0u;
    failures += check(run_source(&vm, state_source) == REXX_VM_STATUS_OK &&
                      capture.output_length == 2u &&
                      memcmp(capture.output, "9\n", 2u) == 0 &&
                      capture.last_environment_length == 5u &&
                      memcmp(capture.last_environment, "OUTER", 5u) == 0,
                      "numeric and ADDRESS state restore on return");
    capture.output_length = 0u;
    failures += check(run_source(&vm, procedure_state_source) ==
                          REXX_VM_STATUS_OK &&
                      capture.output_length == 2u &&
                      memcmp(capture.output, "9\n", 2u) == 0 &&
                      capture.last_environment_length == 5u &&
                      memcmp(capture.last_environment, "OUTER", 5u) == 0,
                      "PROCEDURE restores numeric and ADDRESS state");
    capture.output_length = 0u;
    failures += check(run_source(&vm, result_source) == REXX_VM_STATUS_OK &&
                      capture.output_length == 13u &&
                      memcmp(capture.output, "23\n42\nRESULT\n", 13u) == 0,
                      "CALL RESULT lifecycle preserves command RC");

    failures += check(run_source(&vm, depth_source) ==
                      REXX_VM_STATUS_ROUTINE_DEPTH_LIMIT &&
                      vm.routine_depth == 0u && vm.current_environment ==
                          vm.root_environment,
                      "bounded recursion unwinds every frame");
    capture.output_length = 0u;
    failures += check(run_source(&vm, recovery_source) == REXX_VM_STATUS_OK &&
                      capture.output_length == 10u &&
                      memcmp(capture.output, "recovered\n", 10u) == 0,
                      "VM recovers after routine depth exhaustion");
    failures += check(rexx_vm_set_instruction_budget(&vm, 24u) ==
                          REXX_VM_STATUS_OK &&
                      run_source(&vm, budget_source) ==
                          REXX_VM_STATUS_INSTRUCTION_LIMIT &&
                      vm.routine_depth == 0u,
                      "instruction budget applies across recursive calls");
    failures += check(rexx_vm_set_instruction_budget(
                          &vm, REXX_VM_DEFAULT_INSTRUCTION_BUDGET) ==
                          REXX_VM_STATUS_OK &&
                      run_source(&vm, recovery_source) == REXX_VM_STATUS_OK,
                      "VM recovers after recursive instruction exhaustion");

    failures += check(rexx_vm_init(&small_vm, 8u) == REXX_VM_STATUS_OK,
                      "small evaluation stack initialization");
    rexx_vm_set_output_writer(&small_vm, output_writer, &capture);
    failures += check(run_source(&small_vm, stack_limit_source) ==
                          REXX_VM_STATUS_STACK_OVERFLOW &&
                      small_vm.routine_depth == 0u &&
                      small_vm.current_environment == small_vm.root_environment,
                      "evaluation stack exhaustion unwinds recursive frames");
    rexx_vm_free(&small_vm);

    rexx_vm_free(&vm);
    if (failures == 0) {
        puts("RX7 routine assertions passed: 13");
        return 0;
    }
    fprintf(stderr, "RX7 routine assertions failed: %d\n", failures);
    return 1;
}
