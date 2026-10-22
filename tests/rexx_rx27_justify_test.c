#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RX27_PROPERTY_CASES 12000u
#define RX27_TEST_BUFFER 1024u
#define RX27_MAX_STRING ((size_t)16777216u)
#define RX27_PROPERTY_SEED UINT32_C(0x52583237)

static size_t assertions;
static size_t failures;
static size_t property_cases;
static size_t interpreter_allocation_failpoints;

typedef struct OutputCapture {
    unsigned char bytes[64];
    size_t length;
} OutputCapture;

static void check(int condition, const char *description)
{
    ++assertions;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", description);
    }
}

static int is_rexx_blank(unsigned char byte)
{
    return byte == (unsigned char)' ' ||
           (byte >= (unsigned char)'\t' && byte <= (unsigned char)'\r');
}

static void clear_values(RexxValue *values, size_t count)
{
    size_t index;
    for (index = 0u; index < count; ++index) rexx_value_clear(&values[index]);
}

static int32_t capture_output(void *user_data,
                              const char *bytes,
                              size_t length)
{
    OutputCapture *capture = (OutputCapture *)user_data;
    if (capture == NULL || bytes == NULL ||
        length > sizeof(capture->bytes) - capture->length)
        return -1;
    memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return (int32_t)length;
}

static int compile_source(const char *source, RexxBytecodeChunk **chunk_out)
{
    RexxLexer lexer = { 0 };
    RexxParser parser = { 0 };
    RexxAstProgram *program = NULL;
    RexxCompilerError error = { 0 };
    RexxLexerStatus lexer_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;

    *chunk_out = NULL;
    lexer_status = rexx_lexer_init(&lexer, source, strlen(source));
    if (lexer_status != REXX_LEXER_STATUS_OK) return 0;
    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status == REXX_PARSER_STATUS_OK)
        parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return 0;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    return compiler_status == REXX_COMPILER_STATUS_OK;
}

static int reference_justify(const unsigned char *input,
                             size_t input_length,
                             size_t requested_length,
                             unsigned char pad,
                             unsigned char *expected,
                             size_t *expected_length,
                             size_t *result_word_count)
{
    unsigned char normalized[RX27_TEST_BUFFER];
    size_t normalized_length = 0u;
    size_t cursor = 0u;
    size_t word_count = 0u;
    size_t prefix_length;
    size_t gaps;
    size_t extra;
    size_t each;
    size_t remainder;
    size_t index;
    int pending_gap = 0;

    if (input_length > sizeof(normalized) || requested_length > sizeof(normalized))
        return 0;

    /* Deliberately materialize a normalized reference string; production
     * JUSTIFY instead scans word views without allocating an intermediate. */
    for (index = 0u; index < input_length; ++index) {
        unsigned char byte = input[index];
        if (is_rexx_blank(byte)) {
            if (normalized_length != 0u) pending_gap = 1;
        } else {
            if (pending_gap) {
                normalized[normalized_length++] = (unsigned char)' ';
                pending_gap = 0;
            }
            normalized[normalized_length++] = byte;
        }
    }

    prefix_length = normalized_length < requested_length
                        ? normalized_length : requested_length;
    if (prefix_length != 0u &&
        normalized[prefix_length - 1u] == (unsigned char)' ')
        --prefix_length;
    for (index = 0u; index < prefix_length; ++index) {
        if (normalized[index] == (unsigned char)' ') ++word_count;
    }
    if (prefix_length != 0u) ++word_count;

    if (word_count < 2u) {
        if (prefix_length != 0u)
            memcpy(expected, normalized, prefix_length);
        *expected_length = prefix_length;
        *result_word_count = word_count;
        return 1;
    }

    gaps = word_count - 1u;
    extra = requested_length - prefix_length;
    each = extra / gaps;
    remainder = extra % gaps;
    cursor = 0u;
    for (index = 0u; index < prefix_length; ++index) {
        if (normalized[index] == (unsigned char)' ') {
            size_t gap_extra = each + (cursor < remainder ? 1u : 0u);
            size_t fill = 1u + gap_extra;
            memset(expected + *expected_length, pad, fill);
            *expected_length += fill;
            ++cursor;
        } else {
            expected[(*expected_length)++] = normalized[index];
        }
    }
    *result_word_count = word_count;
    return 1;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static int dispatch_bytes(const unsigned char *input,
                          size_t input_length,
                          const char *length_text,
                          const unsigned char *pad,
                          size_t pad_length,
                          int include_pad,
                          size_t argument_count,
                          RexxVMStatus expected_status,
                          const unsigned char *expected,
                          size_t expected_length,
                          const char *label)
{
    RexxValue arguments[4] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    unsigned char input_copy[RX27_TEST_BUFFER];
    RexxVMStatus status = REXX_VM_STATUS_OK;
    size_t setup_count = argument_count < 4u ? argument_count : 4u;
    size_t index;
    int passed = 1;

    if (input_length > sizeof(input_copy)) {
        check(0, "test input fits the local snapshot buffer");
        return 1;
    }
    if (input_length != 0u) memcpy(input_copy, input, input_length);
    if (setup_count >= 1u && input != NULL)
        status = rexx_value_set_string(&arguments[0], (const char *)input,
                                       input_length);
    if (status == REXX_VM_STATUS_OK && setup_count >= 2u &&
        length_text != NULL)
        status = rexx_value_set_string(&arguments[1], length_text,
                                       strlen(length_text));
    if (status == REXX_VM_STATUS_OK && setup_count >= 3u && include_pad)
        status = rexx_value_set_string(&arguments[2], (const char *)pad,
                                       pad_length);
    if (status == REXX_VM_STATUS_OK && setup_count >= 4u)
        status = rexx_value_set_string(&arguments[3], "extra", 5u);
    if (status != REXX_VM_STATUS_OK) {
        check(0, "JUSTIFY test arguments allocate");
        clear_values(arguments, 4u);
        return 1;
    }

    status = rexx_builtin_dispatch("JUSTIFY", 7u, arguments,
                                   argument_count, &result);
    check(status == expected_status, label);
    passed = passed && status == expected_status;
    if (status == REXX_VM_STATUS_OK) {
        check(result.type == REXX_VALUE_STRING &&
                  result.as.string.length == expected_length,
              "JUSTIFY result has the reference length");
        check(result.type == REXX_VALUE_STRING &&
                  result.as.string.length == expected_length &&
                  (expected_length == 0u ||
                   memcmp(result.as.string.bytes, expected,
                          expected_length) == 0),
              "JUSTIFY result bytes match the independent reference");
        passed = passed && result.type == REXX_VALUE_STRING &&
                 result.as.string.length == expected_length &&
                 (expected_length == 0u ||
                  memcmp(result.as.string.bytes, expected,
                         expected_length) == 0);
    } else {
        check(result.type == REXX_VALUE_UNSET,
              "invalid JUSTIFY call leaves no partial result");
        passed = passed && result.type == REXX_VALUE_UNSET;
    }
    if (setup_count >= 1u && input != NULL) {
        check(arguments[0].type == REXX_VALUE_STRING &&
                  arguments[0].as.string.length == input_length &&
                  (input_length == 0u ||
                   memcmp(arguments[0].as.string.bytes, input_copy,
                          input_length) == 0),
              "JUSTIFY does not mutate the source string");
    }
    for (index = 0u; index < 4u; ++index) rexx_value_clear(&arguments[index]);
    rexx_value_clear(&result);
    return passed ? 0 : 1;
}

static void test_exact_examples_and_edges(void)
{
    static const unsigned char pad_plus[] = { '+' };
    static const unsigned char pad_dash[] = { '-' };
    static const unsigned char pad_dot[] = { '.' };
    static const unsigned char pad_nul[] = { 0u };
    static const unsigned char pad_high[] = { 0xe3u };
    static const unsigned char binary_expected[] = { 'a', 0u, 0u, 0u, 'b' };
    static const unsigned char tab_input[] = { ' ', 'a', '\t', '\r', 'b', '\n' };
    static const unsigned char tab_expected[] = { 'a', ' ', 'b' };

    dispatch_bytes((const unsigned char *)"The blue sky", 12u, "14", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"The  blue  sky", 14u,
                   "IBM JUSTIFY example width 14");
    dispatch_bytes((const unsigned char *)"The blue sky", 12u, "8", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"The blue", 8u,
                   "IBM JUSTIFY example width 8");
    dispatch_bytes((const unsigned char *)"The blue sky", 12u, "9", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"The  blue", 9u,
                   "IBM JUSTIFY example width 9");
    dispatch_bytes((const unsigned char *)"The blue sky", 12u, "9", pad_plus,
                   sizeof(pad_plus), 1, 3u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"The++blue", 9u,
                   "IBM JUSTIFY example custom pad");
    dispatch_bytes((const unsigned char *)"a b c", 5u, "9", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a   b   c", 9u,
                   "three-way even separator distribution");
    dispatch_bytes((const unsigned char *)"a b c", 5u, "8", pad_dash,
                   sizeof(pad_dash), 1, 3u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a---b--c", 8u,
                   "odd added padding distributes left to right");
    dispatch_bytes((const unsigned char *)"  a   b  ", 9u, "5", pad_dot,
                   sizeof(pad_dot), 1, 3u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a...b", 5u,
                   "SPACE normalization trims and collapses blanks");
    dispatch_bytes((const unsigned char *)"abc def", 7u, "3", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"abc", 3u,
                   "truncation in first word");
    dispatch_bytes((const unsigned char *)"a b", 3u, "3", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a b", 3u,
                   "normalized width remains unchanged");
    dispatch_bytes((const unsigned char *)"a b", 3u, "5", pad_nul,
                   sizeof(pad_nul), 1, 3u, REXX_VM_STATUS_OK,
                   binary_expected, sizeof(binary_expected),
                   "zero-byte pad is preserved");
    dispatch_bytes((const unsigned char *)"", 0u, "7", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK, (const unsigned char *)"", 0u,
                   "empty input is not extended outside word gaps");
    dispatch_bytes((const unsigned char *)" \t \r\n", 5u, "4", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK, (const unsigned char *)"", 0u,
                   "all blank input normalizes to empty");
    dispatch_bytes((const unsigned char *)"solo", 4u, "8", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"solo", 4u,
                   "single word receives no invented outer padding");
    dispatch_bytes((const unsigned char *)"solo", 4u, "2", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"so", 2u,
                   "single word truncates without separator distribution");
    dispatch_bytes((const unsigned char *)"a b", 3u, "0", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK, (const unsigned char *)"", 0u,
                   "zero width produces empty prefix");
    dispatch_bytes(tab_input, sizeof(tab_input), "3", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK, tab_expected,
                   sizeof(tab_expected), "classic REXX blank class is shared with SPACE");
    dispatch_bytes((const unsigned char *)"a b", 3u, "4", pad_high,
                   sizeof(pad_high), 1, 3u, REXX_VM_STATUS_OK,
                   (const unsigned char[]){ 'a', 0xe3u, 0xe3u, 'b' }, 4u,
                   "high-bit pad byte remains unchanged");

    /* The final blank in each truncated normalized prefix is removed before
     * the surviving gaps receive any remaining pads. */
    dispatch_bytes((const unsigned char *)"a b c", 5u, "4", pad_dot,
                   sizeof(pad_dot), 1, 3u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a..b", 4u,
                   "truncation at separator redistributes after trim");
    dispatch_bytes((const unsigned char *)"a b c", 5u, "2", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a", 1u,
                   "truncation immediately after separator drops that blank");
    dispatch_bytes((const unsigned char *)"a b c", 5u, "3", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a b", 3u,
                   "truncation immediately after second word");
    dispatch_bytes((const unsigned char *)"a b c", 5u, "5", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a b c", 5u,
                   "truncation at normalized input end");
    dispatch_bytes((const unsigned char *)"a b c", 5u, "6", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a  b c", 6u,
                   "one added blank goes to the leftmost gap");
}

static void test_argument_validation(void)
{
    static const unsigned char empty[] = { 0u };
    static const unsigned char wide_pad[] = { 'x', 'y' };
    static const unsigned char nul_pad[] = { 'x', 0u };

    dispatch_bytes(NULL, 0u, NULL, NULL, 0u, 0, 0u,
                   REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "missing required string is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, NULL, NULL, 0u,
                   0, 1u, REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "missing required length is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, "-1", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "negative length is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, "1.5", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "fractional length is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, "NaN", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_NUMERIC_ERROR, NULL, 0u,
                   "non-numeric length follows established numeric error");
    dispatch_bytes((const unsigned char *)"a b", 3u, "4294967296", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_RESOURCE_LIMIT, NULL, 0u,
                   "large positive length rejects before narrowing or allocation");
    dispatch_bytes((const unsigned char *)"a b", 3u, "16777217", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_RESOURCE_LIMIT, NULL, 0u,
                   "length above runtime string ceiling is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, "3", empty, 0u,
                   1, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "explicit empty pad is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, "3", wide_pad,
                   sizeof(wide_pad), 1, 3u,
                   REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "multibyte pad is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, "3", nul_pad,
                   sizeof(nul_pad), 1, 3u,
                   REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "embedded NUL does not make a two-byte pad valid");
    dispatch_bytes((const unsigned char *)"a b", 3u, "3", NULL, 0u,
                   0, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL, NULL, 0u,
                   "extra argument is rejected");
    dispatch_bytes((const unsigned char *)"a b", 3u, "+3", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a b", 3u,
                   "signed positive whole representation is accepted");
    dispatch_bytes((const unsigned char *)"a b", 3u, "3.0", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a b", 3u,
                   "integral decimal representation is accepted");
    dispatch_bytes((const unsigned char *)"a b", 3u, "3E0", NULL, 0u,
                   0, 2u, REXX_VM_STATUS_OK,
                   (const unsigned char *)"a b", 3u,
                   "integral exponent representation is accepted");
}

static void test_binary_safety(void)
{
    static const unsigned char input[] = {
        ' ', 0x80u, 0u, 0xffu, '\t', 'Z', 0x81u, ' '
    };
    static const unsigned char expected[] = {
        0x80u, 0u, 0xffu, '-', '-', '-', 'Z', 0x81u
    };

    dispatch_bytes(input, sizeof(input), "8", (const unsigned char *)"-", 1u,
                   1, 3u, REXX_VM_STATUS_OK, expected, sizeof(expected),
                   "NUL and high-bit bytes within words are preserved");
}

static void test_properties(void)
{
    unsigned char input[RX27_TEST_BUFFER];
    unsigned char input_copy[RX27_TEST_BUFFER];
    unsigned char expected[RX27_TEST_BUFFER];
    unsigned char pad;
    uint32_t state = RX27_PROPERTY_SEED;
    size_t case_index;
    RexxVM vm_low_precision = { 0 };
    RexxVM vm_high_precision = { 0 };
    RexxVMStatus vm_status;

    vm_status = rexx_vm_init(&vm_low_precision, 0u);
    check(vm_status == REXX_VM_STATUS_OK,
          "first independent property VM initializes");
    if (vm_status != REXX_VM_STATUS_OK) return;
    vm_status = rexx_vm_init(&vm_high_precision, 0u);
    check(vm_status == REXX_VM_STATUS_OK,
          "second independent property VM initializes");
    if (vm_status != REXX_VM_STATUS_OK) {
        rexx_vm_free(&vm_low_precision);
        return;
    }
    vm_low_precision.numeric_digits = 2u;
    vm_high_precision.numeric_digits = 34u;

    for (case_index = 0u; case_index < RX27_PROPERTY_CASES; ++case_index) {
        size_t input_length = 0u;
        size_t requested_length;
        size_t expected_length = 0u;
        size_t result_word_count = 0u;
        size_t word_count = (size_t)(next_random(&state) % 7u);
        size_t word_index;
        size_t normalized_length;
        unsigned char length_text[32];
        RexxValue arguments[3] = {
            REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
        };
        RexxValue result_low_precision = REXX_VALUE_INIT;
        RexxValue result_high_precision = REXX_VALUE_INIT;
        RexxVMStatus status;
        RexxVMStatus high_precision_status;
        size_t index;
        int result_matches;

        for (index = 0u; index < (size_t)(next_random(&state) % 5u); ++index) {
            static const unsigned char blanks[] = { ' ', '\t', '\n', '\v', '\f', '\r' };
            input[input_length++] = blanks[next_random(&state) % sizeof(blanks)];
        }
        for (word_index = 0u; word_index < word_count; ++word_index) {
            size_t word_length = 1u + (size_t)(next_random(&state) % 12u);
            for (index = 0u; index < word_length; ++index) {
                unsigned char byte = (unsigned char)next_random(&state);
                while (is_rexx_blank(byte)) byte = (unsigned char)next_random(&state);
                input[input_length++] = byte;
            }
            if (word_index + 1u < word_count) {
                size_t blank_count = 1u + (size_t)(next_random(&state) % 6u);
                static const unsigned char blanks[] = { ' ', '\t', '\n', '\v', '\f', '\r' };
                for (index = 0u; index < blank_count; ++index)
                    input[input_length++] = blanks[next_random(&state) % sizeof(blanks)];
            }
        }
        for (index = 0u; index < (size_t)(next_random(&state) % 5u); ++index) {
            static const unsigned char blanks[] = { ' ', '\t', '\n', '\v', '\f', '\r' };
            input[input_length++] = blanks[next_random(&state) % sizeof(blanks)];
        }
        if (input_length != 0u) memcpy(input_copy, input, input_length);

        /* First compute normalized width independently for scenario selection. */
        {
            size_t cursor = 0u;
            size_t words = 0u;
            size_t bytes = 0u;
            int in_word = 0;
            while (cursor < input_length) {
                if (is_rexx_blank(input[cursor])) {
                    in_word = 0;
                } else {
                    if (!in_word) ++words;
                    ++bytes;
                    in_word = 1;
                }
                ++cursor;
            }
            normalized_length = bytes + (words == 0u ? 0u : words - 1u);
        }
        switch (case_index % 6u) {
        case 0u: requested_length = 0u; break;
        case 1u: requested_length = normalized_length; break;
        case 2u: requested_length = normalized_length + (next_random(&state) % 21u); break;
        case 3u: requested_length = normalized_length == 0u ? 0u : next_random(&state) % (normalized_length + 1u); break;
        case 4u: requested_length = normalized_length == 0u ? 0u : normalized_length - 1u; break;
        default: requested_length = next_random(&state) % (normalized_length + 21u); break;
        }
        pad = (unsigned char)next_random(&state);
        if (snprintf((char *)length_text, sizeof(length_text), "%zu",
                     requested_length) <= 0 ||
            !reference_justify(input, input_length, requested_length, pad,
                               expected, &expected_length,
                               &result_word_count)) {
            check(0, "property reference builds within fixed bounds");
            continue;
        }
        status = rexx_value_set_string(&arguments[0], (const char *)input,
                                       input_length);
        if (status == REXX_VM_STATUS_OK)
            status = rexx_value_set_string(&arguments[1], (const char *)length_text,
                                           strlen((char *)length_text));
        if (status == REXX_VM_STATUS_OK)
            status = rexx_value_set_string(&arguments[2], (const char *)&pad, 1u);
        if (status == REXX_VM_STATUS_OK)
            status = rexx_builtin_dispatch_with_vm(
                &vm_low_precision, "JUSTIFY", 7u, arguments, 3u,
                &result_low_precision);
        high_precision_status = status == REXX_VM_STATUS_OK
            ? rexx_builtin_dispatch_with_vm(
                  &vm_high_precision, "JUSTIFY", 7u, arguments, 3u,
                  &result_high_precision)
            : status;
        result_matches = status == REXX_VM_STATUS_OK &&
            high_precision_status == REXX_VM_STATUS_OK &&
            result_low_precision.type == REXX_VALUE_STRING &&
            result_low_precision.as.string.length == expected_length &&
            (expected_length == 0u ||
             memcmp(result_low_precision.as.string.bytes, expected,
                    expected_length) == 0) &&
            result_high_precision.type == REXX_VALUE_STRING &&
            result_high_precision.as.string.length == expected_length &&
            (expected_length == 0u ||
             memcmp(result_high_precision.as.string.bytes, expected,
                    expected_length) == 0);
        check(result_matches,
              "fixed-seed property matches oracle in both independent VMs");
        check(status == high_precision_status &&
                  result_low_precision.type == result_high_precision.type &&
                  result_low_precision.type == REXX_VALUE_STRING &&
                  result_low_precision.as.string.length ==
                      result_high_precision.as.string.length &&
                  (expected_length == 0u ||
                   memcmp(result_low_precision.as.string.bytes,
                          result_high_precision.as.string.bytes,
                          expected_length) == 0),
              "JUSTIFY result is identical across independent NUMERIC DIGITS contexts");
        check(arguments[0].as.string.length == input_length &&
                  (input_length == 0u ||
                   memcmp(arguments[0].as.string.bytes, input_copy,
                          input_length) == 0),
              "property call preserves source bytes");
        if (result_low_precision.type == REXX_VALUE_STRING &&
            result_word_count > 1u)
            check(result_low_precision.as.string.length == requested_length,
                  "valid multiword property has requested output width");
        clear_values(arguments, 3u);
        rexx_value_clear(&result_low_precision);
        rexx_value_clear(&result_high_precision);
        ++property_cases;
    }
    rexx_vm_free(&vm_high_precision);
    rexx_vm_free(&vm_low_precision);
}

static void test_allocation_recovery(void)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    size_t live_before;
    RexxVMStatus status;

    status = rexx_value_set_string(&arguments[0], "a b c", 5u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "12", 2u);
    check(status == REXX_VM_STATUS_OK, "allocation recovery arguments are prepared");
    if (status != REXX_VM_STATUS_OK) {
        clear_values(arguments, 2u);
        return;
    }
    live_before = rexx_vm_test_allocator_live_blocks();
    rexx_vm_test_allocator_fail_on(1u);
    status = rexx_builtin_dispatch("JUSTIFY", 7u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OUT_OF_MEMORY,
          "injected JUSTIFY output allocation failure is reported");
    check(result.type == REXX_VALUE_UNSET,
          "failed output allocation leaves no result value");
    check(rexx_vm_test_allocator_attempts() == 1u,
          "JUSTIFY performs one result allocation and no temporary allocations");
    rexx_vm_test_allocator_disable();
    check(rexx_vm_test_allocator_live_blocks() == live_before,
          "failed allocation preserves VM allocator ownership count");
    status = rexx_builtin_dispatch("JUSTIFY", 7u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OK && result.type == REXX_VALUE_STRING &&
              result.as.string.length == 12u &&
              memcmp(result.as.string.bytes, "a     b    c", 12u) == 0,
          "successful JUSTIFY call follows the injected failure");
    clear_values(arguments, 2u);
    rexx_value_clear(&result);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "allocation recovery test releases all values");
}

static void test_interpreter_failure_recovery(void)
{
    static const char source[] = "say justify('a b',6)\n";
    static const unsigned char expected[] = { 'a', ' ', ' ', ' ', ' ', 'b', '\n' };
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm = { 0 };
    OutputCapture capture = { { 0u }, 0u };
    RexxVMStatus status;
    size_t allocation_points;
    size_t failpoint;

    check(compile_source(source, &chunk),
          "interpreter recovery program compiles before allocation injection");
    if (chunk == NULL) return;
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK,
          "interpreter recovery VM initializes");
    if (status != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        rexx_vm_free(&vm);
        return;
    }
    rexx_vm_set_output_writer(&vm, capture_output, &capture);
    rexx_vm_test_allocator_fail_on(SIZE_MAX);
    status = rexx_vm_execute(&vm, chunk);
    allocation_points = rexx_vm_test_allocator_attempts();
    interpreter_allocation_failpoints = allocation_points;
    rexx_vm_test_allocator_disable();
    check(status == REXX_VM_STATUS_OK &&
              capture.length == sizeof(expected) &&
              memcmp(capture.bytes, expected, sizeof(expected)) == 0,
          "interpreter baseline JUSTIFY program returns its expected output");
    check(allocation_points != 0u,
          "interpreter baseline discovers VM allocation failpoints");

    for (failpoint = 1u; failpoint <= allocation_points; ++failpoint) {
        RexxVMStatus injected_status;
        size_t attempts;

        rexx_vm_free(&vm);
        memset(&vm, 0, sizeof(vm));
        memset(&capture, 0, sizeof(capture));
        status = rexx_vm_init(&vm, 0u);
        check(status == REXX_VM_STATUS_OK,
              "interpreter failpoint VM initializes");
        if (status != REXX_VM_STATUS_OK) break;
        rexx_vm_set_output_writer(&vm, capture_output, &capture);
        rexx_vm_test_allocator_fail_on(failpoint);
        injected_status = rexx_vm_execute(&vm, chunk);
        attempts = rexx_vm_test_allocator_attempts();
        rexx_vm_test_allocator_disable();
        check(injected_status == REXX_VM_STATUS_OUT_OF_MEMORY,
              "JUSTIFY interpreter allocation failpoint reports OOM");
        check(attempts >= failpoint,
              "JUSTIFY interpreter reaches each measured allocation failpoint");

        capture.length = 0u;
        status = rexx_vm_execute(&vm, chunk);
        check(status == REXX_VM_STATUS_OK &&
                  capture.length == sizeof(expected) &&
                  memcmp(capture.bytes, expected, sizeof(expected)) == 0,
              "interpreter executes successfully after JUSTIFY allocation failure");
        rexx_vm_free(&vm);
        check(rexx_vm_test_allocator_live_blocks() == 0u,
              "interpreter failure/recovery releases all VM allocations");
    }
    rexx_vm_test_allocator_disable();
    rexx_bytecode_chunk_free(chunk);
}

static void test_maximum_output(void)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    size_t index;
    const char *large_length = "16777216";

    status = rexx_value_set_string(&arguments[0], "a b", 3u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], large_length,
                                       strlen(large_length));
    check(status == REXX_VM_STATUS_OK, "maximum-size test inputs allocate");
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_dispatch("JUSTIFY", 7u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OK && result.type == REXX_VALUE_STRING &&
              result.as.string.length == RX27_MAX_STRING,
          "maximum practical result length is accepted");
    if (status == REXX_VM_STATUS_OK && result.type == REXX_VALUE_STRING &&
        result.as.string.length == RX27_MAX_STRING) {
        check(result.as.string.bytes[0] == 'a' &&
                  result.as.string.bytes[RX27_MAX_STRING - 1u] == 'b',
              "maximum result retains boundary word bytes");
        for (index = 1u; index + 1u < RX27_MAX_STRING; ++index) {
            if (result.as.string.bytes[index] != ' ') break;
        }
        check(index + 1u == RX27_MAX_STRING,
              "maximum result fills only the bounded interword gap");
    }
    clear_values(arguments, 2u);
    rexx_value_clear(&result);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "maximum-size test releases all values");
}

int main(void)
{
    test_exact_examples_and_edges();
    test_argument_validation();
    test_binary_safety();
    test_properties();
    test_allocation_recovery();
    test_interpreter_failure_recovery();
    test_maximum_output();
    printf("RX27 JUSTIFY seed=0x%08x property_cases=%zu assertions=%zu failures=%zu interpreter_failpoints=%zu limit=%zu\n",
           (unsigned)RX27_PROPERTY_SEED, property_cases, assertions, failures,
           interpreter_allocation_failpoints, RX27_MAX_STRING);
    rexx_vm_test_allocator_disable();
    return failures == 0u ? 0 : 1;
}
