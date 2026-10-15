#include "rexx_builtin.h"
#include "rexx_numeric.h"

#include <stdlib.h>
#include <string.h>

typedef struct RexxBuiltinStringView {
    const char *bytes;
    size_t length;
    char number_buffer[64];
} RexxBuiltinStringView;

static const char rexx_builtin_empty[] = "";
static const char rexx_builtin_true[] = "1";
static const char rexx_builtin_false[] = "0";
static const char rexx_builtin_uppercase[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char rexx_builtin_lowercase[] = "abcdefghijklmnopqrstuvwxyz";

static int rexx_builtin_ascii_space(unsigned char byte)
{
    return byte == (unsigned char)' ' ||
           (byte >= (unsigned char)'\t' && byte <= (unsigned char)'\r');
}

static unsigned char rexx_builtin_ascii_upper(unsigned char byte)
{
    if (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') {
        return (unsigned char)(byte - (unsigned char)'a' + (unsigned char)'A');
    }
    return byte;
}

static int rexx_builtin_ascii_equal(const char *left,
                                    size_t left_length,
                                    const char *right,
                                    size_t right_length)
{
    size_t index;

    if (left_length != right_length || left == NULL || right == NULL) {
        return 0;
    }
    for (index = 0u; index < left_length; ++index) {
        if (rexx_builtin_ascii_upper((unsigned char)left[index]) !=
            rexx_builtin_ascii_upper((unsigned char)right[index])) {
            return 0;
        }
    }
    return 1;
}

/* Render a RexxValue without allocating; string views borrow their source. */
static RexxVMStatus rexx_builtin_string_view(const RexxValue *value,
                                            RexxBuiltinStringView *view)
{
    int written;

    if (value == NULL || view == NULL) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }

    switch (value->type) {
    case REXX_VALUE_UNSET:
        view->bytes = rexx_builtin_empty;
        view->length = 0u;
        return REXX_VM_STATUS_OK;
    case REXX_VALUE_STRING:
        if (value->as.string.bytes == NULL && value->as.string.length != 0u) {
            return REXX_VM_STATUS_INVALID_ARGUMENT;
        }
        view->bytes = value->as.string.bytes != NULL
                          ? value->as.string.bytes : rexx_builtin_empty;
        view->length = value->as.string.length;
        return REXX_VM_STATUS_OK;
    case REXX_VALUE_BOOLEAN:
        view->bytes = value->as.boolean != 0
                          ? rexx_builtin_true : rexx_builtin_false;
        view->length = 1u;
        return REXX_VM_STATUS_OK;
    case REXX_VALUE_NUMBER:
        if (!rexx_numeric_is_finite(value->as.number)) {
            return REXX_VM_STATUS_NUMERIC_ERROR;
        }
        written = rexx_numeric_format(view->number_buffer,
                                      sizeof(view->number_buffer),
                                      value->as.number);
        if (written < 0 || (size_t)written >= sizeof(view->number_buffer)) {
            return REXX_VM_STATUS_NUMERIC_ERROR;
        }
        view->bytes = view->number_buffer;
        view->length = (size_t)written;
        return REXX_VM_STATUS_OK;
    default:
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
}

/*
 * Parse a finite REXX number without accepting embedded NULs or trailing
 * nonblank bytes. The empty string follows the VM's numeric convention (0).
 */
static RexxVMStatus rexx_builtin_number(const RexxValue *value, double *number)
{
    RexxBuiltinStringView view;
    RexxVMStatus status;

    if (value == NULL || number == NULL) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    if (value->type == REXX_VALUE_NUMBER) {
        if (!rexx_numeric_is_finite(value->as.number)) {
            return REXX_VM_STATUS_NUMERIC_ERROR;
        }
        *number = value->as.number;
        return REXX_VM_STATUS_OK;
    }
    if (value->type == REXX_VALUE_BOOLEAN) {
        *number = value->as.boolean != 0 ? 1.0 : 0.0;
        return REXX_VM_STATUS_OK;
    }
    if (value->type == REXX_VALUE_UNSET) {
        *number = 0.0;
        return REXX_VM_STATUS_OK;
    }

    status = rexx_builtin_string_view(value, &view);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    if (view.length == SIZE_MAX) {
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }

    {
        char *copy = (char *)malloc(view.length + 1u);
        char *start;
        char *end;
        size_t trimmed_length;
        double parsed;
        RexxNumericStatus numeric_status;

        if (copy == NULL) {
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        }
        if (view.length != 0u) {
            memcpy(copy, view.bytes, view.length);
        }
        copy[view.length] = '\0';

        start = copy;
        while (*start != '\0' &&
               rexx_builtin_ascii_space((unsigned char)*start)) {
            ++start;
        }
        end = copy + view.length;
        while (end > start &&
               rexx_builtin_ascii_space((unsigned char)end[-1])) {
            --end;
        }
        *end = '\0';
        trimmed_length = (size_t)(end - start);
        if (trimmed_length == 0u) {
            free(copy);
            *number = 0.0;
            return REXX_VM_STATUS_OK;
        }

        numeric_status = rexx_numeric_parse(start, &end, &parsed);
        if (numeric_status != REXX_NUMERIC_OK || end == start ||
            (size_t)(end - start) != trimmed_length ||
            !rexx_numeric_is_finite(parsed)) {
            free(copy);
            return REXX_VM_STATUS_NUMERIC_ERROR;
        }
        free(copy);
        *number = parsed;
        return REXX_VM_STATUS_OK;
    }
}

/* Convert to a nonnegative integral size, rejecting fractions and overflow. */
static RexxVMStatus rexx_builtin_size(const RexxValue *value, size_t *number)
{
    double parsed;
    size_t converted;
    RexxVMStatus status;

    if (number == NULL) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    status = rexx_builtin_number(value, &parsed);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    if (parsed < 0.0 || parsed >= (double)SIZE_MAX ||
        !rexx_numeric_is_integral(parsed)) {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    converted = (size_t)parsed;
    if ((double)converted != parsed) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    *number = converted;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_set_empty(RexxValue *result)
{
    return rexx_value_set_string(result, rexx_builtin_empty, 0u);
}

static RexxVMStatus rexx_builtin_length(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_value_set_number(result, (double)string.length);
    }
    return status;
}

static RexxVMStatus rexx_builtin_substr(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinStringView pad;
    size_t start;
    size_t requested_length = 0u;
    size_t available;
    size_t copied;
    size_t output_length;
    int has_length;
    char pad_byte = ' ';
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    status = rexx_builtin_size(&arguments[1], &start);
    if (status != REXX_VM_STATUS_OK || start == 0u) {
        return status == REXX_VM_STATUS_OK
                   ? REXX_VM_STATUS_INVALID_FUNCTION_CALL : status;
    }

    has_length = argument_count >= 3u &&
                 arguments[2].type != REXX_VALUE_UNSET;
    if (has_length) {
        status = rexx_builtin_size(&arguments[2], &requested_length);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
    }
    if (argument_count >= 4u && arguments[3].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[3], &pad);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
        if (pad.length != 1u) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        pad_byte = pad.bytes[0];
    }

    if (has_length) {
        output_length = requested_length;
    } else {
        output_length = start <= string.length
                            ? string.length - (start - 1u) : 0u;
    }
    if (output_length == SIZE_MAX) {
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    available = start <= string.length
                    ? string.length - (start - 1u) : 0u;
    copied = output_length < available ? output_length : available;

    if (copied == output_length) {
        const char *slice = copied == 0u
                                ? rexx_builtin_empty
                                : string.bytes + (start - 1u);
        return rexx_value_set_string(result, slice, copied);
    }

    {
        char *buffer = (char *)malloc(output_length + 1u);
        if (buffer == NULL) {
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        }
        if (copied != 0u) {
            memcpy(buffer, string.bytes + (start - 1u), copied);
        }
        memset(buffer + copied, (unsigned char)pad_byte,
               output_length - copied);
        buffer[output_length] = '\0';
        status = rexx_value_set_string(result, buffer, output_length);
        free(buffer);
        return status;
    }
}

static RexxVMStatus rexx_builtin_word(const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    RexxBuiltinStringView string;
    double word_number;
    size_t index = 0u;
    size_t position = 0u;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    status = rexx_builtin_number(&arguments[1], &word_number);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    if (!rexx_numeric_is_integral(word_number)) {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    if (word_number <= 0.0 || word_number >= (double)SIZE_MAX) {
        return rexx_builtin_set_empty(result);
    }
    index = (size_t)word_number;

    while (position < string.length) {
        size_t word_start;
        size_t word_length;

        while (position < string.length &&
               rexx_builtin_ascii_space((unsigned char)string.bytes[position])) {
            ++position;
        }
        if (position == string.length) {
            break;
        }
        word_start = position;
        while (position < string.length &&
               !rexx_builtin_ascii_space((unsigned char)string.bytes[position])) {
            ++position;
        }
        --index;
        if (index == 0u) {
            word_length = position - word_start;
            return rexx_value_set_string(result,
                                         string.bytes + word_start,
                                         word_length);
        }
    }
    return rexx_builtin_set_empty(result);
}

static RexxVMStatus rexx_builtin_words(const RexxValue *arguments,
                                       size_t argument_count,
                                       RexxValue *result)
{
    RexxBuiltinStringView string;
    size_t position = 0u;
    size_t count = 0u;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    while (position < string.length) {
        while (position < string.length &&
               rexx_builtin_ascii_space((unsigned char)string.bytes[position])) {
            ++position;
        }
        if (position == string.length) {
            break;
        }
        ++count;
        while (position < string.length &&
               !rexx_builtin_ascii_space((unsigned char)string.bytes[position])) {
            ++position;
        }
    }
    return rexx_value_set_number(result, (double)count);
}

static RexxVMStatus rexx_builtin_strip(const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinStringView option;
    RexxBuiltinStringView character_set;
    size_t first = 0u;
    size_t last;
    char option_byte = 'B';
    char strip_byte = ' ';
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    if (argument_count >= 2u && arguments[1].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[1], &option);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
        if (option.length != 1u) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        option_byte = (char)rexx_builtin_ascii_upper((unsigned char)option.bytes[0]);
    }
    if (option_byte != 'B' && option_byte != 'L' && option_byte != 'T') {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[2], &character_set);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
        if (character_set.length != 1u) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        strip_byte = character_set.bytes[0];
    }

    last = string.length;
    if (option_byte == 'B' || option_byte == 'L') {
        while (first < last && string.bytes[first] == strip_byte) {
            ++first;
        }
    }
    if (option_byte == 'B' || option_byte == 'T') {
        while (last > first && string.bytes[last - 1u] == strip_byte) {
            --last;
        }
    }
    return rexx_value_set_string(result, string.bytes + first, last - first);
}

static RexxVMStatus rexx_builtin_pos(const RexxValue *arguments,
                                     size_t argument_count,
                                     RexxValue *result)
{
    RexxBuiltinStringView needle;
    RexxBuiltinStringView haystack;
    size_t start = 1u;
    size_t start_index;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &needle);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_string_view(&arguments[1], &haystack);
    }
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_size(&arguments[2], &start);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
        if (start == 0u) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
    }

    start_index = start - 1u;
    if (start_index > haystack.length) {
        return rexx_value_set_number(result, 0.0);
    }
    if (needle.length == 0u) {
        return rexx_value_set_number(result, (double)start);
    }
    if (needle.length > haystack.length - start_index) {
        return rexx_value_set_number(result, 0.0);
    }

    {
        size_t last_start = haystack.length - needle.length;
        size_t position;
        for (position = start_index; position <= last_start; ++position) {
            if (memcmp(haystack.bytes + position, needle.bytes,
                       needle.length) == 0) {
                return rexx_value_set_number(result, (double)(position + 1u));
            }
            if (position == last_start) {
                break;
            }
        }
    }
    return rexx_value_set_number(result, 0.0);
}

static size_t rexx_builtin_table_find(const char *table,
                                      size_t table_length,
                                      unsigned char byte)
{
    size_t index;
    for (index = 0u; index < table_length; ++index) {
        if ((unsigned char)table[index] == byte) {
            return index;
        }
    }
    return SIZE_MAX;
}

static RexxVMStatus rexx_builtin_translate(const RexxValue *arguments,
                                           size_t argument_count,
                                           RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinStringView table_out;
    RexxBuiltinStringView table_in;
    RexxBuiltinStringView pad;
    char *translated;
    char pad_byte = ' ';
    size_t index;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    table_out.bytes = rexx_builtin_uppercase;
    table_out.length = sizeof(rexx_builtin_uppercase) - 1u;
    table_in.bytes = rexx_builtin_lowercase;
    table_in.length = sizeof(rexx_builtin_lowercase) - 1u;
    if (argument_count >= 2u && arguments[1].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[1], &table_out);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
    }
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[2], &table_in);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
    }
    if (argument_count >= 4u && arguments[3].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[3], &pad);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
        if (pad.length != 1u) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        pad_byte = pad.bytes[0];
    }
    if (string.length == SIZE_MAX) {
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    translated = (char *)malloc(string.length == 0u ? 1u : string.length);
    if (translated == NULL) {
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    }
    for (index = 0u; index < string.length; ++index) {
        size_t table_index = rexx_builtin_table_find(
            table_in.bytes, table_in.length,
            (unsigned char)string.bytes[index]);
        translated[index] = table_index == SIZE_MAX
                                 ? string.bytes[index]
                                 : table_index < table_out.length
                                       ? table_out.bytes[table_index]
                                       : pad_byte;
    }
    status = rexx_value_set_string(result, translated, string.length);
    free(translated);
    return status;
}

static const RexxBuiltinDescriptor rexx_builtin_table[] = {
    { "LENGTH", 1u, 1u, rexx_builtin_length },
    { "SUBSTR", 2u, 4u, rexx_builtin_substr },
    { "WORD", 2u, 2u, rexx_builtin_word },
    { "WORDS", 1u, 1u, rexx_builtin_words },
    { "STRIP", 1u, 3u, rexx_builtin_strip },
    { "POS", 2u, 3u, rexx_builtin_pos },
    { "TRANSLATE", 1u, 4u, rexx_builtin_translate }
};

const RexxBuiltinDescriptor *rexx_builtin_lookup(const char *name,
                                                 size_t name_length)
{
    size_t index;

    if (name == NULL || name_length == 0u) {
        return NULL;
    }
    for (index = 0u;
         index < sizeof(rexx_builtin_table) / sizeof(rexx_builtin_table[0]);
         ++index) {
        const RexxBuiltinDescriptor *descriptor = &rexx_builtin_table[index];
        if (rexx_builtin_ascii_equal(name, name_length, descriptor->name,
                                     strlen(descriptor->name))) {
            return descriptor;
        }
    }
    return NULL;
}

RexxVMStatus rexx_builtin_dispatch(const char *name,
                                   size_t name_length,
                                   const RexxValue *arguments,
                                   size_t argument_count,
                                   RexxValue *result)
{
    const RexxBuiltinDescriptor *descriptor;
    RexxVMStatus status;

    if (result == NULL || (arguments == NULL && argument_count != 0u)) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    rexx_value_clear(result);
    descriptor = rexx_builtin_lookup(name, name_length);
    if (descriptor == NULL) {
        return REXX_VM_STATUS_UNKNOWN_FUNCTION;
    }
    if (argument_count < descriptor->minimum_arguments ||
        argument_count > descriptor->maximum_arguments) {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    status = descriptor->function(arguments, argument_count, result);
    if (status != REXX_VM_STATUS_OK) {
        rexx_value_clear(result);
    }
    return status;
}
