#include "../rexx_builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t assertion_count;
static size_t dispatch_case_count;
static size_t property_case_count;
static size_t failure_count;
static uint32_t random_state = UINT32_C(0x52583138);

static int check(int condition, const char *description)
{
    ++assertion_count;
    if (condition) return 0;
    ++failure_count;
    fprintf(stderr, "FAIL: %s\n", description);
    return 1;
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

static RexxVMStatus set_bytes(RexxValue *value,
                              const unsigned char *bytes, size_t length)
{
    static const unsigned char empty[] = { 0u };
    if (bytes == NULL) bytes = empty;
    return rexx_value_set_string(value, (const char *)bytes, length);
}

static int run_abbrev_case(const unsigned char *information,
                           size_t information_length,
                           const unsigned char *info, size_t info_length,
                           const char *minimum, int expected,
                           const char *description)
{
    static const unsigned char empty[] = { 0u };
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    const unsigned char *expected_bytes =
        (const unsigned char *)(expected ? "1" : "0");
    size_t argument_count = minimum == NULL ? 2u : 3u;
    RexxVMStatus status;
    int failed = 0;

    if (information == NULL) information = empty;
    if (info == NULL) info = empty;
    status = set_bytes(&arguments[0], information, information_length);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX18 ABBREV information allocation succeeds");
    if (status == REXX_VM_STATUS_OK)
        status = set_bytes(&arguments[1], info, info_length);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX18 ABBREV info allocation succeeds");
    if (status == REXX_VM_STATUS_OK && minimum != NULL)
        status = rexx_value_set_string(&arguments[2], minimum,
                                       strlen(minimum));
    if (minimum != NULL)
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX18 ABBREV minimum allocation succeeds");
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch("ABBREV", 6u, arguments,
                                       argument_count, &result);
        ++dispatch_case_count;
    }
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX18 ABBREV dispatch succeeds");
    failed += check_string(&result, expected_bytes, 1u, description);
    failed += check(arguments[0].type == REXX_VALUE_STRING &&
                    arguments[0].as.string.length == information_length &&
                    (information_length == 0u ||
                     memcmp(arguments[0].as.string.bytes, information,
                            information_length) == 0) &&
                    arguments[1].type == REXX_VALUE_STRING &&
                    arguments[1].as.string.length == info_length &&
                    (info_length == 0u ||
                     memcmp(arguments[1].as.string.bytes, info,
                            info_length) == 0),
                    "RX18 ABBREV preserves both input byte strings");
    if (minimum != NULL) {
        failed += check(arguments[2].type == REXX_VALUE_STRING &&
                        arguments[2].as.string.length == strlen(minimum) &&
                        memcmp(arguments[2].as.string.bytes, minimum,
                               strlen(minimum)) == 0,
                        "RX18 ABBREV preserves the minimum-length operand");
    }
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    rexx_value_clear(&result);
    return failed;
}

static void reference_center(const unsigned char *source,
                             size_t source_length, size_t result_length,
                             unsigned char pad, unsigned char *expected)
{
    size_t index;
    if (result_length >= source_length) {
        size_t first_source_column = (result_length - source_length) / 2u;
        for (index = 0u; index < result_length; ++index) {
            if (index >= first_source_column &&
                index - first_source_column < source_length) {
                expected[index] = source[index - first_source_column];
            } else {
                expected[index] = pad;
            }
        }
    } else {
        size_t first_retained_source_column =
            (source_length - result_length) / 2u;
        for (index = 0u; index < result_length; ++index)
            expected[index] = source[first_retained_source_column + index];
    }
}

static int dispatch_center(const char *name,
                           const unsigned char *source, size_t source_length,
                           size_t result_length, int include_pad,
                           unsigned char pad, RexxValue *result)
{
    static const unsigned char empty[] = { 0u };
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    char length_text[32];
    int length_written;
    int failed = 0;
    size_t argument_count = include_pad ? 3u : 2u;
    RexxVMStatus status;

    if (source == NULL) source = empty;
    status = set_bytes(&arguments[0], source, source_length);
    length_written = snprintf(length_text, sizeof(length_text), "%zu",
                              result_length);
    if (status == REXX_VM_STATUS_OK && length_written > 0 &&
        (size_t)length_written < sizeof(length_text)) {
        status = rexx_value_set_string(&arguments[1], length_text,
                                       (size_t)length_written);
    } else if (status == REXX_VM_STATUS_OK) {
        status = REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    if (status == REXX_VM_STATUS_OK && include_pad)
        status = rexx_value_set_string(&arguments[2], (const char *)&pad, 1u);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch(name, strlen(name), arguments,
                                       argument_count, result);
        ++dispatch_case_count;
    }
    if (status == REXX_VM_STATUS_OK) {
        char expected_length[32];
        int written = snprintf(expected_length, sizeof(expected_length), "%zu",
                               result_length);
        failed += check(arguments[0].type == REXX_VALUE_STRING &&
                        arguments[0].as.string.length == source_length &&
                        (source_length == 0u ||
                         memcmp(arguments[0].as.string.bytes, source,
                                source_length) == 0),
                        "RX18 CENTER/CENTRE preserve the source bytes");
        failed += check(written > 0 && (size_t)written < sizeof(expected_length) &&
                        arguments[1].type == REXX_VALUE_STRING &&
                        arguments[1].as.string.length == (size_t)written &&
                        memcmp(arguments[1].as.string.bytes, expected_length,
                               (size_t)written) == 0,
                        "RX18 CENTER/CENTRE preserve the requested length operand");
        if (include_pad)
            failed += check(arguments[2].type == REXX_VALUE_STRING &&
                            arguments[2].as.string.length == 1u &&
                            (unsigned char)arguments[2].as.string.bytes[0] == pad,
                            "RX18 CENTER/CENTRE preserve the pad byte");
    }
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    return status == REXX_VM_STATUS_OK ? (failed == 0 ? 0 : -1) : (int)status;
}

static int run_center_case(const unsigned char *source, size_t source_length,
                           size_t result_length, int include_pad,
                           unsigned char pad,
                           const unsigned char *expected,
                           const char *description)
{
    RexxValue center = REXX_VALUE_INIT;
    RexxValue centre = REXX_VALUE_INIT;
    int failed = 0;
    int center_status = dispatch_center("CENTER", source, source_length,
                                        result_length, include_pad, pad,
                                        &center);
    int centre_status = dispatch_center("CENTRE", source, source_length,
                                        result_length, include_pad, pad,
                                        &centre);
    failed += check(center_status == REXX_VM_STATUS_OK &&
                    centre_status == REXX_VM_STATUS_OK,
                    "RX18 CENTER and CENTRE dispatch successfully");
    failed += check_string(&center, expected, result_length, description);
    failed += check_string(&centre, expected, result_length,
                           "RX18 CENTRE matches the independent expected bytes");
    failed += check(center.type == centre.type &&
                    center.type == REXX_VALUE_STRING &&
                    center.as.string.length == centre.as.string.length &&
                    (result_length == 0u ||
                     memcmp(center.as.string.bytes, centre.as.string.bytes,
                            result_length) == 0),
                    "RX18 CENTER and CENTRE produce identical bytes");
    if (source_length != 0u && result_length > source_length) {
        size_t first_source_column = (result_length - source_length) / 2u;
        failed += check(memcmp(center.as.string.bytes + first_source_column,
                               source, source_length) == 0,
                        "RX18 centering retains the source bytes unchanged");
    }
    rexx_value_clear(&center);
    rexx_value_clear(&centre);
    return failed;
}

static int test_reference_vectors(void)
{
    static const unsigned char empty[] = { 0u };
    static const unsigned char abc[] = { 'a', 'b', 'c' };
    static const unsigned char abc7[] = { ' ', ' ', 'a', 'b', 'c', ' ', ' ' };
    static const unsigned char abc8dash[] = { '-', '-', 'a', 'b', 'c', '-', '-', '-' };
    static const unsigned char abc6dash[] = { '-', 'a', 'b', 'c', '-', '-' };
    static const unsigned char blue8[] = { 'e', ' ', 'b', 'l', 'u', 'e', ' ', 's' };
    static const unsigned char blue7[] = { 'e', ' ', 'b', 'l', 'u', 'e', ' ', ' ' };
    static const unsigned char bcd[] = { 'b', 'c', 'd' };
    static const unsigned char empty4dash[] = { '-', '-', '-', '-' };
    static const unsigned char nul6[] = { 0u, 'a', 'b', 'c', 0u, 0u };
    static const unsigned char nul8[] = { 0u, 0u, 'a', 'b', 'c', 0u, 0u, 0u };
    static const unsigned char high_pad[] = { 0xffu, 'a', 0xffu };
    static const unsigned char binary_source[] = { 0x00u, 0xffu, 0x80u, 0x41u };
    static const unsigned char binary_center[] = { 0xffu, 0x80u };
    static const unsigned char blanks[] = { ' ', ' ', 'x', ' ' };
    int failed = 0;

    failed += run_abbrev_case((const unsigned char *)"Print", 5u,
                              (const unsigned char *)"Pri", 3u, NULL, 1,
                              "ABBREV matches an exact case-sensitive prefix");
    failed += run_abbrev_case((const unsigned char *)"PRINT", 5u,
                              (const unsigned char *)"Pri", 3u, NULL, 0,
                              "ABBREV compares bytes case-sensitively");
    failed += run_abbrev_case((const unsigned char *)"PRINT", 5u,
                              (const unsigned char *)"PRI", 3u, "4", 0,
                              "ABBREV enforces the explicit minimum length");
    failed += run_abbrev_case((const unsigned char *)"PRINT", 5u,
                              (const unsigned char *)"PRY", 3u, NULL, 0,
                              "ABBREV rejects a nonmatching leading byte");
    failed += run_abbrev_case((const unsigned char *)"PRINT", 5u,
                              empty, 0u, NULL, 1,
                              "ABBREV accepts the default empty abbreviation");
    failed += run_abbrev_case((const unsigned char *)"PRINT", 5u,
                              empty, 0u, "1", 0,
                              "ABBREV rejects an empty abbreviation below its minimum");
    failed += run_abbrev_case((const unsigned char *)"abcdef", 6u,
                              (const unsigned char *)"abc", 3u, "2", 1,
                              "ABBREV accepts a minimum shorter than info");
    failed += run_abbrev_case((const unsigned char *)"abcdef", 6u,
                              (const unsigned char *)"abc", 3u, "4", 0,
                              "ABBREV rejects a minimum longer than info");
    failed += run_abbrev_case((const unsigned char *)"abc", 3u,
                              (const unsigned char *)"abcd", 4u, NULL, 0,
                              "ABBREV rejects info longer than information");
    failed += run_abbrev_case(empty, 0u, empty, 0u, NULL, 1,
                              "ABBREV matches two empty strings");
    failed += run_abbrev_case(empty, 0u, (const unsigned char *)"A", 1u,
                              NULL, 0, "ABBREV rejects a nonempty info on empty input");
    failed += run_abbrev_case((const unsigned char *)"\x00\xff\x80", 3u,
                              (const unsigned char *)"\x00\xff", 2u,
                              NULL, 1, "ABBREV compares embedded NUL and high-bit bytes");
    failed += run_abbrev_case((const unsigned char *)"\x00\xff\x80", 3u,
                              (const unsigned char *)"\x00\xff\x81", 3u,
                              NULL, 0, "ABBREV rejects unequal binary bytes");
    failed += run_abbrev_case((const unsigned char *)"abcdef", 6u,
                              (const unsigned char *)"abc", 3u,
                              "4294967296", 0,
                              "ABBREV returns false for an unrepresentably large minimum");
    failed += run_abbrev_case((const unsigned char *)"abcdef", 6u,
                              (const unsigned char *)"abc", 3u,
                              "999999999999999999999999999", 0,
                              "ABBREV returns false for a very large integer minimum");

    failed += run_center_case(abc, sizeof(abc), 7u, 0, 0u,
                              abc7, "CENTER pads two bytes on the left and right");
    failed += run_center_case(abc, sizeof(abc), 7u, 1, ' ',
                              abc7, "CENTER accepts an explicit ASCII-space pad");
    failed += run_center_case(abc, sizeof(abc), 8u, 1, '-',
                              abc8dash, "CENTER puts the odd extra pad on the right");
    failed += run_center_case(abc, sizeof(abc), 6u, 1, '-',
                              abc6dash, "CENTER distributes odd padding to the right");
    failed += run_center_case((const unsigned char *)"The blue sky", 12u,
                              8u, 0, 0u, blue8,
                              "CENTRE truncates equal counts from each side");
    failed += run_center_case((const unsigned char *)"The blue sky", 12u,
                              7u, 0, 0u, blue7,
                              "CENTRE removes the odd extra source byte on the right");
    failed += run_center_case((const unsigned char *)"abcdef", 6u,
                              3u, 0, 0u, bcd,
                              "CENTER selects the centered three-byte substring");
    failed += run_center_case(abc, sizeof(abc), 3u, 0, 0u,
                              abc, "CENTER preserves an equal-length source");
    failed += run_center_case(abc, sizeof(abc), 0u, 0, 0u,
                              empty, "CENTER returns zero bytes for length zero");
    failed += run_center_case(empty, 0u, 4u, 1, '-',
                              empty4dash, "CENTER pads an empty source");
    failed += run_center_case(abc, sizeof(abc), 6u, 1, 0u,
                              nul6, "CENTER preserves NUL padding");
    failed += run_center_case(abc, sizeof(abc), 8u, 1, 0u,
                              nul8, "CENTRE preserves NUL padding");
    failed += run_center_case((const unsigned char *)"a", 1u, 3u,
                              1, 0xffu, high_pad,
                              "CENTER preserves high-bit pad bytes");
    failed += run_center_case(binary_source, sizeof(binary_source), 2u,
                              0, 0u, binary_center,
                              "CENTRE retains binary source bytes while truncating");
    failed += run_center_case(blanks, sizeof(blanks), sizeof(blanks),
                              1, ' ', blanks,
                              "CENTER preserves significant leading and trailing blanks");
    return failed;
}

static RexxVMStatus dispatch_abbrev_text(const char *information,
                                         const char *info,
                                         const char *minimum,
                                         RexxValue *result)
{
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxVMStatus status;
    size_t count = minimum == NULL ? 2u : 3u;
    status = rexx_value_set_string(&arguments[0], information,
                                   strlen(information));
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], info, strlen(info));
    if (status == REXX_VM_STATUS_OK && minimum != NULL)
        status = rexx_value_set_string(&arguments[2], minimum,
                                       strlen(minimum));
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch("ABBREV", 6u, arguments, count,
                                       result);
        ++dispatch_case_count;
    }
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    return status;
}

static RexxVMStatus dispatch_center_text(const char *name, const char *source,
                                         const char *length,
                                         const unsigned char *pad,
                                         size_t pad_length,
                                         RexxValue *result)
{
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxVMStatus status;
    size_t count = pad == NULL ? 2u : 3u;
    status = rexx_value_set_string(&arguments[0], source, strlen(source));
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], length, strlen(length));
    if (status == REXX_VM_STATUS_OK && pad != NULL)
        status = rexx_value_set_string(&arguments[2], (const char *)pad,
                                       pad_length);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch(name, strlen(name), arguments, count,
                                       result);
        ++dispatch_case_count;
    }
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    return status;
}

static int test_invalid_arguments(void)
{
    static const unsigned char empty_pad[] = { 0u };
    static const unsigned char multi_pad[] = { 'x', 'y' };
    RexxValue result = REXX_VALUE_INIT;
    RexxValue arguments[4] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT,
        REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxVMStatus status;
    int failed = 0;

#define EXPECT_STATUS(expr, expected, label) do { \
    status = (expr); \
    ++dispatch_case_count; \
    failed += check(status == (expected) && result.type == REXX_VALUE_UNSET, label); \
    rexx_value_clear(&result); \
} while (0)

    EXPECT_STATUS(rexx_builtin_dispatch("ABBREV", 6u, NULL, 0u, &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 ABBREV rejects missing required arguments");
    EXPECT_STATUS(rexx_builtin_dispatch("CENTER", 6u, NULL, 0u, &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 CENTER rejects missing required arguments");
    EXPECT_STATUS(rexx_builtin_dispatch("CENTRE", 6u, arguments, 1u, &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 CENTRE rejects a missing required length");

    status = rexx_value_set_string(&arguments[0], "abc", 3u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "a", 1u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX18 invalid-call baseline arguments are created");
    EXPECT_STATUS(rexx_builtin_dispatch("ABBREV", 6u, arguments, 4u, &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 ABBREV rejects additional arguments");
    EXPECT_STATUS(rexx_builtin_dispatch("CENTER", 6u, arguments, 4u, &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 CENTER rejects additional arguments");
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);

    status = dispatch_abbrev_text("PRINT", "PRI", "-1", &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 ABBREV rejects a negative minimum length");
    status = dispatch_abbrev_text("PRINT", "PRI", "1.5", &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 ABBREV rejects a fractional minimum length");
    status = dispatch_abbrev_text("PRINT", "PRI", "", &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 ABBREV rejects an empty explicit minimum");
    status = dispatch_abbrev_text("PRINT", "PRI", "nope", &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_NUMERIC_ERROR &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 ABBREV rejects a nonnumeric minimum");

    status = dispatch_center_text("CENTER", "x", "-1", NULL, 0u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 CENTER rejects a negative result length");
    status = dispatch_center_text("CENTER", "x", "1.5", NULL, 0u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 CENTER rejects a fractional result length");
    status = dispatch_center_text("CENTRE", "x", "", NULL, 0u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 CENTRE rejects an empty explicit result length");
    status = dispatch_center_text("CENTER", "x", "not-a-number",
                                  NULL, 0u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_NUMERIC_ERROR &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 CENTER rejects a nonnumeric result length");
    status = dispatch_center_text("CENTER", "x", "1", empty_pad,
                                  0u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 CENTER rejects an empty pad");
    status = dispatch_center_text("CENTRE", "x", "1", multi_pad,
                                  sizeof(multi_pad), &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 CENTRE rejects a multibyte pad");

    arguments[0].type = REXX_VALUE_STRING;
    arguments[0].as.string.bytes = (char *)"PRINT";
    arguments[0].as.string.length = 5u;
    arguments[1].type = REXX_VALUE_UNSET;
    EXPECT_STATUS(rexx_builtin_dispatch("ABBREV", 6u, arguments, 2u,
                                        &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 ABBREV rejects an omitted required info slot");
    arguments[0].type = REXX_VALUE_UNSET;
    arguments[1].type = REXX_VALUE_STRING;
    arguments[1].as.string.bytes = (char *)"P";
    arguments[1].as.string.length = 1u;
    EXPECT_STATUS(rexx_builtin_dispatch("ABBREV", 6u, arguments, 2u,
                                        &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 ABBREV rejects an omitted required information slot");
    arguments[0].type = REXX_VALUE_UNSET;
    arguments[1].type = REXX_VALUE_UNSET;

    arguments[0].type = REXX_VALUE_STRING;
    arguments[0].as.string.bytes = (char *)"x";
    arguments[0].as.string.length = 1u;
    arguments[1].type = REXX_VALUE_UNSET;
    EXPECT_STATUS(rexx_builtin_dispatch("CENTER", 6u, arguments, 2u,
                                        &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 CENTER rejects an omitted required length slot");
    arguments[0].type = REXX_VALUE_UNSET;
    arguments[1].type = REXX_VALUE_STRING;
    arguments[1].as.string.bytes = (char *)"1";
    arguments[1].as.string.length = 1u;
    EXPECT_STATUS(rexx_builtin_dispatch("CENTRE", 6u, arguments, 2u,
                                        &result),
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                  "RX18 CENTRE rejects an omitted required source slot");
    arguments[0].type = REXX_VALUE_UNSET;
    arguments[1].type = REXX_VALUE_UNSET;

    status = dispatch_center_text("CENTER", "x", "4294967296",
                                  NULL, 0u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_RESOURCE_LIMIT &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 CENTER rejects a result larger than the runtime limit");
    status = rexx_builtin_dispatch("CENTER", 6u, arguments, 2u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "RX18 invalid calls leave the result uninitialized");

    rexx_value_clear(&result);
#undef EXPECT_STATUS
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

static int reference_abbrev(const unsigned char *information,
                            size_t information_length,
                            const unsigned char *info, size_t info_length,
                            size_t minimum_length)
{
    size_t index;
    if (info_length > information_length || info_length < minimum_length)
        return 0;
    for (index = 0u; index < info_length; ++index)
        if (information[index] != info[index]) return 0;
    return 1;
}

static int test_abbrev_properties(void)
{
    unsigned char information[64];
    unsigned char info[64];
    unsigned int case_index;
    int failed = 0;

    for (case_index = 0u; case_index < 384u; ++case_index) {
        size_t information_length = (size_t)(next_random() % 65u);
        size_t info_length = (size_t)(next_random() % 65u);
        size_t minimum;
        fill_random(information, information_length);
        fill_random(info, info_length);
        if (case_index % 3u == 0u && info_length <= information_length &&
            info_length != 0u) {
            memcpy(information, info, info_length);
        }
        for (minimum = 0u; minimum <= info_length + 1u; ++minimum) {
            char minimum_text[24];
            int written = snprintf(minimum_text, sizeof(minimum_text), "%zu",
                                   minimum);
            int expected = reference_abbrev(information, information_length,
                                            info, info_length, minimum);
            if (written <= 0 || (size_t)written >= sizeof(minimum_text)) {
                failed += check(0, "RX18 property minimum formats safely");
                continue;
            }
            failed += run_abbrev_case(information, information_length, info,
                                      info_length, minimum_text, expected,
                                      "RX18 ABBREV property matches bytewise oracle");
            ++property_case_count;
        }
    }
    return failed;
}

static int test_center_properties(void)
{
    static const size_t boundaries[] = {
        0u, 1u, 2u, 3u, 127u, 128u, 255u, 256u, 1024u, 4096u
    };
    unsigned char source[4096];
    unsigned char expected[4096];
    size_t source_index, target_index;
    unsigned int case_index;
    int failed = 0;

    for (source_index = 0u;
         source_index < sizeof(boundaries) / sizeof(boundaries[0]);
         ++source_index) {
        for (target_index = 0u;
             target_index < sizeof(boundaries) / sizeof(boundaries[0]);
             ++target_index) {
            size_t source_length = boundaries[source_index];
            size_t result_length = boundaries[target_index];
            unsigned char pad = (unsigned char)(source_index * 31u +
                                                target_index * 17u);
            int include_pad = (int)((source_index + target_index) % 2u);
            fill_random(source, source_length);
            reference_center(source, source_length, result_length,
                             include_pad ? pad : (unsigned char)' ',
                             expected);
            failed += run_center_case(source, source_length, result_length,
                                      include_pad,
                                      pad, expected,
                                      "RX18 boundary property matches center oracle");
            ++property_case_count;
        }
    }

    for (case_index = 0u; case_index < 512u; ++case_index) {
        size_t source_length = (size_t)(next_random() % 1025u);
        size_t result_length = (size_t)(next_random() % 1025u);
        unsigned char pad = (unsigned char)(next_random() >> 24);
        int include_pad = (int)(next_random() & 1u);
        fill_random(source, source_length);
        reference_center(source, source_length, result_length,
                         include_pad ? pad : (unsigned char)' ', expected);
        failed += run_center_case(source, source_length, result_length,
                                  include_pad, pad, expected,
                                  "RX18 randomized property matches center oracle");
        ++property_case_count;
    }
    return failed;
}

static int test_maximum_result_and_resource_recovery(void)
{
    size_t maximum = REXX_BUILTIN_MAX_RESULT_LENGTH;
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int failed = 0;

    status = rexx_value_set_string(&argument, "", 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX18 empty maximum-length source is created");
    status = rexx_builtin_dispatch("CENTER", 6u, &argument, 0u, &result);
    ++dispatch_case_count;
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX18 invalid invocation before maximum-size request is handled");
    rexx_value_clear(&result);

    {
        RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
        char number[32];
        int written;
        char *bytes = NULL;
        status = rexx_value_set_string(&arguments[0], "", 0u);
        written = snprintf(number, sizeof(number), "%zu", maximum);
        if (status == REXX_VM_STATUS_OK && written > 0 &&
            (size_t)written < sizeof(number))
            status = rexx_value_set_string(&arguments[1], number,
                                           (size_t)written);
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX18 maximum output arguments are created");
        if (status == REXX_VM_STATUS_OK) {
            status = rexx_builtin_dispatch("CENTRE", 6u, arguments, 2u,
                                           &result);
            ++dispatch_case_count;
            failed += check(status == REXX_VM_STATUS_OK &&
                            result.type == REXX_VALUE_STRING &&
                            result.as.string.length == maximum,
                            "RX18 accepts the existing 16 MiB runtime string limit");
            if (status == REXX_VM_STATUS_OK) {
                bytes = result.as.string.bytes;
                failed += check((unsigned char)bytes[0] == (unsigned char)' ' &&
                                (unsigned char)bytes[maximum - 1u] ==
                                    (unsigned char)' ',
                                "RX18 maximum result is filled within its exact boundary");
            }
        }
        rexx_value_clear(&arguments[0]);
        rexx_value_clear(&arguments[1]);
        rexx_value_clear(&result);
    }

    {
        char too_large[32];
        int written = snprintf(too_large, sizeof(too_large), "%zu", maximum + 1u);
        RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
        if (written > 0 && (size_t)written < sizeof(too_large)) {
            status = rexx_value_set_string(&arguments[0], "x", 1u);
            if (status == REXX_VM_STATUS_OK)
                status = rexx_value_set_string(&arguments[1], too_large,
                                               (size_t)written);
            if (status == REXX_VM_STATUS_OK) {
                status = rexx_builtin_dispatch("CENTER", 6u, arguments, 2u,
                                               &result);
                ++dispatch_case_count;
            }
            failed += check(status == REXX_VM_STATUS_RESOURCE_LIMIT &&
                            result.type == REXX_VALUE_UNSET,
                            "RX18 rejects a result one byte beyond the runtime limit");
            rexx_value_clear(&arguments[0]);
            rexx_value_clear(&arguments[1]);
            rexx_value_clear(&result);
        } else {
            failed += check(0, "RX18 oversized result length formats safely");
        }
    }
    failed += run_center_case((const unsigned char *)"x", 1u, 3u, 0, 0u,
                              (const unsigned char *)" x ",
                              "RX18 valid centering recovers after resource error");
    rexx_value_clear(&argument);
    rexx_value_clear(&result);
    return failed;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static int test_allocation_failure_recovery(void)
{
    static const unsigned char information[] = { 'a', 'b', 'c' };
    static const unsigned char source[] = { 0x00u, 'a', 0xffu };
    static const unsigned char expected[] = { ' ', 0x00u, 'a', 0xffu, ' ' };
    const size_t live_before = rexx_vm_test_allocator_live_blocks();
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    size_t live_with_arguments;
    RexxVMStatus status;
    int failed = 0;

    status = rexx_value_set_string(&arguments[0], "abc", 3u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "a", 1u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX18 ABBREV allocation-failure inputs are created");
    if (status == REXX_VM_STATUS_OK) {
        live_with_arguments = rexx_vm_test_allocator_live_blocks();
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch("ABBREV", 6u, arguments, 2u, &result);
        rexx_vm_test_allocator_disable();
        ++dispatch_case_count;
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET,
                        "RX18 ABBREV allocation failure publishes no result");
        failed += check(rexx_vm_test_allocator_live_blocks() == live_with_arguments &&
                        arguments[0].as.string.length == sizeof(information) &&
                        memcmp(arguments[0].as.string.bytes, information,
                               sizeof(information)) == 0,
                        "RX18 ABBREV allocation failure preserves inputs and frees result state");
        status = rexx_builtin_dispatch("ABBREV", 6u, arguments, 2u, &result);
        ++dispatch_case_count;
        failed += check(status == REXX_VM_STATUS_OK &&
                        result.type == REXX_VALUE_STRING &&
                        result.as.string.length == 1u &&
                        result.as.string.bytes[0] == '1',
                        "RX18 ABBREV succeeds after allocation failure");
        rexx_value_clear(&arguments[0]);
        rexx_value_clear(&arguments[1]);
        rexx_value_clear(&result);
        failed += check(rexx_vm_test_allocator_live_blocks() == live_before,
                        "RX18 ABBREV failure and recovery leak no allocations");
    }

    status = set_bytes(&arguments[0], source, sizeof(source));
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "5", 1u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX18 CENTER allocation-failure inputs are created");
    if (status == REXX_VM_STATUS_OK) {
        live_with_arguments = rexx_vm_test_allocator_live_blocks();
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch("CENTER", 6u, arguments, 2u, &result);
        rexx_vm_test_allocator_disable();
        ++dispatch_case_count;
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET,
                        "RX18 CENTER allocation failure publishes no partial result");
        failed += check(rexx_vm_test_allocator_live_blocks() == live_with_arguments &&
                        arguments[0].as.string.length == sizeof(source) &&
                        memcmp(arguments[0].as.string.bytes, source,
                               sizeof(source)) == 0,
                        "RX18 CENTER allocation failure preserves binary input bytes");
        status = rexx_builtin_dispatch("CENTRE", 6u, arguments, 2u, &result);
        ++dispatch_case_count;
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX18 CENTRE succeeds after allocation failure");
        failed += check_string(&result, expected, sizeof(expected),
                               "RX18 recovered CENTRE result retains binary bytes");
        rexx_value_clear(&arguments[0]);
        rexx_value_clear(&arguments[1]);
        rexx_value_clear(&result);
        failed += check(rexx_vm_test_allocator_live_blocks() == live_before,
                        "RX18 CENTER failure and recovery leak no allocations");
    }
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    rexx_value_clear(&result);
    return failed;
}
#endif

int main(void)
{
    int failed = 0;
    failed += test_reference_vectors();
    failed += test_invalid_arguments();
    failed += test_abbrev_properties();
    failed += test_center_properties();
    failed += test_maximum_result_and_resource_recovery();
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    failed += test_allocation_failure_recovery();
#endif
    printf("RX18_DIRECT_ASSERTIONS=%zu\n", assertion_count);
    printf("RX18_DIRECT_CASES=%zu\n", dispatch_case_count);
    printf("RX18_PROPERTY_CASES=%zu\n", property_case_count);
    printf("RX18_PROPERTY_SEED=0x52583138\n");
    printf("RX18_FAILURES=%zu\n", failure_count);
    return failed == 0 ? 0 : 1;
}
