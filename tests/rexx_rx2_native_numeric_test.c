#include "../rexx_numeric.h"

#include <stdio.h>
#include <string.h>

static int assertions;

#define CHECK(condition, name) do { \
    ++assertions; \
    if (!(condition)) { \
        fprintf(stderr, "[FAIL] %s (line %d)\n", name, __LINE__); \
        return 1; \
    } \
} while (0)

static int check_format(double value, const char *expected)
{
    char buffer[64];
    int length = rexx_numeric_format(buffer, sizeof(buffer), value);
    ++assertions;
    if (length < 0 || strcmp(buffer, expected) != 0) {
        fprintf(stderr, "[FAIL] numeric format %.17g: expected %s, got %s\n",
                value, expected, length < 0 ? "<error>" : buffer);
        return 0;
    }
    return 1;
}

int main(void)
{
    char *end = NULL;
    double value = 0.0;
    CHECK(rexx_numeric_parse(" -12.5e2 tail", &end, &value) == REXX_NUMERIC_OK,
          "native decimal and exponent parse");
    CHECK(value == -1250.0 && strcmp(end, " tail") == 0,
          "native parser reports consumed bytes");
    CHECK(rexx_numeric_parse("1e9999", &end, &value) == REXX_NUMERIC_RANGE,
          "native overflow is rejected");
    CHECK(rexx_numeric_parse("1e-9999", &end, &value) == REXX_NUMERIC_RANGE,
          "native underflow is rejected");
    CHECK(rexx_numeric_parse(".25", &end, &value) == REXX_NUMERIC_OK &&
          value == 0.25, "native leading-decimal form");
    CHECK(rexx_numeric_parse("7.", &end, &value) == REXX_NUMERIC_OK &&
          value == 7.0, "native trailing-decimal form");
    CHECK(rexx_numeric_parse("1e+", &end, &value) == REXX_NUMERIC_OK &&
          value == 1.0 && *end == 'e', "malformed exponent remains unconsumed");
    CHECK(check_format(0.0, "0"), "format zero");
    CHECK(check_format(-0.0, "-0"), "format negative zero");
    CHECK(check_format(123.456, "123.456"), "format decimal");
    CHECK(check_format(0.0000123, "1.23e-5"), "format small scientific value");
    CHECK(check_format(1000000000000000.0, "1e+15"), "format large scientific value");
    CHECK(rexx_numeric_is_integral(-12.0) && !rexx_numeric_is_integral(12.5),
          "native integral test");
    printf("RX2 freestanding numeric assertions passed: %d\n", assertions);
    return 0;
}
