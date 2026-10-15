#include "rexx_numeric.h"

#include <stdint.h>

#if !defined(GXOS_BARE_METAL)
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

RexxNumericStatus rexx_numeric_parse(const char *text,
                                     char **end_out,
                                     double *value_out)
{
    char *end;
    double value;

    if (end_out != NULL) *end_out = (char *)text;
    if (text == NULL || value_out == NULL) return REXX_NUMERIC_INVALID;

    errno = 0;
    value = strtod(text, &end);
    if (end_out != NULL) *end_out = end;
    if (end == text) return REXX_NUMERIC_INVALID;
    if (errno == ERANGE) return REXX_NUMERIC_RANGE;
    *value_out = value;
    return REXX_NUMERIC_OK;
}

int rexx_numeric_format(char *output,
                        size_t output_capacity,
                        double value)
{
    int written;

    if (output == NULL || output_capacity == 0u) return -1;
    written = snprintf(output, output_capacity, "%.15g", value);
    if (written < 0 || (size_t)written >= output_capacity) return -1;
    return written;
}

int rexx_numeric_is_finite(double value)
{
    return isfinite(value) != 0;
}

int rexx_numeric_is_integral(double value)
{
    const double signed_limit = 9223372036854775808.0;
    int64_t integer;
    if (!rexx_numeric_is_finite(value)) return 0;
    if (value >= signed_limit || value < -signed_limit) return 1;
    integer = (int64_t)value;
    return (double)integer == value;
}

#else

#include <float.h>
#include <stdint.h>

static int rexx_is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f' || value == '\v';
}

static int rexx_is_digit(char value)
{
    return value >= '0' && value <= '9';
}

RexxNumericStatus rexx_numeric_parse(const char *text,
                                     char **end_out,
                                     double *value_out)
{
    const char *cursor;
    const char *number_end;
    uint64_t mantissa = 0u;
    size_t kept_digits = 0u;
    size_t fractional_digits = 0u;
    size_t dropped_digits = 0u;
    int negative = 0;
    int after_decimal = 0;
    int saw_digit = 0;
    int saw_nonzero = 0;
    int exponent_sign = 1;
    int exponent_value = 0;
    int64_t decimal_scale;
    double value;

    if (end_out != NULL) *end_out = (char *)text;
    if (text == NULL || value_out == NULL) return REXX_NUMERIC_INVALID;

    cursor = text;
    while (rexx_is_space(*cursor)) ++cursor;
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        ++cursor;
    }

    while (rexx_is_digit(*cursor) || (!after_decimal && *cursor == '.')) {
        if (*cursor == '.') {
            after_decimal = 1;
            ++cursor;
            continue;
        }

        saw_digit = 1;
        if (after_decimal) ++fractional_digits;
        if (*cursor != '0') saw_nonzero = 1;
        if (saw_nonzero && kept_digits < 19u) {
            mantissa = mantissa * UINT64_C(10) +
                       (uint64_t)(*cursor - '0');
            ++kept_digits;
        } else if (saw_nonzero) {
            ++dropped_digits;
        }
        ++cursor;
    }

    if (!saw_digit) return REXX_NUMERIC_INVALID;
    number_end = cursor;

    if (*cursor == 'e' || *cursor == 'E') {
        const char *exponent_start = cursor;
        const char *digits;

        ++cursor;
        if (*cursor == '+' || *cursor == '-') {
            exponent_sign = *cursor == '-' ? -1 : 1;
            ++cursor;
        }
        digits = cursor;
        while (rexx_is_digit(*cursor)) {
            if (exponent_value < 100000) {
                exponent_value = exponent_value * 10 + (*cursor - '0');
                if (exponent_value > 100000) exponent_value = 100000;
            }
            ++cursor;
        }
        if (cursor == digits) {
            cursor = (char *)exponent_start;
        } else {
            number_end = cursor;
        }
    }

    if (end_out != NULL) *end_out = (char *)number_end;
    if (!saw_nonzero) {
        *value_out = negative ? -0.0 : 0.0;
        return REXX_NUMERIC_OK;
    }

    decimal_scale = (int64_t)exponent_sign * exponent_value -
                    (int64_t)fractional_digits + (int64_t)dropped_digits;
    if (decimal_scale > 400 || decimal_scale < -400) {
        return REXX_NUMERIC_RANGE;
    }

    value = (double)mantissa;
    while (decimal_scale > 0) {
        if (value > DBL_MAX / 10.0) return REXX_NUMERIC_RANGE;
        value *= 10.0;
        --decimal_scale;
    }
    while (decimal_scale < 0) {
        value /= 10.0;
        ++decimal_scale;
        if (value == 0.0) return REXX_NUMERIC_RANGE;
    }

    if (!rexx_numeric_is_finite(value)) return REXX_NUMERIC_RANGE;
    *value_out = negative ? -value : value;
    return REXX_NUMERIC_OK;
}

static int append_char(char *output, size_t capacity, size_t *length, char value)
{
    if (*length + 1u >= capacity) return 0;
    output[(*length)++] = value;
    output[*length] = '\0';
    return 1;
}

static int append_unsigned(char *output,
                           size_t capacity,
                           size_t *length,
                           unsigned value)
{
    char reverse[12];
    size_t count = 0u;

    do {
        reverse[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(reverse));

    while (count != 0u) {
        if (!append_char(output, capacity, length, reverse[--count])) return 0;
    }
    return 1;
}

int rexx_numeric_format(char *output,
                        size_t output_capacity,
                        double value)
{
    union {
        double number;
        uint64_t bits;
    } representation;
    char digits[15];
    double magnitude;
    double scaled;
    uint64_t rounded;
    int exponent = 0;
    int digit_count = 15;
    int scientific;
    size_t length = 0u;
    size_t index;
    int negative;

    if (output == NULL || output_capacity == 0u ||
        !rexx_numeric_is_finite(value)) return -1;
    output[0] = '\0';
    representation.number = value;
    negative = (int)(representation.bits >> 63);
    magnitude = negative ? -value : value;
    if (negative && !append_char(output, output_capacity, &length, '-'))
        return -1;

    if (magnitude == 0.0) {
        if (!append_char(output, output_capacity, &length, '0')) return -1;
        return (int)length;
    }

    while (magnitude >= 10.0 && exponent < 400) {
        magnitude /= 10.0;
        ++exponent;
    }
    while (magnitude < 1.0 && exponent > -400) {
        magnitude *= 10.0;
        --exponent;
    }

    scaled = magnitude * 100000000000000.0;
    rounded = (uint64_t)scaled;
    if (scaled - (double)rounded >= 0.5) ++rounded;
    if (rounded >= UINT64_C(1000000000000000)) {
        rounded /= UINT64_C(10);
        ++exponent;
    }
    for (index = 15u; index > 0u; --index) {
        digits[index - 1u] = (char)('0' + (rounded % UINT64_C(10)));
        rounded /= UINT64_C(10);
    }
    while (digit_count > 1 && digits[digit_count - 1] == '0') --digit_count;

    scientific = exponent < -4 || exponent >= 15;
    if (scientific) {
        if (!append_char(output, output_capacity, &length, digits[0])) return -1;
        if (digit_count > 1) {
            if (!append_char(output, output_capacity, &length, '.')) return -1;
            for (index = 1u; index < (size_t)digit_count; ++index) {
                if (!append_char(output, output_capacity, &length, digits[index]))
                    return -1;
            }
        }
        if (!append_char(output, output_capacity, &length, 'e') ||
            !append_char(output, output_capacity, &length,
                         exponent < 0 ? '-' : '+') ||
            !append_unsigned(output, output_capacity, &length,
                             (unsigned)(exponent < 0 ? -exponent : exponent))) {
            return -1;
        }
    } else {
        int decimal_point = exponent + 1;
        if (decimal_point <= 0) {
            if (!append_char(output, output_capacity, &length, '0') ||
                !append_char(output, output_capacity, &length, '.')) return -1;
            for (index = 0u; index < (size_t)(-decimal_point); ++index) {
                if (!append_char(output, output_capacity, &length, '0')) return -1;
            }
            for (index = 0u; index < (size_t)digit_count; ++index) {
                if (!append_char(output, output_capacity, &length, digits[index]))
                    return -1;
            }
        } else {
            for (index = 0u; index < (size_t)decimal_point; ++index) {
                char digit = index < (size_t)digit_count ? digits[index] : '0';
                if (!append_char(output, output_capacity, &length, digit)) return -1;
            }
            if ((size_t)decimal_point < (size_t)digit_count) {
                if (!append_char(output, output_capacity, &length, '.')) return -1;
                for (index = (size_t)decimal_point;
                     index < (size_t)digit_count; ++index) {
                    if (!append_char(output, output_capacity, &length, digits[index]))
                        return -1;
                }
            }
        }
    }
    return (int)length;
}

int rexx_numeric_is_finite(double value)
{
    return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

int rexx_numeric_is_integral(double value)
{
    const double signed_limit = 9223372036854775808.0;
    int64_t integer;
    if (!rexx_numeric_is_finite(value)) return 0;
    if (value >= signed_limit || value < -signed_limit) return 1;
    integer = (int64_t)value;
    return (double)integer == value;
}

#endif
