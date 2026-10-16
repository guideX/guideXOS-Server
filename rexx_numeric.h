#ifndef GUIDEXOS_REXX_NUMERIC_H
#define GUIDEXOS_REXX_NUMERIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RexxNumericStatus {
    REXX_NUMERIC_OK = 0,
    REXX_NUMERIC_INVALID,
    REXX_NUMERIC_RANGE,
    REXX_NUMERIC_RESOURCE_LIMIT
} RexxNumericStatus;

typedef enum RexxDecimalFormat {
    REXX_DECIMAL_FORMAT_SCIENTIFIC = 0,
    REXX_DECIMAL_FORMAT_ENGINEERING = 1
} RexxDecimalFormat;

/* Bounded base-10 decimal used by classic REXX arithmetic. Digits are stored
 * most-significant first. The value is sign * coefficient * 10^(-scale).
 * Trailing coefficient zeroes and scale are retained for result formatting. */
#define REXX_DECIMAL_MAX_DIGITS ((size_t)512u)
#define REXX_DECIMAL_MAX_INPUT_BYTES ((size_t)512u)
#define REXX_NUMERIC_MAX_DIGITS ((size_t)254u)
#define REXX_DECIMAL_MAX_EXPONENT 1000000

typedef struct RexxDecimal {
    char digits[REXX_DECIMAL_MAX_DIGITS + 1u];
    size_t length;
    int32_t scale;
    int8_t sign;
} RexxDecimal;

/* Parse one complete numeric string (surrounding ASCII blanks are allowed). */
RexxNumericStatus rexx_decimal_parse(const char *text,
                                     size_t length,
                                     RexxDecimal *value_out);
RexxNumericStatus rexx_decimal_add(const RexxDecimal *left,
                                   const RexxDecimal *right,
                                   size_t precision,
                                   RexxDecimal *result);
RexxNumericStatus rexx_decimal_subtract(const RexxDecimal *left,
                                        const RexxDecimal *right,
                                        size_t precision,
                                        RexxDecimal *result);
RexxNumericStatus rexx_decimal_multiply(const RexxDecimal *left,
                                        const RexxDecimal *right,
                                        size_t precision,
                                        RexxDecimal *result);
/* work_remaining is decremented for iterative work when non-NULL. */
RexxNumericStatus rexx_decimal_divide(const RexxDecimal *left,
                                     const RexxDecimal *right,
                                     size_t precision,
                                     size_t *work_remaining,
                                     RexxDecimal *result);
RexxNumericStatus rexx_decimal_integer_divide(const RexxDecimal *left,
                                              const RexxDecimal *right,
                                              size_t precision,
                                              size_t *work_remaining,
                                              RexxDecimal *result);
RexxNumericStatus rexx_decimal_remainder(const RexxDecimal *left,
                                         const RexxDecimal *right,
                                         size_t precision,
                                         size_t *work_remaining,
                                         RexxDecimal *result);
RexxNumericStatus rexx_decimal_power(const RexxDecimal *base,
                                     const RexxDecimal *exponent,
                                     size_t precision,
                                     size_t *work_remaining,
                                     RexxDecimal *result);
RexxNumericStatus rexx_decimal_round(RexxDecimal *value, size_t precision);
int rexx_decimal_compare(const RexxDecimal *left, const RexxDecimal *right);
RexxNumericStatus rexx_decimal_to_uint32(const RexxDecimal *value,
                                         uint32_t *result);
RexxNumericStatus rexx_decimal_to_int32(const RexxDecimal *value,
                                        int32_t *result);
/* Formats according to the classic exponent trigger and SCIENTIFIC FORM. */
int rexx_decimal_format(char *output,
                        size_t output_capacity,
                        const RexxDecimal *value,
                        size_t precision);
/* Same bounded formatter with the active NUMERIC FORM selected explicitly. */
int rexx_decimal_format_with_form(char *output,
                                  size_t output_capacity,
                                  const RexxDecimal *value,
                                  size_t precision,
                                  RexxDecimalFormat form);

/* Legacy double adapters retained for C embedders and RX2 compatibility.
 * REXX source values and implemented arithmetic use RexxDecimal below. */
RexxNumericStatus rexx_numeric_parse(const char *text,
                                     char **end_out,
                                     double *value_out);

/* Format the legacy C-embedding double. Returns bytes excluding NUL. */
int rexx_numeric_format(char *output,
                        size_t output_capacity,
                        double value);

int rexx_numeric_is_finite(double value);
int rexx_numeric_is_integral(double value);

#ifdef __cplusplus
}
#endif

#endif
