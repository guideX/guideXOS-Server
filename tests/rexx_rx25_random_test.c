#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_numeric.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdint.h>
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

typedef struct ReferencePcg {
    uint64_t state;
} ReferencePcg;

/* Independent test oracle for PCG-XSH-RR 64/32. */
static uint32_t reference_word(ReferencePcg *rng)
{
    const uint64_t old = rng->state;
    const uint32_t x = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    const uint32_t r = (uint32_t)(old >> 59u);
    rng->state = old * UINT64_C(6364136223846793005) +
                 UINT64_C(1442695040888963407);
    return (x >> r) | (x << ((32u - r) & 31u));
}

static void reference_seed(ReferencePcg *rng, uint32_t seed)
{
    rng->state = 0u;
    (void)reference_word(rng);
    rng->state += (uint64_t)seed;
    (void)reference_word(rng);
}

static uint32_t reference_bounded(ReferencePcg *rng, uint32_t bound)
{
    const uint32_t threshold = (uint32_t)(
        (UINT64_C(1) << 32u) % (uint64_t)bound);
    for (;;) {
        const uint32_t word = reference_word(rng);
        if (word >= threshold) return word % bound;
    }
}

static RexxVMStatus compile_source(const char *source,
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
    if (rexx_lexer_init(&lexer, source, strlen(source)) !=
        REXX_LEXER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return parser_status == REXX_PARSER_STATUS_OUT_OF_MEMORY
                   ? REXX_VM_STATUS_OUT_OF_MEMORY
                   : parser_status == REXX_PARSER_STATUS_RESOURCE_LIMIT
                         ? REXX_VM_STATUS_RESOURCE_LIMIT
                         : REXX_VM_STATUS_DYNAMIC_SYNTAX;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    if (compiler_status == REXX_COMPILER_STATUS_OK)
        return REXX_VM_STATUS_OK;
    return compiler_status == REXX_COMPILER_STATUS_OUT_OF_MEMORY
               ? REXX_VM_STATUS_OUT_OF_MEMORY
               : compiler_status == REXX_COMPILER_STATUS_RESOURCE_LIMIT
                     ? REXX_VM_STATUS_RESOURCE_LIMIT
                     : REXX_VM_STATUS_INVALID_ARGUMENT;
}

static RexxVMStatus execute_source(RexxVM *vm, const char *source)
{
    RexxBytecodeChunk *chunk = NULL;
    RexxVMStatus status = compile_source(source, &chunk);
    if (status == REXX_VM_STATUS_OK) status = rexx_vm_execute(vm, chunk);
    rexx_bytecode_chunk_free(chunk);
    return status;
}

static RexxVMStatus call_random_values(RexxVM *vm,
                                      const char *const *values,
                                      size_t count,
                                      RexxValue *result)
{
    RexxValue arguments[3] = { REXX_VALUE_INIT,
                               REXX_VALUE_INIT,
                               REXX_VALUE_INIT };
    RexxVMStatus status = REXX_VM_STATUS_OK;
    size_t index;
    if (count > 3u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    for (index = 0u; index < count; ++index) {
        if (values[index] != NULL) {
            status = rexx_value_set_string(&arguments[index], values[index],
                                           strlen(values[index]));
            if (status != REXX_VM_STATUS_OK) break;
        }
    }
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_dispatch_with_vm(
            vm, "RANDOM", 6u, count == 0u ? NULL : arguments, count, result);
    for (index = 0u; index < 3u; ++index)
        rexx_value_clear(&arguments[index]);
    return status;
}

static int parse_decimal_result(const RexxValue *value, RexxDecimal *number)
{
    return value != NULL && value->type == REXX_VALUE_STRING &&
           rexx_decimal_parse(value->as.string.bytes, value->as.string.length,
                              number) == REXX_NUMERIC_OK;
}

static int result_equals_text(const RexxValue *value, const char *expected)
{
    RexxDecimal actual, wanted;
    return parse_decimal_result(value, &actual) &&
           rexx_decimal_parse(expected, strlen(expected), &wanted) ==
               REXX_NUMERIC_OK &&
           rexx_decimal_compare(&actual, &wanted) == 0;
}

static int result_in_range(const RexxValue *value, const char *minimum,
                           const char *maximum)
{
    RexxDecimal actual, lower, upper;
    return parse_decimal_result(value, &actual) &&
           rexx_decimal_parse(minimum, strlen(minimum), &lower) ==
               REXX_NUMERIC_OK &&
           rexx_decimal_parse(maximum, strlen(maximum), &upper) ==
               REXX_NUMERIC_OK &&
           actual.sign >= 0 && rexx_decimal_compare(&actual, &lower) >= 0 &&
           rexx_decimal_compare(&actual, &upper) <= 0;
}

static int exit_value_equals(const RexxVM *vm, const char *expected)
{
    return vm->exit_has_value && result_equals_text(&vm->exit_value, expected);
}

static void check_random_range(RexxVM *vm, const char *const *values,
                               size_t count, const char *minimum,
                               const char *maximum, const char *label)
{
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status = call_random_values(vm, values, count, &result);
    CHECK(status == REXX_VM_STATUS_OK, label);
    CHECK(status == REXX_VM_STATUS_OK &&
          result_in_range(&result, minimum, maximum),
          "RANDOM result is an exact whole number within the inclusive bounds");
    rexx_value_clear(&result);
}

static void check_error(RexxVM *vm, const char *const *values, size_t count,
                        RexxVMStatus expected, const char *label)
{
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status = call_random_values(vm, values, count, &result);
    CHECK(status == expected, label);
    CHECK(result.type == REXX_VALUE_UNSET,
          "failed RANDOM call exposes no partial result");
    rexx_value_clear(&result);
}

static int initialize_vm(RexxVM *vm)
{
    RexxVMStatus status = rexx_vm_init(vm, 0u);
    CHECK(status == REXX_VM_STATUS_OK, "VM initializes");
    return status == REXX_VM_STATUS_OK;
}

static int run_positional_source(void)
{
    static const char source[] =
        "a = RANDOM()\n"
        "b = RANDOM(10)\n"
        "c = RANDOM(10,)\n"
        "d = RANDOM(,10)\n"
        "e = RANDOM(5,8)\n"
        "f = RANDOM(,,12345)\n"
        "g = RANDOM(1,6,12345)\n"
        "IF a < 0 THEN EXIT 11\n"
        "IF a > 999 THEN EXIT 12\n"
        "IF b < 0 THEN EXIT 13\n"
        "IF b > 10 THEN EXIT 14\n"
        "IF c < 10 THEN EXIT 15\n"
        "IF c > 999 THEN EXIT 16\n"
        "IF d < 0 THEN EXIT 17\n"
        "IF d > 10 THEN EXIT 18\n"
        "IF e < 5 THEN EXIT 19\n"
        "IF e > 8 THEN EXIT 20\n"
        "IF f < 0 THEN EXIT 21\n"
        "IF f > 999 THEN EXIT 22\n"
        "IF g < 1 THEN EXIT 23\n"
        "IF g > 6 THEN EXIT 24\n"
        "EXIT 0\n";
    RexxVM vm;
    RexxVMStatus status;
    if (!initialize_vm(&vm)) return 0;
    status = execute_source(&vm, source);
    CHECK(status == REXX_VM_STATUS_OK,
          "REXX parser preserves omitted RANDOM positional arguments");
    CHECK(exit_value_equals(&vm, "0"),
          "RANDOM() and all positional omission forms satisfy their bounds");
    rexx_vm_free(&vm);
    return status == REXX_VM_STATUS_OK;
}

static int run_large_digits_source(void)
{
    static const char source[] =
        "NUMERIC DIGITS 1\n"
        "EXIT RANDOM('18446744073709551616',"
        "'18446744073709651616',12345)\n";
    RexxVM vm;
    RexxVMStatus status;
    int in_range;
    if (!initialize_vm(&vm)) return 0;
    status = execute_source(&vm, source);
    in_range = status == REXX_VM_STATUS_OK && vm.exit_has_value &&
        result_in_range(&vm.exit_value, "18446744073709551616",
                        "18446744073709651616");
    CHECK(status == REXX_VM_STATUS_OK && in_range,
          "RANDOM preserves large decimal bounds with NUMERIC DIGITS 1");
    rexx_vm_free(&vm);
    return status == REXX_VM_STATUS_OK && in_range;
}

static int run_routine_state_source(void)
{
    static const char source[] =
        "first = RANDOM(0,100000,12345)\n"
        "CALL helper\n"
        "after = RANDOM(0,100000)\n"
        "EXIT after\n"
        "helper:\n"
        "inside = RANDOM(0,100000)\n"
        "RETURN\n";
    RexxVM vm;
    RexxVMStatus status;
    ReferencePcg expected;
    uint32_t first, second, third;
    char third_text[24];
    if (!initialize_vm(&vm)) return 0;
    reference_seed(&expected, 12345u);
    first = reference_bounded(&expected, 100001u);
    second = reference_bounded(&expected, 100001u);
    third = reference_bounded(&expected, 100001u);
    (void)first;
    (void)second;
    (void)snprintf(third_text, sizeof(third_text), "%u", third);
    status = execute_source(&vm, source);
    CHECK(status == REXX_VM_STATUS_OK,
          "RANDOM runs before, during, and after an internal routine");
    CHECK(status == REXX_VM_STATUS_OK && exit_value_equals(&vm, third_text),
          "internal routine return does not roll back program RANDOM state");
    rexx_vm_free(&vm);
    return status == REXX_VM_STATUS_OK;
}

static int run_unwind_state_source(void)
{
    static const char source[] =
        "SIGNAL ON SYNTAX NAME caught\n"
        "first = RANDOM(0,100000,12345)\n"
        "CALL helper\n"
        "EXIT 91\n"
        "helper:\n"
        "inside = RANDOM(0,100000)\n"
        "bad = 1 / 0\n"
        "RETURN\n"
        "caught:\n"
        "after = RANDOM(0,100000)\n"
        "EXIT after\n";
    RexxVM vm;
    RexxVMStatus status;
    ReferencePcg expected;
    uint32_t first, second, third;
    char third_text[24];
    if (!initialize_vm(&vm)) return 0;
    reference_seed(&expected, 12345u);
    first = reference_bounded(&expected, 100001u);
    second = reference_bounded(&expected, 100001u);
    third = reference_bounded(&expected, 100001u);
    (void)first;
    (void)second;
    (void)snprintf(third_text, sizeof(third_text), "%u", third);
    status = execute_source(&vm, source);
    CHECK(status == REXX_VM_STATUS_OK,
          "syntax trap recovers from an error inside a routine");
    CHECK(status == REXX_VM_STATUS_OK && exit_value_equals(&vm, third_text),
          "routine unwind retains the state consumed before the error");
    rexx_vm_free(&vm);
    return status == REXX_VM_STATUS_OK;
}

static int run_repeatability_and_isolation(void)
{
    RexxVM left, right;
    ReferencePcg expected;
    const char *seeded[] = { "1", "6", "12345" };
    const char *ordinary[] = { "1", "6" };
    RexxVMStatus left_status, right_status;
    RexxValue left_result = REXX_VALUE_INIT;
    RexxValue right_result = REXX_VALUE_INIT;
    size_t index;
    int ok = 1;
    if (!initialize_vm(&left)) return 0;
    if (!initialize_vm(&right)) { rexx_vm_free(&left); return 0; }

    reference_seed(&expected, 12345u);
    left_status = call_random_values(&left, seeded, 3u, &left_result);
    right_status = call_random_values(&right, seeded, 3u, &right_result);
    CHECK(left_status == REXX_VM_STATUS_OK &&
          right_status == REXX_VM_STATUS_OK,
          "explicit seed initializes independent VM streams");
    {
        uint32_t wanted = reference_bounded(&expected, 6u) + 1u;
        char wanted_text[8];
        (void)snprintf(wanted_text, sizeof(wanted_text), "%u", wanted);
        CHECK(left_status == REXX_VM_STATUS_OK &&
              right_status == REXX_VM_STATUS_OK &&
              result_equals_text(&left_result, wanted_text) &&
              result_equals_text(&right_result, wanted_text),
              "first seeded values match the independent PCG reference");
    }
    CHECK(left_status == REXX_VM_STATUS_OK &&
          result_in_range(&left_result, "1", "6") &&
          result_in_range(&right_result, "1", "6"),
          "seeded results stay inside 1..6");
    rexx_value_clear(&left_result);
    rexx_value_clear(&right_result);

    for (index = 0u; index < 1000u; ++index) {
        uint32_t wanted = reference_bounded(&expected, 6u) + 1u;
        char wanted_text[8];
        (void)snprintf(wanted_text, sizeof(wanted_text), "%u", wanted);
        left_status = call_random_values(&left, ordinary, 2u, &left_result);
        right_status = call_random_values(&right, ordinary, 2u, &right_result);
        CHECK(left_status == REXX_VM_STATUS_OK &&
              right_status == REXX_VM_STATUS_OK,
              "matching VMs complete every seeded sequence call");
        CHECK(left_status == REXX_VM_STATUS_OK &&
              right_status == REXX_VM_STATUS_OK &&
              result_equals_text(&left_result, wanted_text) &&
              result_equals_text(&right_result, wanted_text),
              "1,000-call sequences match the independent reference exactly");
        rexx_value_clear(&left_result);
        rexx_value_clear(&right_result);
    }

    left_status = call_random_values(&left, seeded, 3u, &left_result);
    reference_seed(&expected, 12345u);
    {
        uint32_t wanted = reference_bounded(&expected, 6u) + 1u;
        char wanted_text[8];
        (void)snprintf(wanted_text, sizeof(wanted_text), "%u", wanted);
        CHECK(left_status == REXX_VM_STATUS_OK &&
              result_equals_text(&left_result, wanted_text),
              "re-seeding an active VM restarts its sequence");
    }
    rexx_value_clear(&left_result);
    rexx_value_clear(&right_result);
    rexx_vm_free(&left);
    rexx_vm_free(&right);
    return ok;
}

static int run_distinct_seed_check(void)
{
    RexxVM left, right;
    const char *seed_one[] = { "0", "100000", "1" };
    const char *seed_two[] = { "0", "100000", "2" };
    const char *ordinary[] = { "0", "100000" };
    RexxValue a = REXX_VALUE_INIT, b = REXX_VALUE_INIT;
    size_t index;
    int sequences_identical = 1;
    if (!initialize_vm(&left)) return 0;
    if (!initialize_vm(&right)) { rexx_vm_free(&left); return 0; }
    for (index = 0u; index < 64u; ++index) {
        RexxVMStatus sa = index == 0u
            ? call_random_values(&left, seed_one, 3u, &a)
            : call_random_values(&left, ordinary, 2u, &a);
        RexxVMStatus sb = index == 0u
            ? call_random_values(&right, seed_two, 3u, &b)
            : call_random_values(&right, ordinary, 2u, &b);
        CHECK(sa == REXX_VM_STATUS_OK && sb == REXX_VM_STATUS_OK,
              "distinct-seed sample calls succeed");
        if (sa != REXX_VM_STATUS_OK || sb != REXX_VM_STATUS_OK ||
            !result_equals_text(&a, b.as.string.bytes))
            sequences_identical = 0;
        rexx_value_clear(&a);
        rexx_value_clear(&b);
    }
    CHECK(!sequences_identical,
          "different valid seeds produce distinct sampled sequences");
    rexx_vm_free(&left);
    rexx_vm_free(&right);
    return !sequences_identical;
}

static int run_error_recovery(void)
{
    RexxVM vm;
    ReferencePcg expected;
    const char *seeded[] = { "0", "100000", "12345" };
    const char *ordinary[] = { "0", "100000" };
    const char *fractional[] = { "1.5", "6" };
    const char *negative[] = { "-1", "6" };
    const char *malformed[] = { "1x", "6" };
    const char *empty[] = { "" };
    const char *too_wide[] = { "1", "100002" };
    const char *reversed[] = { "8", "5" };
    const char *fraction_seed[] = { "0", "10", "1.5" };
    const char *negative_seed[] = { "0", "10", "-1" };
    const char *too_large_seed[] = { "0", "10", "1000000000" };
    const char *valid_max_seed[] = { "0", "10", "999999999" };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    uint32_t first, second;
    char second_text[24];

    if (!initialize_vm(&vm)) return 0;
    status = call_random_values(&vm, valid_max_seed, 3u, &result);
    CHECK(status == REXX_VM_STATUS_OK &&
          result_in_range(&result, "0", "10"),
          "maximum documented seed 999999999 is accepted");
    rexx_value_clear(&result);

    reference_seed(&expected, 12345u);
    first = reference_bounded(&expected, 100001u);
    second = reference_bounded(&expected, 100001u);
    (void)first;
    (void)snprintf(second_text, sizeof(second_text), "%u", second);
    status = call_random_values(&vm, seeded, 3u, &result);
    CHECK(status == REXX_VM_STATUS_OK, "recovery sequence seeds successfully");
    rexx_value_clear(&result);

    check_error(&vm, fractional, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "fractional range endpoint is rejected");
    check_error(&vm, negative, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "negative range endpoint is rejected");
    check_error(&vm, malformed, 2u, REXX_VM_STATUS_NUMERIC_ERROR,
                "malformed decimal endpoint is rejected");
    check_error(&vm, empty, 1u, REXX_VM_STATUS_NUMERIC_ERROR,
                "explicit empty string is not treated as an omitted argument");
    check_error(&vm, too_wide, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "range difference 100001 is rejected");
    check_error(&vm, reversed, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "maximum below minimum is rejected");
    check_error(&vm, fraction_seed, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "fractional seed is rejected");
    check_error(&vm, negative_seed, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "negative seed is rejected");
    check_error(&vm, too_large_seed, 3u,
                REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "seed 1000000000 is rejected");
    {
        RexxValue extra_arguments[4] = { REXX_VALUE_INIT,
                                         REXX_VALUE_INIT,
                                         REXX_VALUE_INIT,
                                         REXX_VALUE_INIT };
        const char *extra_values[] = { "0", "1", "2", "3" };
        size_t index;
        for (index = 0u; index < 4u; ++index) {
            CHECK(rexx_value_set_string(&extra_arguments[index],
                                        extra_values[index], 1u) ==
                      REXX_VM_STATUS_OK,
                  "four-argument dispatcher test input is prepared");
        }
        status = rexx_builtin_dispatch_with_vm(
            &vm, "RANDOM", 6u, extra_arguments, 4u, &result);
        CHECK(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
              "the built-in dispatcher rejects a fourth positional argument");
        CHECK(result.type == REXX_VALUE_UNSET,
              "extra-argument rejection produces no result");
        for (index = 0u; index < 4u; ++index)
            rexx_value_clear(&extra_arguments[index]);
        rexx_value_clear(&result);
    }

    status = call_random_values(&vm, ordinary, 2u, &result);
    CHECK(status == REXX_VM_STATUS_OK &&
          result_equals_text(&result, second_text),
          "rejected calls leave the generator at its prior sequence position");
    rexx_value_clear(&result);

    {
        ReferencePcg numeric_expected;
        RexxValue numeric_arguments[2] = { REXX_VALUE_INIT,
                                           REXX_VALUE_INIT };
        const char *large_seed[] = { "0", "100000", "7" };
        char expected_after_numeric_error[24];
        status = call_random_values(&vm, large_seed, 3u, &result);
        CHECK(status == REXX_VM_STATUS_OK, "numeric adapter test reseeds");
        rexx_value_clear(&result);
        reference_seed(&numeric_expected, 7u);
        (void)reference_bounded(&numeric_expected, 100001u);
        (void)snprintf(expected_after_numeric_error,
                       sizeof(expected_after_numeric_error), "%u",
                       reference_bounded(&numeric_expected, 100001u));
        CHECK(rexx_value_set_number(&numeric_arguments[0], 0.0) ==
                  REXX_VM_STATUS_OK &&
              rexx_value_set_number(&numeric_arguments[1], 10.0) ==
                  REXX_VM_STATUS_OK,
              "legacy number argument values are prepared");
        status = rexx_builtin_dispatch_with_vm(
            &vm, "RANDOM", 6u, numeric_arguments, 2u, &result);
        CHECK(status == REXX_VM_STATUS_NUMERIC_ERROR,
              "RANDOM does not narrow decimal arguments through binary floating point");
        CHECK(result.type == REXX_VALUE_UNSET,
              "unsupported binary number arguments return no result");
        status = call_random_values(&vm, ordinary, 2u, &result);
        CHECK(status == REXX_VM_STATUS_OK &&
              result_equals_text(&result, expected_after_numeric_error),
              "unsupported binary values leave PRNG state unchanged");
        rexx_value_clear(&numeric_arguments[0]);
        rexx_value_clear(&numeric_arguments[1]);
        rexx_value_clear(&result);
    }

    rexx_vm_free(&vm);
    return 1;
}

static int run_allocation_atomicity(void)
{
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    RexxVM vm;
    ReferencePcg expected;
    const char *initial[] = { "0", "100000", "12345" };
    const char *ordinary[] = { "0", "100000" };
    RexxValue arguments[3] = { REXX_VALUE_INIT,
                               REXX_VALUE_INIT,
                               REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    uint32_t first, second;
    char second_text[24];
    if (!initialize_vm(&vm)) return 0;
    reference_seed(&expected, 12345u);
    first = reference_bounded(&expected, 100001u);
    second = reference_bounded(&expected, 100001u);
    (void)first;
    (void)snprintf(second_text, sizeof(second_text), "%u", second);
    status = call_random_values(&vm, initial, 3u, &result);
    CHECK(status == REXX_VM_STATUS_OK, "allocation test seed call succeeds");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_string(&arguments[0], "0", 1u) ==
              REXX_VM_STATUS_OK &&
          rexx_value_set_string(&arguments[1], "100000", 6u) ==
              REXX_VM_STATUS_OK &&
          rexx_value_set_string(&arguments[2], "999999999", 9u) ==
              REXX_VM_STATUS_OK,
          "allocation test reseed arguments are prepared before failpoint");
    rexx_vm_test_allocator_fail_on(1u);
    status = rexx_builtin_dispatch_with_vm(
        &vm, "RANDOM", 6u, arguments, 3u, &result);
    rexx_vm_test_allocator_disable();
    CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY,
          "result allocation failure rejects a tentative reseed");
    CHECK(result.type == REXX_VALUE_UNSET,
          "allocation failure exposes no partially initialized result");
    rexx_value_clear(&result);
    status = call_random_values(&vm, ordinary, 2u, &result);
    CHECK(status == REXX_VM_STATUS_OK &&
          result_equals_text(&result, second_text),
          "failed reseeding does not change or advance the active sequence");
    rexx_value_clear(&result);
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    rexx_vm_free(&vm);
    return 1;
#else
    CHECK(0, "allocation failure qualification build enables test allocator");
    return 0;
#endif
}

static int run_execution_reset(void)
{
    RexxVM vm;
    RexxVMStatus status;
    ReferencePcg expected;
    uint32_t wanted;
    char wanted_text[24];
    if (!initialize_vm(&vm)) return 0;
    status = execute_source(&vm, "EXIT RANDOM(0,100000,12345)\n");
    CHECK(status == REXX_VM_STATUS_OK,
          "first program execution completes with an explicit seed");
    reference_seed(&expected, 12345u);
    wanted = reference_bounded(&expected, 100001u);
    (void)snprintf(wanted_text, sizeof(wanted_text), "%u", wanted);
    CHECK(status == REXX_VM_STATUS_OK && exit_value_equals(&vm, wanted_text),
          "first program result matches explicit sequence");

    status = execute_source(&vm, "EXIT RANDOM(0,100000)\n");
    CHECK(status == REXX_VM_STATUS_OK,
          "reused VM accepts a new program execution context");
    reference_seed(&expected, 0u);
    wanted = reference_bounded(&expected, 100001u);
    (void)snprintf(wanted_text, sizeof(wanted_text), "%u", wanted);
    CHECK(status == REXX_VM_STATUS_OK && exit_value_equals(&vm, wanted_text),
          "new program execution resets to the documented fallback sequence");
    rexx_vm_free(&vm);
    return status == REXX_VM_STATUS_OK;
}

static int run_distribution_sanity(void)
{
    enum { sample_count = 60000, categories = 6 };
    size_t counts[categories] = { 0u };
    RexxVM vm;
    RexxValue result = REXX_VALUE_INIT;
    ReferencePcg unused_reference;
    const char *seeded[] = { "0", "5", "246813579" };
    const char *ordinary[] = { "0", "5" };
    size_t index;
    int all_within_tolerance = 1;
    char summary[256];
    if (!initialize_vm(&vm)) return 0;
    (void)unused_reference;
    for (index = 0u; index < sample_count; ++index) {
        RexxVMStatus status = index == 0u
            ? call_random_values(&vm, seeded, 3u, &result)
            : call_random_values(&vm, ordinary, 2u, &result);
        RexxDecimal number;
        uint32_t converted;
        if (status != REXX_VM_STATUS_OK ||
            !parse_decimal_result(&result, &number) ||
            rexx_decimal_to_uint32(&number, &converted) != REXX_NUMERIC_OK ||
            converted >= categories) {
            CHECK(0, "statistical sample remains an integer in 0..5");
            rexx_value_clear(&result);
            rexx_vm_free(&vm);
            return 0;
        }
        ++counts[converted];
        rexx_value_clear(&result);
    }
    for (index = 0u; index < categories; ++index) {
        if (counts[index] < 9500u || counts[index] > 10500u)
            all_within_tolerance = 0;
    }
    (void)snprintf(summary, sizeof(summary),
        "fixed-seed 60,000-call counts 0..5 = %lu,%lu,%lu,%lu,%lu,%lu",
        (unsigned long)counts[0], (unsigned long)counts[1],
        (unsigned long)counts[2], (unsigned long)counts[3],
        (unsigned long)counts[4], (unsigned long)counts[5]);
    CHECK(all_within_tolerance,
          "each small-range frequency is within 5% of the 10,000 expectation");
    printf("[RX25-DISTRIBUTION] %s; tolerance=9500..10500 per category\n",
           summary);
    rexx_vm_free(&vm);
    return all_within_tolerance;
}

int main(void)
{
    RexxVM vm;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    const char *zero[] = { "0" };
    const char *one[] = { "1" };
    const char *same[] = { "999", NULL };
    const char *two_bounds[] = { "5", "8" };
    const char *boundary100000[] = { "0", "100000" };
    const char *boundary100001[] = { "1", "100001" };
    const char *boundary101000[] = { "1000", "101000" };
    const char *large_bounds[] = { "18446744073709551616",
                                   "18446744073709651616", "12345" };
    const char *equal_zero[] = { "0" };
    const char *bad_seed_over_u32[] = { "0", "10",
                                        "999999999999999999999" };
    const char *seed_zero[] = { "0", "1", "0" };
    const char *seed_one[] = { "0", "1", "1" };
    const char *seed_max[] = { "0", "1", "999999999" };
    ReferencePcg expected;
    uint32_t reference;
    int passed = 1;

    if (!initialize_vm(&vm)) return 1;

    check_random_range(&vm, NULL, 0u, "0", "999", "RANDOM() accepts defaults");
    check_random_range(&vm, zero, 1u, "0", "0", "RANDOM(0) maps a lone zero to maximum");
    check_random_range(&vm, one, 1u, "0", "1", "RANDOM(1) maps a lone argument to maximum");
    check_random_range(&vm, same, 2u, "999", "999", "RANDOM(999,) uses min,max positions");
    check_random_range(&vm, two_bounds, 2u, "5", "8", "RANDOM(5,8) uses inclusive bounds");
    check_random_range(&vm, boundary100000, 2u, "0", "100000",
                       "RANDOM accepts a 100000 difference");
    check_random_range(&vm, boundary100001, 2u, "1", "100001",
                       "RANDOM accepts 1..100001 at the maximum difference");
    check_random_range(&vm, boundary101000, 2u, "1000", "101000",
                       "RANDOM accepts a nonzero 100000 difference");
    check_random_range(&vm, large_bounds, 3u,
                       "18446744073709551616", "18446744073709651616",
                       "RANDOM handles decimal endpoints beyond uint64_t");

    status = call_random_values(&vm, equal_zero, 1u, &result);
    CHECK(status == REXX_VM_STATUS_OK && result_equals_text(&result, "0"),
          "equal minimum and maximum always return that value");
    rexx_value_clear(&result);

    check_error(&vm, bad_seed_over_u32, 3u,
                REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                "seed above uint32 and IBM range is rejected");
    status = call_random_values(&vm, seed_zero, 3u, &result);
    reference_seed(&expected, 0u);
    reference = reference_bounded(&expected, 2u);
    CHECK(status == REXX_VM_STATUS_OK &&
          result_equals_text(&result, reference == 0u ? "0" : "1"),
          "seed zero has a defined sequence");
    rexx_value_clear(&result);
    status = call_random_values(&vm, seed_one, 3u, &result);
    reference_seed(&expected, 1u);
    reference = reference_bounded(&expected, 2u);
    CHECK(status == REXX_VM_STATUS_OK &&
          result_equals_text(&result, reference == 0u ? "0" : "1"),
          "seed one has a defined sequence");
    rexx_value_clear(&result);
    check_random_range(&vm, seed_max, 3u, "0", "1",
                       "seed 999999999 is accepted");
    rexx_value_clear(&result);

    rexx_vm_free(&vm);
    passed &= run_positional_source();
    passed &= run_large_digits_source();
    passed &= run_repeatability_and_isolation();
    passed &= run_distinct_seed_check();
    passed &= run_error_recovery();
    passed &= run_allocation_atomicity();
    passed &= run_routine_state_source();
    passed &= run_unwind_state_source();
    passed &= run_execution_reset();
    passed &= run_distribution_sanity();

    printf("[RX25-RANDOM] assertions=%lu failures=%lu\n",
           (unsigned long)assertions, (unsigned long)failures);
    return failures == 0u && passed ? 0 : 1;
}
