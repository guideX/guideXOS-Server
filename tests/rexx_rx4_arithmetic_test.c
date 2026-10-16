#include "../rexx_numeric.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

#define WORK_BUDGET ((size_t)10000000u)

typedef RexxNumericStatus (*RexxBinaryOperation)(const RexxDecimal *,
                                                 const RexxDecimal *,
                                                 size_t,
                                                 size_t *,
                                                 RexxDecimal *);

typedef RexxNumericStatus (*RexxPowerOperation)(const RexxDecimal *,
                                                const RexxDecimal *,
                                                size_t,
                                                size_t *,
                                                RexxDecimal *);

static size_t assertions;
static size_t failures;

#define CHECK(condition, label) do { \
    ++assertions; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "[FAIL] %s (line %d)\n", label, __LINE__); \
    } \
} while (0)

static RexxNumericStatus parse(const char *text, RexxDecimal *value)
{
    return rexx_decimal_parse(text, strlen(text), value);
}

static int format_is(const RexxDecimal *value, size_t precision,
                     const char *expected, const char *label)
{
    char output[REXX_DECIMAL_MAX_DIGITS + 64u];
    int length = rexx_decimal_format(output, sizeof(output), value, precision);
    if (length < 0 || strcmp(output, expected) != 0) {
        fprintf(stderr, "[FAIL] %s: expected '%s', got '%s'\n", label,
                expected, length < 0 ? "<format error>" : output);
        return 0;
    }
    return 1;
}

static int check_binary(const char *left_text, const char *right_text,
                        size_t precision, RexxBinaryOperation operation,
                        const char *expected, const char *label)
{
    RexxDecimal left, right, result;
    size_t work = WORK_BUDGET;
    if (parse(left_text, &left) != REXX_NUMERIC_OK ||
        parse(right_text, &right) != REXX_NUMERIC_OK ||
        operation(&left, &right, precision, &work, &result) != REXX_NUMERIC_OK) {
        fprintf(stderr, "[FAIL] %s: operation failed\n", label);
        return 0;
    }
    return format_is(&result, precision, expected, label);
}

static int check_power(const char *base_text, const char *exponent_text,
                       size_t precision, const char *expected,
                       const char *label)
{
    RexxDecimal base, exponent, result;
    size_t work = WORK_BUDGET;
    if (parse(base_text, &base) != REXX_NUMERIC_OK ||
        parse(exponent_text, &exponent) != REXX_NUMERIC_OK ||
        rexx_decimal_power(&base, &exponent, precision, &work,
                           &result) != REXX_NUMERIC_OK) {
        fprintf(stderr, "[FAIL] %s: operation failed\n", label);
        return 0;
    }
    return format_is(&result, precision, expected, label);
}

static int compile_source(const char *source, RexxBytecodeChunk **chunk_out)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxCompilerError error;
    RexxCompilerStatus compiler_status;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&error, 0, sizeof(error));
    *chunk_out = NULL;
    if (rexx_lexer_init(&lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK ||
        rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return 0;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    return compiler_status == REXX_COMPILER_STATUS_OK;
}

typedef struct OutputCapture {
    char bytes[128];
    size_t length;
} OutputCapture;

static int capture_output(void *user_data, const char *bytes, size_t length)
{
    OutputCapture *capture = (OutputCapture *)user_data;
    if (capture == NULL || length > sizeof(capture->bytes) - capture->length)
        return 0;
    if (length != 0u) memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
}

static void test_decimal_arithmetic(void)
{
    RexxDecimal left, right, result;
    size_t work;
    char output[REXX_DECIMAL_MAX_DIGITS + 64u];
    int written;

    CHECK(check_binary("10", "2", 9u, rexx_decimal_divide,
                       "5", "exact division"), "10 / 2 formatting");
    CHECK(check_binary("7", "2", 9u, rexx_decimal_divide,
                       "3.5", "finite decimal division"), "7 / 2 formatting");
    CHECK(check_binary("2", "3", 9u, rexx_decimal_divide,
                       "0.666666667", "rounded repeating division"), "2 / 3 formatting");
    CHECK(check_binary("1", "3", 5u, rexx_decimal_divide,
                       "0.33333", "context precision division"), "1 / 3 at DIGITS 5");
    CHECK(check_binary("1", "7", 12u, rexx_decimal_divide,
                       "0.142857142857", "12 digit repeating division"), "1 / 7 at DIGITS 12");
    CHECK(check_binary("2.40", "2", 9u, rexx_decimal_divide,
                       "1.2", "division strips insignificant zeros"), "2.40 / 2 formatting");
    CHECK(check_binary("1", "160", 2u, rexx_decimal_divide,
                       "0.0063", "division half-up rounding"), "division rounding boundary");
    CHECK(check_binary("-1", "160", 2u, rexx_decimal_divide,
                       "-0.0063", "negative division half-up rounding"), "negative division rounding");
    CHECK(check_binary("999", "100", 2u, rexx_decimal_divide,
                       "10", "division rounding carry"), "division carry boundary");

    CHECK(check_binary("7", "2", 9u, rexx_decimal_integer_divide,
                       "3", "positive integer division"), "7 % 2 formatting");
    CHECK(check_binary("10", "3", 9u, rexx_decimal_integer_divide,
                       "3", "integer division truncates"), "10 % 3 formatting");
    CHECK(check_binary("9", "3", 9u, rexx_decimal_integer_divide,
                       "3", "exact integer division"), "9 % 3 formatting");
    CHECK(check_binary("-7", "2", 9u, rexx_decimal_integer_divide,
                       "-3", "negative integer division"), "-7 % 2 formatting");
    CHECK(check_binary("7", "-2", 9u, rexx_decimal_integer_divide,
                       "-3", "negative divisor integer division"), "7 % -2 formatting");
    CHECK(check_binary("7.5", "2", 9u, rexx_decimal_integer_divide,
                       "3", "fractional integer division"), "7.5 % 2 formatting");
    CHECK(check_binary("7", "2", 9u, rexx_decimal_remainder,
                       "1", "positive remainder"), "7 // 2 formatting");
    CHECK(check_binary("-7", "2", 9u, rexx_decimal_remainder,
                       "-1", "remainder follows dividend sign"), "-7 // 2 formatting");
    CHECK(check_binary("7", "-2", 9u, rexx_decimal_remainder,
                       "1", "remainder ignores divisor sign"), "7 // -2 formatting");
    CHECK(check_binary("-7", "-2", 9u, rexx_decimal_remainder,
                       "-1", "negative remainder with negative divisor"), "-7 // -2 formatting");
    CHECK(check_binary("7.5", "2", 9u, rexx_decimal_remainder,
                       "1.5", "fractional remainder"), "7.5 // 2 formatting");
    CHECK(check_binary("10", "0.3", 9u, rexx_decimal_remainder,
                       "0.1", "fractional divisor remainder"), "10 // 0.3 formatting");
    CHECK(check_binary("3.6", "1.3", 9u, rexx_decimal_remainder,
                       "1.0", "remainder retains aligned scale"), "3.6 // 1.3 formatting");

    CHECK(check_power("2", "3", 9u, "8", "positive power"), "2 ** 3 formatting");
    CHECK(check_power("2", "10", 9u, "1024", "bounded large power"), "2 ** 10 formatting");
    CHECK(check_power("10", "0", 9u, "1", "zero power"), "10 ** 0 formatting");
    CHECK(check_power("0", "0", 9u, "1", "zero to zero"), "0 ** 0 formatting");
    CHECK(check_power("2", "-2", 9u, "0.25", "negative power reciprocal"), "2 ** -2 formatting");
    CHECK(check_power("-2", "3", 9u, "-8", "negative base odd power"), "-2 ** 3 formatting");
    CHECK(check_power("-2", "4", 9u, "16", "negative base even power"), "(-2) ** 4 formatting");
    CHECK(check_power("1.7", "8", 5u, "69.758", "power intermediate precision"), "1.7 ** 8 at DIGITS 5");

    CHECK(parse("1", &left) == REXX_NUMERIC_OK &&
          parse("0", &right) == REXX_NUMERIC_OK &&
          rexx_decimal_divide(&left, &right, 9u, NULL, &result) ==
              REXX_NUMERIC_INVALID,
          "division by zero is rejected");
    CHECK(parse("10000000000", &left) == REXX_NUMERIC_OK &&
          parse("3", &right) == REXX_NUMERIC_OK &&
          rexx_decimal_integer_divide(&left, &right, 9u, NULL, &result) ==
              REXX_NUMERIC_RANGE,
          "integer quotient over DIGITS is rejected");
    CHECK(parse("1", &left) == REXX_NUMERIC_OK &&
          parse("0", &right) == REXX_NUMERIC_OK &&
          rexx_decimal_remainder(&left, &right, 9u, NULL, &result) ==
              REXX_NUMERIC_INVALID,
          "remainder by zero is rejected");
    CHECK(parse("2", &left) == REXX_NUMERIC_OK &&
          parse("1.5", &right) == REXX_NUMERIC_OK &&
          rexx_decimal_power(&left, &right, 9u, NULL, &result) ==
              REXX_NUMERIC_INVALID,
          "fractional power is rejected");
    CHECK(parse("0", &left) == REXX_NUMERIC_OK &&
          parse("-1", &right) == REXX_NUMERIC_OK &&
          rexx_decimal_power(&left, &right, 9u, NULL, &result) ==
              REXX_NUMERIC_INVALID,
          "zero to a negative power is rejected");
    CHECK(parse("2", &left) == REXX_NUMERIC_OK &&
          parse("1000001", &right) == REXX_NUMERIC_OK &&
          rexx_decimal_power(&left, &right, 9u, NULL, &result) ==
              REXX_NUMERIC_RANGE,
          "power exponent is explicitly bounded");
    CHECK(parse("1e1000000", &left) == REXX_NUMERIC_OK &&
          parse("0.1", &right) == REXX_NUMERIC_OK &&
          rexx_decimal_divide(&left, &right, 9u, NULL, &result) ==
              REXX_NUMERIC_RANGE,
          "division result scale overflow is detected");

    CHECK(parse("1", &left) == REXX_NUMERIC_OK &&
          parse("3", &right) == REXX_NUMERIC_OK,
          "resource-limit operands parse");
    work = 0u;
    CHECK(rexx_decimal_divide(&left, &right, 9u, &work, &result) ==
              REXX_NUMERIC_RESOURCE_LIMIT,
          "division consumes a bounded work budget");
    work = 12u;
    CHECK(rexx_decimal_divide(&left, &right, 9u, &work, &result) ==
              REXX_NUMERIC_RESOURCE_LIMIT && work == 0u,
          "division work budget stops during quotient digit generation");
    work = 0u;
    CHECK(rexx_decimal_power(&left, &right, 9u, &work, &result) ==
              REXX_NUMERIC_RESOURCE_LIMIT,
          "power consumes a bounded work budget");
    CHECK(parse("2", &left) == REXX_NUMERIC_OK &&
          parse("1000000", &right) == REXX_NUMERIC_OK,
          "large bounded exponent parses");
    work = 100000u;
    CHECK(rexx_decimal_power(&left, &right, 254u, &work, &result) ==
              REXX_NUMERIC_RESOURCE_LIMIT && work < 100000u,
          "power work budget stops repeated squaring safely");

    CHECK(parse("1", &left) == REXX_NUMERIC_OK &&
          parse("3", &right) == REXX_NUMERIC_OK,
          "maximum-precision operands parse");
    work = WORK_BUDGET;
    CHECK(rexx_decimal_divide(&left, &right, REXX_NUMERIC_MAX_DIGITS,
                              &work, &result) == REXX_NUMERIC_OK &&
          result.length == REXX_NUMERIC_MAX_DIGITS,
          "division supports maximum NUMERIC DIGITS precision");
    written = rexx_decimal_format(output, sizeof(output), &result,
                                  REXX_NUMERIC_MAX_DIGITS);
    CHECK(written == (int)(REXX_NUMERIC_MAX_DIGITS + 2u) &&
          output[0] == '0' && output[1] == '.' && output[written - 1] == '3',
          "maximum-precision division formats without truncation");

    {
        char input[REXX_DECIMAL_MAX_INPUT_BYTES + 1u];
        memset(input, '7', sizeof(input));
        CHECK(rexx_decimal_parse(input, REXX_DECIMAL_MAX_INPUT_BYTES,
                                 &result) == REXX_NUMERIC_OK &&
              result.length == REXX_DECIMAL_MAX_DIGITS,
              "maximum-size numeric input is accepted");
        CHECK(rexx_decimal_parse(input, sizeof(input), &result) ==
                  REXX_NUMERIC_RESOURCE_LIMIT,
              "numeric input over the fixed bound is rejected");
        CHECK(rexx_decimal_parse("12x", 3u, &result) == REXX_NUMERIC_INVALID,
              "invalid numeric text is rejected");
    }
}

static void test_vm_work_exhaustion_recovery(void)
{
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    OutputCapture capture;
    RexxVMStatus status;
    static const char source[] = "say 1 / 3\n";

    CHECK(compile_source(source, &chunk), "VM resource test compiles");
    if (chunk == NULL) return;
    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    status = rexx_vm_init(&vm, 0u);
    CHECK(status == REXX_VM_STATUS_OK, "VM resource test initializes");
    if (status == REXX_VM_STATUS_OK) {
        rexx_vm_set_output_writer(&vm, capture_output, &capture);
        vm.numeric_work_budget = 12u;
        status = rexx_vm_execute(&vm, chunk);
        CHECK(status == REXX_VM_STATUS_RESOURCE_LIMIT && vm.sp == 0u,
              "arithmetic work exhaustion clears the VM evaluation stack");
        vm.numeric_work_budget = REXX_VM_DEFAULT_NUMERIC_WORK_BUDGET;
        capture.length = 0u;
        status = rexx_vm_execute(&vm, chunk);
        CHECK(status == REXX_VM_STATUS_OK && vm.sp == 0u &&
              capture.length == strlen("0.333333333\n") &&
              memcmp(capture.bytes, "0.333333333\n", capture.length) == 0,
              "VM executes arithmetic normally after work exhaustion");
        rexx_vm_free(&vm);
    }
    rexx_bytecode_chunk_free(chunk);
}

int main(void)
{
    test_decimal_arithmetic();
    test_vm_work_exhaustion_recovery();
    if (failures != 0u) {
        fprintf(stderr, "RX4 arithmetic assertions failed: %lu/%lu\n",
                (unsigned long)failures, (unsigned long)assertions);
        return 1;
    }
    printf("RX4 arithmetic assertions passed: %lu\n",
           (unsigned long)assertions);
    return 0;
}
