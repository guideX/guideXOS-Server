#ifndef GUIDEXOS_REXX_NUMERIC_H
#define GUIDEXOS_REXX_NUMERIC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RexxNumericStatus {
    REXX_NUMERIC_OK = 0,
    REXX_NUMERIC_INVALID,
    REXX_NUMERIC_RANGE
} RexxNumericStatus;

/* Parse the decimal forms used by REXX numeric values, returning the first
 * unconsumed byte. Hosted builds delegate conversion to strtod so existing
 * behavior remains tied to the host C runtime. */
RexxNumericStatus rexx_numeric_parse(const char *text,
                                     char **end_out,
                                     double *value_out);

/* Format a number using the interpreter's existing 15-significant-digit
 * general format. Returns the byte count excluding NUL, or -1 on failure. */
int rexx_numeric_format(char *output,
                        size_t output_capacity,
                        double value);

int rexx_numeric_is_finite(double value);
int rexx_numeric_is_integral(double value);

#ifdef __cplusplus
}
#endif

#endif
