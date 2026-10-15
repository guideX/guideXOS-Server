#include "../rexx_numeric.h"

#include <stdio.h>
#include <string.h>

static int assertions;

#define CHECK(condition, label) do { \
    ++assertions; \
    if (!(condition)) { \
        fprintf(stderr, "[FAIL] %s (line %d)\n", label, __LINE__); \
        return 1; \
    } \
} while (0)

static RexxNumericStatus parse(const char *text, RexxDecimal *value)
{
    return rexx_decimal_parse(text, strlen(text), value);
}

static int check_binary(const char *left_text,
                       const char *right_text,
                       size_t precision,
                       RexxNumericStatus (*operation)(const RexxDecimal *,
                                                      const RexxDecimal *,
                                                      size_t,
                                                      RexxDecimal *),
                       const char *expected,
                       const char *label)
{
    RexxDecimal left, right, result;
    char output[1024];
    int written;
    if (parse(left_text, &left) != REXX_NUMERIC_OK ||
        parse(right_text, &right) != REXX_NUMERIC_OK ||
        operation(&left, &right, precision, &result) != REXX_NUMERIC_OK) {
        fprintf(stderr, "[FAIL] %s (numeric operation failed)\n", label);
        return 0;
    }
    written = rexx_decimal_format(output, sizeof(output), &result, precision);
    ++assertions;
    if (written < 0 || strcmp(output, expected) != 0) {
        fprintf(stderr, "[FAIL] %s: expected %s, got %s\n", label,
                expected, written < 0 ? "<format error>" : output);
        return 0;
    }
    return 1;
}

int main(void)
{
    RexxDecimal value, other;
    uint32_t integer;
    char output[1024];
    char oversized[600];
    int written;

    CHECK(check_binary("0.1", "0.2", 9u, rexx_decimal_add,
                       "0.3", "decimal addition"), "addition helper");
    CHECK(check_binary("1.25", "2.75", 9u, rexx_decimal_add,
                       "4.00", "addition preserves scale"), "scale-preserving addition");
    CHECK(check_binary("10", "3", 9u, rexx_decimal_subtract,
                       "7", "decimal subtraction"), "subtraction helper");
    CHECK(check_binary("1.20", "3", 9u, rexx_decimal_multiply,
                       "3.60", "multiplication preserves scale"), "scale-preserving multiplication");
    CHECK(check_binary("1.234567", "1", 5u, rexx_decimal_multiply,
                       "1.2346", "precision rounding"), "precision rounding helper");
    CHECK(check_binary("-1.23455", "1", 5u, rexx_decimal_multiply,
                       "-1.2346", "half-up rounding away from zero"), "negative rounding helper");
    CHECK(check_binary("9.999", "1", 2u, rexx_decimal_multiply,
                       "10", "rounding carry across decimal exponent"), "rounding carry helper");
    CHECK(check_binary("12345678901234567890", "0", 9u, rexx_decimal_add,
                       "1.23456789E+19", "large integer formatting"), "large integer helper");
    CHECK(check_binary("1e-20", "0", 9u, rexx_decimal_add,
                       "1E-20", "small scientific formatting"), "small fraction helper");

    CHECK(parse("  - 12.50  ", &value) == REXX_NUMERIC_OK,
          "leading and trailing blanks around a number");
    CHECK(parse("1e+", &value) == REXX_NUMERIC_INVALID,
          "incomplete exponent is rejected");
    CHECK(parse("-0.000", &value) == REXX_NUMERIC_OK && value.sign == 0,
          "negative zero canonicalizes in decimal arithmetic");
    written = rexx_decimal_format(output, sizeof(output), &value, 9u);
    CHECK(written == 1 && strcmp(output, "0") == 0,
          "arithmetic zero is formatted as one digit");
    CHECK(parse("1e3", &value) == REXX_NUMERIC_OK &&
          parse("1000.0", &other) == REXX_NUMERIC_OK &&
          rexx_decimal_compare(&value, &other) == 0,
          "exponential and plain decimal forms compare equally");
    CHECK(parse("12.000", &value) == REXX_NUMERIC_OK &&
          rexx_decimal_to_uint32(&value, &integer) == REXX_NUMERIC_OK &&
          integer == 12u, "whole-number conversion accepts zero fractional digits");
    CHECK(parse("12.01", &value) == REXX_NUMERIC_OK &&
          rexx_decimal_to_uint32(&value, &integer) == REXX_NUMERIC_INVALID,
          "whole-number conversion rejects fractional values");
    memset(oversized, '7', sizeof(oversized));
    CHECK(rexx_decimal_parse(oversized, sizeof(oversized), &value) ==
              REXX_NUMERIC_RESOURCE_LIMIT,
          "decimal input limit is enforced");

    printf("RX3 decimal assertions passed: %d\n", assertions);
    return 0;
}
