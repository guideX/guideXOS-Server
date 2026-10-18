#include "rexx_builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum Rx17Operation {
    RX17_AND,
    RX17_OR,
    RX17_XOR
} Rx17Operation;

static size_t assertion_count;
static size_t dispatch_case_count;
static size_t property_case_count;
static size_t failure_count;
static uint32_t random_state = UINT32_C(0x52583137);

static const char *operation_name(Rx17Operation operation)
{
    switch (operation) {
    case RX17_AND: return "BITAND";
    case RX17_OR: return "BITOR";
    case RX17_XOR: return "BITXOR";
    default: return "";
    }
}

static int check(int condition, const char *description)
{
    ++assertion_count;
    if (condition) return 0;
    ++failure_count;
    fprintf(stderr, "FAIL: %s\n", description);
    return 1;
}

static unsigned char reference_byte(Rx17Operation operation,
                                    unsigned char left,
                                    unsigned char right)
{
    switch (operation) {
    case RX17_AND: return (unsigned char)(left & right);
    case RX17_OR: return (unsigned char)(left | right);
    case RX17_XOR: return (unsigned char)(left ^ right);
    default: return 0u;
    }
}

/* Independent test oracle for classic REXX unequal-length and pad behavior. */
static size_t reference_result(Rx17Operation operation,
                              const unsigned char *left, size_t left_length,
                              const unsigned char *right, size_t right_length,
                              int has_pad, unsigned char pad,
                              unsigned char *output)
{
    size_t output_length = left_length > right_length
                               ? left_length : right_length;
    size_t overlap = left_length < right_length
                         ? left_length : right_length;
    size_t index;
    for (index = 0u; index < output_length; ++index) {
        if (index >= overlap && !has_pad) {
            output[index] = left_length > right_length
                                ? left[index] : right[index];
        } else {
            unsigned char left_byte = index < left_length
                                          ? left[index] : pad;
            unsigned char right_byte = index < right_length
                                           ? right[index] : pad;
            output[index] = reference_byte(operation, left_byte, right_byte);
        }
    }
    return output_length;
}

static int check_string(const RexxValue *value,
                        const unsigned char *expected, size_t expected_length,
                        const char *description)
{
    int valid = value != NULL && value->type == REXX_VALUE_STRING &&
                value->as.string.length == expected_length &&
                (expected_length == 0u ||
                 (value->as.string.bytes != NULL && expected != NULL &&
                  memcmp(value->as.string.bytes, expected,
                         expected_length) == 0));
    return check(valid, description);
}

static int run_case(Rx17Operation operation,
                    const unsigned char *first, size_t first_length,
                    int include_second,
                    const unsigned char *second, size_t second_length,
                    int include_pad, unsigned char pad,
                    const unsigned char *expected, size_t expected_length,
                    const char *description)
{
    static const unsigned char empty[] = { 0u };
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    size_t argument_count = 1u;
    RexxVMStatus status;
    int failed = 0;

    if (first == NULL) first = empty;
    status = rexx_value_set_string(&arguments[0], (const char *)first,
                                   first_length);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX17 first argument allocation succeeds");
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    if (include_second) {
        if (second == NULL) second = empty;
        status = rexx_value_set_string(&arguments[1], (const char *)second,
                                       second_length);
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 second argument allocation succeeds");
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        argument_count = 2u;
    }
    if (include_pad) {
        unsigned char pad_byte = pad;
        status = rexx_value_set_string(&arguments[2], (const char *)&pad_byte,
                                       1u);
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 pad argument allocation succeeds");
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        argument_count = 3u;
    }

    status = rexx_builtin_dispatch(operation_name(operation),
                                   strlen(operation_name(operation)), arguments,
                                   argument_count, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX17 valid bytewise dispatch succeeds");
    failed += check_string(&result, expected, expected_length, description);
    failed += check(arguments[0].type == REXX_VALUE_STRING &&
                    arguments[0].as.string.length == first_length &&
                    (first_length == 0u ||
                     memcmp(arguments[0].as.string.bytes, first,
                            first_length) == 0),
                    "RX17 dispatch preserves its first input buffer");
    if (include_second) {
        failed += check(arguments[1].type == REXX_VALUE_STRING &&
                        arguments[1].as.string.length == second_length &&
                        (second_length == 0u ||
                         memcmp(arguments[1].as.string.bytes, second,
                                second_length) == 0),
                        "RX17 dispatch preserves its second input buffer");
    }

cleanup:
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    rexx_value_clear(&result);
    return failed;
}

static uint32_t next_random(void)
{
    uint32_t value = random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static void fill_random(unsigned char *bytes, size_t length)
{
    size_t index;
    for (index = 0u; index < length; ++index)
        bytes[index] = (unsigned char)(next_random() >> 24);
}

static int test_reference_vectors(void)
{
    static const unsigned char and_result[] = { 0x23u };
    static const unsigned char and_a[] = { 0x73u };
    static const unsigned char and_b[] = { 0x27u };
    static const unsigned char and_short[] = { 0x13u };
    static const unsigned char and_long[] = { 0x55u, 0x55u };
    static const unsigned char and_pad_result[] = { 0x11u, 0x54u };
    static const unsigned char and_no_pad_result[] = { 0x11u, 0x55u };
    static const unsigned char or_a[] = { 0x15u };
    static const unsigned char or_b[] = { 0x24u };
    static const unsigned char or_long[] = { 0x24u, 0x56u };
    static const unsigned char or_pad_result[] = { 0x35u, 0xf6u };
    static const unsigned char or_long_result[] = { 0x35u, 0x56u };
    static const unsigned char or_result[] = { 0x35u };
    static const unsigned char xor_a[] = { 0x12u };
    static const unsigned char xor_b[] = { 0x22u };
    static const unsigned char xor_result[] = { 0x30u };
    static const unsigned char xor_long_a[] = { 0x12u, 0x11u };
    static const unsigned char xor_long_b[] = { 0x22u };
    static const unsigned char xor_long_result[] = { 0x30u, 0x11u };
    static const unsigned char xor_three_a[] = { 0x11u, 0x11u };
    static const unsigned char xor_three_b[] = { 0x44u, 0x44u, 0x44u };
    static const unsigned char xor_three_result[] = { 0x55u, 0x55u, 0x44u };
    static const unsigned char xor_pad_result[] = { 0x55u, 0x55u, 0x04u };
    static const unsigned char and_single[] = { 0x13u };
    static const unsigned char zero[] = { 0x00u };
    static const unsigned char or_pad_result_one[] = { 0x93u };
    static const unsigned char xor_pad_result_one[] = { 0xecu };
    static const unsigned char empty[] = { 0u };
    static const unsigned char aabb[] = { 0xaau, 0xbbu };
    static const unsigned char aabb_ffand[] = { 0xaau, 0xbbu };
    static const unsigned char aabb_zeroor[] = { 0xaau, 0xbbu };
    static const unsigned char aabb_ffxor[] = { 0x55u, 0x44u };
    static const unsigned char raw_numeric[] = { '+','0','0','1','.','2','3','0','0' };
    int failed = 0;
    failed += run_case(RX17_AND, and_a, 1u, 1, and_b, 1u, 0, 0u,
                       and_result, sizeof(and_result),
                       "BITAND equal-length byte result");
    failed += run_case(RX17_AND, and_short, 1u, 1, and_long, 2u, 0, 0u,
                       and_no_pad_result, sizeof(and_no_pad_result),
                       "BITAND appends the unmatched longer tail");
    failed += run_case(RX17_AND, and_short, 1u, 1, and_long, 2u, 1, 0x74u,
                       and_pad_result, sizeof(and_pad_result),
                       "BITAND pads the shorter operand on the right");
    failed += run_case(RX17_AND, and_long, 2u, 1, and_short, 1u, 1, 0x74u,
                       and_pad_result, sizeof(and_pad_result),
                       "BITAND pads the shorter second operand on the right");
    failed += run_case(RX17_OR, or_a, 1u, 1, or_b, 1u, 0, 0u,
                       or_result, sizeof(or_result),
                       "BITOR equal-length byte result");
    failed += run_case(RX17_OR, or_a, 1u, 1, or_long, 2u, 0, 0u,
                       or_long_result, sizeof(or_long_result),
                       "BITOR appends the unmatched longer tail");
    failed += run_case(RX17_OR, or_a, 1u, 1, or_long, 2u, 1, 0xf0u,
                       or_pad_result, sizeof(or_pad_result),
                       "BITOR uses an explicit high-bit pad");
    failed += run_case(RX17_OR, or_long, 2u, 1, or_a, 1u, 1, 0xf0u,
                       or_pad_result, sizeof(or_pad_result),
                       "BITOR pads the shorter second operand on the right");
    failed += run_case(RX17_XOR, xor_a, 1u, 1, xor_b, 1u, 0, 0u,
                       xor_result, sizeof(xor_result),
                       "BITXOR is bytewise exclusive OR");
    failed += run_case(RX17_XOR, xor_long_a, 2u, 1, xor_long_b, 1u, 0, 0u,
                       xor_long_result, sizeof(xor_long_result),
                       "BITXOR preserves the unmatched longer tail");
    failed += run_case(RX17_XOR, xor_three_a, 2u, 1, xor_three_b, 3u, 0, 0u,
                       xor_three_result, sizeof(xor_three_result),
                       "BITXOR shorter-first unequal lengths");
    failed += run_case(RX17_XOR, xor_three_a, 2u, 1, xor_three_b, 3u, 1, 0x40u,
                       xor_pad_result, sizeof(xor_pad_result),
                       "BITXOR pads the shorter first operand");
    failed += run_case(RX17_XOR, xor_three_b, 3u, 1, xor_three_a, 2u, 1, 0x40u,
                       xor_pad_result, sizeof(xor_pad_result),
                       "BITXOR pads the shorter second operand");
    failed += run_case(RX17_AND, and_single, 1u, 0, NULL, 0u, 0, 0u,
                       and_single, sizeof(and_single),
                       "BITAND omitted second operand defaults to empty");
    failed += run_case(RX17_OR, and_single, 1u, 0, NULL, 0u, 0, 0u,
                       and_single, sizeof(and_single),
                       "BITOR omitted second operand defaults to empty");
    failed += run_case(RX17_XOR, and_single, 1u, 0, NULL, 0u, 0, 0u,
                       and_single, sizeof(and_single),
                       "BITXOR omitted second operand defaults to empty");
    failed += run_case(RX17_AND, and_single, 1u, 0, NULL, 0u, 1, 0x00u,
                       zero, sizeof(zero),
                       "BITAND supports an omitted middle operand and NUL pad");
    failed += run_case(RX17_OR, and_single, 1u, 0, NULL, 0u, 1, 0x80u,
                       or_pad_result_one, sizeof(or_pad_result_one),
                       "BITOR supports an omitted middle operand and high-bit pad");
    failed += run_case(RX17_XOR, and_single, 1u, 0, NULL, 0u, 1, 0xffu,
                       xor_pad_result_one, sizeof(xor_pad_result_one),
                       "BITXOR supports an omitted middle operand and FF pad");
    failed += run_case(RX17_AND, and_single, 1u, 1, empty, 0u, 1, 0x00u,
                       zero, sizeof(zero),
                       "BITAND extends an explicitly empty second operand");
    failed += run_case(RX17_OR, and_single, 1u, 1, empty, 0u, 1, 0x80u,
                       or_pad_result_one, sizeof(or_pad_result_one),
                       "BITOR extends an explicitly empty second operand");
    failed += run_case(RX17_XOR, and_single, 1u, 1, empty, 0u, 1, 0xffu,
                       xor_pad_result_one, sizeof(xor_pad_result_one),
                       "BITXOR extends an explicitly empty second operand");
    failed += run_case(RX17_AND, empty, 0u, 1, aabb, 2u, 0, 0u,
                       aabb_ffand, sizeof(aabb_ffand),
                       "BITAND accepts an explicitly empty first operand");
    failed += run_case(RX17_OR, empty, 0u, 1, empty, 0u, 0, 0u,
                       empty, 0u, "BITOR returns an empty string for empty inputs");
    failed += run_case(RX17_XOR, empty, 0u, 1, empty, 0u, 0, 0u,
                       empty, 0u, "BITXOR returns an empty string for empty inputs");
    failed += run_case(RX17_AND, empty, 0u, 1, aabb, 2u, 1, 0xffu,
                       aabb_ffand, sizeof(aabb_ffand),
                       "BITAND accepts an explicit FF pad");
    failed += run_case(RX17_OR, empty, 0u, 1, aabb, 2u, 1, 0x00u,
                       aabb_zeroor, sizeof(aabb_zeroor),
                       "BITOR accepts an explicit NUL pad");
    failed += run_case(RX17_XOR, empty, 0u, 1, aabb, 2u, 1, 0xffu,
                       aabb_ffxor, sizeof(aabb_ffxor),
                       "BITXOR accepts an explicit FF pad");
    failed += run_case(RX17_AND, raw_numeric, sizeof(raw_numeric), 0,
                       NULL, 0u, 0, 0u, raw_numeric, sizeof(raw_numeric),
                       "numeric-looking quoted bytes retain their exact representation");
    {
        RexxValue arguments[3] = {
            REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
        };
        RexxValue result = REXX_VALUE_INIT;
        RexxVMStatus status;
        status = rexx_value_set_string(&arguments[0], (const char *)xor_long_a,
                                       sizeof(xor_long_a));
        if (status == REXX_VM_STATUS_OK)
            status = rexx_value_set_string(&arguments[1], (const char *)xor_long_b,
                                           sizeof(xor_long_b));
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 omitted-pad inputs are created");
        if (status == REXX_VM_STATUS_OK) {
            status = rexx_builtin_dispatch("BITXOR", 6u, arguments, 3u,
                                           &result);
            ++dispatch_case_count;
            failed += check(status == REXX_VM_STATUS_OK,
                            "RX17 accepts an omitted trailing pad slot");
            failed += check_string(&result, xor_long_result,
                                   sizeof(xor_long_result),
                                   "RX17 omitted trailing pad preserves longer tail");
        }
        rexx_value_clear(&arguments[0]);
        rexx_value_clear(&arguments[1]);
        rexx_value_clear(&arguments[2]);
        rexx_value_clear(&result);
    }
    return failed;
}

static int test_binary_boundaries(void)
{
    static const unsigned char expected_unaligned[] = { 0x00u, 0x81u, 0xffu };
    static const unsigned char shared[] = { 0x00u, 0x80u, 0xffu, 0x7fu };
    static const unsigned char expected_shared[] = { 0x00u, 0x80u, 0xffu, 0x7fu };
    unsigned char unaligned_left[] = { 0xeeu, 0x12u, 0x34u, 0xffu, 0xeeu };
    unsigned char unaligned_right[] = { 0xeeu, 0x12u, 0xb5u, 0x00u, 0xeeu };
    RexxValue unaligned_args[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue shared_value = REXX_VALUE_INIT;
    RexxValue shared_args[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int failed = 0;

    unaligned_args[0].type = REXX_VALUE_STRING;
    unaligned_args[0].as.string.bytes = (char *)(unaligned_left + 1u);
    unaligned_args[0].as.string.length = 3u;
    unaligned_args[1].type = REXX_VALUE_STRING;
    unaligned_args[1].as.string.bytes = (char *)(unaligned_right + 1u);
    unaligned_args[1].as.string.length = 3u;
    status = rexx_builtin_dispatch("BITXOR", 6u, unaligned_args, 2u, &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX17 supports unaligned borrowed source buffers");
    failed += check_string(&result, expected_unaligned,
                           sizeof(expected_unaligned),
                           "RX17 unaligned byte result is exact");
    rexx_value_clear(&result);

    status = rexx_value_set_string(&shared_value, (const char *)shared,
                                   sizeof(shared));
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX17 shared backing input allocation succeeds");
    if (status == REXX_VM_STATUS_OK) {
        shared_args[0] = shared_value;
        shared_args[1] = shared_value;
        status = rexx_builtin_dispatch("BITOR", 5u, shared_args, 2u, &result);
        ++dispatch_case_count;
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 accepts aliased read-only arguments");
        failed += check_string(&result, expected_shared,
                               sizeof(expected_shared),
                               "RX17 aliased result uses distinct output storage");
        failed += check(result.as.string.bytes != shared_value.as.string.bytes,
                        "RX17 result does not alias a shared argument buffer");
        failed += check(shared_value.as.string.length == sizeof(shared) &&
                        memcmp(shared_value.as.string.bytes, shared,
                               sizeof(shared)) == 0,
                        "RX17 leaves shared input backing unchanged");
    }
    rexx_value_clear(&shared_value);
    rexx_value_clear(&result);
    return failed;
}

static int test_argument_errors(void)
{
    static const char *const names[] = { "BITAND", "BITOR", "BITXOR" };
    static const unsigned char first[] = { 0x13u };
    static const unsigned char two_byte_pad[] = { 0x00u, 0xffu };
    size_t operation;
    int failed = 0;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;

    status = rexx_builtin_dispatch("BITAND", 6u, NULL, 0u, &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX17 rejects a missing required string1");

    for (operation = 0u; operation < 3u; ++operation) {
        RexxValue arguments[4] = {
            REXX_VALUE_INIT, REXX_VALUE_INIT,
            REXX_VALUE_INIT, REXX_VALUE_INIT
        };
        status = rexx_value_set_string(&arguments[0], (const char *)first,
                                       sizeof(first));
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 invalid-argument first input is created");
        if (status != REXX_VM_STATUS_OK) {
            rexx_value_clear(&arguments[0]);
            continue;
        }

        status = rexx_builtin_dispatch(names[operation], strlen(names[operation]),
                                       arguments, 1u, &result);
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 accepts a single required argument");
        rexx_value_clear(&result);

        status = rexx_value_set_string(&arguments[2], "", 0u);
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 zero-length pad input is created");
        if (status == REXX_VM_STATUS_OK) {
            status = rexx_builtin_dispatch(names[operation],
                                           strlen(names[operation]), arguments,
                                           3u, &result);
            failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                            result.type == REXX_VALUE_UNSET,
                            "RX17 rejects an explicitly empty pad");
        }
        rexx_value_clear(&arguments[2]);

        status = rexx_value_set_string(&arguments[2],
                                       (const char *)two_byte_pad,
                                       sizeof(two_byte_pad));
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 multibyte pad input is created");
        if (status == REXX_VM_STATUS_OK) {
            status = rexx_builtin_dispatch(names[operation],
                                           strlen(names[operation]), arguments,
                                           3u, &result);
            failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                            result.type == REXX_VALUE_UNSET,
                            "RX17 rejects a multibyte pad");
        }
        rexx_value_clear(&arguments[2]);

        arguments[3].type = REXX_VALUE_STRING;
        arguments[3].as.string.bytes = (char *)"x";
        arguments[3].as.string.length = 1u;
        status = rexx_builtin_dispatch(names[operation], strlen(names[operation]),
                                       arguments, 4u, &result);
        failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                        result.type == REXX_VALUE_UNSET,
                        "RX17 rejects an excess argument");
        arguments[3].type = REXX_VALUE_UNSET;
        rexx_value_clear(&arguments[0]);
        rexx_value_clear(&result);
    }

    {
        RexxValue missing_first[1] = { REXX_VALUE_INIT };
        status = rexx_builtin_dispatch("BITXOR", 6u, missing_first, 1u,
                                       &result);
        failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                        result.type == REXX_VALUE_UNSET,
                        "RX17 rejects an omitted required first slot");
    }
    rexx_value_clear(&result);
    return failed;
}

static int test_random_oracle(void)
{
    unsigned char left[32], right[32], expected[32];
    unsigned int case_index;
    int failed = 0;

    for (case_index = 0u; case_index < 256u; ++case_index) {
        size_t left_length = (size_t)(next_random() % 33u);
        size_t right_length = (size_t)(next_random() % 33u);
        unsigned char pad = (unsigned char)(next_random() >> 24);
        size_t operation;
        if ((case_index & 1u) != 0u && left_length == right_length) {
            if (right_length < sizeof(right)) ++right_length;
            else --right_length;
        }
        fill_random(left, left_length);
        fill_random(right, right_length);
        for (operation = 0u; operation < 3u; ++operation) {
            size_t expected_length = reference_result(
                (Rx17Operation)operation, left, left_length, right,
                right_length, 0, 0u, expected);
            failed += run_case((Rx17Operation)operation, left, left_length,
                               1, right, right_length, 0, 0u, expected,
                               expected_length,
                               "RX17 random no-pad output matches independent oracle");
            expected_length = reference_result(
                (Rx17Operation)operation, left, left_length, right,
                right_length, 1, pad, expected);
            failed += run_case((Rx17Operation)operation, left, left_length,
                               1, right, right_length, 1, pad, expected,
                               expected_length,
                               "RX17 random padded output matches independent oracle");
            property_case_count += 2u;
        }
    }
    return failed;
}

static int test_equal_length_identities(void)
{
    unsigned char left[32], right[32], zeros[32], ones[32];
    unsigned char expected[32], complemented_left[32], complemented_right[32];
    unsigned int case_index;
    int failed = 0;

    memset(zeros, 0, sizeof(zeros));
    memset(ones, 0xff, sizeof(ones));
    for (case_index = 0u; case_index < 64u; ++case_index) {
        size_t length = (size_t)(next_random() % 33u);
        size_t index, operation;
        fill_random(left, length);
        fill_random(right, length);
        for (index = 0u; index < length; ++index) {
            complemented_left[index] = (unsigned char)~left[index];
            complemented_right[index] = (unsigned char)~right[index];
        }

        failed += run_case(RX17_AND, left, length, 1, left, length, 0, 0u,
                           left, length, "A AND A equals A");
        failed += run_case(RX17_OR, left, length, 1, left, length, 0, 0u,
                           left, length, "A OR A equals A");
        memset(expected, 0, length);
        failed += run_case(RX17_XOR, left, length, 1, left, length, 0, 0u,
                           expected, length, "A XOR A equals zero bytes");
        failed += run_case(RX17_XOR, left, length, 1, zeros, length, 0, 0u,
                           left, length, "A XOR zero bytes equals A");
        failed += run_case(RX17_AND, left, length, 1, ones, length, 0, 0u,
                           left, length, "A AND FF bytes equals A");
        property_case_count += 5u;

        for (operation = 0u; operation < 3u; ++operation) {
            size_t expected_length = reference_result(
                (Rx17Operation)operation, left, length, right, length,
                0, 0u, expected);
            failed += run_case((Rx17Operation)operation, left, length,
                               1, right, length, 0, 0u, expected,
                               expected_length,
                               "RX17 equal-length operation matches oracle");
            failed += run_case((Rx17Operation)operation, right, length,
                               1, left, length, 0, 0u, expected,
                               expected_length,
                               "RX17 bytewise operation is commutative");
            property_case_count += 2u;
        }

        /* De Morgan: complement bytes are computed here, independent of BIFs. */
        for (index = 0u; index < length; ++index) {
            unsigned char de_morgan_left =
                (unsigned char)~(left[index] | right[index]);
            unsigned char de_morgan_right = (unsigned char)(
                (unsigned char)~left[index] & (unsigned char)~right[index]);
            expected[index] = de_morgan_left;
            failed += check(de_morgan_left == de_morgan_right,
                            "RX17 independent De Morgan byte identities agree");
        }
        failed += run_case(RX17_AND, complemented_left, length, 1,
                           complemented_right, length, 0, 0u,
                           expected, length,
                           "RX17 De Morgan AND identity with independent complements");
        ++property_case_count;
    }
    return failed;
}

static int test_maximum_runtime_string(void)
{
    size_t length = REXX_BUILTIN_MAX_RESULT_LENGTH;
    unsigned char *source = (unsigned char *)malloc(length);
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int failed = 0;

    failed += check(source != NULL, "RX17 maximum-length source allocation succeeds");
    if (source == NULL) return failed;
    memset(source, 0xa5, length);
    source[0] = 0x00u;
    source[length / 2u] = 0x80u;
    source[length - 1u] = 0xffu;
    status = rexx_value_set_string(&argument, (const char *)source, length);
    free(source);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX17 accepts the existing maximum runtime string length");
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch("BITOR", 5u, &argument, 1u, &result);
        ++dispatch_case_count;
        failed += check(status == REXX_VM_STATUS_OK &&
                        result.type == REXX_VALUE_STRING &&
                        result.as.string.length == length,
                        "RX17 does not impose the RX16 250-byte literal bound on runtime strings");
        if (status == REXX_VM_STATUS_OK && result.type == REXX_VALUE_STRING) {
            failed += check((unsigned char)result.as.string.bytes[0] == 0x00u &&
                            (unsigned char)result.as.string.bytes[length / 2u] == 0x80u &&
                            (unsigned char)result.as.string.bytes[length - 1u] == 0xffu,
                            "RX17 maximum-length result preserves boundary bytes");
        }
    }
    rexx_value_clear(&argument);
    rexx_value_clear(&result);
    return failed;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static int test_allocation_failure_recovery(void)
{
    static const char *const names[] = { "BITAND", "BITOR", "BITXOR" };
    static const unsigned char first[] = { 0x13u, 0x00u, 0xffu };
    static const unsigned char second[] = { 0x55u };
    static const unsigned char expected_and[] = { 0x11u, 0x00u, 0xffu };
    static const unsigned char expected_or[] = { 0x57u, 0x00u, 0xffu };
    static const unsigned char expected_xor[] = { 0x46u, 0x00u, 0xffu };
    const unsigned char *expected[] = {
        expected_and, expected_or, expected_xor
    };
    const size_t live_before = rexx_vm_test_allocator_live_blocks();
    size_t operation;
    int failed = 0;

    for (operation = 0u; operation < 3u; ++operation) {
        RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
        RexxValue result = REXX_VALUE_INIT;
        size_t live_with_arguments;
        RexxVMStatus status;
        status = rexx_value_set_string(&arguments[0], (const char *)first,
                                       sizeof(first));
        if (status == REXX_VM_STATUS_OK)
            status = rexx_value_set_string(&arguments[1], (const char *)second,
                                           sizeof(second));
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 allocation recovery inputs are created");
        if (status != REXX_VM_STATUS_OK) {
            rexx_value_clear(&arguments[0]);
            rexx_value_clear(&arguments[1]);
            continue;
        }
        live_with_arguments = rexx_vm_test_allocator_live_blocks();
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch(names[operation], strlen(names[operation]),
                                       arguments, 2u, &result);
        rexx_vm_test_allocator_disable();
        ++dispatch_case_count;
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET,
                        "RX17 allocation failure publishes no partial result");
        failed += check(rexx_vm_test_allocator_live_blocks() == live_with_arguments &&
                        arguments[0].type == REXX_VALUE_STRING &&
                        memcmp(arguments[0].as.string.bytes, first,
                               sizeof(first)) == 0,
                        "RX17 allocation failure retains inputs and frees partial state");
        status = rexx_builtin_dispatch(names[operation], strlen(names[operation]),
                                       arguments, 2u, &result);
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX17 operation recovers after allocation failure");
        failed += check_string(&result, expected[operation], sizeof(first),
                               "RX17 recovered result matches expected bytes");
        rexx_value_clear(&arguments[0]);
        rexx_value_clear(&arguments[1]);
        rexx_value_clear(&result);
        failed += check(rexx_vm_test_allocator_live_blocks() == live_before,
                        "RX17 allocation failure/recovery leaks no buffers");
    }
    return failed;
}
#endif

int main(void)
{
    int failed = 0;
    failed += test_reference_vectors();
    failed += test_binary_boundaries();
    failed += test_argument_errors();
    failed += test_random_oracle();
    failed += test_equal_length_identities();
    failed += test_maximum_runtime_string();
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    failed += test_allocation_failure_recovery();
#endif
    printf("RX17_DIRECT_ASSERTIONS=%zu\n", assertion_count);
    printf("RX17_DIRECT_CASES=%zu\n", dispatch_case_count);
    printf("RX17_PROPERTY_CASES=%zu\n", property_case_count);
    printf("RX17_PROPERTY_SEED=0x52583137\n");
    printf("RX17_FAILURES=%zu\n", failure_count);
    return failed == 0 ? 0 : 1;
}

