#include "rexx_numeric.h"

#include <stdint.h>
#include <string.h>

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

/* The routines below are the authoritative REXX decimal path. The double
 * adapters above remain only for the RX2 embedding API and its legacy tests. */

static int decimal_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

static int decimal_valid_precision(size_t precision)
{
    /* One extra coefficient digit is needed by callers for guard precision. */
    return precision != 0u && precision < REXX_DECIMAL_MAX_DIGITS;
}

static void decimal_zero(RexxDecimal *value)
{
    memset(value, 0, sizeof(*value));
    value->digits[0] = '0';
    value->length = 1u;
}

static int decimal_adjusted_exponent(const RexxDecimal *value)
{
    return (int)value->length - value->scale - 1;
}

static RexxNumericStatus decimal_truncate(RexxDecimal *value,
                                          size_t precision)
{
    size_t drop;
    if (precision == 0u || precision > REXX_DECIMAL_MAX_DIGITS || value == NULL ||
        value->length == 0u || value->length > REXX_DECIMAL_MAX_DIGITS) {
        return REXX_NUMERIC_INVALID;
    }
    if (value->sign == 0) return REXX_NUMERIC_OK;
    if (value->length <= precision) return REXX_NUMERIC_OK;
    drop = value->length - precision;
    value->length = precision;
    value->digits[precision] = '\0';
    value->scale -= (int32_t)drop;
    return REXX_NUMERIC_OK;
}

RexxNumericStatus rexx_decimal_parse(const char *text,
                                     size_t length,
                                     RexxDecimal *value_out)
{
    char digits[REXX_DECIMAL_MAX_DIGITS + 1u];
    size_t cursor = 0u, used = 0u, fractional = 0u, first_nonzero = SIZE_MAX;
    int sign = 1, after_point = 0, saw_digit = 0, exponent_sign = 1;
    int exponent = 0;
    int64_t scale;

    if (text == NULL || value_out == NULL) return REXX_NUMERIC_INVALID;
    if (length > REXX_DECIMAL_MAX_INPUT_BYTES)
        return REXX_NUMERIC_RESOURCE_LIMIT;
    while (cursor < length && decimal_space(text[cursor])) ++cursor;
    if (cursor < length && (text[cursor] == '+' || text[cursor] == '-')) {
        if (text[cursor] == '-') sign = -1;
        ++cursor;
        while (cursor < length && decimal_space(text[cursor])) ++cursor;
    }

    while (cursor < length) {
        char c = text[cursor];
        if (c >= '0' && c <= '9') {
            saw_digit = 1;
            if (after_point) ++fractional;
            if (first_nonzero == SIZE_MAX && c != '0') first_nonzero = used;
            if (first_nonzero != SIZE_MAX) {
                if (used >= REXX_DECIMAL_MAX_DIGITS)
                    return REXX_NUMERIC_RESOURCE_LIMIT;
                digits[used++] = c;
            }
            ++cursor;
        } else if (c == '.' && !after_point) {
            after_point = 1;
            ++cursor;
        } else {
            break;
        }
    }
    if (!saw_digit) return REXX_NUMERIC_INVALID;

    if (cursor < length && (text[cursor] == 'e' || text[cursor] == 'E')) {
        size_t exponent_start = cursor++;
        size_t exponent_digits = 0u;
        if (cursor < length && (text[cursor] == '+' || text[cursor] == '-')) {
            if (text[cursor] == '-') exponent_sign = -1;
            ++cursor;
        }
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            if (exponent <= REXX_DECIMAL_MAX_EXPONENT) {
                exponent = exponent * 10 + (text[cursor] - '0');
                if (exponent > REXX_DECIMAL_MAX_EXPONENT)
                    exponent = REXX_DECIMAL_MAX_EXPONENT + 1;
            }
            ++cursor;
            ++exponent_digits;
        }
        if (exponent_digits == 0u) return REXX_NUMERIC_INVALID;
        (void)exponent_start;
    }
    while (cursor < length && decimal_space(text[cursor])) ++cursor;
    if (cursor != length) return REXX_NUMERIC_INVALID;
    if (exponent > REXX_DECIMAL_MAX_EXPONENT)
        return REXX_NUMERIC_RANGE;

    if (first_nonzero == SIZE_MAX) {
        decimal_zero(value_out);
        return REXX_NUMERIC_OK;
    }
    scale = (int64_t)fractional - (int64_t)exponent_sign * exponent;
    if (scale > REXX_DECIMAL_MAX_EXPONENT ||
        scale < -REXX_DECIMAL_MAX_EXPONENT) return REXX_NUMERIC_RANGE;
    memset(value_out, 0, sizeof(*value_out));
    memcpy(value_out->digits, digits, used);
    value_out->digits[used] = '\0';
    value_out->length = used;
    value_out->scale = (int32_t)scale;
    value_out->sign = (int8_t)sign;
    return REXX_NUMERIC_OK;
}

RexxNumericStatus rexx_decimal_round(RexxDecimal *value, size_t precision)
{
    size_t dropped, i;
    int carry;
    if (!decimal_valid_precision(precision) || value == NULL ||
        value->length == 0u || value->length > REXX_DECIMAL_MAX_DIGITS)
        return REXX_NUMERIC_INVALID;
    if (value->sign == 0 || value->length <= precision) return REXX_NUMERIC_OK;
    dropped = value->length - precision;
    carry = value->digits[precision] >= '5';
    value->length = precision;
    value->digits[precision] = '\0';
    value->scale -= (int32_t)dropped;
    for (i = precision; carry && i != 0u; --i) {
        size_t index = i - 1u;
        if (value->digits[index] == '9') value->digits[index] = '0';
        else { ++value->digits[index]; carry = 0; }
    }
    if (carry) {
        if (precision >= REXX_DECIMAL_MAX_DIGITS) return REXX_NUMERIC_RESOURCE_LIMIT;
        value->digits[0] = '1';
        for (i = 1u; i < precision; ++i) value->digits[i] = '0';
        value->digits[precision] = '\0';
        value->length = precision;
        --value->scale;
    }
    return REXX_NUMERIC_OK;
}

int rexx_decimal_compare(const RexxDecimal *left, const RexxDecimal *right)
{
    int left_exp, right_exp;
    size_t i, count;
    if (left->sign != right->sign) return left->sign < right->sign ? -1 : 1;
    if (left->sign == 0) return 0;
    left_exp = decimal_adjusted_exponent(left);
    right_exp = decimal_adjusted_exponent(right);
    if (left_exp != right_exp)
        return (left_exp < right_exp ? -1 : 1) * left->sign;
    count = left->length > right->length ? left->length : right->length;
    for (i = 0u; i < count; ++i) {
        char a = i < left->length ? left->digits[i] : '0';
        char b = i < right->length ? right->digits[i] : '0';
        if (a != b) return (a < b ? -1 : 1) * left->sign;
    }
    return 0;
}

static RexxNumericStatus decimal_from_work(RexxDecimal *result,
                                           const unsigned char *little,
                                           size_t length,
                                           int32_t scale,
                                           int sign,
                                           size_t precision)
{
    size_t i, dropped;
    int carry;
    if (result == NULL || length == 0u ||
        length > REXX_DECIMAL_MAX_DIGITS * 2u + 2u ||
        !decimal_valid_precision(precision))
        return REXX_NUMERIC_RESOURCE_LIMIT;
    while (length > 1u && little[length - 1u] == 0u) --length;
    if (length == 1u && little[0] == 0u) {
        decimal_zero(result);
        return REXX_NUMERIC_OK;
    }
    dropped = length > precision ? length - precision : 0u;
    if ((int64_t)scale - (int64_t)dropped < -REXX_DECIMAL_MAX_EXPONENT ||
        (int64_t)scale - (int64_t)dropped > REXX_DECIMAL_MAX_EXPONENT)
        return REXX_NUMERIC_RANGE;
    memset(result, 0, sizeof(*result));
    result->length = length - dropped;
    result->scale = scale - (int32_t)dropped;
    result->sign = (int8_t)sign;
    for (i = 0u; i < result->length; ++i)
        result->digits[i] = (char)('0' + little[length - 1u - i]);
    result->digits[result->length] = '\0';
    carry = dropped != 0u && little[dropped - 1u] >= 5u;
    for (i = result->length; carry && i != 0u; --i) {
        size_t index = i - 1u;
        if (result->digits[index] == '9') result->digits[index] = '0';
        else { ++result->digits[index]; carry = 0; }
    }
    if (carry) {
        result->digits[0] = '1';
        for (i = 1u; i < result->length; ++i) result->digits[i] = '0';
        --result->scale;
    }
    return REXX_NUMERIC_OK;
}

static int decimal_adjusted(const RexxDecimal *value)
{
    return decimal_adjusted_exponent(value);
}

RexxNumericStatus rexx_decimal_add(const RexxDecimal *left,
                                   const RexxDecimal *right,
                                   size_t precision,
                                   RexxDecimal *result)
{
    RexxDecimal a, b;
    unsigned char aa[REXX_DECIMAL_MAX_DIGITS * 2u + 2u];
    unsigned char bb[REXX_DECIMAL_MAX_DIGITS * 2u + 2u];
    unsigned char rr[REXX_DECIMAL_MAX_DIGITS * 2u + 2u];
    size_t alen, blen, length, i, delta;
    int32_t scale;
    int sign;
    RexxNumericStatus status;

    if (left == NULL || right == NULL || result == NULL ||
        !decimal_valid_precision(precision)) return REXX_NUMERIC_INVALID;
    a = *left; b = *right;
    status = decimal_truncate(&a, precision + 1u);
    if (status == REXX_NUMERIC_OK) status = decimal_truncate(&b, precision + 1u);
    if (status != REXX_NUMERIC_OK) return status;
    if (a.sign == 0) { *result = b; return rexx_decimal_round(result, precision); }
    if (b.sign == 0) { *result = a; return rexx_decimal_round(result, precision); }
    {
        int difference = decimal_adjusted(&a) - decimal_adjusted(&b);
        if (difference < 0) difference = -difference;
        if (difference > (int)precision + 2) {
        *result = decimal_adjusted(&a) > decimal_adjusted(&b) ? a : b;
        return rexx_decimal_round(result, precision);
        }
    }

    scale = a.scale > b.scale ? a.scale : b.scale;
    delta = (size_t)(scale - a.scale);
    alen = a.length + delta;
    if (alen > sizeof(aa)) return REXX_NUMERIC_RESOURCE_LIMIT;
    memset(aa, 0, sizeof(aa));
    for (i = 0u; i < a.length; ++i)
        aa[i + delta] = (unsigned char)(a.digits[a.length - 1u - i] - '0');
    delta = (size_t)(scale - b.scale);
    blen = b.length + delta;
    if (blen > sizeof(bb)) return REXX_NUMERIC_RESOURCE_LIMIT;
    memset(bb, 0, sizeof(bb));
    for (i = 0u; i < b.length; ++i)
        bb[i + delta] = (unsigned char)(b.digits[b.length - 1u - i] - '0');
    length = alen > blen ? alen : blen;
    sign = a.sign;
    memset(rr, 0, sizeof(rr));

    if (a.sign == b.sign) {
        unsigned int carry = 0u;
        for (i = 0u; i < length; ++i) {
            unsigned int sum = (unsigned int)aa[i] + bb[i] + carry;
            rr[i] = (unsigned char)(sum % 10u);
            carry = sum / 10u;
        }
        if (carry != 0u) rr[length++] = (unsigned char)carry;
    } else {
        int cmp = 0;
        while (length > 0u) {
            --length;
            if (aa[length] != bb[length]) { cmp = aa[length] > bb[length] ? 1 : -1; break; }
        }
        length = alen > blen ? alen : blen;
        if (cmp == 0) { decimal_zero(result); return REXX_NUMERIC_OK; }
        if (cmp < 0) { unsigned char temp[sizeof(aa)]; memcpy(temp, aa, sizeof(aa)); memcpy(aa, bb, sizeof(aa)); memcpy(bb, temp, sizeof(bb)); sign = b.sign; }
        {
            int borrow = 0;
            for (i = 0u; i < length; ++i) {
                int digit = (int)aa[i] - (int)bb[i] - borrow;
                if (digit < 0) { digit += 10; borrow = 1; } else borrow = 0;
                rr[i] = (unsigned char)digit;
            }
        }
    }
    return decimal_from_work(result, rr, length, scale, sign, precision);
}

RexxNumericStatus rexx_decimal_subtract(const RexxDecimal *left,
                                        const RexxDecimal *right,
                                        size_t precision,
                                        RexxDecimal *result)
{
    RexxDecimal negated;
    if (right == NULL) return REXX_NUMERIC_INVALID;
    negated = *right;
    negated.sign = (int8_t)-negated.sign;
    return rexx_decimal_add(left, &negated, precision, result);
}

RexxNumericStatus rexx_decimal_multiply(const RexxDecimal *left,
                                        const RexxDecimal *right,
                                        size_t precision,
                                        RexxDecimal *result)
{
    RexxDecimal a, b;
    unsigned int work[REXX_DECIMAL_MAX_DIGITS * 2u + 2u];
    unsigned char little[REXX_DECIMAL_MAX_DIGITS * 2u + 2u];
    size_t i, j, length;
    int64_t scale;
    RexxNumericStatus status;

    if (left == NULL || right == NULL || result == NULL ||
        !decimal_valid_precision(precision)) return REXX_NUMERIC_INVALID;
    a = *left; b = *right;
    status = decimal_truncate(&a, precision + 1u);
    if (status == REXX_NUMERIC_OK) status = decimal_truncate(&b, precision + 1u);
    if (status != REXX_NUMERIC_OK) return status;
    if (a.sign == 0 || b.sign == 0) { decimal_zero(result); return REXX_NUMERIC_OK; }
    scale = (int64_t)a.scale + b.scale;
    if (scale > REXX_DECIMAL_MAX_EXPONENT || scale < -REXX_DECIMAL_MAX_EXPONENT)
        return REXX_NUMERIC_RANGE;
    memset(work, 0, sizeof(work));
    memset(little, 0, sizeof(little));
    for (i = 0u; i < a.length; ++i) {
        unsigned int da = (unsigned int)(a.digits[a.length - 1u - i] - '0');
        for (j = 0u; j < b.length; ++j) {
            unsigned int db = (unsigned int)(b.digits[b.length - 1u - j] - '0');
            work[i + j] += da * db;
        }
    }
    length = a.length + b.length;
    for (i = 0u; i < length; ++i) {
        work[i + 1u] += work[i] / 10u;
        little[i] = (unsigned char)(work[i] % 10u);
    }
    if (work[length] != 0u) {
        little[length] = (unsigned char)work[length];
        ++length;
    }
    return decimal_from_work(result, little, length, (int32_t)scale,
                             a.sign * b.sign, precision);
}

static RexxNumericStatus decimal_charge_work(size_t *remaining, size_t cost)
{
    if (remaining == NULL) return REXX_NUMERIC_OK;
    if (cost > *remaining) return REXX_NUMERIC_RESOURCE_LIMIT;
    *remaining -= cost;
    return REXX_NUMERIC_OK;
}

static void decimal_big_normalize(char *digits, size_t *length)
{
    size_t first = 0u;
    while (first + 1u < *length && digits[first] == '0') ++first;
    if (first != 0u) {
        memmove(digits, digits + first, *length - first);
        *length -= first;
    }
}

static int decimal_big_compare(const char *left, size_t left_length,
                               const char *right, size_t right_length)
{
    size_t i;
    if (left_length != right_length)
        return left_length < right_length ? -1 : 1;
    for (i = 0u; i < left_length; ++i) {
        if (left[i] != right[i]) return left[i] < right[i] ? -1 : 1;
    }
    return 0;
}

static void decimal_big_shift_add(char *digits, size_t *length, char digit)
{
    if (*length == 1u && digits[0] == '0') {
        digits[0] = digit;
    } else {
        digits[*length] = digit;
        ++*length;
    }
}

static void decimal_big_subtract(char *left, size_t *left_length,
                                 const char *right, size_t right_length)
{
    size_t i;
    int borrow = 0;
    for (i = 0u; i < *left_length; ++i) {
        size_t left_index = *left_length - 1u - i;
        int digit = left[left_index] - '0' - borrow;
        if (i < right_length) digit -= right[right_length - 1u - i] - '0';
        if (digit < 0) { digit += 10; borrow = 1; }
        else borrow = 0;
        left[left_index] = (char)('0' + digit);
    }
    decimal_big_normalize(left, left_length);
}

static RexxNumericStatus decimal_big_divide_digit(char *remainder,
                                                  size_t *remainder_length,
                                                  const char *divisor,
                                                  size_t divisor_length,
                                                  char *quotient_digit,
                                                  size_t *work_remaining)
{
    unsigned int quotient = 0u;
    for (;;) {
        size_t compare_cost = *remainder_length > divisor_length
                                  ? *remainder_length : divisor_length;
        RexxNumericStatus status = decimal_charge_work(work_remaining,
                                                       compare_cost);
        int comparison;
        if (status != REXX_NUMERIC_OK) return status;
        comparison = decimal_big_compare(remainder, *remainder_length,
                                         divisor, divisor_length);
        if (comparison < 0) break;
        status = decimal_charge_work(work_remaining, *remainder_length);
        if (status != REXX_NUMERIC_OK) return status;
        decimal_big_subtract(remainder, remainder_length,
                             divisor, divisor_length);
        ++quotient;
    }
    *quotient_digit = (char)('0' + quotient);
    return REXX_NUMERIC_OK;
}

/* Return the leading significant digits of numerator/denominator, ignoring
 * each decimal value's scale. The caller applies the scale difference. */
static RexxNumericStatus decimal_ratio_digits(const RexxDecimal *numerator,
                                              const RexxDecimal *denominator,
                                              size_t limit,
                                              char *digits,
                                              size_t *digits_length,
                                              int *adjusted_exponent,
                                              size_t *work_remaining)
{
    char remainder[REXX_DECIMAL_MAX_DIGITS + 2u];
    char integer_quotient[REXX_DECIMAL_MAX_DIGITS];
    size_t remainder_length = 1u, i, first_nonzero, quotient_length;
    size_t used = 0u, leading_fractional_zeroes = 0u;
    RexxNumericStatus status;

    if (numerator == NULL || denominator == NULL || digits == NULL ||
        digits_length == NULL || adjusted_exponent == NULL || limit == 0u ||
        limit >= REXX_DECIMAL_MAX_DIGITS)
        return REXX_NUMERIC_INVALID;
    remainder[0] = '0';
    for (i = 0u; i < numerator->length; ++i) {
        char digit;
        status = decimal_charge_work(work_remaining, remainder_length + 1u);
        if (status != REXX_NUMERIC_OK) return status;
        decimal_big_shift_add(remainder, &remainder_length,
                              numerator->digits[i]);
        status = decimal_big_divide_digit(remainder, &remainder_length,
                                          denominator->digits,
                                          denominator->length, &digit,
                                          work_remaining);
        if (status != REXX_NUMERIC_OK) return status;
        integer_quotient[i] = digit;
    }

    first_nonzero = 0u;
    while (first_nonzero < numerator->length &&
           integer_quotient[first_nonzero] == '0')
        ++first_nonzero;
    if (first_nonzero < numerator->length) {
        quotient_length = numerator->length - first_nonzero;
        *adjusted_exponent = (int)quotient_length - 1;
        used = quotient_length < limit ? quotient_length : limit;
        memcpy(digits, integer_quotient + first_nonzero, used);
    }

    if (used == 0u) {
        *adjusted_exponent = 0;
        while (used < limit) {
            char digit;
            status = decimal_charge_work(work_remaining,
                                         remainder_length + 1u);
            if (status != REXX_NUMERIC_OK) return status;
            decimal_big_shift_add(remainder, &remainder_length, '0');
            status = decimal_big_divide_digit(remainder, &remainder_length,
                                              denominator->digits,
                                              denominator->length, &digit,
                                              work_remaining);
            if (status != REXX_NUMERIC_OK) return status;
            if (used == 0u && digit == '0') {
                ++leading_fractional_zeroes;
                continue;
            }
            if (used == 0u) {
                *adjusted_exponent = -(int)(leading_fractional_zeroes + 1u);
            }
            digits[used++] = digit;
            if (remainder_length == 1u && remainder[0] == '0') break;
        }
    } else {
        while (used < limit &&
               !(remainder_length == 1u && remainder[0] == '0')) {
            char digit;
            status = decimal_charge_work(work_remaining,
                                         remainder_length + 1u);
            if (status != REXX_NUMERIC_OK) return status;
            decimal_big_shift_add(remainder, &remainder_length, '0');
            status = decimal_big_divide_digit(remainder, &remainder_length,
                                              denominator->digits,
                                              denominator->length, &digit,
                                              work_remaining);
            if (status != REXX_NUMERIC_OK) return status;
            digits[used++] = digit;
        }
    }
    *digits_length = used;
    return used == 0u ? REXX_NUMERIC_RESOURCE_LIMIT : REXX_NUMERIC_OK;
}

static RexxNumericStatus decimal_strip_trailing_zeroes(
    RexxDecimal *value, size_t *work_remaining)
{
    if (value == NULL || value->length == 0u) return REXX_NUMERIC_INVALID;
    while (value->sign != 0 && value->length > 1u &&
           value->digits[value->length - 1u] == '0') {
        RexxNumericStatus status = decimal_charge_work(work_remaining, 1u);
        if (status != REXX_NUMERIC_OK) return status;
        if ((int64_t)value->scale - 1 < -REXX_DECIMAL_MAX_EXPONENT)
            return REXX_NUMERIC_RANGE;
        --value->length;
        --value->scale;
        value->digits[value->length] = '\0';
    }
    return REXX_NUMERIC_OK;
}

RexxNumericStatus rexx_decimal_divide(const RexxDecimal *left,
                                     const RexxDecimal *right,
                                     size_t precision,
                                     size_t *work_remaining,
                                     RexxDecimal *result)
{
    RexxDecimal a, b;
    char quotient[REXX_DECIMAL_MAX_DIGITS];
    size_t quotient_length = 0u, limit;
    int coefficient_exponent = 0;
    int64_t exponent, scale;
    RexxNumericStatus status;

    if (left == NULL || right == NULL || result == NULL ||
        !decimal_valid_precision(precision)) return REXX_NUMERIC_INVALID;
    a = *left;
    b = *right;
    status = decimal_truncate(&a, precision + 1u);
    if (status == REXX_NUMERIC_OK) status = decimal_truncate(&b, precision + 1u);
    if (status != REXX_NUMERIC_OK) return status;
    status = decimal_charge_work(work_remaining, a.length + b.length);
    if (status != REXX_NUMERIC_OK) return status;
    if (b.sign == 0) return REXX_NUMERIC_INVALID;
    if (a.sign == 0) { decimal_zero(result); return REXX_NUMERIC_OK; }

    limit = precision + 1u;
    status = decimal_ratio_digits(&a, &b, limit, quotient, &quotient_length,
                                  &coefficient_exponent, work_remaining);
    if (status != REXX_NUMERIC_OK) return status;
    exponent = (int64_t)coefficient_exponent + (int64_t)b.scale - a.scale;
    scale = (int64_t)quotient_length - 1 - exponent;
    if (scale > REXX_DECIMAL_MAX_EXPONENT ||
        scale < -REXX_DECIMAL_MAX_EXPONENT)
        return REXX_NUMERIC_RANGE;

    memset(result, 0, sizeof(*result));
    memcpy(result->digits, quotient, quotient_length);
    result->digits[quotient_length] = '\0';
    result->length = quotient_length;
    result->scale = (int32_t)scale;
    result->sign = (int8_t)(a.sign * b.sign);
    status = decimal_charge_work(work_remaining, quotient_length);
    if (status == REXX_NUMERIC_OK) status = rexx_decimal_round(result, precision);
    if (status == REXX_NUMERIC_OK)
        status = decimal_strip_trailing_zeroes(result, work_remaining);
    return status;
}

static RexxNumericStatus decimal_integer_operation(
    const RexxDecimal *left, const RexxDecimal *right, size_t precision,
    size_t *work_remaining, RexxDecimal *quotient_result,
    RexxDecimal *remainder_result)
{
    RexxDecimal a, b, quotient, product;
    char quotient_digits[REXX_DECIMAL_MAX_DIGITS];
    size_t quotient_digit_count = 0u, target_digits;
    int coefficient_exponent = 0;
    int64_t adjusted_exponent, scale_shift;
    RexxNumericStatus status;

    if (left == NULL || right == NULL || quotient_result == NULL ||
        !decimal_valid_precision(precision)) return REXX_NUMERIC_INVALID;
    a = *left;
    b = *right;
    status = decimal_truncate(&a, precision + 1u);
    if (status == REXX_NUMERIC_OK) status = decimal_truncate(&b, precision + 1u);
    if (status != REXX_NUMERIC_OK) return status;
    status = decimal_charge_work(work_remaining, a.length + b.length);
    if (status != REXX_NUMERIC_OK) return status;
    if (b.sign == 0) return REXX_NUMERIC_INVALID;
    if (a.sign == 0) {
        decimal_zero(quotient_result);
        if (remainder_result != NULL) decimal_zero(remainder_result);
        return REXX_NUMERIC_OK;
    }

    status = decimal_ratio_digits(&a, &b, 1u, quotient_digits,
                                  &quotient_digit_count, &coefficient_exponent,
                                  work_remaining);
    if (status != REXX_NUMERIC_OK) return status;
    scale_shift = (int64_t)b.scale - a.scale;
    adjusted_exponent = (int64_t)coefficient_exponent + scale_shift;
    decimal_zero(&quotient);
    if (adjusted_exponent < 0) {
        *quotient_result = quotient;
        if (remainder_result != NULL) *remainder_result = a;
        return REXX_NUMERIC_OK;
    }
    if (adjusted_exponent >= (int64_t)precision)
        return REXX_NUMERIC_RANGE;

    target_digits = (size_t)adjusted_exponent + 1u;
    status = decimal_ratio_digits(&a, &b, target_digits, quotient_digits,
                                  &quotient_digit_count, &coefficient_exponent,
                                  work_remaining);
    if (status != REXX_NUMERIC_OK) return status;
    if (quotient_digit_count < target_digits) {
        memset(quotient_digits + quotient_digit_count, '0',
               target_digits - quotient_digit_count);
        quotient_digit_count = target_digits;
    }
    memset(&quotient, 0, sizeof(quotient));
    memcpy(quotient.digits, quotient_digits, target_digits);
    quotient.digits[target_digits] = '\0';
    quotient.length = target_digits;
    quotient.scale = 0;
    quotient.sign = (int8_t)(a.sign * b.sign);
    *quotient_result = quotient;

    if (remainder_result != NULL) {
        size_t multiply_cost = quotient.length * b.length +
                               quotient.length + b.length;
        int64_t scale_gap = (int64_t)a.scale - b.scale;
        size_t subtract_cost;
        if (scale_gap < 0) scale_gap = -scale_gap;
        if ((uint64_t)scale_gap > (uint64_t)SIZE_MAX - a.length - b.length)
            return REXX_NUMERIC_RESOURCE_LIMIT;
        subtract_cost = a.length + b.length + (size_t)scale_gap;
        status = decimal_charge_work(work_remaining, multiply_cost);
        if (status == REXX_NUMERIC_OK)
            status = rexx_decimal_multiply(&quotient, &b,
                                           REXX_DECIMAL_MAX_DIGITS - 1u,
                                           &product);
        if (status == REXX_NUMERIC_OK)
            status = decimal_charge_work(work_remaining, subtract_cost +
                                         product.length);
        if (status == REXX_NUMERIC_OK)
            status = rexx_decimal_subtract(&a, &product,
                                           REXX_DECIMAL_MAX_DIGITS - 1u,
                                           remainder_result);
        if (status != REXX_NUMERIC_OK) return status;
    }
    return REXX_NUMERIC_OK;
}

RexxNumericStatus rexx_decimal_integer_divide(const RexxDecimal *left,
                                              const RexxDecimal *right,
                                              size_t precision,
                                              size_t *work_remaining,
                                              RexxDecimal *result)
{
    return decimal_integer_operation(left, right, precision, work_remaining,
                                     result, NULL);
}

RexxNumericStatus rexx_decimal_remainder(const RexxDecimal *left,
                                         const RexxDecimal *right,
                                         size_t precision,
                                         size_t *work_remaining,
                                         RexxDecimal *result)
{
    RexxDecimal quotient;
    if (result == NULL) return REXX_NUMERIC_INVALID;
    return decimal_integer_operation(left, right, precision, work_remaining,
                                     &quotient, result);
}

static RexxNumericStatus decimal_power_exponent(const RexxDecimal *value,
                                                uint32_t *magnitude,
                                                int *negative,
                                                size_t *integer_digits)
{
    size_t whole_length, i;
    uint64_t converted = 0u;
    if (value == NULL || magnitude == NULL || negative == NULL ||
        integer_digits == NULL || value->length == 0u)
        return REXX_NUMERIC_INVALID;
    *negative = value->sign < 0;
    if (value->sign == 0) {
        *magnitude = 0u;
        *integer_digits = 0u;
        return REXX_NUMERIC_OK;
    }
    if (value->scale > 0) {
        size_t fractional = (size_t)value->scale;
        whole_length = fractional >= value->length
                           ? 0u : value->length - fractional;
        for (i = whole_length; i < value->length; ++i)
            if (value->digits[i] != '0') return REXX_NUMERIC_INVALID;
    } else {
        size_t zeroes = (size_t)(-(int64_t)value->scale);
        if (zeroes > 7u || value->length > 7u - zeroes)
            return REXX_NUMERIC_RANGE;
        whole_length = value->length + zeroes;
    }
    if (whole_length > 7u) return REXX_NUMERIC_RANGE;
    for (i = 0u; i < value->length &&
                (value->scale <= 0 || i < whole_length); ++i) {
        converted = converted * 10u + (uint64_t)(value->digits[i] - '0');
        if (converted > (uint64_t)REXX_DECIMAL_MAX_EXPONENT)
            return REXX_NUMERIC_RANGE;
    }
    if (value->scale <= 0) {
        size_t zeroes = (size_t)(-(int64_t)value->scale);
        while (zeroes-- != 0u) {
            converted *= 10u;
            if (converted > (uint64_t)REXX_DECIMAL_MAX_EXPONENT)
                return REXX_NUMERIC_RANGE;
        }
    }
    *magnitude = (uint32_t)converted;
    *integer_digits = whole_length;
    return REXX_NUMERIC_OK;
}

static RexxNumericStatus decimal_power_multiply(const RexxDecimal *left,
                                               const RexxDecimal *right,
                                               size_t precision,
                                               size_t *work_remaining,
                                               RexxDecimal *result)
{
    size_t cost = left->length * right->length +
                  left->length + right->length;
    RexxNumericStatus status = decimal_charge_work(work_remaining, cost);
    if (status != REXX_NUMERIC_OK) return status;
    return rexx_decimal_multiply(left, right, precision, result);
}

RexxNumericStatus rexx_decimal_power(const RexxDecimal *base,
                                     const RexxDecimal *exponent,
                                     size_t precision,
                                     size_t *work_remaining,
                                     RexxDecimal *result)
{
    RexxDecimal factor, accumulator, temporary, one;
    uint32_t power;
    size_t exponent_digits, work_precision, bit;
    int negative_exponent;
    RexxNumericStatus status;

    if (base == NULL || exponent == NULL || result == NULL ||
        !decimal_valid_precision(precision)) return REXX_NUMERIC_INVALID;
    status = decimal_power_exponent(exponent, &power, &negative_exponent,
                                    &exponent_digits);
    if (status != REXX_NUMERIC_OK) return status;
    if (negative_exponent && base->sign == 0) return REXX_NUMERIC_INVALID;
    if (exponent_digits > SIZE_MAX - precision - 1u)
        return REXX_NUMERIC_RESOURCE_LIMIT;
    work_precision = precision + exponent_digits + 1u;
    if (work_precision >= REXX_DECIMAL_MAX_DIGITS)
        return REXX_NUMERIC_RESOURCE_LIMIT;
    factor = *base;
    status = decimal_truncate(&factor, precision + 1u);
    if (status != REXX_NUMERIC_OK) return status;
    status = decimal_charge_work(work_remaining, exponent->length);
    if (status != REXX_NUMERIC_OK) return status;
    if (power == 0u) {
        decimal_zero(&accumulator);
        accumulator.digits[0] = '1';
        accumulator.sign = 1;
        *result = accumulator;
        return REXX_NUMERIC_OK;
    }

    bit = 31u;
    while (((power >> bit) & 1u) == 0u) --bit;
    accumulator = factor;
    while (bit != 0u) {
        --bit;
        status = decimal_charge_work(work_remaining, 1u);
        if (status != REXX_NUMERIC_OK) return status;
        status = decimal_power_multiply(&accumulator, &accumulator,
                                       work_precision, work_remaining,
                                       &temporary);
        if (status != REXX_NUMERIC_OK) return status;
        accumulator = temporary;
        if (((power >> bit) & 1u) != 0u) {
            status = decimal_power_multiply(&accumulator, &factor,
                                           work_precision, work_remaining,
                                           &temporary);
            if (status != REXX_NUMERIC_OK) return status;
            accumulator = temporary;
        }
    }

    if (negative_exponent) {
        decimal_zero(&one);
        one.digits[0] = '1';
        one.sign = 1;
        status = rexx_decimal_divide(&one, &accumulator, work_precision,
                                     work_remaining, &temporary);
        if (status != REXX_NUMERIC_OK) return status;
        accumulator = temporary;
    }
    status = decimal_charge_work(work_remaining, accumulator.length);
    if (status == REXX_NUMERIC_OK)
        status = rexx_decimal_round(&accumulator, precision);
    if (status == REXX_NUMERIC_OK)
        status = decimal_strip_trailing_zeroes(&accumulator, work_remaining);
    if (status == REXX_NUMERIC_OK) *result = accumulator;
    return status;
}

RexxNumericStatus rexx_decimal_to_uint32(const RexxDecimal *value,
                                         uint32_t *result)
{
    size_t integer_digits, i;
    uint64_t converted = 0u;
    if (value == NULL || result == NULL || value->length == 0u)
        return REXX_NUMERIC_INVALID;
    if (value->sign < 0) return REXX_NUMERIC_INVALID;
    if (value->sign == 0) { *result = 0u; return REXX_NUMERIC_OK; }
    if (value->scale > 0) {
        size_t fractional = (size_t)value->scale;
        if (fractional >= value->length) return REXX_NUMERIC_INVALID;
        for (i = value->length - fractional; i < value->length; ++i)
            if (value->digits[i] != '0') return REXX_NUMERIC_INVALID;
        integer_digits = value->length - fractional;
    } else {
        integer_digits = value->length + (size_t)(-(int64_t)value->scale);
    }
    if (integer_digits > 10u) return REXX_NUMERIC_RANGE;
    for (i = 0u; i < value->length && (value->scale <= 0 || i < integer_digits); ++i) {
        converted = converted * 10u + (uint64_t)(value->digits[i] - '0');
    }
    if (value->scale <= 0) {
        size_t zeroes = (size_t)(-(int64_t)value->scale);
        while (zeroes-- != 0u) converted *= 10u;
    }
    if (converted > UINT32_MAX) return REXX_NUMERIC_RANGE;
    *result = (uint32_t)converted;
    return REXX_NUMERIC_OK;
}

RexxNumericStatus rexx_decimal_to_int32(const RexxDecimal *value,
                                        int32_t *result)
{
    RexxDecimal magnitude;
    uint32_t converted;
    RexxNumericStatus status;
    if (value == NULL || result == NULL) return REXX_NUMERIC_INVALID;
    magnitude = *value;
    magnitude.sign = magnitude.sign < 0 ? 1 : magnitude.sign;
    status = rexx_decimal_to_uint32(&magnitude, &converted);
    if (status != REXX_NUMERIC_OK) return status;
    if (value->sign < 0) {
        if (converted > UINT32_C(2147483648)) return REXX_NUMERIC_RANGE;
        *result = converted == UINT32_C(2147483648)
                      ? INT32_MIN : -(int32_t)converted;
    } else {
        if (converted > UINT32_C(2147483647)) return REXX_NUMERIC_RANGE;
        *result = (int32_t)converted;
    }
    return REXX_NUMERIC_OK;
}

static int decimal_append(char *out, size_t cap, size_t *length, char c)
{
    if (*length + 1u >= cap) return 0;
    out[(*length)++] = c;
    out[*length] = '\0';
    return 1;
}

static int decimal_append_unsigned(char *out, size_t cap, size_t *length,
                                   unsigned int value)
{
    char reverse[16];
    size_t count = 0u;
    do { reverse[count++] = (char)('0' + value % 10u); value /= 10u; }
    while (value != 0u && count < sizeof(reverse));
    while (count != 0u)
        if (!decimal_append(out, cap, length, reverse[--count])) return 0;
    return 1;
}

int rexx_decimal_format_with_form(char *output, size_t output_capacity,
                                  const RexxDecimal *value, size_t precision,
                                  RexxDecimalFormat form)
{
    size_t length = 0u, index;
    int exponent, output_exponent, integer_digits, scientific;
    if (output == NULL || output_capacity == 0u || value == NULL ||
        value->length == 0u || value->length > REXX_DECIMAL_MAX_DIGITS ||
        !decimal_valid_precision(precision) ||
        (form != REXX_DECIMAL_FORMAT_SCIENTIFIC &&
         form != REXX_DECIMAL_FORMAT_ENGINEERING)) return -1;
    output[0] = '\0';
    if (value->sign == 0)
        return decimal_append(output, output_capacity, &length, '0')
                   ? (int)length : -1;
    if (value->sign < 0 && !decimal_append(output, output_capacity, &length, '-'))
        return -1;
    exponent = decimal_adjusted_exponent(value);
    output_exponent = exponent;
    integer_digits = 1;
    scientific = exponent + 1 > (int)precision ||
                 (value->scale > 0 && (size_t)value->scale > precision * 2u);
    if (scientific) {
        if (form == REXX_DECIMAL_FORMAT_ENGINEERING) {
            output_exponent = (exponent / 3) * 3;
            if (exponent < 0 && exponent % 3 != 0) output_exponent -= 3;
            integer_digits = exponent - output_exponent + 1;
            /* Classic REXX does not emit an exponential part of E+0. */
            if (output_exponent == 0) scientific = 0;
        }
    }
    if (scientific) {
        for (index = 0u; index < (size_t)integer_digits; ++index) {
            char digit = index < value->length ? value->digits[index] : '0';
            if (!decimal_append(output, output_capacity, &length, digit)) return -1;
        }
        if (value->length > (size_t)integer_digits) {
            if (!decimal_append(output, output_capacity, &length, '.')) return -1;
            for (index = (size_t)integer_digits; index < value->length; ++index)
                if (!decimal_append(output, output_capacity, &length, value->digits[index])) return -1;
        }
        if (output_exponent != 0 &&
            (!decimal_append(output, output_capacity, &length, 'E') ||
             !decimal_append(output, output_capacity, &length,
                             output_exponent < 0 ? '-' : '+') ||
             !decimal_append_unsigned(
                 output, output_capacity, &length,
                 (unsigned int)(output_exponent < 0
                                    ? -output_exponent : output_exponent))))
                return -1;
        if (scientific) return (int)length;
    }
    if (value->scale <= 0) {
        for (index = 0u; index < value->length; ++index)
            if (!decimal_append(output, output_capacity, &length, value->digits[index])) return -1;
        for (index = 0u; index < (size_t)(-(int64_t)value->scale); ++index)
            if (!decimal_append(output, output_capacity, &length, '0')) return -1;
    } else {
        int64_t point = (int64_t)value->length - value->scale;
        if (point <= 0) {
            if (!decimal_append(output, output_capacity, &length, '0') ||
                !decimal_append(output, output_capacity, &length, '.')) return -1;
            for (index = 0u; index < (size_t)(-point); ++index)
                if (!decimal_append(output, output_capacity, &length, '0')) return -1;
            for (index = 0u; index < value->length; ++index)
                if (!decimal_append(output, output_capacity, &length, value->digits[index])) return -1;
        } else {
            for (index = 0u; index < value->length; ++index) {
                if ((int64_t)index == point &&
                    !decimal_append(output, output_capacity, &length, '.')) return -1;
                if (!decimal_append(output, output_capacity, &length, value->digits[index])) return -1;
            }
            if ((size_t)point >= value->length) {
                for (index = value->length; (int64_t)index < point; ++index)
                    if (!decimal_append(output, output_capacity, &length, '0')) return -1;
            }
            if ((int64_t)value->length == point &&
                !decimal_append(output, output_capacity, &length, '.')) return -1;
        }
    }
    return (int)length;
}

int rexx_decimal_format(char *output, size_t output_capacity,
                        const RexxDecimal *value, size_t precision)
{
    return rexx_decimal_format_with_form(
        output, output_capacity, value, precision,
        REXX_DECIMAL_FORMAT_SCIENTIFIC);
}
