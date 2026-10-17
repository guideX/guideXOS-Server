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
static RexxVMStatus rexx_builtin_decimal(const RexxValue *value,
                                         RexxDecimal *number)
{
    RexxBuiltinStringView view;
    RexxNumericStatus numeric_status;

    if (value == NULL || number == NULL) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    if (value->type == REXX_VALUE_UNSET) return REXX_VM_STATUS_NUMERIC_ERROR;
    if (value->type == REXX_VALUE_STRING) {
        if (value->as.string.bytes == NULL && value->as.string.length != 0u)
            return REXX_VM_STATUS_INVALID_ARGUMENT;
        numeric_status = rexx_decimal_parse(value->as.string.bytes,
                                            value->as.string.length, number);
    } else {
        RexxVMStatus status = rexx_builtin_string_view(value, &view);
        if (status != REXX_VM_STATUS_OK) return status;
        numeric_status = rexx_decimal_parse(view.bytes, view.length, number);
    }
    if (numeric_status == REXX_NUMERIC_OK) return REXX_VM_STATUS_OK;
    if (numeric_status == REXX_NUMERIC_RESOURCE_LIMIT)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    return REXX_VM_STATUS_NUMERIC_ERROR;
}

/* Convert to a nonnegative integral size, rejecting fractions and overflow. */
static RexxVMStatus rexx_builtin_size(const RexxValue *value, size_t *number)
{
    RexxDecimal parsed;
    uint32_t converted;
    RexxVMStatus status;
    RexxNumericStatus numeric_status;

    if (number == NULL) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    status = rexx_builtin_decimal(value, &parsed);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    if (parsed.sign < 0) {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    numeric_status = rexx_decimal_to_uint32(&parsed, &converted);
    if (numeric_status != REXX_NUMERIC_OK)
        return numeric_status == REXX_NUMERIC_RANGE
                   ? REXX_VM_STATUS_RESOURCE_LIMIT
                   : REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    *number = (size_t)converted;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_set_size(RexxValue *result, size_t value)
{
    char reverse[3u * sizeof(size_t) + 1u];
    char output[3u * sizeof(size_t) + 1u];
    size_t count = 0u, index;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0u; index < count; ++index)
        output[index] = reverse[count - index - 1u];
    return rexx_value_set_string(result, output, count);
}

static RexxVMStatus rexx_builtin_set_empty(RexxValue *result)
{
    return rexx_value_set_string(result, rexx_builtin_empty, 0u);
}

static RexxVMStatus rexx_builtin_allocate_result(RexxValue *result,
                                                size_t length,
                                                char **bytes)
{
    if (length > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    return rexx_value_allocate_string(result, length, bytes);
}

typedef struct RexxBuiltinWord {
    size_t start;
    size_t length;
} RexxBuiltinWord;

/* Advance a bounded byte-oriented scanner to the next blank-delimited word. */
static int rexx_builtin_next_word(const RexxBuiltinStringView *string,
                                  size_t *cursor,
                                  RexxBuiltinWord *word)
{
    size_t position;
    if (string == NULL || cursor == NULL || word == NULL) return 0;
    position = *cursor;
    while (position < string->length &&
           rexx_builtin_ascii_space((unsigned char)string->bytes[position])) {
        ++position;
    }
    if (position == string->length) {
        *cursor = position;
        return 0;
    }
    word->start = position;
    while (position < string->length &&
           !rexx_builtin_ascii_space((unsigned char)string->bytes[position])) {
        ++position;
    }
    word->length = position - word->start;
    *cursor = position;
    return 1;
}

static RexxVMStatus rexx_builtin_word_number(const RexxValue *value,
                                            size_t *number)
{
    RexxVMStatus status = rexx_builtin_size(value, number);
    if (status != REXX_VM_STATUS_OK) return status;
    if (*number == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    return REXX_VM_STATUS_OK;
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
        status = rexx_builtin_set_size(result, string.length);
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
    if (output_length > REXX_BUILTIN_MAX_RESULT_LENGTH) {
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
        char *buffer = NULL;
        status = rexx_builtin_allocate_result(result, output_length, &buffer);
        if (status != REXX_VM_STATUS_OK) return status;
        if (copied != 0u) {
            memcpy(buffer, string.bytes + (start - 1u), copied);
        }
        memset(buffer + copied, (unsigned char)pad_byte,
               output_length - copied);
        return REXX_VM_STATUS_OK;
    }
}

static RexxVMStatus rexx_builtin_word(const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    RexxBuiltinStringView string;
    size_t word_index;
    size_t current_index = 0u;
    size_t position = 0u;
    RexxBuiltinWord word;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    status = rexx_builtin_word_number(&arguments[1], &word_index);
    if (status == REXX_VM_STATUS_RESOURCE_LIMIT) {
        return rexx_builtin_set_empty(result);
    }
    if (status != REXX_VM_STATUS_OK) return status;

    while (rexx_builtin_next_word(&string, &position, &word)) {
        if (current_index == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
        ++current_index;
        if (current_index == word_index) {
            if (word.length > REXX_BUILTIN_MAX_RESULT_LENGTH)
                return REXX_VM_STATUS_RESOURCE_LIMIT;
            return rexx_value_set_string(result, string.bytes + word.start,
                                         word.length);
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
    RexxBuiltinWord word;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    while (rexx_builtin_next_word(&string, &position, &word)) {
        if (count == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
        ++count;
    }
    return rexx_builtin_set_size(result, count);
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
        if (option.length == 0u) {
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
    if (last - first > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
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
        return rexx_builtin_set_size(result, 0u);
    }
    if (needle.length == 0u) return rexx_builtin_set_size(result, 0u);
    if (needle.length > haystack.length - start_index) {
        return rexx_builtin_set_size(result, 0u);
    }

    {
        size_t last_start = haystack.length - needle.length;
        size_t position;
        for (position = start_index; position <= last_start; ++position) {
            if (memcmp(haystack.bytes + position, needle.bytes,
                       needle.length) == 0) {
                return rexx_builtin_set_size(result, position + 1u);
            }
            if (position == last_start) {
                break;
            }
        }
    }
    return rexx_builtin_set_size(result, 0u);
}

static RexxVMStatus rexx_builtin_copies(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView string;
    size_t count;
    size_t output_length;
    size_t filled;
    char *output = NULL;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_size(&arguments[1], &count);
    if (status != REXX_VM_STATUS_OK) return status;
    if (string.length != 0u && count > SIZE_MAX / string.length)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    output_length = string.length * count;
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK || output_length == 0u) return status;

    memcpy(output, string.bytes, string.length);
    filled = string.length;
    while (filled < output_length) {
        size_t chunk = filled < output_length - filled
                           ? filled : output_length - filled;
        memcpy(output + filled, output, chunk);
        filled += chunk;
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_space(const RexxValue *arguments,
                                       size_t argument_count,
                                       RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinStringView pad;
    RexxBuiltinWord word;
    size_t separator_count = 1u;
    size_t position = 0u;
    size_t word_count = 0u;
    size_t content_length = 0u;
    size_t separator_length = 0u;
    size_t output_length;
    size_t output_position = 0u;
    char pad_byte = ' ';
    char *output = NULL;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count >= 2u && arguments[1].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_size(&arguments[1], &separator_count);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[2], &pad);
        if (status != REXX_VM_STATUS_OK) return status;
        if (pad.length != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        pad_byte = pad.bytes[0];
    }

    while (rexx_builtin_next_word(&string, &position, &word)) {
        if (word.length > REXX_BUILTIN_MAX_RESULT_LENGTH - content_length)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        content_length += word.length;
        if (word_count == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
        ++word_count;
    }
    if (word_count > 1u) {
        size_t gaps = word_count - 1u;
        if (separator_count >
            (REXX_BUILTIN_MAX_RESULT_LENGTH - content_length) / gaps) {
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        separator_length = separator_count * gaps;
    }
    output_length = content_length + separator_length;
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;

    position = 0u;
    word_count = 0u;
    while (rexx_builtin_next_word(&string, &position, &word)) {
        if (word_count != 0u && separator_count != 0u) {
            memset(output + output_position, (unsigned char)pad_byte,
                   separator_count);
            output_position += separator_count;
        }
        memcpy(output + output_position, string.bytes + word.start,
               word.length);
        output_position += word.length;
        ++word_count;
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_compare(const RexxValue *arguments,
                                         size_t argument_count,
                                         RexxValue *result)
{
    RexxBuiltinStringView left;
    RexxBuiltinStringView right;
    RexxBuiltinStringView pad;
    size_t length;
    size_t position;
    unsigned char pad_byte = (unsigned char)' ';
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &left);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_string_view(&arguments[1], &right);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[2], &pad);
        if (status != REXX_VM_STATUS_OK) return status;
        if (pad.length != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        pad_byte = (unsigned char)pad.bytes[0];
    }

    length = left.length > right.length ? left.length : right.length;
    for (position = 0u; position < length; ++position) {
        unsigned char left_byte = position < left.length
            ? (unsigned char)left.bytes[position] : pad_byte;
        unsigned char right_byte = position < right.length
            ? (unsigned char)right.bytes[position] : pad_byte;
        if (left_byte != right_byte)
            return rexx_builtin_set_size(result, position + 1u);
    }
    return rexx_builtin_set_size(result, 0u);
}

static RexxVMStatus rexx_builtin_verify(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinStringView reference;
    RexxBuiltinStringView option;
    size_t start = 1u;
    size_t position;
    int find_member = 0;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_string_view(&arguments[1], &reference);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[2], &option);
        if (status != REXX_VM_STATUS_OK) return status;
        if (option.length == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (rexx_builtin_ascii_upper((unsigned char)option.bytes[0]) == 'M') {
            find_member = 1;
        } else if (rexx_builtin_ascii_upper(
                       (unsigned char)option.bytes[0]) != 'N') {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
    }
    if (argument_count >= 4u && arguments[3].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_size(&arguments[3], &start);
        if (status == REXX_VM_STATUS_RESOURCE_LIMIT)
            return rexx_builtin_set_size(result, 0u);
        if (status != REXX_VM_STATUS_OK) return status;
        if (start == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    if (string.length == 0u || start > string.length)
        return rexx_builtin_set_size(result, 0u);

    for (position = start - 1u; position < string.length; ++position) {
        int is_member = memchr(reference.bytes,
                               (unsigned char)string.bytes[position],
                               reference.length) != NULL;
        if (is_member == find_member)
            return rexx_builtin_set_size(result, position + 1u);
    }
    return rexx_builtin_set_size(result, 0u);
}

static int rexx_builtin_same_word(const RexxBuiltinStringView *left_string,
                                  const RexxBuiltinWord *left,
                                  const RexxBuiltinStringView *right_string,
                                  const RexxBuiltinWord *right)
{
    return left->length == right->length &&
           (left->length == 0u ||
            memcmp(left_string->bytes + left->start,
                   right_string->bytes + right->start,
                   left->length) == 0);
}

static RexxVMStatus rexx_builtin_wordpos(const RexxValue *arguments,
                                         size_t argument_count,
                                         RexxValue *result)
{
    const size_t max_word_comparisons = REXX_BUILTIN_MAX_RESULT_LENGTH;
    const size_t max_compare_bytes = REXX_BUILTIN_MAX_RESULT_LENGTH;
    RexxBuiltinStringView phrase;
    RexxBuiltinStringView string;
    RexxBuiltinWord phrase_first;
    RexxBuiltinWord string_word;
    size_t start = 1u;
    size_t phrase_cursor = 0u;
    size_t string_cursor = 0u;
    size_t string_word_index = 0u;
    size_t comparisons = 0u;
    size_t compare_bytes = 0u;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &phrase);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_string_view(&arguments[1], &string);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_word_number(&arguments[2], &start);
        if (status == REXX_VM_STATUS_RESOURCE_LIMIT)
            return rexx_builtin_set_size(result, 0u);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (!rexx_builtin_next_word(&phrase, &phrase_cursor, &phrase_first))
        return rexx_builtin_set_size(result, 0u);

    while (rexx_builtin_next_word(&string, &string_cursor, &string_word)) {
        size_t search_cursor;
        size_t phrase_position;
        RexxBuiltinWord wanted = phrase_first;
        RexxBuiltinWord actual = string_word;

        if (string_word_index == SIZE_MAX)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        ++string_word_index;
        if (string_word_index < start) continue;
        search_cursor = string_cursor;
        phrase_position = phrase_cursor;

        for (;;) {
            if (comparisons >= max_word_comparisons)
                return REXX_VM_STATUS_RESOURCE_LIMIT;
            ++comparisons;
            if (wanted.length == actual.length) {
                if (wanted.length > max_compare_bytes - compare_bytes)
                    return REXX_VM_STATUS_RESOURCE_LIMIT;
                compare_bytes += wanted.length;
            }
            if (!rexx_builtin_same_word(&phrase, &wanted,
                                        &string, &actual)) {
                break;
            }
            if (!rexx_builtin_next_word(&phrase, &phrase_position, &wanted))
                return rexx_builtin_set_size(result, string_word_index);
            if (!rexx_builtin_next_word(&string, &search_cursor, &actual)) {
                break;
            }
        }
    }
    return rexx_builtin_set_size(result, 0u);
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
    unsigned char default_input[256];
    char *translated = NULL;
    char pad_byte = ' ';
    size_t index;
    int has_table_out;
    int has_table_in;
    int has_pad;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    has_table_out = argument_count >= 2u &&
                    arguments[1].type != REXX_VALUE_UNSET;
    has_table_in = argument_count >= 3u &&
                   arguments[2].type != REXX_VALUE_UNSET;
    has_pad = argument_count >= 4u &&
              arguments[3].type != REXX_VALUE_UNSET;
    table_out.bytes = rexx_builtin_empty;
    table_out.length = 0u;
    table_in.bytes = (const char *)default_input;
    table_in.length = sizeof(default_input);
    if (has_table_out) {
        status = rexx_builtin_string_view(&arguments[1], &table_out);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
    }
    if (has_table_in) {
        status = rexx_builtin_string_view(&arguments[2], &table_in);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
    }
    if (has_pad) {
        status = rexx_builtin_string_view(&arguments[3], &pad);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
        if (pad.length != 1u) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        pad_byte = pad.bytes[0];
    }

    if (string.length > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    if (!has_table_out && !has_table_in && !has_pad) {
        status = rexx_builtin_allocate_result(result, string.length,
                                              &translated);
        if (status != REXX_VM_STATUS_OK) return status;
        for (index = 0u; index < string.length; ++index) {
            translated[index] = (char)rexx_builtin_ascii_upper(
                (unsigned char)string.bytes[index]);
        }
        return REXX_VM_STATUS_OK;
    }

    if (!has_table_in) {
        for (index = 0u; index < sizeof(default_input); ++index)
            default_input[index] = (unsigned char)index;
    }
    status = rexx_builtin_allocate_result(result, string.length, &translated);
    if (status != REXX_VM_STATUS_OK) return status;
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
    return REXX_VM_STATUS_OK;
}

static const RexxBuiltinDescriptor rexx_builtin_table[] = {
    { "LENGTH", 1u, 1u, rexx_builtin_length },
    { "SUBSTR", 2u, 4u, rexx_builtin_substr },
    { "WORD", 2u, 2u, rexx_builtin_word },
    { "WORDS", 1u, 1u, rexx_builtin_words },
    { "STRIP", 1u, 3u, rexx_builtin_strip },
    { "POS", 2u, 3u, rexx_builtin_pos },
    { "TRANSLATE", 1u, 4u, rexx_builtin_translate },
    { "WORDPOS", 2u, 3u, rexx_builtin_wordpos },
    { "SPACE", 1u, 3u, rexx_builtin_space },
    { "COPIES", 2u, 2u, rexx_builtin_copies },
    { "COMPARE", 2u, 3u, rexx_builtin_compare },
    { "VERIFY", 2u, 4u, rexx_builtin_verify }
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
    {
        size_t index;
        for (index = 0u; index < descriptor->minimum_arguments; ++index) {
            if (arguments[index].type == REXX_VALUE_UNSET) {
                return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
            }
        }
    }
    status = descriptor->function(arguments, argument_count, result);
    if (status != REXX_VM_STATUS_OK) {
        rexx_value_clear(result);
    }
    return status;
}
