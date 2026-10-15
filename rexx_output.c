#include "rexx_output.h"

#include <stdarg.h>
#include <stdint.h>

#if !defined(GXOS_BARE_METAL)
#include <stdio.h>
#endif

#define REXX_OUTPUT_FORMAT_CAPACITY 512u
#define REXX_OUTPUT_DEFAULT_STDOUT_LIMIT 65536u
#define REXX_OUTPUT_DEFAULT_STDERR_LIMIT 4096u

static int append_char(char *buffer, size_t capacity, size_t *length, char value)
{
    if (*length + 1u >= capacity) return 0;
    buffer[(*length)++] = value;
    buffer[*length] = '\0';
    return 1;
}

static int append_text(char *buffer,
                       size_t capacity,
                       size_t *length,
                       const char *value)
{
    if (value == NULL) value = "(null)";
    while (*value != '\0') {
        if (!append_char(buffer, capacity, length, *value++)) return 0;
    }
    return 1;
}

static int append_unsigned(char *buffer,
                           size_t capacity,
                           size_t *length,
                           unsigned long long value)
{
    char digits[24];
    size_t count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));

    while (count != 0u) {
        if (!append_char(buffer, capacity, length, digits[--count])) return 0;
    }
    return 1;
}

static int append_signed(char *buffer,
                         size_t capacity,
                         size_t *length,
                         int value)
{
    unsigned int magnitude;
    if (value < 0) {
        if (!append_char(buffer, capacity, length, '-')) return 0;
        magnitude = (unsigned int)(-(value + 1)) + 1u;
    } else {
        magnitude = (unsigned int)value;
    }
    return append_unsigned(buffer, capacity, length, magnitude);
}

static int format_bounded(char *buffer,
                          size_t capacity,
                          size_t *length_out,
                          const char *format,
                          va_list arguments)
{
    size_t length = 0u;

    if (buffer == NULL || capacity == 0u || format == NULL) return 0;
    buffer[0] = '\0';
    while (*format != '\0') {
        if (*format != '%') {
            if (!append_char(buffer, capacity, &length, *format++)) return 0;
            continue;
        }

        ++format;
        if (*format == '%') {
            if (!append_char(buffer, capacity, &length, '%')) return 0;
            ++format;
            continue;
        }

        {
            int is_long = 0;
            int is_size = 0;
            char conversion;
            if (*format == 'l') { is_long = 1; ++format; }
            else if (*format == 'z') { is_size = 1; ++format; }
            conversion = *format;
            if (conversion == '\0') return 0;
            ++format;

            if (conversion == 's') {
                if (!append_text(buffer, capacity, &length,
                                 va_arg(arguments, const char *))) return 0;
            } else if (conversion == 'd' && !is_long && !is_size) {
                if (!append_signed(buffer, capacity, &length,
                                   va_arg(arguments, int))) return 0;
            } else if (conversion == 'u' || (conversion == 'd' && (is_long || is_size))) {
                unsigned long long value;
                if (is_size) value = (unsigned long long)va_arg(arguments, size_t);
                else if (is_long) value = (unsigned long long)va_arg(arguments, unsigned long);
                else value = (unsigned long long)va_arg(arguments, unsigned int);
                if (conversion == 'd' && (is_long || is_size)) {
                    long long signed_value = (long long)value;
                    if (signed_value < 0) {
                        if (!append_char(buffer, capacity, &length, '-') ||
                            !append_unsigned(buffer, capacity, &length,
                                             (unsigned long long)(-(signed_value + 1)) + 1u))
                            return 0;
                    } else if (!append_unsigned(buffer, capacity, &length, value)) {
                        return 0;
                    }
                } else if (!append_unsigned(buffer, capacity, &length, value)) {
                    return 0;
                }
            } else {
                return 0;
            }
        }
    }

    *length_out = length;
    return 1;
}

void rexx_output_init(RexxOutputContext *output,
                      const RexxOutputHost *host)
{
    if (output == NULL) return;
    output->host = host;
    output->stdout_bytes = 0u;
    output->stderr_bytes = 0u;
}

int rexx_output_write(RexxOutputContext *output,
                      RexxOutputStream stream,
                      const char *bytes,
                      size_t length)
{
    size_t *written;
    size_t limit;
    int result;

    if (output == NULL || (bytes == NULL && length != 0u) ||
        (stream != REXX_OUTPUT_STDOUT && stream != REXX_OUTPUT_STDERR)) return 0;
    if (length == 0u) return 1;

    written = stream == REXX_OUTPUT_STDOUT
                  ? &output->stdout_bytes : &output->stderr_bytes;
    limit = output->host == NULL ? 0u :
            (stream == REXX_OUTPUT_STDOUT
                 ? output->host->max_stdout_bytes
                 : output->host->max_stderr_bytes);
    if (output->host != NULL && limit == 0u) {
        limit = stream == REXX_OUTPUT_STDOUT
                    ? REXX_OUTPUT_DEFAULT_STDOUT_LIMIT
                    : REXX_OUTPUT_DEFAULT_STDERR_LIMIT;
    }
    if (output->host != NULL && (length > limit || *written > limit - length))
        return 0;

    if (output->host != NULL && output->host->write != NULL) {
        result = output->host->write(output->host->context, stream, bytes, length);
    } else {
#if !defined(GXOS_BARE_METAL)
        FILE *file = stream == REXX_OUTPUT_STDOUT ? stdout : stderr;
        result = fwrite(bytes, 1u, length, file) == length;
#else
        result = 0;
#endif
    }
    if (!result) return 0;
    if (output->host != NULL) *written += length;
    return 1;
}

int rexx_output_flush(RexxOutputContext *output, RexxOutputStream stream)
{
    if (output == NULL) return 0;
    if (output->host != NULL && output->host->flush != NULL) {
        return output->host->flush(output->host->context, stream) != 0;
    }
#if !defined(GXOS_BARE_METAL)
    return fflush(stream == REXX_OUTPUT_STDOUT ? stdout : stderr) == 0;
#else
    return output->host != NULL && output->host->write != NULL;
#endif
}

int rexx_output_format(RexxOutputContext *output,
                       RexxOutputStream stream,
                       const char *format,
                       ...)
{
    char buffer[REXX_OUTPUT_FORMAT_CAPACITY];
    size_t length = 0u;
    va_list arguments;
    int formatted;

    va_start(arguments, format);
    formatted = format_bounded(buffer, sizeof(buffer), &length, format, arguments);
    va_end(arguments);
    return formatted && rexx_output_write(output, stream, buffer, length);
}
