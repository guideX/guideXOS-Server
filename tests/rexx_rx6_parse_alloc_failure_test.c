#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct OutputCapture {
    char bytes[128];
    size_t length;
} OutputCapture;

static size_t assertions;
static size_t failures;

#define CHECK(condition, label) do { \
    ++assertions; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "[FAIL] %s (line %d)\n", label, __LINE__); \
    } \
} while (0)

static int capture_output(void *user_data, const char *bytes, size_t length)
{
    OutputCapture *capture = (OutputCapture *)user_data;
    if (capture == NULL || length > sizeof(capture->bytes) - capture->length)
        return 0;
    if (length != 0u) memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
}

static int capture_equals(const OutputCapture *capture, const char *expected)
{
    size_t expected_length = strlen(expected);
    return capture->length == expected_length &&
           memcmp(capture->bytes, expected, expected_length) == 0;
}

static int compile_source(const char *source, RexxBytecodeChunk **chunk_out)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxCompilerError error;
    RexxLexerStatus lexer_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&error, 0, sizeof(error));
    *chunk_out = NULL;
    lexer_status = rexx_lexer_init(&lexer, source, strlen(source));
    if (lexer_status != REXX_LEXER_STATUS_OK) return 0;
    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status != REXX_PARSER_STATUS_OK) return 0;
    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return 0;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    return compiler_status == REXX_COMPILER_STATUS_OK;
}

static int parse_source(const char *source,
                        RexxLexer *lexer,
                        RexxParser *parser,
                        RexxAstProgram **program)
{
    memset(lexer, 0, sizeof(*lexer));
    memset(parser, 0, sizeof(*parser));
    *program = NULL;
    if (rexx_lexer_init(lexer, source, strlen(source)) !=
        REXX_LEXER_STATUS_OK)
        return 0;
    if (rexx_parser_init(parser, lexer) != REXX_PARSER_STATUS_OK)
        return 0;
    return 1;
}

static void test_parser_template_allocation_matrix(void)
{
    static const char source[] =
        "parse value 'Alpha, Beta' with first ', ' second\n"
        "say first\n";
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxParserStatus status;
    size_t allocation_points;
    size_t point;

    CHECK(parse_source(source, &lexer, &parser, &program),
          "parser allocation-count fixture initializes");
    if (parser.initialized == 0) return;
    rexx_parser_test_allocator_fail_on((size_t)-1);
    status = rexx_parser_parse(&parser, &program);
    allocation_points = rexx_parser_test_allocator_attempts();
    CHECK(status == REXX_PARSER_STATUS_OK && program != NULL,
          "PARSE template AST builds before parser failpoint injection");
    CHECK(allocation_points != 0u &&
              rexx_parser_test_allocator_live_blocks() == allocation_points,
          "parser fixture reports all owned AST allocations");
    rexx_parser_test_allocator_disable();
    rexx_parser_free_ast(program);
    CHECK(rexx_parser_test_allocator_live_blocks() == 0u,
          "successful PARSE AST cleanup releases every parser allocation");

    for (point = 1u; point <= allocation_points; ++point) {
        size_t attempts;
        char label[128];

        CHECK(parse_source(source, &lexer, &parser, &program),
              "parser failpoint fixture initializes");
        if (parser.initialized == 0) continue;
        rexx_parser_test_allocator_fail_on(point);
        status = rexx_parser_parse(&parser, &program);
        attempts = rexx_parser_test_allocator_attempts();
        (void)snprintf(label, sizeof(label),
                       "parser AST failpoint %lu reports out-of-memory",
                       (unsigned long)point);
        CHECK(status == REXX_PARSER_STATUS_OUT_OF_MEMORY && program == NULL &&
                  parser.error.status == REXX_PARSER_STATUS_OUT_OF_MEMORY,
              label);
        (void)snprintf(label, sizeof(label),
                       "parser AST failpoint %lu is reached",
                       (unsigned long)point);
        CHECK(attempts == point, label);
        CHECK(rexx_parser_test_allocator_live_blocks() == 0u,
              "failed PARSE AST construction frees the partial tree");

        rexx_parser_test_allocator_disable();
        CHECK(parse_source(source, &lexer, &parser, &program),
              "parser can be initialized again after an allocation error");
        status = rexx_parser_parse(&parser, &program);
        CHECK(status == REXX_PARSER_STATUS_OK && program != NULL,
              "parser recovers after an injected allocation failure");
        rexx_parser_free_ast(program);
        CHECK(rexx_parser_test_allocator_live_blocks() == 0u,
              "recovered PARSE AST cleanup releases every parser allocation");
    }
}

static void test_compiler_template_allocation_matrix(void)
{
    static const char source[] =
        "parse value 'Alpha, Beta' with first ', ' second\n"
        "say first\n";
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError error;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;
    size_t allocation_points;
    size_t point;

    CHECK(parse_source(source, &lexer, &parser, &program),
          "compiler allocation-matrix AST initializes");
    if (parser.initialized == 0) return;
    parser_status = rexx_parser_parse(&parser, &program);
    CHECK(parser_status == REXX_PARSER_STATUS_OK && program != NULL,
          "compiler allocation-matrix AST parses");
    if (program == NULL) return;

    rexx_compiler_test_allocator_fail_on((size_t)-1);
    compiler_status = rexx_compiler_compile(program, &chunk, &error);
    allocation_points = rexx_compiler_test_allocator_attempts();
    CHECK(compiler_status == REXX_COMPILER_STATUS_OK && chunk != NULL,
          "PARSE bytecode compiles before compiler failpoint injection");
    CHECK(allocation_points != 0u &&
              rexx_compiler_test_allocator_live_blocks() == allocation_points,
          "compiler fixture reports all chunk allocations");
    rexx_compiler_test_allocator_disable();
    rexx_bytecode_chunk_free(chunk);
    CHECK(rexx_compiler_test_allocator_live_blocks() == 0u,
          "successful PARSE bytecode cleanup releases every compiler allocation");

    for (point = 1u; point <= allocation_points; ++point) {
        size_t attempts;
        char label[128];

        chunk = NULL;
        rexx_compiler_test_allocator_fail_on(point);
        compiler_status = rexx_compiler_compile(program, &chunk, &error);
        attempts = rexx_compiler_test_allocator_attempts();
        (void)snprintf(label, sizeof(label),
                       "compiler PARSE failpoint %lu reports out-of-memory",
                       (unsigned long)point);
        CHECK(compiler_status == REXX_COMPILER_STATUS_OUT_OF_MEMORY &&
                  chunk == NULL &&
                  error.status == REXX_COMPILER_STATUS_OUT_OF_MEMORY,
              label);
        (void)snprintf(label, sizeof(label),
                       "compiler PARSE failpoint %lu is reached",
                       (unsigned long)point);
        CHECK(attempts == point, label);
        CHECK(rexx_compiler_test_allocator_live_blocks() == 0u,
              "failed bytecode generation frees the partial chunk");

        rexx_compiler_test_allocator_disable();
        compiler_status = rexx_compiler_compile(program, &chunk, &error);
        CHECK(compiler_status == REXX_COMPILER_STATUS_OK && chunk != NULL,
              "compiler recovers after an injected allocation failure");
        rexx_bytecode_chunk_free(chunk);
        CHECK(rexx_compiler_test_allocator_live_blocks() == 0u,
              "recovered PARSE bytecode cleanup releases every compiler allocation");
    }
    rexx_parser_free_ast(program);
}

static void test_argument_string_replacement(void)
{
    RexxVM vm;
    RexxVMStatus status;
    size_t baseline;

    memset(&vm, 0, sizeof(vm));
    status = rexx_vm_init(&vm, 0u);
    CHECK(status == REXX_VM_STATUS_OK, "argument VM initializes");
    if (status != REXX_VM_STATUS_OK) return;
    CHECK(rexx_vm_set_argument_string(&vm, "old value", 9u) ==
              REXX_VM_STATUS_OK,
          "initial PARSE ARG string is stored");
    baseline = rexx_vm_test_allocator_live_blocks();

    rexx_vm_test_allocator_fail_on(1u);
    status = rexx_vm_set_argument_string(&vm, "replacement", 11u);
    CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY,
          "failed argument replacement reports out-of-memory");
    CHECK(rexx_vm_test_allocator_attempts() == 1u,
          "argument replacement failpoint is reached");
    CHECK(vm.argument_string_length == 9u &&
              memcmp(vm.argument_string, "old value", 9u) == 0,
          "failed replacement preserves the current argument string");
    CHECK(rexx_vm_test_allocator_live_blocks() == baseline,
          "failed replacement leaks no argument buffer");

    rexx_vm_test_allocator_disable();
    status = rexx_vm_set_argument_string(
        &vm, "x", REXX_VM_MAX_PARSE_SOURCE_LENGTH + 1u);
    CHECK(status == REXX_VM_STATUS_RESOURCE_LIMIT,
          "oversize argument string reports a resource limit");
    CHECK(vm.argument_string_length == 9u &&
              memcmp(vm.argument_string, "old value", 9u) == 0,
          "oversize replacement preserves the current argument string");

    status = rexx_vm_set_argument_string(&vm, "new", 3u);
    CHECK(status == REXX_VM_STATUS_OK && vm.argument_string_length == 3u &&
              memcmp(vm.argument_string, "new", 3u) == 0,
          "argument string recovers after allocation and size errors");
    rexx_vm_free(&vm);
    CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
          "argument setter cleanup returns to zero live VM blocks");
}

static void test_parse_runtime_allocation_matrix(void)
{
    static const char source[] =
        "source = 'alpha|beta'\n"
        "delimiter = '|'\n"
        "parse var source source (delimiter) remainder\n"
        "say '['||source||']['||remainder||']'\n";
    static const char expected[] = "[alpha][beta]\n";
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    OutputCapture capture;
    RexxVMStatus status;
    size_t allocation_points;
    size_t point;

    CHECK(compile_source(source, &chunk),
          "dynamic PARSE VAR allocation-matrix source compiles");
    if (chunk == NULL) return;

    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "allocation-count VM initializes");
    rexx_vm_set_output_writer(&vm, capture_output, &capture);
    rexx_vm_test_allocator_fail_on((size_t)-1);
    status = rexx_vm_execute(&vm, chunk);
    allocation_points = rexx_vm_test_allocator_attempts();
    CHECK(status == REXX_VM_STATUS_OK && capture_equals(&capture, expected),
          "dynamic PARSE VAR completes before failpoint injection");
    CHECK(allocation_points != 0u,
          "PARSE execution exercises VM allocation paths");
    rexx_vm_test_allocator_disable();
    rexx_vm_free(&vm);
    CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
          "allocation-count VM cleanup releases every block");

    for (point = 1u; point <= allocation_points; ++point) {
        size_t attempts;
        char label[128];

        memset(&vm, 0, sizeof(vm));
        memset(&capture, 0, sizeof(capture));
        status = rexx_vm_init(&vm, 0u);
        (void)snprintf(label, sizeof(label),
                       "PARSE allocation failpoint %lu VM initializes",
                       (unsigned long)point);
        CHECK(status == REXX_VM_STATUS_OK, label);
        if (status != REXX_VM_STATUS_OK) {
            rexx_vm_free(&vm);
            continue;
        }
        rexx_vm_set_output_writer(&vm, capture_output, &capture);
        rexx_vm_test_allocator_fail_on(point);
        status = rexx_vm_execute(&vm, chunk);
        attempts = rexx_vm_test_allocator_attempts();
        (void)snprintf(label, sizeof(label),
                       "PARSE allocation failpoint %lu reports OOM",
                       (unsigned long)point);
        CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY && vm.sp == 0u &&
                  vm.error.status == REXX_VM_STATUS_OUT_OF_MEMORY,
              label);
        (void)snprintf(label, sizeof(label),
                       "PARSE allocation failpoint %lu is reached",
                       (unsigned long)point);
        CHECK(attempts == point, label);

        rexx_vm_test_allocator_disable();
        capture.length = 0u;
        capture.bytes[0] = '\0';
        status = rexx_vm_execute(&vm, chunk);
        (void)snprintf(label, sizeof(label),
                       "PARSE VM recovers after failpoint %lu",
                       (unsigned long)point);
        CHECK(status == REXX_VM_STATUS_OK && vm.sp == 0u, label);
        (void)snprintf(label, sizeof(label),
                       "PARSE output is exact after failpoint %lu",
                       (unsigned long)point);
        CHECK(capture_equals(&capture, expected), label);
        rexx_vm_free(&vm);
        CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
              "PARSE failpoint VM cleanup releases every block");
    }
    rexx_bytecode_chunk_free(chunk);
}

int main(void)
{
    rexx_vm_test_allocator_disable();
    rexx_parser_test_allocator_disable();
    rexx_compiler_test_allocator_disable();
    test_argument_string_replacement();
    test_parser_template_allocation_matrix();
    test_compiler_template_allocation_matrix();
    test_parse_runtime_allocation_matrix();
    rexx_vm_test_allocator_disable();
    rexx_parser_test_allocator_disable();
    rexx_compiler_test_allocator_disable();
    CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
          "RX6 allocation-failure suite leaves zero live VM blocks");
    CHECK(rexx_parser_test_allocator_live_blocks() == 0u,
          "RX6 allocation-failure suite leaves zero live parser blocks");
    CHECK(rexx_compiler_test_allocator_live_blocks() == 0u,
          "RX6 allocation-failure suite leaves zero live compiler blocks");
    if (failures != 0u) {
        fprintf(stderr, "RX6 PARSE allocation-failure assertions failed: %lu/%lu\n",
                (unsigned long)failures, (unsigned long)assertions);
        return 1;
    }
    printf("RX6 PARSE allocation-failure assertions passed: %lu\n",
           (unsigned long)assertions);
    return 0;
}
