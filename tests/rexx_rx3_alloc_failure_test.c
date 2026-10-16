#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_numeric.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct OutputCapture {
    char bytes[256];
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

static void clear_capture(OutputCapture *capture)
{
    capture->length = 0u;
    capture->bytes[0] = '\0';
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

static int string_value_is(const RexxValue *value, const char *expected)
{
    size_t length = strlen(expected);
    return value != NULL && value->type == REXX_VALUE_STRING &&
           value->as.string.length == length &&
           memcmp(value->as.string.bytes, expected, length) == 0;
}

static const RexxValue *find_local(const RexxVM *vm, const char *name)
{
    size_t i, length = strlen(name);
    for (i = 0u; i < vm->local_count; ++i) {
        if (vm->locals[i].name_length == length &&
            memcmp(vm->locals[i].name, name, length) == 0)
            return &vm->locals[i].value;
    }
    return NULL;
}

static const RexxValue *find_stem(const RexxVM *vm,
                                  const char *stem,
                                  const char *tail)
{
    size_t i, stem_length = strlen(stem), tail_length = strlen(tail);
    for (i = 0u; i < vm->stem_count; ++i) {
        if (vm->stems[i].stem_length == stem_length &&
            vm->stems[i].tail_length == tail_length &&
            memcmp(vm->stems[i].stem, stem, stem_length) == 0 &&
            memcmp(vm->stems[i].tail, tail, tail_length) == 0)
            return &vm->stems[i].value;
    }
    return NULL;
}

static void test_decimal_core_has_no_heap_failure_points(void)
{
    RexxDecimal left, right, result, exponent;
    size_t work = REXX_VM_DEFAULT_NUMERIC_WORK_BUDGET;
    char formatted[128];
    int written;

    rexx_vm_test_allocator_fail_on(1u);
    CHECK(rexx_decimal_parse("12.50", 5u, &left) == REXX_NUMERIC_OK,
          "numeric parse works while the next VM allocation is set to fail");
    CHECK(rexx_decimal_parse(".25", 3u, &right) == REXX_NUMERIC_OK,
          "fraction parse works while allocation failure is armed");
    CHECK(rexx_decimal_add(&left, &right, 9u, &result) == REXX_NUMERIC_OK,
          "decimal addition uses no VM allocation");
    CHECK(rexx_decimal_subtract(&left, &right, 9u, &result) == REXX_NUMERIC_OK,
          "decimal subtraction uses no VM allocation");
    CHECK(rexx_decimal_multiply(&left, &right, 9u, &result) == REXX_NUMERIC_OK,
          "decimal multiplication uses no VM allocation");
    CHECK(rexx_decimal_divide(&left, &right, 9u, &work, &result) ==
              REXX_NUMERIC_OK,
          "decimal division uses fixed storage under the armed failpoint");
    CHECK(rexx_decimal_integer_divide(&left, &right, 9u, &work, &result) ==
              REXX_NUMERIC_OK,
          "integer division uses fixed storage under the armed failpoint");
    CHECK(rexx_decimal_remainder(&left, &right, 9u, &work, &result) ==
              REXX_NUMERIC_OK,
          "remainder uses fixed storage under the armed failpoint");
    CHECK(rexx_decimal_parse("3", 1u, &exponent) == REXX_NUMERIC_OK &&
              rexx_decimal_power(&left, &exponent, 9u, &work, &result) ==
                  REXX_NUMERIC_OK,
          "power uses fixed storage under the armed failpoint");
    CHECK(rexx_decimal_parse("9.999", 5u, &result) == REXX_NUMERIC_OK &&
              rexx_decimal_round(&result, 2u) == REXX_NUMERIC_OK,
          "precision adjustment uses no VM allocation");
    written = rexx_decimal_format(formatted, sizeof(formatted), &result, 2u);
    CHECK(written == 2 && strcmp(formatted, "10") == 0,
          "numeric formatting uses fixed storage");
    CHECK(rexx_vm_test_allocator_attempts() == 0u,
          "decimal core made no allocation attempt to inject");
    rexx_vm_test_allocator_disable();
}

static void test_value_copy_and_replacement(void)
{
    RexxValue destination = REXX_VALUE_INIT;
    RexxValue source = REXX_VALUE_INIT;
    size_t baseline;

    CHECK(rexx_value_set_string(&destination, "old", 3u) == REXX_VM_STATUS_OK,
          "initial destination string created");
    CHECK(rexx_value_set_string(&source, "new", 3u) == REXX_VM_STATUS_OK,
          "source string created");
    baseline = rexx_vm_test_allocator_live_blocks();

    rexx_vm_test_allocator_fail_on(1u);
    CHECK(rexx_value_copy(&destination, &source) == REXX_VM_STATUS_OUT_OF_MEMORY,
          "failed value copy propagates out-of-memory");
    rexx_vm_test_allocator_disable();
    CHECK(string_value_is(&destination, "old"),
          "failed value copy leaves destination unchanged");
    CHECK(rexx_vm_test_allocator_live_blocks() == baseline,
          "failed value copy frees no owned destination storage");

    rexx_vm_test_allocator_fail_on(1u);
    CHECK(rexx_value_set_string(&destination, "replacement", 11u) ==
              REXX_VM_STATUS_OUT_OF_MEMORY,
          "failed string replacement propagates out-of-memory");
    rexx_vm_test_allocator_disable();
    CHECK(string_value_is(&destination, "old"),
          "failed string replacement preserves the original value");
    CHECK(rexx_vm_test_allocator_live_blocks() == baseline,
          "failed string replacement does not leak a partial allocation");

    CHECK(rexx_value_copy(&destination, &source) == REXX_VM_STATUS_OK &&
              string_value_is(&destination, "new"),
          "successful value copy replaces and releases the old string");
    rexx_value_clear(&destination);
    rexx_value_clear(&source);
    CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
          "value copy and replacement cleanup returns to zero live blocks");
}

static void test_expression_failure_matrix(const char *label,
                                           const char *source,
                                           const char *expected,
                                           size_t allocation_points,
                                           uint32_t expected_digits)
{
    RexxBytecodeChunk *chunk = NULL;
    size_t point;

    CHECK(compile_source(source, &chunk), "allocation matrix source compiles");
    if (chunk == NULL) return;

    for (point = 1u; point <= allocation_points; ++point) {
        RexxVM vm;
        OutputCapture capture;
        RexxVMStatus status;
        size_t baseline;
        char description[128];

        memset(&vm, 0, sizeof(vm));
        memset(&capture, 0, sizeof(capture));
        status = rexx_vm_init(&vm, 0u);
        CHECK(status == REXX_VM_STATUS_OK, "matrix VM initializes");
        if (status != REXX_VM_STATUS_OK) {
            rexx_vm_free(&vm);
            continue;
        }
        rexx_vm_set_output_writer(&vm, capture_output, &capture);
        baseline = rexx_vm_test_allocator_live_blocks();
        rexx_vm_test_allocator_fail_on(point);
        status = rexx_vm_execute(&vm, chunk);
        (void)snprintf(description, sizeof(description), "%s failpoint %lu reports OOM",
                       label, (unsigned long)point);
        CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                  vm.error.status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                  strcmp(vm.error.message, "out of memory") == 0,
              description);
        (void)snprintf(description, sizeof(description), "%s failpoint %lu is reached",
                       label, (unsigned long)point);
        CHECK(rexx_vm_test_allocator_attempts() == point, description);
        (void)snprintf(description, sizeof(description), "%s failpoint %lu clears the stack",
                       label, (unsigned long)point);
        CHECK(vm.sp == 0u, description);
        (void)snprintf(description, sizeof(description), "%s failpoint %lu releases temporaries",
                       label, (unsigned long)point);
        CHECK(rexx_vm_test_allocator_live_blocks() == baseline, description);

        rexx_vm_test_allocator_disable();
        clear_capture(&capture);
        status = rexx_vm_execute(&vm, chunk);
        (void)snprintf(description, sizeof(description),
                       "%s VM runs successfully after failpoint %lu",
                       label, (unsigned long)point);
        CHECK(status == REXX_VM_STATUS_OK && vm.sp == 0u, description);
        (void)snprintf(description, sizeof(description),
                       "%s output is correct after failpoint %lu",
                       label, (unsigned long)point);
        CHECK(capture_equals(&capture, expected), description);
        (void)snprintf(description, sizeof(description),
                       "%s numeric context is correct after failpoint %lu",
                       label, (unsigned long)point);
        CHECK(vm.numeric_digits == expected_digits, description);
        CHECK(rexx_vm_test_allocator_live_blocks() == baseline,
              "successful retry releases temporary allocations");
        rexx_vm_free(&vm);
        CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
              "failed-operation VM cleanup returns to zero live blocks");
    }
    rexx_bytecode_chunk_free(chunk);
}

static void test_variable_update_failure_matrix(void)
{
    RexxBytecodeChunk *setup = NULL, *update = NULL, *read = NULL;
    size_t point;

    CHECK(compile_source("item = \"old\"\n", &setup),
          "variable setup source compiles");
    CHECK(compile_source("item = \"new\"\n", &update),
          "variable update source compiles");
    CHECK(compile_source("say item\n", &read),
          "variable read source compiles");
    if (setup == NULL || update == NULL || read == NULL) goto cleanup;

    for (point = 1u; point <= 3u; ++point) {
        RexxVM vm;
        OutputCapture capture;
        RexxVMStatus status;
        size_t baseline;

        memset(&vm, 0, sizeof(vm));
        memset(&capture, 0, sizeof(capture));
        status = rexx_vm_init(&vm, 0u);
        CHECK(status == REXX_VM_STATUS_OK, "variable update VM initializes");
        if (status != REXX_VM_STATUS_OK) {
            rexx_vm_free(&vm);
            continue;
        }
        rexx_vm_set_output_writer(&vm, capture_output, &capture);
        CHECK(rexx_vm_execute(&vm, setup) == REXX_VM_STATUS_OK,
              "variable prior value is installed");
        baseline = rexx_vm_test_allocator_live_blocks();
        rexx_vm_test_allocator_fail_on(point);
        status = rexx_vm_execute(&vm, update);
        CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY && vm.sp == 0u,
              "variable update allocation failure propagates and clears stack");
        CHECK(rexx_vm_test_allocator_attempts() == point,
              "variable update failure point is reached");
        CHECK(string_value_is(find_local(&vm, "ITEM"), "old"),
              "failed variable replacement keeps the old value");
        CHECK(rexx_vm_test_allocator_live_blocks() == baseline,
              "failed variable replacement releases copied values");

        rexx_vm_test_allocator_disable();
        clear_capture(&capture);
        CHECK(rexx_vm_execute(&vm, read) == REXX_VM_STATUS_OK &&
                  capture_equals(&capture, "old\n"),
              "VM reads the old variable after failed replacement");
        clear_capture(&capture);
        CHECK(rexx_vm_execute(&vm, update) == REXX_VM_STATUS_OK &&
                  string_value_is(find_local(&vm, "ITEM"), "new"),
              "VM can replace the variable after recovery");
        rexx_vm_free(&vm);
        CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
              "variable update VM cleanup returns to zero live blocks");
    }

cleanup:
    rexx_bytecode_chunk_free(setup);
    rexx_bytecode_chunk_free(update);
    rexx_bytecode_chunk_free(read);
}

static void test_stem_update_failure_matrix(void)
{
    RexxBytecodeChunk *setup = NULL, *update = NULL, *read = NULL;
    size_t point;

    CHECK(compile_source("item.1 = \"old\"\n", &setup),
          "stem setup source compiles");
    CHECK(compile_source("item.1 = \"new\"\n", &update),
          "stem update source compiles");
    CHECK(compile_source("say item.1\n", &read),
          "stem read source compiles");
    if (setup == NULL || update == NULL || read == NULL) goto cleanup;

    for (point = 1u; point <= 5u; ++point) {
        RexxVM vm;
        OutputCapture capture;
        RexxVMStatus status;
        size_t baseline;
        char description[128];

        memset(&vm, 0, sizeof(vm));
        memset(&capture, 0, sizeof(capture));
        status = rexx_vm_init(&vm, 0u);
        CHECK(status == REXX_VM_STATUS_OK, "stem update VM initializes");
        if (status != REXX_VM_STATUS_OK) {
            rexx_vm_free(&vm);
            continue;
        }
        rexx_vm_set_output_writer(&vm, capture_output, &capture);
        CHECK(rexx_vm_execute(&vm, setup) == REXX_VM_STATUS_OK,
              "stem prior value is installed");
        baseline = rexx_vm_test_allocator_live_blocks();
        rexx_vm_test_allocator_fail_on(point);
        status = rexx_vm_execute(&vm, update);
        CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY && vm.sp == 0u,
              "stem update allocation failure propagates and clears stack");
        (void)snprintf(description, sizeof(description),
                       "stem update failpoint %lu reached (attempts=%lu)",
                       (unsigned long)point,
                       (unsigned long)rexx_vm_test_allocator_attempts());
        CHECK(rexx_vm_test_allocator_attempts() >= point, description);
        CHECK(string_value_is(find_stem(&vm, "ITEM.", "1"), "old"),
              "failed stem replacement keeps the old value");
        CHECK(rexx_vm_test_allocator_live_blocks() == baseline,
              "failed stem replacement releases temporary names and values");

        rexx_vm_test_allocator_disable();
        clear_capture(&capture);
        CHECK(rexx_vm_execute(&vm, read) == REXX_VM_STATUS_OK &&
                  capture_equals(&capture, "old\n"),
              "VM reads the old stem after failed replacement");
        clear_capture(&capture);
        CHECK(rexx_vm_execute(&vm, update) == REXX_VM_STATUS_OK &&
                  string_value_is(find_stem(&vm, "ITEM.", "1"), "new"),
              "VM can replace the stem after recovery");
        rexx_vm_free(&vm);
        CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
              "stem update VM cleanup returns to zero live blocks");
    }

cleanup:
    rexx_bytecode_chunk_free(setup);
    rexx_bytecode_chunk_free(update);
    rexx_bytecode_chunk_free(read);
}

static void test_new_entry_failure_matrix(const char *label,
                                          const char *source,
                                          size_t allocation_points,
                                          int is_stem)
{
    RexxBytecodeChunk *chunk = NULL;
    size_t point;

    CHECK(compile_source(source, &chunk), "new-entry source compiles");
    if (chunk == NULL) return;
    for (point = 1u; point <= allocation_points; ++point) {
        RexxVM vm;
        RexxVMStatus status;
        size_t baseline;
        char description[128];

        memset(&vm, 0, sizeof(vm));
        status = rexx_vm_init(&vm, 0u);
        CHECK(status == REXX_VM_STATUS_OK, "new-entry VM initializes");
        if (status != REXX_VM_STATUS_OK) {
            rexx_vm_free(&vm);
            continue;
        }
        baseline = rexx_vm_test_allocator_live_blocks();
        rexx_vm_test_allocator_fail_on(point);
        status = rexx_vm_execute(&vm, chunk);
        (void)snprintf(description, sizeof(description),
                       "%s insertion point %lu reports OOM", label,
                       (unsigned long)point);
        CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY && vm.sp == 0u,
              description);
        (void)snprintf(description, sizeof(description),
                       "%s insertion point %lu is reached (attempts=%lu)", label,
                       (unsigned long)point,
                       (unsigned long)rexx_vm_test_allocator_attempts());
        CHECK(rexx_vm_test_allocator_attempts() >= point, description);
        (void)snprintf(description, sizeof(description),
                       "%s insertion point %lu releases partial state", label,
                       (unsigned long)point);
        CHECK(rexx_vm_test_allocator_live_blocks() == baseline, description);
        CHECK(is_stem ? vm.stem_count == 0u : vm.local_count == 0u,
              "failed new entry is not partially installed");

        rexx_vm_test_allocator_disable();
        status = rexx_vm_execute(&vm, chunk);
        (void)snprintf(description, sizeof(description),
                       "%s VM executes successfully after insertion failure",
                       label);
        CHECK(status == REXX_VM_STATUS_OK, description);
        CHECK(is_stem ? vm.stem_count == 1u : vm.local_count == 1u,
              "new entry is installed successfully after recovery");
        rexx_vm_free(&vm);
        CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
              "new-entry VM cleanup returns to zero live blocks");
    }
    rexx_bytecode_chunk_free(chunk);
}

int main(void)
{
    test_decimal_core_has_no_heap_failure_points();
    test_value_copy_and_replacement();
    test_expression_failure_matrix("addition", "say 0.1 + 0.2\n", "0.3\n",
                                   4u, REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_expression_failure_matrix("subtraction", "say 10.25 - 2.75\n", "7.50\n",
                                   4u, REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_expression_failure_matrix("multiplication", "say 0.1 * 0.2\n", "0.02\n",
                                   4u, REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_expression_failure_matrix("division", "say 1 / 3\n",
                                   "0.333333333\n", 4u,
                                   REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_expression_failure_matrix("integer division", "say 7 % 2\n",
                                   "3\n", 4u,
                                   REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_expression_failure_matrix("remainder", "say 10 // 3\n",
                                   "1\n", 4u,
                                   REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_expression_failure_matrix("power", "say 2 ** 8\n",
                                   "256\n", 4u,
                                   REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_expression_failure_matrix("precision adjustment",
                                   "numeric digits 3\nsay 9.999 * 1\n",
                                   "10.0\n", 5u, 3u);
    test_expression_failure_matrix("numeric-context built-ins",
                                   "numeric fuzz 2\nnumeric form engineering\n"
                                   "say fuzz()\nsay form()\n",
                                   "2\nENGINEERING\n", 4u,
                                   REXX_VM_DEFAULT_NUMERIC_DIGITS);
    test_variable_update_failure_matrix();
    test_stem_update_failure_matrix();
    test_new_entry_failure_matrix("variable", "item = \"new\"\n", 5u, 0);
    test_new_entry_failure_matrix("stem", "item.1 = \"new\"\n", 6u, 1);

    rexx_vm_test_allocator_disable();
    CHECK(rexx_vm_test_allocator_live_blocks() == 0u,
          "complete allocator-failure suite leaves zero live VM blocks");
    if (failures != 0u) {
        fprintf(stderr, "RX3 allocator-failure assertions failed: %lu/%lu\n",
                (unsigned long)failures, (unsigned long)assertions);
        return 1;
    }
    printf("RX3 allocator-failure assertions passed: %lu\n",
           (unsigned long)assertions);
    return 0;
}
