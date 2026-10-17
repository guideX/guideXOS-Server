#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

static const char routine_source[] =
    "x = 10\n"
    "items.1 = 'root'\n"
    "call work 'procedure'\n"
    "do index = 1 to 2\n"
    "  call loopWork\n"
    "end\n"
    "say x\n"
    "say items.1\n"
    "exit 0\n"
    "work: procedure expose x items.\n"
    "  parse arg text\n"
    "  local = text\n"
    "  x = x + 1\n"
    "  items.1 = local\n"
    "  return local\n"
    "loopWork:\n"
    "  do localIndex = 1 to 2\n"
    "    x = x + 1\n"
    "  end\n"
    "  return\n";

typedef struct OutputCapture {
    char bytes[128];
    size_t length;
} OutputCapture;

static int output_writer(void *user_data, const char *bytes, size_t length)
{
    OutputCapture *capture = (OutputCapture *)user_data;
    if (length > sizeof(capture->bytes) - capture->length) return 0;
    memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
}

static int parse_program(RexxAstProgram **program, RexxLexer *lexer,
                         RexxParser *parser)
{
    if (rexx_lexer_init(lexer, routine_source,
                        sizeof(routine_source) - 1u) != REXX_LEXER_STATUS_OK)
        return 0;
    if (rexx_parser_init(parser, lexer) != REXX_PARSER_STATUS_OK) return 0;
    return rexx_parser_parse(parser, program) == REXX_PARSER_STATUS_OK;
}

static int check(int condition, const char *name)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    return condition ? 0 : 1;
}

static int qualify_parser_failures(void)
{
    size_t attempts;
    size_t failure;
    int failures = 0;
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    rexx_parser_test_allocator_fail_on(SIZE_MAX);
    failures += check(parse_program(&program, &lexer, &parser),
                      "parser baseline compile of PROCEDURE EXPOSE");
    attempts = rexx_parser_test_allocator_attempts();
    rexx_parser_test_allocator_disable();
    rexx_parser_free_ast(program);
    failures += check(rexx_parser_test_allocator_live_blocks() == 0u,
                      "parser baseline AST cleanup");
    for (failure = 1u; failure <= attempts; ++failure) {
        memset(&lexer, 0, sizeof(lexer));
        memset(&parser, 0, sizeof(parser));
        program = NULL;
        rexx_parser_test_allocator_fail_on(failure);
        failures += check(!parse_program(&program, &lexer, &parser) &&
                          parser.error.status == REXX_PARSER_STATUS_OUT_OF_MEMORY,
                          "parser allocator failure diagnostic");
        rexx_parser_test_allocator_disable();
        rexx_parser_free_ast(program);
        failures += check(rexx_parser_test_allocator_live_blocks() == 0u,
                          "parser allocator failure cleanup");
    }
    printf("RX7 parser allocation failures: %lu/%lu\n",
           (unsigned long)attempts, (unsigned long)attempts);
    return failures;
}

static int qualify_compiler_failures(void)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError error;
    size_t attempts;
    size_t failure;
    int failures = 0;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    if (!parse_program(&program, &lexer, &parser)) return 1;
    rexx_compiler_test_allocator_fail_on(SIZE_MAX);
    failures += check(rexx_compiler_compile(program, &chunk, &error) ==
                      REXX_COMPILER_STATUS_OK,
                      "compiler baseline PROCEDURE EXPOSE bytecode");
    attempts = rexx_compiler_test_allocator_attempts();
    rexx_compiler_test_allocator_disable();
    rexx_bytecode_chunk_free(chunk);
    chunk = NULL;
    failures += check(rexx_compiler_test_allocator_live_blocks() == 0u,
                      "compiler baseline chunk cleanup");
    for (failure = 1u; failure <= attempts; ++failure) {
        chunk = NULL;
        rexx_compiler_test_allocator_fail_on(failure);
        failures += check(rexx_compiler_compile(program, &chunk, &error) ==
                              REXX_COMPILER_STATUS_OUT_OF_MEMORY &&
                          chunk == NULL,
                          "compiler allocator failure diagnostic");
        rexx_compiler_test_allocator_disable();
        rexx_bytecode_chunk_free(chunk);
        failures += check(rexx_compiler_test_allocator_live_blocks() == 0u,
                          "compiler allocator failure cleanup");
    }
    rexx_parser_free_ast(program);
    printf("RX7 compiler allocation failures: %lu/%lu\n",
           (unsigned long)attempts, (unsigned long)attempts);
    return failures;
}

static int qualify_vm_failures(void)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxVM vm;
    OutputCapture capture;
    size_t attempts;
    size_t failure;
    int failures = 0;
    const char expected[] = "15\nprocedure\n";
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    if (!parse_program(&program, &lexer, &parser) ||
        rexx_compiler_compile(program, &chunk, &compiler_error) !=
            REXX_COMPILER_STATUS_OK ||
        rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        rexx_parser_free_ast(program);
        return 1;
    }
    rexx_vm_set_output_writer(&vm, output_writer, &capture);
    rexx_vm_test_allocator_fail_on(SIZE_MAX);
    failures += check(rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK &&
                      capture.length == sizeof(expected) - 1u &&
                      memcmp(capture.bytes, expected, sizeof(expected) - 1u) == 0,
                      "VM baseline routine execution");
    attempts = rexx_vm_test_allocator_attempts();
    rexx_vm_test_allocator_disable();
    for (failure = 1u; failure <= attempts; ++failure) {
        RexxVMStatus injected_status;
        rexx_vm_free(&vm);
        memset(&vm, 0, sizeof(vm));
        if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
            ++failures;
            break;
        }
        rexx_vm_set_output_writer(&vm, output_writer, &capture);
        capture.length = 0u;
        rexx_vm_test_allocator_fail_on(failure);
        injected_status = rexx_vm_execute(&vm, chunk);
        if (injected_status != REXX_VM_STATUS_OUT_OF_MEMORY)
            fprintf(stderr, "VM failpoint %lu returned %s\n",
                    (unsigned long)failure,
                    rexx_vm_status_name(injected_status));
        failures += check(injected_status == REXX_VM_STATUS_OUT_OF_MEMORY,
                          "VM routine allocator failure is reported");
        rexx_vm_test_allocator_disable();
        failures += check(vm.routine_depth == 0u && vm.do_depth == 0u &&
                          vm.current_environment == vm.root_environment,
                          "VM routine allocator failure unwinds frames");
        capture.length = 0u;
        failures += check(rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK &&
                          capture.length == sizeof(expected) - 1u &&
                          memcmp(capture.bytes, expected,
                                 sizeof(expected) - 1u) == 0,
                          "VM recovers after routine allocation failure");
    }
    rexx_vm_free(&vm);
    failures += check(rexx_vm_test_allocator_live_blocks() == 0u,
                      "VM routine allocator returns to zero live blocks");
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    printf("RX7 VM allocation failures: %lu/%lu\n",
           (unsigned long)attempts, (unsigned long)attempts);
    return failures;
}

int main(void)
{
    int failures = 0;
    failures += qualify_parser_failures();
    failures += qualify_compiler_failures();
    failures += qualify_vm_failures();
    if (failures == 0) {
        puts("RX7 allocator assertions passed");
        return 0;
    }
    fprintf(stderr, "RX7 allocator assertions failed: %d\n", failures);
    return 1;
}
