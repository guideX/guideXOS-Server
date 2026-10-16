#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_numeric.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
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

typedef struct OutputCapture {
    char bytes[2048];
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
    RexxCompilerStatus status;

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
    status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    return status == REXX_COMPILER_STATUS_OK;
}

static int format_is(const char *text, size_t precision,
                     RexxDecimalFormat form, const char *expected)
{
    RexxDecimal value;
    char output[128];
    int written;
    if (rexx_decimal_parse(text, strlen(text), &value) != REXX_NUMERIC_OK)
        return 0;
    written = rexx_decimal_format_with_form(output, sizeof(output), &value,
                                            precision, form);
    return written >= 0 && (size_t)written == strlen(expected) &&
           memcmp(output, expected, (size_t)written) == 0;
}

static void test_formatter_forms(void)
{
    RexxDecimal value;
    char output[128];
    int written;

    CHECK(format_is("12345678", 5u, REXX_DECIMAL_FORMAT_SCIENTIFIC,
                    "1.2345678E+7"),
          "scientific formatter keeps the one-digit mantissa");
    CHECK(format_is("12345678", 5u, REXX_DECIMAL_FORMAT_ENGINEERING,
                    "12.345678E+6"),
          "engineering formatter uses a multiple-of-three exponent");
    CHECK(format_is("-0.000000123456", 5u,
                    REXX_DECIMAL_FORMAT_ENGINEERING, "-123.456E-9"),
          "engineering formatter handles negative small exponents");
    CHECK(format_is("1e6", 9u, REXX_DECIMAL_FORMAT_ENGINEERING, "1000000"),
          "FORM does not change the exponential-output threshold");

    CHECK(rexx_decimal_parse("999.5", 5u, &value) == REXX_NUMERIC_OK &&
              rexx_decimal_round(&value, 3u) == REXX_NUMERIC_OK,
          "engineering boundary rounds before formatting");
    written = rexx_decimal_format_with_form(
        output, sizeof(output), &value, 3u, REXX_DECIMAL_FORMAT_ENGINEERING);
    CHECK(written == 7 && strcmp(output, "1.00E+3") == 0,
          "rounded carry advances the engineering exponent");
    CHECK(rexx_decimal_format(output, sizeof(output), &value, 3u) == 7 &&
              strcmp(output, "1.00E+3") == 0,
          "legacy formatter entry point remains scientific");
}

static void test_vm_defaults_and_isolation(void)
{
    RexxVM first, second;
    RexxBytecodeChunk *chunk = NULL;
    RexxVMStatus status;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    CHECK(rexx_vm_init(&first, 0u) == REXX_VM_STATUS_OK,
          "first VM initializes");
    CHECK(rexx_vm_init(&second, 0u) == REXX_VM_STATUS_OK,
          "second VM initializes");
    CHECK(first.numeric_digits == 9u && first.numeric_fuzz == 0u &&
              first.numeric_form == REXX_NUMERIC_FORM_SCIENTIFIC,
          "new VM starts with the classic numeric defaults");
    CHECK(compile_source("numeric digits 12\nnumeric fuzz 2\n"
                         "numeric form engineering\n", &chunk),
          "context-changing source compiles");
    if (chunk != NULL) {
        status = rexx_vm_execute(&first, chunk);
        CHECK(status == REXX_VM_STATUS_OK && first.numeric_digits == 12u &&
                  first.numeric_fuzz == 2u &&
                  first.numeric_form == REXX_NUMERIC_FORM_ENGINEERING,
              "first VM accepts all three context changes");
        CHECK(second.numeric_digits == 9u && second.numeric_fuzz == 0u &&
                  second.numeric_form == REXX_NUMERIC_FORM_SCIENTIFIC,
              "numeric settings do not leak between VM instances");
        rexx_bytecode_chunk_free(chunk);
    }
    rexx_vm_free(&first);
    rexx_vm_free(&second);
}

static void test_invalid_changes_are_atomic(void)
{
    struct Case {
        const char *source;
        RexxVMStatus expected_status;
        uint32_t digits;
        uint32_t fuzz;
        RexxNumericForm form;
        const char *label;
    } cases[] = {
        { "numeric digits 5\nnumeric fuzz 2\nnumeric fuzz 5\n",
          REXX_VM_STATUS_INVALID_NUMERIC_FUZZ, 5u, 2u,
          REXX_NUMERIC_FORM_SCIENTIFIC, "invalid FUZZ leaves prior settings" },
        { "numeric digits 10\nnumeric fuzz 9\nnumeric digits\n",
          REXX_VM_STATUS_INVALID_NUMERIC_SETTING, 10u, 9u,
          REXX_NUMERIC_FORM_SCIENTIFIC,
          "DIGITS reset validates against active FUZZ" },
        { "numeric form engineering\nselected = \"INVALID\"\n"
          "numeric form value selected\n",
          REXX_VM_STATUS_INVALID_NUMERIC_FORM, 9u, 0u,
          REXX_NUMERIC_FORM_ENGINEERING,
          "invalid FORM leaves the previous form active" }
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RexxVM vm;
        RexxBytecodeChunk *chunk = NULL;
        RexxVMStatus status;
        memset(&vm, 0, sizeof(vm));
        CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
              "atomicity VM initializes");
        CHECK(compile_source(cases[index].source, &chunk),
              "atomicity source compiles");
        if (chunk != NULL) {
            status = rexx_vm_execute(&vm, chunk);
            CHECK(status == cases[index].expected_status &&
                      vm.numeric_digits == cases[index].digits &&
                      vm.numeric_fuzz == cases[index].fuzz &&
                      vm.numeric_form == cases[index].form,
                  cases[index].label);
            rexx_bytecode_chunk_free(chunk);
        }
        rexx_vm_free(&vm);
    }
}

static void test_builtins_and_context_survive_failure(void)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = NULL;
    OutputCapture capture;
    RexxVMStatus status;
    const char *source =
        "say digits()\nsay fuzz()\nsay form()\n"
        "numeric digits 14\nnumeric fuzz 3\nnumeric form engineering\n"
        "say digits()\nsay fuzz()\nsay form()\n";

    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "built-in VM initializes");
    rexx_vm_set_output_writer(&vm, capture_output, &capture);
    CHECK(compile_source(source, &chunk), "built-in source compiles");
    if (chunk != NULL) {
        status = rexx_vm_execute(&vm, chunk);
        CHECK(status == REXX_VM_STATUS_OK &&
                  capture_equals(&capture,
                      "9\n0\nSCIENTIFIC\n14\n3\nENGINEERING\n"),
              "DIGITS(), FUZZ(), and FORM() reflect active settings");
        rexx_bytecode_chunk_free(chunk);
    }
    rexx_vm_free(&vm);
}

static void test_builtin_argument_counts(void)
{
    const char *sources[] = { "say fuzz(1)\n", "say form(1)\n" };
    size_t index;
    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        RexxVM vm;
        RexxBytecodeChunk *chunk = NULL;
        memset(&vm, 0, sizeof(vm));
        CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
              "argument-count VM initializes");
        CHECK(compile_source(sources[index], &chunk),
              "argument-count source compiles");
        if (chunk != NULL) {
            CHECK(rexx_vm_execute(&vm, chunk) ==
                      REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "zero-argument numeric built-ins reject extra arguments");
            rexx_bytecode_chunk_free(chunk);
        }
        rexx_vm_free(&vm);
    }
}

static void test_instruction_budget_still_applies(void)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = NULL;
    memset(&vm, 0, sizeof(vm));
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "budget VM initializes");
    CHECK(rexx_vm_set_instruction_budget(&vm, 1u) == REXX_VM_STATUS_OK,
          "small instruction budget is accepted");
    CHECK(compile_source("numeric fuzz 1\nsay fuzz()\n", &chunk),
          "budget source compiles");
    if (chunk != NULL) {
        CHECK(rexx_vm_execute(&vm, chunk) ==
                  REXX_VM_STATUS_INSTRUCTION_LIMIT,
              "numeric setting instructions remain budgeted");
        rexx_bytecode_chunk_free(chunk);
    }
    rexx_vm_free(&vm);
}

int main(void)
{
    test_formatter_forms();
    test_vm_defaults_and_isolation();
    test_invalid_changes_are_atomic();
    test_builtins_and_context_survive_failure();
    test_builtin_argument_counts();
    test_instruction_budget_still_applies();
    printf("RX5 numeric-context assertions passed: %lu/%lu\n",
           (unsigned long)(assertions - failures), (unsigned long)assertions);
    return failures == 0u ? 0 : 1;
}
