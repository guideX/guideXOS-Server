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

static RexxVMStatus rexx_builtin_address(RexxVM *vm,
                                       const RexxValue *arguments,
                                       size_t argument_count,
                                       RexxValue *result)
{
    const char *environment;
    size_t environment_length;
    RexxVMStatus status;
    (void)arguments;
    if (argument_count != 0u || vm == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    status = rexx_vm_get_address_environment(
        vm, &environment, &environment_length);
    if (status != REXX_VM_STATUS_OK) return status;
    while (environment_length != 0u &&
           environment[environment_length - 1u] == ' ')
        --environment_length;
    return rexx_value_set_string(result, environment, environment_length);
}

static int rexx_builtin_trace_option_action(const char *bytes, size_t length,
                                            char *action)
{
    size_t index;
    unsigned char first;
    if (bytes == NULL || action == NULL || length == 0u)
        return -1;
    for (index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)bytes[index];
        if (!((byte >= 'A' && byte <= 'Z') ||
              (byte >= 'a' && byte <= 'z')))
            return -1;
    }
    first = rexx_builtin_ascii_upper((unsigned char)bytes[0]);
    if (first == 'N' || first == 'O' || first == 'E' || first == 'F') {
        *action = (char)first;
        return 1;
    }
    if (first == 'A' || first == 'C' || first == 'I' || first == 'L' ||
        first == 'R' || first == 'S') {
        *action = (char)first;
        return 0;
    }
    return -1;
}

static RexxVMStatus rexx_builtin_trace(RexxVM *vm,
                                      const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    RexxBuiltinStringView option;
    size_t index = 0u;
    char new_action;
    int option_capability;
    RexxVMStatus status;

    if (vm == NULL || (argument_count != 0u && arguments == NULL))
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (vm->trace_action != 'N' && vm->trace_action != 'O' &&
        vm->trace_action != 'E' && vm->trace_action != 'F')
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (argument_count == 0u)
        return rexx_value_set_string(result, &vm->trace_action, 1u);
    if (arguments[0].type != REXX_VALUE_STRING)
        return REXX_VM_STATUS_INVALID_TRACE_REQUEST;
    status = rexx_builtin_string_view(&arguments[0], &option);
    if (status != REXX_VM_STATUS_OK) return status;
    if (option.length == 0u)
        return REXX_VM_STATUS_INVALID_TRACE_REQUEST;
    while (index < option.length &&
           (option.bytes[index] == '?' || option.bytes[index] == '!'))
        ++index;
    if (index == option.length) {
        if (index != 0u)
            return REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY;
        return REXX_VM_STATUS_INVALID_TRACE_REQUEST;
    }
    option_capability = rexx_builtin_trace_option_action(
        option.bytes + index, option.length - index, &new_action);
    if (option_capability < 0)
        return REXX_VM_STATUS_INVALID_TRACE_REQUEST;
    if (index != 0u || option_capability == 0)
        return REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY;

    /* Construct the previous value before changing VM state so allocation
     * failure cannot leave a partially applied TRACE request. */
    status = rexx_value_set_string(result, &vm->trace_action, 1u);
    if (status != REXX_VM_STATUS_OK) return status;
    vm->trace_action = new_action;
    vm->trace_enabled = new_action == 'O' ? 0u : 1u;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_allocate_result(RexxValue *result,
                                                size_t length,
                                                char **bytes)
{
    if (length > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    return rexx_value_allocate_string(result, length, bytes);
}

static RexxVMStatus rexx_builtin_reverse(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView source;
    char *output;
    size_t index;
    RexxVMStatus status;

    if (argument_count != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    status = rexx_builtin_string_view(&arguments[0], &source);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_allocate_result(result, source.length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    for (index = 0u; index < source.length; ++index)
        output[source.length - index - 1u] = source.bytes[index];
    return REXX_VM_STATUS_OK;
}

/* IBM z/OS 2.5 TSO/E REXX error text. These are syntax error numbers, not
 * guideXOS VM status values; host diagnostics remain in rexx_vm.c. */
static const char *const rexx_builtin_ibm_error_text[47] = {
    "Program is unreadable",                         /* 3 */
    "Program interrupted",                          /* 4 */
    "Machine storage exhausted",                    /* 5 */
    "Unmatched \"/*\" or quote",                    /* 6 */
    "WHEN or OTHERWISE expected",                   /* 7 */
    "Unexpected THEN or ELSE",                      /* 8 */
    "Unexpected WHEN or OTHERWISE",                 /* 9 */
    "Unexpected or unmatched END",                  /* 10 */
    "Control stack full",                           /* 11 */
    "Clause too long",                              /* 12 */
    "Invalid character in program",                 /* 13 */
    "Incomplete DO/SELECT/IF",                      /* 14 */
    "Invalid hexadecimal or binary string",         /* 15 */
    "Label not found",                              /* 16 */
    "Unexpected PROCEDURE",                         /* 17 */
    "THEN expected",                                /* 18 */
    "String or symbol expected",                    /* 19 */
    "Symbol expected",                              /* 20 */
    "Invalid data at end of clause",                /* 21 */
    "Invalid character string",                     /* 22 */
    "Invalid SBCS/DBCS mixed string",               /* 23 */
    "Invalid TRACE request",                        /* 24 */
    "Invalid subkeyword found",                     /* 25 */
    "Invalid whole number",                         /* 26 */
    "Invalid DO syntax",                            /* 27 */
    "Invalid LEAVE or ITERATE",                     /* 28 */
    "Environment name too long",                    /* 29 */
    "Name or string > 250 characters",              /* 30 */
    "Name starts with number or \".\"",           /* 31 */
    "Invalid use of stem",                          /* 32 */
    "Invalid expression result",                    /* 33 */
    "Logical value not 0 or 1",                     /* 34 */
    "Invalid expression",                           /* 35 */
    "Unmatched \"(\" in expression",                 /* 36 */
    "Unexpected \",\" or \")\"",                      /* 37 */
    "Invalid template or pattern",                  /* 38 */
    "Evaluation stack overflow",                    /* 39 */
    "Incorrect call to routine",                    /* 40 */
    "Bad arithmetic conversion",                    /* 41 */
    "Arithmetic overflow/underflow",                /* 42 */
    "Routine not found",                            /* 43 */
    "Function did not return data",                 /* 44 */
    "No data specified on function RETURN",         /* 45 */
    "Invalid variable reference",                   /* 46 */
    "Unexpected label",                             /* 47 */
    "Failure in system service",                    /* 48 */
    "Language processor failure"                    /* 49 */
};

static RexxVMStatus rexx_builtin_errortext(const RexxValue *arguments,
                                           size_t argument_count,
                                           RexxValue *result)
{
    size_t error_number;
    RexxVMStatus status;

    if (argument_count != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    status = rexx_builtin_size(&arguments[0], &error_number);
    if (status == REXX_VM_STATUS_NUMERIC_ERROR ||
        status == REXX_VM_STATUS_RESOURCE_LIMIT)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (status != REXX_VM_STATUS_OK) return status;
    if (error_number > 99u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (error_number < 3u || error_number > 49u)
        return rexx_builtin_set_empty(result);
    {
        const char *message =
            rexx_builtin_ibm_error_text[error_number - 3u];
        return rexx_value_set_string(result, message, strlen(message));
    }
}

static RexxVMStatus rexx_builtin_source_line(const RexxBytecodeChunk *chunk,
                                            size_t requested,
                                            RexxValue *result)
{
    size_t line;
    size_t start = 0u;
    size_t end;

    if (chunk == NULL || chunk->source_text == NULL)
        return rexx_builtin_set_empty(result);
    if (requested == 0u || requested > chunk->source_line_count)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    for (line = 1u; line < requested; ++line) {
        while (start < chunk->source_length &&
               chunk->source_text[start] != '\n')
            ++start;
        if (start < chunk->source_length) ++start;
    }
    end = start;
    while (end < chunk->source_length && chunk->source_text[end] != '\n')
        ++end;
    if (end < chunk->source_length && chunk->source_text[end] == '\n' &&
        end > start && chunk->source_text[end - 1u] == '\r')
        --end;
    return rexx_value_set_string(result, chunk->source_text + start,
                                 end - start);
}

static RexxVMStatus rexx_builtin_sourceline(RexxVM *vm,
                                            const RexxValue *arguments,
                                            size_t argument_count,
                                            RexxValue *result)
{
    const RexxBytecodeChunk *chunk = vm != NULL
        ? (const RexxBytecodeChunk *)vm->active_chunk : NULL;
    size_t requested;
    RexxVMStatus status;

    if (argument_count == 0u)
        return rexx_builtin_set_size(
            result, chunk != NULL && chunk->source_text != NULL
                        ? chunk->source_line_count : 0u);
    status = rexx_builtin_size(&arguments[0], &requested);
    if (status == REXX_VM_STATUS_NUMERIC_ERROR ||
        status == REXX_VM_STATUS_RESOURCE_LIMIT)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (status != REXX_VM_STATUS_OK) return status;
    if (requested == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    return rexx_builtin_source_line(chunk, requested, result);
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

static RexxVMStatus rexx_builtin_optional_size(const RexxValue *arguments,
                                               size_t argument_count,
                                               size_t argument_index,
                                               size_t default_value,
                                               size_t *value)
{
    if (value == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    *value = default_value;
    if (argument_count <= argument_index ||
        arguments[argument_index].type == REXX_VALUE_UNSET) {
        return REXX_VM_STATUS_OK;
    }
    return rexx_builtin_size(&arguments[argument_index], value);
}

static RexxVMStatus rexx_builtin_optional_pad(const RexxValue *arguments,
                                               size_t argument_count,
                                               size_t argument_index,
                                               char *pad_byte)
{
    RexxBuiltinStringView pad;
    RexxVMStatus status;

    if (pad_byte == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    *pad_byte = ' ';
    if (argument_count <= argument_index ||
        arguments[argument_index].type == REXX_VALUE_UNSET) {
        return REXX_VM_STATUS_OK;
    }
    status = rexx_builtin_string_view(&arguments[argument_index], &pad);
    if (status != REXX_VM_STATUS_OK) return status;
    if (pad.length != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    *pad_byte = pad.bytes[0];
    return REXX_VM_STATUS_OK;
}

/* Length operands in these classic string functions must be present numeric
 * values. The general built-in size helper intentionally treats the empty
 * string as zero for inherited functions, so keep the stricter RX18 rule
 * local to the new functions. */
static RexxVMStatus rexx_builtin_required_length(const RexxValue *value,
                                                 size_t *length)
{
    RexxBuiltinStringView view;
    if (value == NULL || length == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (value->type == REXX_VALUE_UNSET)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (value->type == REXX_VALUE_STRING) {
        RexxVMStatus status = rexx_builtin_string_view(value, &view);
        if (status != REXX_VM_STATUS_OK) return status;
        if (view.length == 0u)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    return rexx_builtin_size(value, length);
}

static RexxVMStatus rexx_builtin_abbrev(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView information, info;
    RexxDecimal minimum_decimal;
    uint32_t minimum_value;
    size_t minimum_length;
    RexxNumericStatus numeric_status;
    RexxVMStatus status;
    int matched = 0;

    if (arguments == NULL || argument_count < 2u || argument_count > 3u ||
        arguments[0].type == REXX_VALUE_UNSET ||
        arguments[1].type == REXX_VALUE_UNSET) {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    status = rexx_builtin_string_view(&arguments[0], &information);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_string_view(&arguments[1], &info);
    if (status != REXX_VM_STATUS_OK) return status;

    minimum_length = info.length;
    if (argument_count == 3u &&
        arguments[2].type != REXX_VALUE_UNSET) {
        RexxBuiltinStringView minimum_view;
        if (arguments[2].type == REXX_VALUE_STRING) {
            status = rexx_builtin_string_view(&arguments[2], &minimum_view);
            if (status != REXX_VM_STATUS_OK) return status;
            if (minimum_view.length == 0u)
                return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        status = rexx_builtin_decimal(&arguments[2], &minimum_decimal);
        if (status != REXX_VM_STATUS_OK) return status;
        if (minimum_decimal.sign < 0)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        numeric_status = rexx_decimal_to_uint32(&minimum_decimal,
                                                &minimum_value);
        if (numeric_status == REXX_NUMERIC_INVALID)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (numeric_status == REXX_NUMERIC_RANGE) {
            /* Every representable input string is far shorter than UINT32_MAX. */
            return rexx_value_set_string(result, rexx_builtin_false, 1u);
        }
        if (numeric_status != REXX_NUMERIC_OK)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        minimum_length = (size_t)minimum_value;
    }

    if (info.length <= information.length &&
        info.length >= minimum_length &&
        (info.length == 0u ||
         memcmp(information.bytes, info.bytes, info.length) == 0)) {
        matched = 1;
    }
    return rexx_value_set_string(result,
                                 matched ? rexx_builtin_true : rexx_builtin_false,
                                 1u);
}

/* CENTER and CENTRE are aliases for this single allocation and byte-copy path. */
static RexxVMStatus rexx_builtin_center(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView source;
    size_t requested_length;
    size_t offset;
    size_t padding;
    char pad_byte;
    char *output = NULL;
    RexxVMStatus status;

    if (arguments == NULL || argument_count < 2u || argument_count > 3u ||
        arguments[0].type == REXX_VALUE_UNSET ||
        arguments[1].type == REXX_VALUE_UNSET) {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    status = rexx_builtin_string_view(&arguments[0], &source);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_required_length(&arguments[1], &requested_length);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_optional_pad(arguments, argument_count, 2u,
                                       &pad_byte);
    if (status != REXX_VM_STATUS_OK) return status;

    status = rexx_builtin_allocate_result(result, requested_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    if (requested_length > source.length) {
        padding = requested_length - source.length;
        offset = padding / 2u;
        if (offset != 0u)
            memset(output, (unsigned char)pad_byte, offset);
        if (source.length != 0u)
            memcpy(output + offset, source.bytes, source.length);
        if (padding != offset)
            memset(output + offset + source.length,
                   (unsigned char)pad_byte, padding - offset);
    } else {
        offset = (source.length - requested_length) / 2u;
        if (requested_length != 0u)
            memcpy(output, source.bytes + offset, requested_length);
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_add_output_length(size_t *total,
                                                   size_t addition)
{
    if (total == NULL || *total > REXX_BUILTIN_MAX_RESULT_LENGTH ||
        addition > REXX_BUILTIN_MAX_RESULT_LENGTH - *total) {
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    *total += addition;
    return REXX_VM_STATUS_OK;
}

static void rexx_builtin_write_padded(const RexxBuiltinStringView *source,
                                      char *destination,
                                      size_t length,
                                      char pad_byte)
{
    size_t copied = source->length < length ? source->length : length;
    if (copied != 0u) memcpy(destination, source->bytes, copied);
    if (length > copied) {
        memset(destination + copied, (unsigned char)pad_byte, length - copied);
    }
}

static RexxVMStatus rexx_builtin_left(const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    RexxBuiltinStringView string;
    size_t requested_length;
    size_t copied;
    char pad_byte;
    char *buffer = NULL;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_size(&arguments[1], &requested_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_pad(arguments, argument_count, 2u,
                                           &pad_byte);
    if (status != REXX_VM_STATUS_OK) return status;
    if (requested_length > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;

    copied = string.length < requested_length ? string.length : requested_length;
    status = rexx_builtin_allocate_result(result, requested_length, &buffer);
    if (status != REXX_VM_STATUS_OK) return status;
    if (copied != 0u) memcpy(buffer, string.bytes, copied);
    if (requested_length > copied) {
        memset(buffer + copied, (unsigned char)pad_byte,
               requested_length - copied);
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_right(const RexxValue *arguments,
                                       size_t argument_count,
                                       RexxValue *result)
{
    RexxBuiltinStringView string;
    size_t requested_length;
    size_t copied;
    size_t padding;
    char pad_byte;
    char *buffer = NULL;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_size(&arguments[1], &requested_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_pad(arguments, argument_count, 2u,
                                           &pad_byte);
    if (status != REXX_VM_STATUS_OK) return status;
    if (requested_length > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;

    copied = string.length < requested_length ? string.length : requested_length;
    padding = requested_length - copied;
    status = rexx_builtin_allocate_result(result, requested_length, &buffer);
    if (status != REXX_VM_STATUS_OK) return status;
    if (padding != 0u) memset(buffer, (unsigned char)pad_byte, padding);
    if (copied != 0u) {
        memcpy(buffer + padding, string.bytes + (string.length - copied), copied);
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_insert(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView inserted;
    RexxBuiltinStringView target;
    size_t position = 0u;
    size_t inserted_length;
    size_t prefix_length;
    size_t gap_length;
    size_t suffix_start;
    size_t suffix_length;
    size_t output_length = 0u;
    size_t offset = 0u;
    char pad_byte;
    char *buffer = NULL;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &inserted);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_string_view(&arguments[1], &target);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_size(arguments, argument_count, 2u,
                                            0u, &position);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_size(arguments, argument_count, 3u,
                                            inserted.length, &inserted_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_pad(arguments, argument_count, 4u,
                                           &pad_byte);
    if (status != REXX_VM_STATUS_OK) return status;

    prefix_length = position < target.length ? position : target.length;
    gap_length = position > target.length ? position - target.length : 0u;
    suffix_start = position < target.length ? position : target.length;
    suffix_length = position < target.length ? target.length - position : 0u;
    status = rexx_builtin_add_output_length(&output_length, prefix_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_add_output_length(&output_length, gap_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_add_output_length(&output_length, inserted_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_add_output_length(&output_length, suffix_length);
    if (status != REXX_VM_STATUS_OK) return status;

    status = rexx_builtin_allocate_result(result, output_length, &buffer);
    if (status != REXX_VM_STATUS_OK) return status;
    if (prefix_length != 0u) {
        memcpy(buffer + offset, target.bytes, prefix_length);
        offset += prefix_length;
    }
    if (gap_length != 0u) {
        memset(buffer + offset, (unsigned char)pad_byte, gap_length);
        offset += gap_length;
    }
    rexx_builtin_write_padded(&inserted, buffer + offset, inserted_length,
                              pad_byte);
    offset += inserted_length;
    if (suffix_length != 0u) {
        memcpy(buffer + offset, target.bytes + suffix_start, suffix_length);
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_overlay(const RexxValue *arguments,
                                         size_t argument_count,
                                         RexxValue *result)
{
    RexxBuiltinStringView inserted;
    RexxBuiltinStringView target;
    size_t position = 1u;
    size_t start;
    size_t overlay_length;
    size_t overlay_end;
    size_t prefix_length;
    size_t gap_length;
    size_t suffix_start;
    size_t suffix_length;
    size_t output_length;
    size_t offset = 0u;
    char pad_byte;
    char *buffer = NULL;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &inserted);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_string_view(&arguments[1], &target);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_size(arguments, argument_count, 2u,
                                            1u, &position);
    if (status == REXX_VM_STATUS_OK && position == 0u)
        status = REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_size(arguments, argument_count, 3u,
                                            inserted.length, &overlay_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_optional_pad(arguments, argument_count, 4u,
                                           &pad_byte);
    if (status != REXX_VM_STATUS_OK) return status;
    start = position - 1u;
    status = rexx_builtin_add_output_length(&start, overlay_length);
    if (status != REXX_VM_STATUS_OK) return status;
    overlay_end = start;
    start = position - 1u;
    prefix_length = start < target.length ? start : target.length;
    gap_length = start > target.length ? start - target.length : 0u;
    suffix_start = overlay_end < target.length ? overlay_end : target.length;
    suffix_length = target.length - suffix_start;
    output_length = target.length > overlay_end ? target.length : overlay_end;
    if (output_length > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;

    status = rexx_builtin_allocate_result(result, output_length, &buffer);
    if (status != REXX_VM_STATUS_OK) return status;
    if (prefix_length != 0u) {
        memcpy(buffer + offset, target.bytes, prefix_length);
        offset += prefix_length;
    }
    if (gap_length != 0u) {
        memset(buffer + offset, (unsigned char)pad_byte, gap_length);
        offset += gap_length;
    }
    rexx_builtin_write_padded(&inserted, buffer + offset, overlay_length,
                              pad_byte);
    offset += overlay_length;
    if (suffix_length != 0u) {
        memcpy(buffer + offset, target.bytes + suffix_start, suffix_length);
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_delstr(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxBuiltinStringView string;
    size_t start;
    size_t delete_length;
    size_t available;
    size_t suffix_start;
    size_t suffix_length;
    size_t output_length;
    char *buffer = NULL;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_word_number(&arguments[1], &start);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count < 3u || arguments[2].type == REXX_VALUE_UNSET) {
        delete_length = string.length;
    } else {
        status = rexx_builtin_size(&arguments[2], &delete_length);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (start > string.length || delete_length == 0u)
        return rexx_value_set_string(result, string.bytes, string.length);

    available = string.length - (start - 1u);
    if (delete_length > available) delete_length = available;
    suffix_start = start - 1u + delete_length;
    suffix_length = string.length - suffix_start;
    output_length = (start - 1u) + suffix_length;
    status = rexx_builtin_allocate_result(result, output_length, &buffer);
    if (status != REXX_VM_STATUS_OK) return status;
    if (start > 1u) memcpy(buffer, string.bytes, start - 1u);
    if (suffix_length != 0u) {
        memcpy(buffer + (start - 1u), string.bytes + suffix_start,
               suffix_length);
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_delword(const RexxValue *arguments,
                                         size_t argument_count,
                                         RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinWord word;
    size_t first_word_number;
    size_t delete_count;
    size_t current_word_number = 0u;
    size_t cursor = 0u;
    size_t delete_start = 0u;
    size_t delete_end = 0u;
    size_t selected = 0u;
    size_t output_length;
    char *buffer = NULL;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_word_number(&arguments[1], &first_word_number);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count < 3u || arguments[2].type == REXX_VALUE_UNSET) {
        delete_count = string.length;
    } else {
        status = rexx_builtin_size(&arguments[2], &delete_count);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (delete_count == 0u)
        return rexx_value_set_string(result, string.bytes, string.length);

    while (rexx_builtin_next_word(&string, &cursor, &word)) {
        ++current_word_number;
        if (current_word_number == first_word_number) {
            delete_start = word.start;
            delete_end = word.start + word.length;
            selected = 1u;
            break;
        }
    }
    if (selected == 0u)
        return rexx_value_set_string(result, string.bytes, string.length);

    while (selected < delete_count &&
           rexx_builtin_next_word(&string, &cursor, &word)) {
        delete_end = word.start + word.length;
        ++selected;
    }
    while (delete_end < string.length &&
           rexx_builtin_ascii_space((unsigned char)string.bytes[delete_end])) {
        ++delete_end;
    }
    output_length = delete_start + (string.length - delete_end);
    status = rexx_builtin_allocate_result(result, output_length, &buffer);
    if (status != REXX_VM_STATUS_OK) return status;
    if (delete_start != 0u) memcpy(buffer, string.bytes, delete_start);
    if (delete_end < string.length) {
        memcpy(buffer + delete_start, string.bytes + delete_end,
               string.length - delete_end);
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_subword(const RexxValue *arguments,
                                         size_t argument_count,
                                         RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinWord word;
    size_t first_word_number;
    size_t requested_words;
    size_t current_word_number = 0u;
    size_t cursor = 0u;
    size_t selection_start = 0u;
    size_t selection_end = 0u;
    size_t selected = 0u;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_word_number(&arguments[1], &first_word_number);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count < 3u || arguments[2].type == REXX_VALUE_UNSET) {
        requested_words = string.length;
    } else {
        status = rexx_builtin_size(&arguments[2], &requested_words);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (requested_words == 0u) return rexx_builtin_set_empty(result);

    while (rexx_builtin_next_word(&string, &cursor, &word)) {
        ++current_word_number;
        if (current_word_number < first_word_number) continue;
        if (selected == 0u) selection_start = word.start;
        selection_end = word.start + word.length;
        ++selected;
        if (selected == requested_words) break;
    }
    if (selected == 0u) return rexx_builtin_set_empty(result);
    return rexx_value_set_string(result, string.bytes + selection_start,
                                 selection_end - selection_start);
}

static RexxVMStatus rexx_builtin_wordindex(const RexxValue *arguments,
                                           size_t argument_count,
                                           RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinWord word;
    size_t requested_word;
    size_t current_word = 0u;
    size_t cursor = 0u;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_word_number(&arguments[1], &requested_word);
    if (status != REXX_VM_STATUS_OK) return status;
    while (rexx_builtin_next_word(&string, &cursor, &word)) {
        ++current_word;
        if (current_word == requested_word)
            return rexx_builtin_set_size(result, word.start + 1u);
    }
    return rexx_builtin_set_size(result, 0u);
}

static RexxVMStatus rexx_builtin_wordlength(const RexxValue *arguments,
                                            size_t argument_count,
                                            RexxValue *result)
{
    RexxBuiltinStringView string;
    RexxBuiltinWord word;
    size_t requested_word;
    size_t current_word = 0u;
    size_t cursor = 0u;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &string);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_word_number(&arguments[1], &requested_word);
    if (status != REXX_VM_STATUS_OK) return status;
    while (rexx_builtin_next_word(&string, &cursor, &word)) {
        ++current_word;
        if (current_word == requested_word)
            return rexx_builtin_set_size(result, word.length);
    }
    return rexx_builtin_set_size(result, 0u);
}

static RexxVMStatus rexx_builtin_lastpos(const RexxValue *arguments,
                                          size_t argument_count,
                                          RexxValue *result)
{
    RexxBuiltinStringView needle;
    RexxBuiltinStringView haystack;
    size_t start;
    size_t candidate;
    RexxVMStatus status;

    status = rexx_builtin_string_view(&arguments[0], &needle);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_string_view(&arguments[1], &haystack);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count < 3u || arguments[2].type == REXX_VALUE_UNSET) {
        start = haystack.length;
    } else {
        status = rexx_builtin_word_number(&arguments[2], &start);
        if (status != REXX_VM_STATUS_OK) return status;
        if (start > haystack.length) start = haystack.length;
    }
    if (needle.length == 0u || needle.length > start)
        return rexx_builtin_set_size(result, 0u);

    candidate = start - needle.length + 1u;
    while (candidate != 0u) {
        size_t byte_start = candidate - 1u;
        if (memcmp(haystack.bytes + byte_start, needle.bytes,
                   needle.length) == 0) {
            return rexx_builtin_set_size(result, candidate);
        }
        --candidate;
    }
    return rexx_builtin_set_size(result, 0u);
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

static RexxVMStatus rexx_builtin_pos_core(const RexxValue *needle_argument,
                                         const RexxValue *haystack_argument,
                                         const RexxValue *start_argument,
                                         RexxValue *result)
{
    RexxBuiltinStringView needle;
    RexxBuiltinStringView haystack;
    size_t start = 1u;
    size_t start_index;
    RexxVMStatus status;
    RexxNumericStatus numeric_status;
    RexxDecimal parsed_start;
    uint32_t converted_start;

    status = rexx_builtin_string_view(needle_argument, &needle);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_string_view(haystack_argument, &haystack);
    }
    if (status != REXX_VM_STATUS_OK) {
        return status;
    }
    if (start_argument != NULL &&
        start_argument->type != REXX_VALUE_UNSET) {
        status = rexx_builtin_decimal(start_argument, &parsed_start);
        if (status != REXX_VM_STATUS_OK) {
            return status;
        }
        if (parsed_start.sign <= 0) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        numeric_status = rexx_decimal_to_uint32(&parsed_start,
                                                &converted_start);
        if (numeric_status == REXX_NUMERIC_RANGE) {
            /* Every supported haystack is shorter than UINT32_MAX. Keep the
             * positive decimal exact and avoid narrowing it through size_t. */
            return rexx_builtin_set_size(result, 0u);
        }
        if (numeric_status != REXX_NUMERIC_OK) {
            return numeric_status == REXX_NUMERIC_RESOURCE_LIMIT
                       ? REXX_VM_STATUS_RESOURCE_LIMIT
                       : REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        if (converted_start == 0u) {
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        start = (size_t)converted_start;
    }

    if (start > haystack.length) {
        return rexx_builtin_set_size(result, 0u);
    }
    start_index = start - 1u;
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

static RexxVMStatus rexx_builtin_pos(const RexxValue *arguments,
                                     size_t argument_count,
                                     RexxValue *result)
{
    const RexxValue *start = argument_count >= 3u ? &arguments[2] : NULL;
    return rexx_builtin_pos_core(&arguments[0], &arguments[1], start, result);
}

/* IBM TSO/E and VM INDEX uses POS's byte search with the string arguments
 * reversed. Keep validation, start handling, and search semantics shared. */
static RexxVMStatus rexx_builtin_index(const RexxValue *arguments,
                                       size_t argument_count,
                                       RexxValue *result)
{
    const RexxValue *start = argument_count >= 3u ? &arguments[2] : NULL;
    return rexx_builtin_pos_core(&arguments[1], &arguments[0], start, result);
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

/* RX14 numeric functions deliberately accept decimal text, never the legacy
 * RexxValue double adapter. Source-language values reach this layer as byte
 * strings, so every numeric operation stays in RexxDecimal. */
static RexxVMStatus rexx_builtin_charge_numeric_work(RexxVM *vm, size_t cost)
{
    if (vm == NULL) return REXX_VM_STATUS_OK;
    if (cost > vm->numeric_work_remaining)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    vm->numeric_work_remaining -= cost;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_decimal_exact(const RexxValue *value,
                                               RexxDecimal *number,
                                               size_t *source_length)
{
    static const char one[] = "1";
    static const char zero[] = "0";
    const char *bytes;
    size_t length;
    RexxNumericStatus status;

    if (value == NULL || number == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (value->type == REXX_VALUE_STRING) {
        if (value->as.string.bytes == NULL && value->as.string.length != 0u)
            return REXX_VM_STATUS_INVALID_ARGUMENT;
        bytes = value->as.string.bytes != NULL
                    ? value->as.string.bytes : rexx_builtin_empty;
        length = value->as.string.length;
    } else if (value->type == REXX_VALUE_BOOLEAN) {
        bytes = value->as.boolean != 0 ? one : zero;
        length = 1u;
    } else {
        /* In particular, do not serialize REXX_VALUE_NUMBER through double. */
        return value->type == REXX_VALUE_UNSET
                   ? REXX_VM_STATUS_NUMERIC_ERROR
                   : REXX_VM_STATUS_NUMERIC_ERROR;
    }
    status = rexx_decimal_parse(bytes, length, number);
    if (source_length != NULL) *source_length = length;
    if (status == REXX_NUMERIC_OK) return REXX_VM_STATUS_OK;
    if (status == REXX_NUMERIC_RESOURCE_LIMIT || status == REXX_NUMERIC_RANGE)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    return REXX_VM_STATUS_NUMERIC_ERROR;
}

static size_t rexx_builtin_digits(const RexxVM *vm)
{
    return vm != NULL ? (size_t)vm->numeric_digits : 9u;
}

static RexxVMStatus rexx_builtin_prepare_decimal(RexxVM *vm,
                                                 const RexxValue *value,
                                                 RexxDecimal *number)
{
    size_t source_length = 0u;
    RexxNumericStatus numeric_status;
    RexxVMStatus status;
    size_t precision = rexx_builtin_digits(vm);

    if (precision == 0u || precision > REXX_NUMERIC_MAX_DIGITS)
        return REXX_VM_STATUS_INVALID_NUMERIC_SETTING;
    status = rexx_builtin_decimal_exact(value, number, &source_length);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_charge_numeric_work(vm,
                                              source_length + number->length);
    if (status != REXX_VM_STATUS_OK) return status;
    numeric_status = rexx_decimal_round(number, precision);
    if (numeric_status == REXX_NUMERIC_RESOURCE_LIMIT ||
        numeric_status == REXX_NUMERIC_RANGE)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    if (numeric_status != REXX_NUMERIC_OK)
        return REXX_VM_STATUS_NUMERIC_ERROR;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_size_exact(RexxVM *vm,
                                           const RexxValue *value,
                                           size_t *number)
{
    RexxDecimal parsed;
    uint32_t converted;
    RexxNumericStatus numeric_status;
    RexxVMStatus status;

    if (number == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    status = rexx_builtin_prepare_decimal(vm, value, &parsed);
    if (status != REXX_VM_STATUS_OK) return status;
    if (parsed.sign < 0) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    numeric_status = rexx_decimal_to_uint32(&parsed, &converted);
    if (numeric_status == REXX_NUMERIC_RANGE)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    if (numeric_status != REXX_NUMERIC_OK)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    *number = (size_t)converted;
    return REXX_VM_STATUS_OK;
}

static RexxDecimalFormat rexx_builtin_decimal_form(const RexxVM *vm)
{
    return vm != NULL && vm->numeric_form == REXX_NUMERIC_FORM_ENGINEERING
               ? REXX_DECIMAL_FORMAT_ENGINEERING
               : REXX_DECIMAL_FORMAT_SCIENTIFIC;
}

static RexxVMStatus rexx_builtin_return_decimal(RexxVM *vm,
                                                const RexxDecimal *number,
                                                RexxValue *result)
{
    char output[REXX_DECIMAL_MAX_DIGITS +
                2u * REXX_NUMERIC_MAX_DIGITS + 32u];
    int length = rexx_decimal_format_with_form(
        output, sizeof(output), number, rexx_builtin_digits(vm),
        rexx_builtin_decimal_form(vm));
    RexxVMStatus status;

    if (length < 0) return REXX_VM_STATUS_RESOURCE_LIMIT;
    status = rexx_builtin_charge_numeric_work(vm, (size_t)length);
    if (status != REXX_VM_STATUS_OK) return status;
    return rexx_value_set_string(result, output, (size_t)length);
}

/* PCG-XSH-RR 64/32, with a fixed stream selector. Unsigned overflow is part
 * of the recurrence and is defined modulo 2^64. */
#define REXX_RANDOM_PCG_MULTIPLIER UINT64_C(6364136223846793005)
#define REXX_RANDOM_PCG_INCREMENT  UINT64_C(1442695040888963407)
#define REXX_RANDOM_DEFAULT_SEED   UINT32_C(0)

static uint32_t rexx_random_next_u32(uint64_t *state)
{
    uint64_t old_state = *state;
    uint32_t shifted = (uint32_t)(((old_state >> 18u) ^ old_state) >> 27u);
    uint32_t rotation = (uint32_t)(old_state >> 59u);
    *state = old_state * REXX_RANDOM_PCG_MULTIPLIER +
             REXX_RANDOM_PCG_INCREMENT;
    return (shifted >> rotation) |
           (shifted << ((UINT32_C(0) - rotation) & UINT32_C(31)));
}

static void rexx_random_seed_state(uint64_t *state, uint32_t seed)
{
    *state = 0u;
    (void)rexx_random_next_u32(state);
    *state += (uint64_t)seed;
    (void)rexx_random_next_u32(state);
}

/* Rejection sampling maps a uniform 32-bit word to [0, bound) without modulo
 * bias. Here bound <= 100001, so threshold < bound <= 100001. The output
 * transform maps old state 0x07fffe0000000000 to 0xffffffff with rotation 0,
 * which is accepted for every supported bound. The LCG has a 2^64 period
 * (increment odd, multiplier congruent to 1 mod 4), so the loop terminates
 * within at most 2^64 draws for each nonconstant requested range. */
static uint32_t rexx_random_bounded(uint64_t *state, uint32_t bound)
{
    uint32_t threshold = (uint32_t)((UINT64_C(1) << 32u) % bound);
    for (;;) {
        uint32_t candidate = rexx_random_next_u32(state);
        if (candidate >= threshold) return candidate % bound;
    }
}

static RexxVMStatus rexx_random_parse_integer(RexxVM *vm,
                                              const RexxValue *value,
                                              RexxDecimal *number)
{
    size_t source_length = 0u;
    uint32_t small_value;
    RexxNumericStatus numeric_status;
    RexxVMStatus status = rexx_builtin_decimal_exact(
        value, number, &source_length);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_charge_numeric_work(
        vm, source_length + number->length);
    if (status != REXX_VM_STATUS_OK) return status;
    numeric_status = rexx_decimal_to_uint32(number, &small_value);
    /* RANGE means an exact nonnegative whole number too large for uint32_t;
     * RANDOM bounds themselves are arbitrary-precision decimal values. */
    if (numeric_status != REXX_NUMERIC_OK &&
        numeric_status != REXX_NUMERIC_RANGE)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    return REXX_VM_STATUS_OK;
}

static size_t rexx_random_integer_length(const RexxDecimal *number)
{
    if (number->sign == 0) return 1u;
    if (number->scale > 0)
        return number->length - (size_t)number->scale;
    return number->length +
           (size_t)(-(int64_t)number->scale);
}

/* Return one base-10 digit by distance from the units position. The operand
 * has already been checked as a nonnegative whole decimal. */
static unsigned int rexx_random_integer_digit(const RexxDecimal *number,
                                              size_t position)
{
    size_t zeroes;
    size_t coefficient_position;
    if (number->sign == 0) return 0u;
    if (number->scale > 0) {
        size_t integer_digits = number->length - (size_t)number->scale;
        if (position >= integer_digits) return 0u;
        return (unsigned int)(number->digits[integer_digits - position - 1u] -
                              '0');
    }
    zeroes = (size_t)(-(int64_t)number->scale);
    if (position < zeroes) return 0u;
    coefficient_position = position - zeroes;
    if (coefficient_position >= number->length) return 0u;
    return (unsigned int)(number->digits[number->length -
                                         coefficient_position - 1u] - '0');
}

/* Compute max-min using virtual integer digits, so a bound above uint64_t
 * never passes through floating point or a narrowed machine integer. */
static RexxVMStatus rexx_random_range_difference(
    RexxVM *vm, const RexxDecimal *minimum, const RexxDecimal *maximum,
    uint32_t *difference)
{
    size_t minimum_length = rexx_random_integer_length(minimum);
    size_t maximum_length = rexx_random_integer_length(maximum);
    size_t positions = minimum_length > maximum_length
                           ? minimum_length : maximum_length;
    size_t position;
    unsigned int borrow = 0u;
    uint32_t value = 0u;
    uint32_t place = 1u;
    RexxVMStatus status;

    if (rexx_decimal_compare(minimum, maximum) > 0)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    status = rexx_builtin_charge_numeric_work(vm, positions);
    if (status != REXX_VM_STATUS_OK) return status;
    for (position = 0u; position < positions; ++position) {
        int digit = (int)rexx_random_integer_digit(maximum, position) -
                    (int)rexx_random_integer_digit(minimum, position) -
                    (int)borrow;
        if (digit < 0) {
            digit += 10;
            borrow = 1u;
        } else {
            borrow = 0u;
        }
        if (position >= 6u) {
            if (digit != 0) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        } else {
            uint32_t contribution = (uint32_t)digit * place;
            if (contribution > UINT32_C(100000) - value)
                return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
            value += contribution;
            if (position < 5u) place *= 10u;
        }
    }
    if (borrow != 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    *difference = value;
    return REXX_VM_STATUS_OK;
}

/* Add a bounded offset to an arbitrary-precision integer without rounding.
 * Trailing zeroes are retained as a negative decimal scale, keeping the
 * coefficient within the existing 512-digit numeric representation. */
static RexxVMStatus rexx_random_add_offset(const RexxDecimal *minimum,
                                           uint32_t offset,
                                           RexxDecimal *result)
{
    char reversed[REXX_DECIMAL_MAX_DIGITS];
    size_t minimum_length;
    size_t coefficient_length = 0u;
    size_t trailing_zeroes = 0u;
    size_t position = 0u;
    uint32_t addend = offset;
    unsigned int carry = 0u;
    int started = 0;

    if (offset == 0u) {
        *result = *minimum;
        return REXX_VM_STATUS_OK;
    }
    minimum_length = rexx_random_integer_length(minimum);
    while (position < minimum_length || addend != 0u || carry != 0u) {
        unsigned int add_digit = (unsigned int)(addend % 10u);
        unsigned int sum;
        unsigned int digit;
        addend /= 10u;
        sum = rexx_random_integer_digit(minimum, position) + add_digit + carry;
        carry = sum / 10u;
        digit = sum % 10u;
        if (!started && digit == 0u) {
            ++trailing_zeroes;
        } else {
            started = 1;
            if (coefficient_length >= sizeof(reversed))
                return REXX_VM_STATUS_RESOURCE_LIMIT;
            reversed[coefficient_length++] = (char)('0' + digit);
        }
        if (position == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
        ++position;
    }
    memset(result, 0, sizeof(*result));
    if (!started) {
        result->digits[0] = '0';
        result->length = 1u;
        return REXX_VM_STATUS_OK;
    }
    if (trailing_zeroes > (size_t)INT32_MAX)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    result->length = coefficient_length;
    result->digits[coefficient_length] = '\0';
    result->scale = -(int32_t)trailing_zeroes;
    result->sign = 1;
    while (coefficient_length != 0u) {
        --coefficient_length;
        result->digits[coefficient_length] = reversed[
            result->length - coefficient_length - 1u];
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_random(RexxVM *vm,
                                        const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    static const char zero_text[] = "0";
    static const char default_max_text[] = "999";
    RexxDecimal minimum, maximum, random_result;
    const RexxValue *minimum_argument = NULL;
    const RexxValue *maximum_argument = NULL;
    uint32_t seed = 0u, difference, offset = 0u;
    int has_seed = 0;
    uint64_t next_state;
    RexxVMStatus status;
    RexxNumericStatus numeric_status;

    if (vm == NULL || !vm->initialized ||
        (argument_count != 0u && arguments == NULL))
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (argument_count > 3u)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;

    numeric_status = rexx_decimal_parse(zero_text, sizeof(zero_text) - 1u,
                                        &minimum);
    if (numeric_status != REXX_NUMERIC_OK) {
        status = REXX_VM_STATUS_NUMERIC_ERROR;
        goto cleanup;
    }
    numeric_status = rexx_decimal_parse(
        default_max_text, sizeof(default_max_text) - 1u, &maximum);
    if (numeric_status != REXX_NUMERIC_OK) {
        status = REXX_VM_STATUS_NUMERIC_ERROR;
        goto cleanup;
    }

    if (argument_count == 1u &&
        arguments[0].type != REXX_VALUE_UNSET) {
        /* The lone positional argument is max; an explicit comma switches
         * the call to min,max positional interpretation. */
        maximum_argument = &arguments[0];
    } else {
        if (argument_count >= 1u &&
            arguments[0].type != REXX_VALUE_UNSET)
            minimum_argument = &arguments[0];
        if (argument_count >= 2u &&
            arguments[1].type != REXX_VALUE_UNSET)
            maximum_argument = &arguments[1];
    }
    if (minimum_argument != NULL) {
        status = rexx_random_parse_integer(vm, minimum_argument, &minimum);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
    }
    if (maximum_argument != NULL) {
        status = rexx_random_parse_integer(vm, maximum_argument, &maximum);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
    }
    if (argument_count >= 3u &&
        arguments[2].type != REXX_VALUE_UNSET) {
        RexxDecimal seed_decimal;
        uint32_t converted_seed;
        has_seed = 1;
        status = rexx_random_parse_integer(vm, &arguments[2], &seed_decimal);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        numeric_status = rexx_decimal_to_uint32(&seed_decimal,
                                                &converted_seed);
        if (numeric_status != REXX_NUMERIC_OK ||
            converted_seed > UINT32_C(999999999)) {
            status = REXX_VM_STATUS_INVALID_FUNCTION_CALL;
            goto cleanup;
        }
        seed = converted_seed;
    }

    status = rexx_random_range_difference(vm, &minimum, &maximum,
                                          &difference);
    if (status != REXX_VM_STATUS_OK) goto cleanup;

    next_state = vm->random_state;
    if (has_seed) {
        rexx_random_seed_state(&next_state, seed);
    } else if (!vm->random_initialized) {
        /* IBM leaves the first unseeded value implementation-defined. Use a
         * deterministic seed-zero fallback in every execution environment. */
        rexx_random_seed_state(&next_state, REXX_RANDOM_DEFAULT_SEED);
    }
    if (difference != 0u)
        offset = rexx_random_bounded(&next_state, difference + 1u);
    status = rexx_random_add_offset(&minimum, offset, &random_result);
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    status = rexx_builtin_return_decimal(vm, &random_result, result);
    if (status != REXX_VM_STATUS_OK) goto cleanup;

    /* Validation, result arithmetic, and allocation all complete before the
     * VM's program-level state changes. Failed calls consume no random word. */
    vm->random_state = next_state;
    vm->random_initialized = 1u;

cleanup:
    return status;
}

static RexxVMStatus rexx_builtin_abs(RexxVM *vm,
                                     const RexxValue *arguments,
                                     size_t argument_count,
                                     RexxValue *result)
{
    RexxDecimal number;
    RexxVMStatus status;
    (void)argument_count;
    status = rexx_builtin_prepare_decimal(vm, &arguments[0], &number);
    if (status != REXX_VM_STATUS_OK) return status;
    if (number.sign < 0) number.sign = (int8_t)-number.sign;
    return rexx_builtin_return_decimal(vm, &number, result);
}

static RexxVMStatus rexx_builtin_sign(RexxVM *vm,
                                      const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    static const char negative[] = "-1";
    static const char zero[] = "0";
    static const char positive[] = "1";
    RexxDecimal number;
    RexxVMStatus status;
    (void)argument_count;
    status = rexx_builtin_prepare_decimal(vm, &arguments[0], &number);
    if (status != REXX_VM_STATUS_OK) return status;
    if (number.sign < 0)
        return rexx_value_set_string(result, negative, sizeof(negative) - 1u);
    if (number.sign > 0)
        return rexx_value_set_string(result, positive, sizeof(positive) - 1u);
    return rexx_value_set_string(result, zero, sizeof(zero) - 1u);
}

static RexxVMStatus rexx_builtin_extreme(RexxVM *vm,
                                         const RexxValue *arguments,
                                         size_t argument_count,
                                         RexxValue *result,
                                         int select_minimum)
{
    RexxDecimal best;
    size_t index;
    RexxVMStatus status;

    if (arguments == NULL || argument_count < 2u || argument_count > 20u)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    status = rexx_builtin_prepare_decimal(vm, &arguments[0], &best);
    for (index = 1u; status == REXX_VM_STATUS_OK && index < argument_count;
         ++index) {
        RexxDecimal candidate;
        status = rexx_builtin_prepare_decimal(vm, &arguments[index],
                                              &candidate);
        if (status == REXX_VM_STATUS_OK) {
            size_t compare_cost = best.length + candidate.length;
            status = rexx_builtin_charge_numeric_work(vm, compare_cost);
            if (status == REXX_VM_STATUS_OK) {
                int comparison = rexx_decimal_compare(&candidate, &best);
                if ((select_minimum && comparison < 0) ||
                    (!select_minimum && comparison > 0))
                    best = candidate;
            }
        }
    }
    if (status != REXX_VM_STATUS_OK) return status;
    return rexx_builtin_return_decimal(vm, &best, result);
}

static RexxVMStatus rexx_builtin_max(RexxVM *vm,
                                     const RexxValue *arguments,
                                     size_t argument_count,
                                     RexxValue *result)
{
    return rexx_builtin_extreme(vm, arguments, argument_count, result, 0);
}

static RexxVMStatus rexx_builtin_min(RexxVM *vm,
                                     const RexxValue *arguments,
                                     size_t argument_count,
                                     RexxValue *result)
{
    return rexx_builtin_extreme(vm, arguments, argument_count, result, 1);
}

static RexxNumericStatus rexx_builtin_round_to_scale(RexxDecimal *number,
                                                      int64_t target_scale)
{
    int64_t drop_signed;
    size_t drop, retained, index;
    int carry;

    if (number == NULL || number->length == 0u)
        return REXX_NUMERIC_INVALID;
    if (number->sign == 0 || target_scale >= number->scale)
        return REXX_NUMERIC_OK;
    drop_signed = (int64_t)number->scale - target_scale;
    if (drop_signed <= 0 || (uint64_t)drop_signed > SIZE_MAX)
        return REXX_NUMERIC_RESOURCE_LIMIT;
    drop = (size_t)drop_signed;
    if (drop > number->length) {
        memset(number, 0, sizeof(*number));
        number->digits[0] = '0';
        number->length = 1u;
        return REXX_NUMERIC_OK;
    }
    retained = number->length - drop;
    carry = number->digits[retained] >= '5';
    if (target_scale < -REXX_DECIMAL_MAX_EXPONENT ||
        target_scale > REXX_DECIMAL_MAX_EXPONENT)
        return REXX_NUMERIC_RANGE;
    if (retained == 0u) {
        if (!carry) {
            memset(number, 0, sizeof(*number));
            number->digits[0] = '0';
            number->length = 1u;
            return REXX_NUMERIC_OK;
        }
        number->digits[0] = '1';
        number->digits[1] = '\0';
        number->length = 1u;
        number->scale = (int32_t)target_scale;
        return REXX_NUMERIC_OK;
    }
    number->length = retained;
    number->digits[retained] = '\0';
    number->scale = (int32_t)target_scale;
    for (index = retained; carry && index != 0u; --index) {
        size_t digit_index = index - 1u;
        if (number->digits[digit_index] == '9')
            number->digits[digit_index] = '0';
        else {
            ++number->digits[digit_index];
            carry = 0;
        }
    }
    if (carry) {
        number->digits[0] = '1';
        for (index = 1u; index <= retained; ++index)
            number->digits[index] = '0';
        ++number->length;
        number->digits[number->length] = '\0';
    }
    return REXX_NUMERIC_OK;
}

static int64_t rexx_builtin_adjusted_exponent(const RexxDecimal *number)
{
    return number->sign == 0
               ? 0
               : (int64_t)number->length - (int64_t)number->scale - 1;
}

static size_t rexx_builtin_integer_places(const RexxDecimal *number,
                                          int64_t display_exponent)
{
    int64_t point;
    if (number->sign == 0) return 1u;
    point = (int64_t)number->length - (int64_t)number->scale -
            display_exponent;
    return point > 0 ? (size_t)point : 1u;
}

static char rexx_builtin_decimal_digit(const RexxDecimal *number,
                                       int64_t point,
                                       size_t fractional_index)
{
    int64_t source = point + (int64_t)fractional_index;
    if (number->sign == 0 || source < 0 ||
        (uint64_t)source >= (uint64_t)number->length)
        return '0';
    return number->digits[(size_t)source];
}

static char rexx_builtin_integer_digit(const RexxDecimal *number,
                                       size_t index,
                                       int64_t point)
{
    if (number->sign == 0 || point <= 0 || (int64_t)index >= point ||
        index >= number->length)
        return '0';
    return number->digits[index];
}

static RexxVMStatus rexx_builtin_trunc(RexxVM *vm,
                                       const RexxValue *arguments,
                                       size_t argument_count,
                                       RexxValue *result)
{
    RexxDecimal number;
    size_t places = 0u;
    size_t integer_places, sign_length, total, index, cursor = 0u;
    int64_t point;
    char *output = NULL;
    RexxVMStatus status;

    status = rexx_builtin_prepare_decimal(vm, &arguments[0], &number);
    if (status == REXX_VM_STATUS_OK && argument_count > 1u &&
        arguments[1].type != REXX_VALUE_UNSET)
        status = rexx_builtin_size_exact(vm, &arguments[1], &places);
    if (status != REXX_VM_STATUS_OK) return status;
    if (number.sign != 0 && (int64_t)number.scale > (int64_t)places) {
        size_t drop = (size_t)((int64_t)number.scale - (int64_t)places);
        if (drop >= number.length) {
            memset(&number, 0, sizeof(number));
            number.digits[0] = '0';
            number.length = 1u;
        } else {
            number.length -= drop;
            number.scale = (int32_t)places;
            number.digits[number.length] = '\0';
        }
    }
    point = (int64_t)number.length - (int64_t)number.scale;
    integer_places = number.sign == 0 || point <= 0
                         ? 1u : (size_t)point;
    sign_length = number.sign < 0 ? 1u : 0u;
    if (integer_places > SIZE_MAX - sign_length ||
        (places != 0u && integer_places + sign_length > SIZE_MAX - places - 1u))
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    total = integer_places + sign_length;
    if (places != 0u) total += 1u + places;
    if (total > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    status = rexx_builtin_charge_numeric_work(vm, total);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_value_allocate_string(result, total, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    if (number.sign < 0) output[cursor++] = '-';
    for (index = 0u; index < integer_places; ++index) {
        output[cursor++] = rexx_builtin_integer_digit(&number, index,
                                                      point);
    }
    if (places != 0u) {
        output[cursor++] = '.';
        for (index = 0u; index < places; ++index)
            output[cursor++] = rexx_builtin_decimal_digit(
                &number, point, index);
    }
    (void)output[cursor];
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_format(RexxVM *vm,
                                        const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    RexxDecimal original;
    size_t before = 0u, after = 0u, expp = 0u, expt;
    int has_before = 0, has_after = 0, has_expp = 0, has_expt = 0;
    int exponent_form = 0, blank_exponent = 0;
    int64_t adjusted, display_exponent = 0, point;
    size_t integer_places, required_before, before_width, fractional_width;
    size_t suffix_length = 0u, total, cursor = 0u, index;
    size_t natural_fraction;
    size_t exponent_digit_count = 0u;
    RexxDecimal number;
    char *output = NULL;
    RexxVMStatus status;

    status = rexx_builtin_prepare_decimal(vm, &arguments[0], &original);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count > 1u && arguments[1].type != REXX_VALUE_UNSET) {
        has_before = 1;
        status = rexx_builtin_size_exact(vm, &arguments[1], &before);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (argument_count > 2u && arguments[2].type != REXX_VALUE_UNSET) {
        has_after = 1;
        status = rexx_builtin_size_exact(vm, &arguments[2], &after);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (argument_count > 3u && arguments[3].type != REXX_VALUE_UNSET) {
        has_expp = 1;
        status = rexx_builtin_size_exact(vm, &arguments[3], &expp);
        if (status != REXX_VM_STATUS_OK) return status;
        if (expp >= 10u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    if (argument_count > 4u && arguments[4].type != REXX_VALUE_UNSET) {
        has_expt = 1;
        status = rexx_builtin_size_exact(vm, &arguments[4], &expt);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (!has_before && !has_after && !has_expp && !has_expt)
        return rexx_builtin_return_decimal(vm, &original, result);

    expt = has_expt ? expt : rexx_builtin_digits(vm);
    adjusted = rexx_builtin_adjusted_exponent(&original);
    if (original.sign != 0 && !(has_expp && expp == 0u)) {
        size_t integer_need = rexx_builtin_integer_places(&original, 0);
        size_t fraction_need = has_after
            ? after : (original.scale > 0 ? (size_t)original.scale : 0u);
        uint64_t twice_expt = (uint64_t)expt * UINT64_C(2);
        int trigger = expt == 0u
            ? adjusted != 0
            : (uint64_t)integer_need > (uint64_t)expt ||
                  (uint64_t)fraction_need > twice_expt;
        exponent_form = trigger && adjusted != 0;
        if (exponent_form) {
            display_exponent = rexx_builtin_decimal_form(vm) ==
                                       REXX_DECIMAL_FORMAT_ENGINEERING
                                   ? (adjusted >= 0
                                          ? (adjusted / 3) * 3
                                          : -(((-adjusted + 2) / 3) * 3))
                                   : adjusted;
        } else if ((trigger || expt == 0u) && adjusted == 0 &&
                   has_expp && expp != 0u) {
            blank_exponent = 1;
        }
    }

    number = original;
    if (has_after && number.sign != 0) {
        int64_t target_scale = (int64_t)after -
                              (exponent_form ? display_exponent : 0);
        RexxNumericStatus numeric_status = rexx_builtin_round_to_scale(
            &number, target_scale);
        if (numeric_status == REXX_NUMERIC_RESOURCE_LIMIT ||
            numeric_status == REXX_NUMERIC_RANGE)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        if (numeric_status != REXX_NUMERIC_OK)
            return REXX_VM_STATUS_NUMERIC_ERROR;
        if (exponent_form && number.sign != 0) {
            int64_t next_adjusted = rexx_builtin_adjusted_exponent(&number);
            int64_t next_display = rexx_builtin_decimal_form(vm) ==
                                           REXX_DECIMAL_FORMAT_ENGINEERING
                                       ? (next_adjusted >= 0
                                              ? (next_adjusted / 3) * 3
                                              : -(((-next_adjusted + 2) / 3) * 3))
                                       : next_adjusted;
            if (next_display != display_exponent) {
                number = original;
                target_scale = (int64_t)after - next_display;
                numeric_status = rexx_builtin_round_to_scale(&number,
                                                              target_scale);
                if (numeric_status == REXX_NUMERIC_RESOURCE_LIMIT ||
                    numeric_status == REXX_NUMERIC_RANGE)
                    return REXX_VM_STATUS_RESOURCE_LIMIT;
                if (numeric_status != REXX_NUMERIC_OK)
                    return REXX_VM_STATUS_NUMERIC_ERROR;
                display_exponent = next_display;
            }
        }
    }
    if (number.sign == 0) {
        exponent_form = 0;
        blank_exponent = 0;
        display_exponent = 0;
    }

    point = (int64_t)number.length - (int64_t)number.scale -
            (exponent_form ? display_exponent : 0);
    integer_places = point > 0 ? (size_t)point : 1u;
    if (exponent_form) {
        int64_t mantissa_scale = (int64_t)number.scale + display_exponent;
        natural_fraction = mantissa_scale > 0 ? (size_t)mantissa_scale : 0u;
    } else {
        natural_fraction = number.scale > 0 ? (size_t)number.scale : 0u;
    }
    fractional_width = has_after ? after : natural_fraction;
    required_before = integer_places + (number.sign < 0 ? 1u : 0u);
    before_width = has_before ? before : required_before;
    if (before_width < required_before)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (blank_exponent) {
        suffix_length = expp + 2u;
    } else if (exponent_form) {
        size_t magnitude = (size_t)(display_exponent < 0
                                         ? -display_exponent
                                         : display_exponent);
        do {
            ++exponent_digit_count;
            magnitude /= 10u;
        } while (magnitude != 0u && exponent_digit_count < 16u);
        if (has_expp && exponent_digit_count > expp)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (has_expp && expp > exponent_digit_count)
            exponent_digit_count = expp;
        suffix_length = 2u + exponent_digit_count;
    }
    if (before_width > REXX_BUILTIN_MAX_RESULT_LENGTH ||
        fractional_width > REXX_BUILTIN_MAX_RESULT_LENGTH ||
        before_width > SIZE_MAX - suffix_length ||
        (fractional_width != 0u &&
         before_width + suffix_length >
             SIZE_MAX - fractional_width - 1u))
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    total = before_width + suffix_length;
    if (fractional_width != 0u) total += 1u + fractional_width;
    if (total > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    status = rexx_builtin_charge_numeric_work(vm,
                                              number.length + total);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_value_allocate_string(result, total, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    for (index = 0u; index < before_width - required_before; ++index)
        output[cursor++] = ' ';
    if (number.sign < 0) output[cursor++] = '-';
    for (index = 0u; index < integer_places; ++index)
        output[cursor++] = rexx_builtin_integer_digit(&number, index, point);
    if (fractional_width != 0u) {
        output[cursor++] = '.';
        for (index = 0u; index < fractional_width; ++index)
            output[cursor++] = rexx_builtin_decimal_digit(
                &number, point, index);
    }
    if (blank_exponent) {
        for (index = 0u; index < suffix_length; ++index)
            output[cursor++] = ' ';
    } else if (exponent_form) {
        size_t actual_digits = 0u;
        size_t magnitude = (size_t)(display_exponent < 0
                                         ? -display_exponent
                                         : display_exponent);
        char reverse[16];
        do {
            reverse[actual_digits++] = (char)('0' + magnitude % 10u);
            magnitude /= 10u;
        } while (magnitude != 0u && actual_digits < sizeof(reverse));
        output[cursor++] = 'E';
        output[cursor++] = display_exponent < 0 ? '-' : '+';
        for (index = 0u; index < exponent_digit_count - actual_digits; ++index)
            output[cursor++] = '0';
        while (actual_digits != 0u)
            output[cursor++] = reverse[--actual_digits];
    }
    (void)output[cursor];
    return REXX_VM_STATUS_OK;
}

static int rexx_builtin_is_digit(unsigned char byte)
{
    return byte >= (unsigned char)'0' && byte <= (unsigned char)'9';
}

static int rexx_builtin_is_hex(unsigned char byte)
{
    return rexx_builtin_is_digit(byte) ||
           (byte >= (unsigned char)'a' && byte <= (unsigned char)'f') ||
           (byte >= (unsigned char)'A' && byte <= (unsigned char)'F');
}

static int rexx_builtin_symbol_start(unsigned char byte)
{
    return (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
           (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
           byte == (unsigned char)'!' || byte == (unsigned char)'?' ||
           byte == (unsigned char)'_' || byte == (unsigned char)'@' ||
           byte == (unsigned char)'#' || byte == (unsigned char)'$';
}

static int rexx_builtin_symbol_part(unsigned char byte)
{
    return rexx_builtin_symbol_start(byte) || rexx_builtin_is_digit(byte) ||
           byte == (unsigned char)'.';
}

static int rexx_builtin_datatype_symbol_chars(const char *bytes,
                                              size_t length)
{
    size_t index;
    if (length == 0u ||
        !rexx_builtin_symbol_start((unsigned char)bytes[0]))
        return 0;
    for (index = 1u; index < length; ++index)
        if (!rexx_builtin_symbol_part((unsigned char)bytes[index])) return 0;
    return 1;
}

static RexxVMStatus rexx_builtin_text_view(const RexxValue *value,
                                           const char **bytes,
                                           size_t *length)
{
    static const char one[] = "1";
    static const char zero[] = "0";
    if (value == NULL || bytes == NULL || length == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (value->type == REXX_VALUE_STRING) {
        if (value->as.string.bytes == NULL && value->as.string.length != 0u)
            return REXX_VM_STATUS_INVALID_ARGUMENT;
        *bytes = value->as.string.bytes != NULL
                     ? value->as.string.bytes : rexx_builtin_empty;
        *length = value->as.string.length;
        return REXX_VM_STATUS_OK;
    }
    if (value->type == REXX_VALUE_BOOLEAN) {
        *bytes = value->as.boolean != 0 ? one : zero;
        *length = 1u;
        return REXX_VM_STATUS_OK;
    }
    return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
}

static int rexx_builtin_datatype_hex(const char *bytes, size_t length)
{
    size_t index, digits = 0u;
    if (length == 0u) return 1;
    for (index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)bytes[index];
        if (rexx_builtin_is_hex(byte)) {
            ++digits;
        } else if (byte == (unsigned char)' ') {
            if (digits == 0u || (digits & 1u) != 0u ||
                index + 1u == length ||
                !rexx_builtin_is_hex((unsigned char)bytes[index + 1u]))
                return 0;
        } else {
            return 0;
        }
    }
    return (digits & 1u) == 0u;
}

static RexxVMStatus rexx_builtin_datatype(RexxVM *vm,
                                          const RexxValue *arguments,
                                          size_t argument_count,
                                          RexxValue *result)
{
    const char *bytes;
    size_t length, index;
    unsigned char option = 0u;
    int matched = 1;
    RexxVMStatus status;

    status = rexx_builtin_text_view(&arguments[0], &bytes, &length);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_charge_numeric_work(vm, length);
    if (status != REXX_VM_STATUS_OK) return status;
    if (argument_count > 1u && arguments[1].type != REXX_VALUE_UNSET) {
        const char *option_bytes;
        size_t option_length;
        status = rexx_builtin_text_view(&arguments[1], &option_bytes,
                                        &option_length);
        if (status != REXX_VM_STATUS_OK) return status;
        if (option_length == 0u)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        option = rexx_builtin_ascii_upper((unsigned char)option_bytes[0]);
    }

    if (option == 0u) {
        RexxDecimal numeric;
        status = rexx_builtin_prepare_decimal(vm, &arguments[0], &numeric);
        if (status == REXX_VM_STATUS_OK)
            return rexx_value_set_string(result, "NUM", 3u);
        if (status == REXX_VM_STATUS_NUMERIC_ERROR)
            return rexx_value_set_string(result, "CHAR", 4u);
        return status;
    }

    if (option == (unsigned char)'N' || option == (unsigned char)'W') {
        RexxDecimal numeric;
        status = rexx_builtin_prepare_decimal(vm, &arguments[0], &numeric);
        if (status == REXX_VM_STATUS_NUMERIC_ERROR) matched = 0;
        else if (status != REXX_VM_STATUS_OK) return status;
        else if (option == (unsigned char)'W' && numeric.sign != 0) {
            size_t integer_digits;
            if (numeric.scale > 0) {
                size_t fractional = (size_t)numeric.scale;
                if (fractional >= numeric.length) matched = 0;
                else {
                    for (index = numeric.length - fractional;
                         index < numeric.length; ++index)
                        if (numeric.digits[index] != '0') matched = 0;
                }
            }
            integer_digits = numeric.scale <= 0
                ? numeric.length + (size_t)(-(int64_t)numeric.scale)
                : numeric.length > (size_t)numeric.scale
                      ? numeric.length - (size_t)numeric.scale : 1u;
            if (integer_digits > rexx_builtin_digits(vm)) matched = 0;
        }
    } else if (option == (unsigned char)'X') {
        matched = rexx_builtin_datatype_hex(bytes, length);
    } else if (option == (unsigned char)'S') {
        matched = rexx_builtin_datatype_symbol_chars(bytes, length);
    } else {
        if (option != (unsigned char)'A' && option != (unsigned char)'B' &&
            option != (unsigned char)'L' && option != (unsigned char)'M' &&
            option != (unsigned char)'U')
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (length == 0u) matched = 0;
        for (index = 0u; matched && index < length; ++index) {
            unsigned char byte = (unsigned char)bytes[index];
            int upper = byte >= (unsigned char)'A' &&
                        byte <= (unsigned char)'Z';
            int lower = byte >= (unsigned char)'a' &&
                        byte <= (unsigned char)'z';
            int digit = rexx_builtin_is_digit(byte);
            if (option == (unsigned char)'A') matched = upper || lower || digit;
            else if (option == (unsigned char)'B')
                matched = byte == (unsigned char)'0' ||
                          byte == (unsigned char)'1';
            else if (option == (unsigned char)'L') matched = lower;
            else if (option == (unsigned char)'M') matched = upper || lower;
            else matched = upper;
        }
    }
    return rexx_value_set_string(result, matched ? "1" : "0", 1u);
}

static int rexx_builtin_symbol_name(const char *bytes, size_t length,
                                    int *constant)
{
    size_t index;
    int has_whitespace = 0;
    RexxDecimal numeric;
    RexxNumericStatus numeric_status;

    if (constant != NULL) *constant = 0;
    if (length == 0u || length > (size_t)65535u) return 0;
    for (index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)bytes[index];
        if (byte == (unsigned char)' ' || byte == (unsigned char)'\t' ||
            byte == (unsigned char)'\n' || byte == (unsigned char)'\r' ||
            byte == (unsigned char)'\f' || byte == (unsigned char)'\v') {
            has_whitespace = 1;
            break;
        }
    }
    if (!has_whitespace) {
        numeric_status = rexx_decimal_parse(bytes, length, &numeric);
        if (numeric_status == REXX_NUMERIC_OK) {
            if (constant != NULL) *constant = 1;
            return 1;
        }
    }
    if (!rexx_builtin_symbol_start((unsigned char)bytes[0])) return 0;
    for (index = 1u; index < length; ++index)
        if (!rexx_builtin_symbol_part((unsigned char)bytes[index])) return 0;
    return 1;
}

static RexxVMStatus rexx_builtin_symbol(RexxVM *vm,
                                        const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    const char *bytes;
    size_t length;
    int constant = 0, initialized = 0;
    RexxVMStatus status;

    (void)argument_count;
    status = rexx_builtin_text_view(&arguments[0], &bytes, &length);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_charge_numeric_work(vm, length);
    if (status != REXX_VM_STATUS_OK) return status;
    if (!rexx_builtin_symbol_name(bytes, length, &constant))
        return rexx_value_set_string(result, "BAD", 3u);
    if (constant)
        return rexx_value_set_string(result, "LIT", 3u);
    if (vm != NULL) {
        status = rexx_vm_symbol_is_initialized(vm, bytes, length,
                                               &initialized);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    return rexx_value_set_string(result, initialized ? "VAR" : "LIT", 3u);
}

static RexxVMStatus rexx_builtin_xrange(RexxVM *vm,
                                        const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    const char *start_bytes = NULL, *end_bytes = NULL;
    size_t start_length = 0u, end_length = 0u;
    size_t start = 0u, end = 255u, output_length, index;
    char *output = NULL;
    RexxVMStatus status;

    if (argument_count > 0u && arguments[0].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_text_view(&arguments[0], &start_bytes,
                                        &start_length);
        if (status != REXX_VM_STATUS_OK) return status;
        if (start_length != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        start = (unsigned char)start_bytes[0];
    }
    if (argument_count > 1u && arguments[1].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_text_view(&arguments[1], &end_bytes,
                                        &end_length);
        if (status != REXX_VM_STATUS_OK) return status;
        if (end_length != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        end = (unsigned char)end_bytes[0];
    }
    output_length = start <= end ? end - start + 1u
                                 : 256u - start + end + 1u;
    status = rexx_builtin_charge_numeric_work(vm, output_length);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_value_allocate_string(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    for (index = 0u; index < output_length; ++index)
        output[index] = (char)(unsigned char)((start + index) & 0xffu);
    return REXX_VM_STATUS_OK;
}

/* RX15 conversion helpers operate on bytes and decimal digit strings only.
 * No integer conversion routes a REXX value through a host machine word. */
static const char rexx_builtin_hex_digits[] = "0123456789ABCDEF";

static int rexx_builtin_hex_value(unsigned char byte)
{
    if (byte >= (unsigned char)'0' && byte <= (unsigned char)'9')
        return (int)(byte - (unsigned char)'0');
    if (byte >= (unsigned char)'a' && byte <= (unsigned char)'f')
        return (int)(byte - (unsigned char)'a') + 10;
    if (byte >= (unsigned char)'A' && byte <= (unsigned char)'F')
        return (int)(byte - (unsigned char)'A') + 10;
    return -1;
}

/* The first hex group can be odd because an implied leading zero belongs to
 * it. Every group after the first ends at a byte boundary. */
static RexxVMStatus rexx_builtin_scan_hex(const RexxBuiltinStringView *input,
                                         size_t *digit_count)
{
    size_t index, count = 0u, group_length = 0u, groups = 0u;
    int saw_blank = 0;
    if (input == NULL || digit_count == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < input->length; ++index) {
        unsigned char byte = (unsigned char)input->bytes[index];
        if (byte == (unsigned char)' ') {
            if (group_length == 0u) {
                if (groups == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
                continue;
            }
            if (groups != 0u && (group_length & 1u) != 0u)
                return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
            ++groups;
            group_length = 0u;
            saw_blank = 1;
            continue;
        }
        if (rexx_builtin_hex_value(byte) < 0)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (count == SIZE_MAX || group_length == SIZE_MAX)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        ++count;
        ++group_length;
    }
    if (saw_blank && group_length == 0u)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (groups != 0u && (group_length & 1u) != 0u)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    *digit_count = count;
    return REXX_VM_STATUS_OK;
}

/* In B2X, groups after the first must contain whole nibbles. */
static RexxVMStatus rexx_builtin_scan_binary(const RexxBuiltinStringView *input,
                                            size_t *digit_count)
{
    size_t index, count = 0u, group_length = 0u, groups = 0u;
    int saw_blank = 0;
    if (input == NULL || digit_count == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < input->length; ++index) {
        unsigned char byte = (unsigned char)input->bytes[index];
        if (byte == (unsigned char)' ') {
            if (group_length == 0u) {
                if (groups == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
                continue;
            }
            if (groups != 0u && (group_length & 3u) != 0u)
                return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
            ++groups;
            group_length = 0u;
            saw_blank = 1;
            continue;
        }
        if (byte != (unsigned char)'0' && byte != (unsigned char)'1')
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (count == SIZE_MAX || group_length == SIZE_MAX)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        ++count;
        ++group_length;
    }
    if (saw_blank && group_length == 0u)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (groups != 0u && (group_length & 3u) != 0u)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    *digit_count = count;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_conversion_width(RexxVM *vm,
                                                  const RexxValue *argument,
                                                  size_t *width)
{
    RexxVMStatus status;
    if (argument == NULL || width == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    status = rexx_builtin_size_exact(vm, argument, width);
    if (status != REXX_VM_STATUS_OK) return status;
    if (*width > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    return REXX_VM_STATUS_OK;
}

typedef struct RexxBuiltinDecimalAccumulator {
    char digits[REXX_NUMERIC_MAX_DIGITS + 1u]; /* Least-significant first. */
    size_t length;
    size_t precision;
} RexxBuiltinDecimalAccumulator;

static void rexx_builtin_decimal_accumulator_init(
    RexxBuiltinDecimalAccumulator *value, size_t precision)
{
    value->digits[0] = '0';
    value->digits[1] = '\0';
    value->length = 1u;
    value->precision = precision;
}

static RexxVMStatus rexx_builtin_decimal_accumulate(
    RexxVM *vm, RexxBuiltinDecimalAccumulator *value, unsigned int radix,
    unsigned int digit)
{
    size_t index;
    unsigned int carry = digit;
    RexxVMStatus status;
    if (value == NULL || radix < 2u || radix > 256u || digit >= radix)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (value->precision == 0u || value->precision > REXX_NUMERIC_MAX_DIGITS)
        return REXX_VM_STATUS_INVALID_NUMERIC_SETTING;
    status = rexx_builtin_charge_numeric_work(vm, value->length);
    if (status != REXX_VM_STATUS_OK) return status;
    for (index = 0u; index < value->length; ++index) {
        unsigned int product =
            (unsigned int)(value->digits[index] - '0') * radix + carry;
        value->digits[index] = (char)('0' + product % 10u);
        carry = product / 10u;
    }
    while (carry != 0u) {
        if (value->length >= value->precision ||
            value->length >= REXX_NUMERIC_MAX_DIGITS)
            return REXX_VM_STATUS_NUMERIC_ERROR;
        value->digits[value->length++] = (char)('0' + carry % 10u);
        carry /= 10u;
    }
    if (value->length > value->precision)
        return REXX_VM_STATUS_NUMERIC_ERROR;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_return_accumulated_decimal(
    RexxVM *vm, const RexxBuiltinDecimalAccumulator *magnitude, int negative,
    RexxValue *result)
{
    RexxDecimal decimal;
    size_t index;
    if (magnitude == NULL || magnitude->length == 0u || result == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    memset(&decimal, 0, sizeof(decimal));
    decimal.length = magnitude->length;
    decimal.scale = 0;
    decimal.sign = negative &&
                           !(magnitude->length == 1u &&
                             magnitude->digits[0] == '0')
                       ? -1 : 1;
    for (index = 0u; index < magnitude->length; ++index)
        decimal.digits[index] = magnitude->digits[magnitude->length - index - 1u];
    decimal.digits[decimal.length] = '\0';
    return rexx_builtin_return_decimal(vm, &decimal, result);
}

/* Normalize a whole numeric argument without narrowing its integer value. */
static RexxVMStatus rexx_builtin_whole_decimal(
    RexxVM *vm, const RexxValue *argument,
    char digits[REXX_DECIMAL_MAX_DIGITS + 1u], size_t *length, int *negative)
{
    RexxDecimal parsed;
    RexxVMStatus status;
    size_t index, whole_length;
    int64_t zeroes;
    status = rexx_builtin_prepare_decimal(vm, argument, &parsed);
    if (status != REXX_VM_STATUS_OK) return status;
    *negative = parsed.sign < 0;
    if (parsed.sign == 0) {
        digits[0] = '0';
        digits[1] = '\0';
        *length = 1u;
        *negative = 0;
        return REXX_VM_STATUS_OK;
    }
    if (parsed.scale > 0) {
        size_t fractional = (size_t)parsed.scale;
        if (fractional >= parsed.length)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        for (index = parsed.length - fractional; index < parsed.length; ++index)
            if (parsed.digits[index] != '0')
                return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        whole_length = parsed.length - fractional;
        memcpy(digits, parsed.digits, whole_length);
    } else {
        zeroes = -(int64_t)parsed.scale;
        if ((uint64_t)zeroes >
            (uint64_t)REXX_DECIMAL_MAX_DIGITS - (uint64_t)parsed.length)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        whole_length = parsed.length + (size_t)zeroes;
        memcpy(digits, parsed.digits, parsed.length);
        memset(digits + parsed.length, '0', (size_t)zeroes);
    }
    if (whole_length == 0u || whole_length > REXX_DECIMAL_MAX_DIGITS)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    digits[whole_length] = '\0';
    *length = whole_length;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_decimal_to_radix_string(
    RexxVM *vm, const RexxValue *arguments, size_t argument_count,
    RexxValue *result, unsigned int radix, int character_output)
{
    char decimal_digits[REXX_DECIMAL_MAX_DIGITS + 1u];
    char reversed[REXX_DECIMAL_MAX_DIGITS + 1u];
    size_t decimal_length, digit_count = 0u, width = 0u, output_length, index;
    size_t work_length;
    int negative;
    int explicit_width = argument_count > 1u &&
                         arguments[1].type != REXX_VALUE_UNSET;
    RexxVMStatus status;
    char *output = NULL;

    if (radix != 16u && radix != 256u)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    status = rexx_builtin_whole_decimal(vm, &arguments[0], decimal_digits,
                                        &decimal_length, &negative);
    if (status != REXX_VM_STATUS_OK) return status;
    if (negative && !explicit_width)
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (explicit_width) {
        status = rexx_builtin_conversion_width(vm, &arguments[1], &width);
        if (status != REXX_VM_STATUS_OK) return status;
    }

    work_length = decimal_length;
    while (work_length != 0u &&
           !(work_length == 1u && decimal_digits[0] == '0')) {
        unsigned int remainder = 0u;
        size_t write = 0u;
        for (index = 0u; index < work_length; ++index) {
            unsigned int dividend =
                remainder * 10u + (unsigned int)(decimal_digits[index] - '0');
            unsigned int quotient = dividend / radix;
            remainder = dividend % radix;
            if (write != 0u || quotient != 0u)
                decimal_digits[write++] = (char)('0' + quotient);
        }
        if (digit_count >= sizeof(reversed))
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        reversed[digit_count++] = radix == 16u
                                      ? rexx_builtin_hex_digits[remainder]
                                      : (char)remainder;
        if (write == 0u) {
            work_length = 1u;
            decimal_digits[0] = '0';
        } else {
            work_length = write;
            decimal_digits[work_length] = '\0';
        }
    }
    if (digit_count == 0u)
        reversed[digit_count++] = radix == 16u ? '0' : 0;
    output_length = explicit_width ? width : digit_count;
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    if (character_output) {
        size_t copy_count = digit_count < output_length ? digit_count : output_length;
        size_t pad_count = output_length - copy_count;
        if (pad_count != 0u) memset(output, 0, pad_count);
        for (index = 0u; index < copy_count; ++index)
            output[output_length - index - 1u] = reversed[index];
        if (negative) {
            unsigned int carry = 1u;
            for (index = output_length; index != 0u; --index) {
                unsigned int value =
                    (unsigned int)(unsigned char)output[index - 1u];
                unsigned int twos = 255u - value + carry;
                output[index - 1u] = (char)(twos & 255u);
                carry = twos >> 8;
            }
        }
    } else {
        size_t copy_count = digit_count < output_length ? digit_count : output_length;
        size_t pad_count = output_length - copy_count;
        if (pad_count != 0u) memset(output, '0', pad_count);
        for (index = 0u; index < copy_count; ++index)
            output[output_length - index - 1u] = reversed[index];
        if (negative) {
            unsigned int carry = 1u;
            for (index = output_length; index != 0u; --index) {
                int digit = rexx_builtin_hex_value((unsigned char)output[index - 1u]);
                unsigned int twos;
                if (digit < 0) return REXX_VM_STATUS_INVALID_ARGUMENT;
                twos = 15u - (unsigned int)digit + carry;
                output[index - 1u] = rexx_builtin_hex_digits[twos & 15u];
                carry = twos >> 4;
            }
        }
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_d2c(RexxVM *vm, const RexxValue *arguments,
                                     size_t argument_count, RexxValue *result)
{
    return rexx_builtin_decimal_to_radix_string(vm, arguments, argument_count,
                                                result, 256u, 1);
}

static RexxVMStatus rexx_builtin_d2x(RexxVM *vm, const RexxValue *arguments,
                                     size_t argument_count, RexxValue *result)
{
    return rexx_builtin_decimal_to_radix_string(vm, arguments, argument_count,
                                                result, 16u, 0);
}

static RexxVMStatus rexx_builtin_c2x(const RexxValue *arguments,
                                     size_t argument_count, RexxValue *result)
{
    RexxBuiltinStringView input;
    RexxVMStatus status;
    char *output = NULL;
    size_t output_length, index;
    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &input);
    if (status != REXX_VM_STATUS_OK) return status;
    if (input.length > REXX_BUILTIN_MAX_RESULT_LENGTH / 2u)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    output_length = input.length * 2u;
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    for (index = 0u; index < input.length; ++index) {
        unsigned int byte = (unsigned char)input.bytes[index];
        output[index * 2u] = rexx_builtin_hex_digits[byte >> 4];
        output[index * 2u + 1u] = rexx_builtin_hex_digits[byte & 15u];
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_x2c(const RexxValue *arguments,
                                     size_t argument_count, RexxValue *result)
{
    RexxBuiltinStringView input;
    RexxVMStatus status;
    char *output = NULL;
    size_t digit_count, output_length, output_index = 0u, input_index;
    int high = -1;
    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &input);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_scan_hex(&input, &digit_count);
    if (status != REXX_VM_STATUS_OK) return status;
    output_length = digit_count / 2u + (digit_count & 1u);
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    for (input_index = 0u; input_index < input.length; ++input_index) {
        unsigned char byte = (unsigned char)input.bytes[input_index];
        int nibble;
        if (byte == (unsigned char)' ') continue;
        nibble = rexx_builtin_hex_value(byte);
        if ((digit_count & 1u) != 0u && output_index == 0u && high < 0) {
            output[output_index++] = (char)nibble;
            continue;
        }
        if (high < 0) high = nibble;
        else {
            output[output_index++] = (char)((high << 4) | nibble);
            high = -1;
        }
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_hex_to_decimal(
    RexxVM *vm, const RexxValue *arguments, size_t argument_count,
    RexxValue *result)
{
    RexxBuiltinStringView input;
    RexxBuiltinDecimalAccumulator magnitude;
    RexxVMStatus status;
    size_t digit_count, width = 0u, skipped, selected, logical_index, source_index;
    size_t last_nonzero = SIZE_MAX, precision = rexx_builtin_digits(vm);
    int explicit_width = argument_count > 1u &&
                         arguments[1].type != REXX_VALUE_UNSET;
    int negative = 0;

    status = rexx_builtin_string_view(&arguments[0], &input);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_scan_hex(&input, &digit_count);
    if (status != REXX_VM_STATUS_OK) return status;
    if (explicit_width) {
        status = rexx_builtin_conversion_width(vm, &arguments[1], &width);
        if (status != REXX_VM_STATUS_OK) return status;
        if (width == 0u) {
            rexx_builtin_decimal_accumulator_init(&magnitude, precision);
            return rexx_builtin_return_accumulated_decimal(vm, &magnitude, 0,
                                                            result);
        }
        selected = digit_count < width ? digit_count : width;
        skipped = digit_count - selected;
        if (digit_count >= width) {
            logical_index = 0u;
            for (source_index = 0u; source_index < input.length; ++source_index) {
                int nibble = rexx_builtin_hex_value(
                    (unsigned char)input.bytes[source_index]);
                if (nibble < 0) continue;
                if (logical_index == skipped) {
                    negative = (nibble & 8) != 0;
                    break;
                }
                ++logical_index;
            }
        }
    } else {
        selected = digit_count;
        skipped = 0u;
    }
    if (negative) {
        logical_index = 0u;
        for (source_index = 0u; source_index < input.length; ++source_index) {
            int nibble = rexx_builtin_hex_value(
                (unsigned char)input.bytes[source_index]);
            if (nibble < 0) continue;
            if (logical_index >= skipped && logical_index - skipped < selected &&
                nibble != 0)
                last_nonzero = logical_index - skipped;
            ++logical_index;
        }
    }
    if (precision == 0u || precision > REXX_NUMERIC_MAX_DIGITS)
        return REXX_VM_STATUS_INVALID_NUMERIC_SETTING;
    rexx_builtin_decimal_accumulator_init(&magnitude, precision);
    if (selected == 0u)
        return rexx_builtin_return_accumulated_decimal(vm, &magnitude, 0,
                                                        result);
    logical_index = 0u;
    for (source_index = 0u; source_index < input.length; ++source_index) {
        unsigned char byte = (unsigned char)input.bytes[source_index];
        int nibble;
        unsigned int value;
        if (byte == (unsigned char)' ') continue;
        nibble = rexx_builtin_hex_value(byte);
        if (logical_index < skipped) {
            ++logical_index;
            continue;
        }
        if (logical_index - skipped >= selected) break;
        value = (unsigned int)nibble;
        if (negative) {
            size_t position = logical_index - skipped;
            if (position < last_nonzero) value = 15u - value;
            else if (position == last_nonzero) value = 16u - value;
            else value = 0u;
        }
        status = rexx_builtin_decimal_accumulate(vm, &magnitude, 16u, value);
        if (status != REXX_VM_STATUS_OK) return status;
        ++logical_index;
    }
    return rexx_builtin_return_accumulated_decimal(vm, &magnitude, negative,
                                                    result);
}

static RexxVMStatus rexx_builtin_c2d(RexxVM *vm, const RexxValue *arguments,
                                     size_t argument_count, RexxValue *result)
{
    RexxBuiltinStringView input;
    RexxBuiltinDecimalAccumulator magnitude;
    RexxVMStatus status;
    size_t width = 0u, start, selected, index, last_nonzero = SIZE_MAX;
    size_t precision = rexx_builtin_digits(vm);
    int explicit_width = argument_count > 1u &&
                         arguments[1].type != REXX_VALUE_UNSET;
    int negative = 0;
    status = rexx_builtin_string_view(&arguments[0], &input);
    if (status != REXX_VM_STATUS_OK) return status;
    if (explicit_width) {
        status = rexx_builtin_conversion_width(vm, &arguments[1], &width);
        if (status != REXX_VM_STATUS_OK) return status;
        if (width == 0u) {
            rexx_builtin_decimal_accumulator_init(&magnitude, precision);
            return rexx_builtin_return_accumulated_decimal(vm, &magnitude, 0,
                                                            result);
        }
        selected = input.length < width ? input.length : width;
        start = input.length - selected;
        if (input.length >= width && selected != 0u)
            negative = (((unsigned char)input.bytes[start]) & 0x80u) != 0u;
    } else {
        start = 0u;
        selected = input.length;
    }
    if (precision == 0u || precision > REXX_NUMERIC_MAX_DIGITS)
        return REXX_VM_STATUS_INVALID_NUMERIC_SETTING;
    if (negative) {
        for (index = start + selected; index != start; --index)
            if ((unsigned char)input.bytes[index - 1u] != 0u) {
                last_nonzero = index - start - 1u;
                break;
            }
    }
    rexx_builtin_decimal_accumulator_init(&magnitude, precision);
    for (index = 0u; index < selected; ++index) {
        unsigned int byte = (unsigned char)input.bytes[start + index];
        if (negative) {
            if (index < last_nonzero) byte = 255u - byte;
            else if (index == last_nonzero) byte = 256u - byte;
            else byte = 0u;
        }
        status = rexx_builtin_decimal_accumulate(vm, &magnitude, 256u, byte);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    return rexx_builtin_return_accumulated_decimal(vm, &magnitude, negative,
                                                    result);
}

static RexxVMStatus rexx_builtin_b2x(const RexxValue *arguments,
                                     size_t argument_count, RexxValue *result)
{
    RexxBuiltinStringView input;
    RexxVMStatus status;
    char *output = NULL;
    size_t bit_count, output_length, padding, input_index, output_index = 0u;
    unsigned int nibble = 0u, nibble_bits;
    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &input);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_scan_binary(&input, &bit_count);
    if (status != REXX_VM_STATUS_OK) return status;
    output_length = bit_count / 4u + ((bit_count & 3u) != 0u);
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    if (bit_count == 0u) return REXX_VM_STATUS_OK;
    padding = output_length * 4u - bit_count;
    nibble_bits = (unsigned int)padding;
    for (input_index = 0u; input_index < input.length; ++input_index) {
        unsigned char byte = (unsigned char)input.bytes[input_index];
        if (byte == (unsigned char)' ') continue;
        nibble = (nibble << 1) | (unsigned int)(byte - (unsigned char)'0');
        ++nibble_bits;
        if (nibble_bits == 4u) {
            output[output_index++] = rexx_builtin_hex_digits[nibble];
            nibble = 0u;
            nibble_bits = 0u;
        }
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_x2b(const RexxValue *arguments,
                                     size_t argument_count, RexxValue *result)
{
    static const char binary[16][5] = {
        "0000", "0001", "0010", "0011", "0100", "0101", "0110", "0111",
        "1000", "1001", "1010", "1011", "1100", "1101", "1110", "1111"
    };
    RexxBuiltinStringView input;
    RexxVMStatus status;
    char *output = NULL;
    size_t digit_count, output_length, input_index, output_index = 0u;
    (void)argument_count;
    status = rexx_builtin_string_view(&arguments[0], &input);
    if (status != REXX_VM_STATUS_OK) return status;
    status = rexx_builtin_scan_hex(&input, &digit_count);
    if (status != REXX_VM_STATUS_OK) return status;
    if (digit_count > REXX_BUILTIN_MAX_RESULT_LENGTH / 4u)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    output_length = digit_count * 4u;
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;
    for (input_index = 0u; input_index < input.length; ++input_index) {
        int nibble;
        unsigned char byte = (unsigned char)input.bytes[input_index];
        if (byte == (unsigned char)' ') continue;
        nibble = rexx_builtin_hex_value(byte);
        memcpy(output + output_index, binary[nibble], 4u);
        output_index += 4u;
    }
    return REXX_VM_STATUS_OK;
}

typedef enum RexxBuiltinBitwiseOperation {
    REXX_BUILTIN_BITWISE_AND,
    REXX_BUILTIN_BITWISE_OR,
    REXX_BUILTIN_BITWISE_XOR
} RexxBuiltinBitwiseOperation;

/* Apply a classic REXX byte-string operation without interpreting bytes as
 * numbers. The result is built once, with the longer operand's exact length. */
static RexxVMStatus rexx_builtin_bitwise_string(
    const RexxValue *arguments, size_t argument_count, RexxValue *result,
    RexxBuiltinBitwiseOperation operation)
{
    RexxBuiltinStringView first, second, pad;
    size_t output_length, overlap, index;
    int has_pad;
    char *output = NULL;
    RexxVMStatus status;

    if (arguments == NULL || argument_count == 0u || argument_count > 3u ||
        arguments[0].type == REXX_VALUE_UNSET) {
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    if (operation != REXX_BUILTIN_BITWISE_AND &&
        operation != REXX_BUILTIN_BITWISE_OR &&
        operation != REXX_BUILTIN_BITWISE_XOR) {
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    status = rexx_builtin_string_view(&arguments[0], &first);
    if (status != REXX_VM_STATUS_OK) return status;

    second.bytes = rexx_builtin_empty;
    second.length = 0u;
    if (argument_count >= 2u &&
        arguments[1].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[1], &second);
        if (status != REXX_VM_STATUS_OK) return status;
    }

    has_pad = argument_count >= 3u &&
              arguments[2].type != REXX_VALUE_UNSET;
    pad.bytes = rexx_builtin_empty;
    pad.length = 0u;
    if (has_pad) {
        status = rexx_builtin_string_view(&arguments[2], &pad);
        if (status != REXX_VM_STATUS_OK) return status;
        if (pad.length != 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }

    output_length = first.length > second.length
                        ? first.length : second.length;
    if (output_length > REXX_BUILTIN_MAX_RESULT_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    status = rexx_builtin_allocate_result(result, output_length, &output);
    if (status != REXX_VM_STATUS_OK) return status;

    overlap = first.length < second.length ? first.length : second.length;
    for (index = 0u; index < output_length; ++index) {
        unsigned char left_byte, right_byte, combined;
        if (index < first.length) {
            left_byte = (unsigned char)first.bytes[index];
        } else if (has_pad) {
            left_byte = (unsigned char)pad.bytes[0];
        } else {
            left_byte = 0u;
        }
        if (index < second.length) {
            right_byte = (unsigned char)second.bytes[index];
        } else if (has_pad) {
            right_byte = (unsigned char)pad.bytes[0];
        } else {
            right_byte = 0u;
        }

        if (index >= overlap && !has_pad) {
            /* Without a pad classic REXX leaves the longer tail untouched. */
            output[index] = first.length > second.length
                                ? first.bytes[index] : second.bytes[index];
            continue;
        }
        if (operation == REXX_BUILTIN_BITWISE_AND) {
            combined = (unsigned char)(left_byte & right_byte);
        } else if (operation == REXX_BUILTIN_BITWISE_OR) {
            combined = (unsigned char)(left_byte | right_byte);
        } else {
            combined = (unsigned char)(left_byte ^ right_byte);
        }
        output[index] = (char)combined;
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus rexx_builtin_bitand(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    return rexx_builtin_bitwise_string(arguments, argument_count, result,
                                       REXX_BUILTIN_BITWISE_AND);
}

static RexxVMStatus rexx_builtin_bitor(const RexxValue *arguments,
                                       size_t argument_count,
                                       RexxValue *result)
{
    return rexx_builtin_bitwise_string(arguments, argument_count, result,
                                       REXX_BUILTIN_BITWISE_OR);
}

static RexxVMStatus rexx_builtin_bitxor(const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    return rexx_builtin_bitwise_string(arguments, argument_count, result,
                                       REXX_BUILTIN_BITWISE_XOR);
}

typedef struct RexxBuiltinDate {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint16_t day_of_year;
    uint32_t base_day;
} RexxBuiltinDate;

static int rexx_builtin_is_leap_year(uint16_t year)
{
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

static uint8_t rexx_builtin_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u
    };
    if (month == 2u && rexx_builtin_is_leap_year(year)) return 29u;
    return month >= 1u && month <= 12u ? days[month - 1u] : 0u;
}

static uint32_t rexx_builtin_days_before_year(uint16_t year)
{
    uint32_t prior = (uint32_t)year - 1u;
    return prior * 365u + prior / 4u - prior / 100u + prior / 400u;
}

static int rexx_builtin_date_from_parts(uint16_t year, uint8_t month,
                                        uint8_t day, RexxBuiltinDate *date)
{
    uint16_t day_of_year = day;
    uint8_t current_month;
    if (date == NULL || year < 1u || year > 9999u || month < 1u ||
        month > 12u || day < 1u ||
        day > rexx_builtin_days_in_month(year, month)) return 0;
    for (current_month = 1u; current_month < month; ++current_month)
        day_of_year = (uint16_t)(day_of_year +
            rexx_builtin_days_in_month(year, current_month));
    date->year = year;
    date->month = month;
    date->day = day;
    date->day_of_year = day_of_year;
    date->base_day = rexx_builtin_days_before_year(year) +
                     (uint32_t)day_of_year - 1u;
    return 1;
}

static int rexx_builtin_date_from_base(uint32_t base_day,
                                       RexxBuiltinDate *date)
{
    uint16_t low = 1u, high = 10000u, year;
    uint8_t month;
    uint32_t day_of_year;
    if (date == NULL || base_day >= rexx_builtin_days_before_year(10000u))
        return 0;
    while (low + 1u < high) {
        uint16_t middle = (uint16_t)(low + (high - low) / 2u);
        if (rexx_builtin_days_before_year(middle) <= base_day)
            low = middle;
        else
            high = middle;
    }
    year = low;
    day_of_year = base_day - rexx_builtin_days_before_year(year) + 1u;
    for (month = 1u; month <= 12u; ++month) {
        uint8_t month_days = rexx_builtin_days_in_month(year, month);
        if (day_of_year <= month_days)
            return rexx_builtin_date_from_parts(
                year, month, (uint8_t)day_of_year, date);
        day_of_year -= month_days;
    }
    return 0;
}

static int rexx_builtin_fixed_digits(const char *bytes, size_t length,
                                    size_t offset, size_t count,
                                    uint32_t *value)
{
    size_t index;
    uint32_t parsed = 0u;
    if (bytes == NULL || value == NULL || offset > length ||
        count > length - offset) return 0;
    for (index = offset; index < offset + count; ++index) {
        unsigned char byte = (unsigned char)bytes[index];
        if (byte < (unsigned char)'0' || byte > (unsigned char)'9')
            return 0;
        parsed = parsed * 10u + (uint32_t)(byte - (unsigned char)'0');
    }
    *value = parsed;
    return 1;
}

static int rexx_builtin_variable_digits(const char *bytes, size_t length,
                                        uint32_t *value)
{
    size_t index;
    uint32_t parsed = 0u;
    if (bytes == NULL || value == NULL || length == 0u ||
        (length > 1u && bytes[0] == '0')) return 0;
    for (index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)bytes[index];
        if (byte < (unsigned char)'0' || byte > (unsigned char)'9' ||
            parsed > (UINT32_MAX - 9u) / 10u) return 0;
        parsed = parsed * 10u + (uint32_t)(byte - (unsigned char)'0');
    }
    *value = parsed;
    return 1;
}

static int rexx_builtin_year_from_yy(uint32_t year_2, uint16_t current_year,
                                     uint16_t *year_out)
{
    int32_t low = (int32_t)current_year - 50;
    int32_t high = (int32_t)current_year + 49;
    int32_t candidate_start = low < 1 ? 1 : low;
    int32_t remainder = candidate_start % 100;
    int32_t addition = ((int32_t)year_2 - remainder + 100) % 100;
    int32_t candidate = candidate_start + addition;
    if (year_out == NULL || candidate > high || candidate > 9999)
        return 0;
    *year_out = (uint16_t)candidate;
    return 1;
}

static int rexx_builtin_month_index(const char *bytes, size_t length,
                                    uint8_t *month)
{
    static const char *const names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    size_t index;
    if (bytes == NULL || length != 3u || month == NULL) return 0;
    for (index = 0u; index < 12u; ++index) {
        if (memcmp(bytes, names[index], 3u) == 0) {
            *month = (uint8_t)(index + 1u);
            return 1;
        }
    }
    return 0;
}

static RexxVMStatus rexx_builtin_current_date(RexxVM *vm,
                                             RexxBuiltinDate *date,
                                             RexxVMClockReading *reading_out)
{
    RexxVMClockReading reading;
    int64_t seconds_of_day;
    int64_t day_delta;
    int64_t base_day;
    RexxVMStatus status;
    if (vm == NULL || date == NULL) return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
    status = rexx_vm_clock_read_for_clause(vm, &reading);
    if (status != REXX_VM_STATUS_OK) return status;
    if ((reading.capabilities & REXX_CLOCK_CAP_WALL_CIVIL) == 0u ||
        (reading.wall_interpretation != REXX_CLOCK_WALL_LOCAL_CIVIL &&
         reading.wall_interpretation != REXX_CLOCK_WALL_UTC_FIXED_OFFSET))
        return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
    if (reading.wall_interpretation == REXX_CLOCK_WALL_UTC_FIXED_OFFSET) {
        if (!rexx_builtin_date_from_parts(reading.year, reading.month,
                                          reading.day, date))
            return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
        base_day = date->base_day;
        seconds_of_day = (int64_t)reading.hour * 3600 +
                         (int64_t)reading.minute * 60 + reading.second +
                         reading.utc_offset_seconds;
        day_delta = seconds_of_day / 86400;
        if (seconds_of_day < 0 && seconds_of_day % 86400 != 0) --day_delta;
        seconds_of_day -= day_delta * 86400;
        base_day += day_delta;
        if (base_day < 0 || base_day >=
                (int64_t)rexx_builtin_days_before_year(10000u) ||
            !rexx_builtin_date_from_base((uint32_t)base_day, date))
            return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
    } else if (!rexx_builtin_date_from_parts(reading.year, reading.month,
                                              reading.day, date)) {
        return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
    }
    if (reading_out != NULL) *reading_out = reading;
    return REXX_VM_STATUS_OK;
}

static int rexx_builtin_parse_date(const char *bytes, size_t length,
                                   char input_option, uint16_t current_year,
                                   RexxBuiltinDate *date)
{
    uint32_t a, b, c;
    uint16_t year;
    uint8_t month;
    switch (input_option) {
    case 'S':
        return length == 8u &&
               rexx_builtin_fixed_digits(bytes, length, 0u, 4u, &a) &&
               rexx_builtin_fixed_digits(bytes, length, 4u, 2u, &b) &&
               rexx_builtin_fixed_digits(bytes, length, 6u, 2u, &c) &&
               rexx_builtin_date_from_parts((uint16_t)a, (uint8_t)b,
                                             (uint8_t)c, date);
    case 'E':
    case 'U':
        if (length != 8u || bytes[2] != '/' || bytes[5] != '/' ||
            !rexx_builtin_fixed_digits(bytes, length, 0u, 2u, &a) ||
            !rexx_builtin_fixed_digits(bytes, length, 3u, 2u, &b) ||
            !rexx_builtin_fixed_digits(bytes, length, 6u, 2u, &c) ||
            !rexx_builtin_year_from_yy(c, current_year, &year)) return 0;
        return input_option == 'E'
            ? rexx_builtin_date_from_parts(year, (uint8_t)b, (uint8_t)a, date)
            : rexx_builtin_date_from_parts(year, (uint8_t)a, (uint8_t)b, date);
    case 'O':
        if (length != 8u || bytes[2] != '/' || bytes[5] != '/' ||
            !rexx_builtin_fixed_digits(bytes, length, 0u, 2u, &a) ||
            !rexx_builtin_fixed_digits(bytes, length, 3u, 2u, &b) ||
            !rexx_builtin_fixed_digits(bytes, length, 6u, 2u, &c) ||
            !rexx_builtin_year_from_yy(a, current_year, &year)) return 0;
        return rexx_builtin_date_from_parts(year, (uint8_t)b, (uint8_t)c,
                                             date);
    case 'J':
        if (length != 5u ||
            !rexx_builtin_fixed_digits(bytes, length, 0u, 2u, &a) ||
            !rexx_builtin_fixed_digits(bytes, length, 2u, 3u, &b) ||
            !rexx_builtin_year_from_yy(a, current_year, &year) || b == 0u ||
            b > (rexx_builtin_is_leap_year(year) ? 366u : 365u)) return 0;
        return rexx_builtin_date_from_base(
            rexx_builtin_days_before_year(year) + b - 1u, date);
    case 'N': {
        size_t cursor = 0u;
        size_t day_length;
        uint32_t day_value;
        uint32_t year_value;
        while (cursor < length && bytes[cursor] >= '0' && bytes[cursor] <= '9')
            ++cursor;
        day_length = cursor;
        if (day_length == 0u || day_length > 2u ||
            (day_length > 1u && bytes[0] == '0') || cursor >= length ||
            bytes[cursor++] != ' ' || length - cursor < 8u ||
            !rexx_builtin_variable_digits(bytes, day_length, &day_value) ||
            day_value > 31u || !rexx_builtin_month_index(bytes + cursor, 3u,
                                                         &month)) return 0;
        cursor += 3u;
        if (cursor >= length || bytes[cursor++] != ' ' ||
            length - cursor != 4u ||
            !rexx_builtin_fixed_digits(bytes, length, cursor, 4u,
                                       &year_value)) return 0;
        return rexx_builtin_date_from_parts((uint16_t)year_value, month,
                                             (uint8_t)day_value, date);
    }
    case 'B':
        if (!rexx_builtin_variable_digits(bytes, length, &a) ||
            a >= rexx_builtin_days_before_year(10000u)) return 0;
        return rexx_builtin_date_from_base(a, date);
    default:
        return 0;
    }
}

static void rexx_builtin_append_char(char *buffer, size_t *length, char value)
{
    buffer[(*length)++] = value;
}

static void rexx_builtin_append_uint(char *buffer, size_t *length,
                                     uint32_t value)
{
    char reversed[10];
    size_t count = 0u;
    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) buffer[(*length)++] = reversed[--count];
}

static void rexx_builtin_append_fixed(char *buffer, size_t *length,
                                     uint32_t value, size_t width)
{
    size_t index = width;
    while (index != 0u) {
        buffer[*length + --index] = (char)('0' + value % 10u);
        value /= 10u;
    }
    *length += width;
}

static RexxVMStatus rexx_builtin_date(RexxVM *vm,
                                      const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    static const char *const month_short[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const char *const month_full[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const char *const weekdays[] = {
        "Monday", "Tuesday", "Wednesday", "Thursday",
        "Friday", "Saturday", "Sunday"
    };
    RexxBuiltinStringView option_view, input_view, input_option_view;
    RexxBuiltinDate date;
    RexxVMClockReading reading;
    char output[40];
    size_t output_length = 0u;
    char output_option = 'N';
    char input_option = 'N';
    int input_present = argument_count >= 2u &&
                        arguments[1].type != REXX_VALUE_UNSET;
    uint16_t current_year = 0u;
    RexxVMStatus status;

    if (argument_count > 3u || (argument_count >= 3u && !input_present))
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (argument_count >= 1u && arguments[0].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[0], &option_view);
        if (status != REXX_VM_STATUS_OK) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (option_view.length == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        output_option = (char)rexx_builtin_ascii_upper(
            (unsigned char)option_view.bytes[0]);
    }
    if (argument_count >= 3u && arguments[2].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[2], &input_option_view);
        if (status != REXX_VM_STATUS_OK) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (input_option_view.length == 0u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        input_option = (char)rexx_builtin_ascii_upper(
            (unsigned char)input_option_view.bytes[0]);
    }
    if (output_option != 'B' && output_option != 'C' && output_option != 'D' &&
        output_option != 'E' && output_option != 'J' && output_option != 'M' &&
        output_option != 'N' && output_option != 'O' && output_option != 'S' &&
        output_option != 'U' && output_option != 'W')
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (input_present && input_option != 'B' && input_option != 'E' &&
        input_option != 'J' && input_option != 'N' && input_option != 'O' &&
        input_option != 'S' && input_option != 'U')
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (input_present && (output_option == 'C' || output_option == 'J' ||
                          output_option == 'M' || output_option == 'W'))
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;

    if (input_present) {
        status = rexx_builtin_string_view(&arguments[1], &input_view);
        if (status != REXX_VM_STATUS_OK || input_view.length == 0u)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        if (input_option == 'E' || input_option == 'J' ||
            input_option == 'O' || input_option == 'U') {
            status = rexx_builtin_current_date(vm, &date, &reading);
            if (status != REXX_VM_STATUS_OK) return status;
            current_year = date.year;
        }
        if (!rexx_builtin_parse_date(input_view.bytes, input_view.length,
                                     input_option, current_year, &date))
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    } else {
        status = rexx_builtin_current_date(vm, &date, &reading);
        if (status != REXX_VM_STATUS_OK) return status;
    }

    switch (output_option) {
    case 'B':
        rexx_builtin_append_uint(output, &output_length, date.base_day);
        break;
    case 'C': {
        uint16_t century_start = (uint16_t)(((date.year - 1u) / 100u) * 100u);
        uint32_t start_day = century_start == 0u
            ? 0u : rexx_builtin_days_before_year(century_start);
        rexx_builtin_append_uint(output, &output_length,
                                 date.base_day - start_day + 1u);
        break;
    }
    case 'D':
        rexx_builtin_append_uint(output, &output_length, date.day_of_year);
        break;
    case 'E':
        rexx_builtin_append_fixed(output, &output_length, date.day, 2u);
        rexx_builtin_append_char(output, &output_length, '/');
        rexx_builtin_append_fixed(output, &output_length, date.month, 2u);
        rexx_builtin_append_char(output, &output_length, '/');
        rexx_builtin_append_fixed(output, &output_length, date.year % 100u, 2u);
        break;
    case 'J':
        rexx_builtin_append_fixed(output, &output_length, date.year % 100u, 2u);
        rexx_builtin_append_fixed(output, &output_length, date.day_of_year, 3u);
        break;
    case 'M':
        memcpy(output, month_full[date.month - 1u],
               strlen(month_full[date.month - 1u]));
        output_length = strlen(month_full[date.month - 1u]);
        break;
    case 'N':
        rexx_builtin_append_uint(output, &output_length, date.day);
        rexx_builtin_append_char(output, &output_length, ' ');
        memcpy(output + output_length, month_short[date.month - 1u], 3u);
        output_length += 3u;
        rexx_builtin_append_char(output, &output_length, ' ');
        rexx_builtin_append_fixed(output, &output_length, date.year, 4u);
        break;
    case 'O':
        rexx_builtin_append_fixed(output, &output_length, date.year % 100u, 2u);
        rexx_builtin_append_char(output, &output_length, '/');
        rexx_builtin_append_fixed(output, &output_length, date.month, 2u);
        rexx_builtin_append_char(output, &output_length, '/');
        rexx_builtin_append_fixed(output, &output_length, date.day, 2u);
        break;
    case 'S':
        rexx_builtin_append_fixed(output, &output_length, date.year, 4u);
        rexx_builtin_append_fixed(output, &output_length, date.month, 2u);
        rexx_builtin_append_fixed(output, &output_length, date.day, 2u);
        break;
    case 'U':
        rexx_builtin_append_fixed(output, &output_length, date.month, 2u);
        rexx_builtin_append_char(output, &output_length, '/');
        rexx_builtin_append_fixed(output, &output_length, date.day, 2u);
        rexx_builtin_append_char(output, &output_length, '/');
        rexx_builtin_append_fixed(output, &output_length, date.year % 100u, 2u);
        break;
    case 'W':
        memcpy(output, weekdays[date.base_day % 7u],
               strlen(weekdays[date.base_day % 7u]));
        output_length = strlen(weekdays[date.base_day % 7u]);
        break;
    default:
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    return rexx_value_set_string(result, output, output_length);
}

static RexxVMStatus rexx_builtin_time(RexxVM *vm,
                                      const RexxValue *arguments,
                                      size_t argument_count,
                                      RexxValue *result)
{
    RexxBuiltinStringView option_view;
    RexxVMClockReading reading;
    RexxVMClockContext *context;
    char output[40];
    size_t output_length = 0u;
    char option = 'N';
    uint32_t hour, minute, second, fraction_microseconds;
    RexxVMStatus status;

    if (argument_count > 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (argument_count == 1u && arguments[0].type != REXX_VALUE_UNSET) {
        status = rexx_builtin_string_view(&arguments[0], &option_view);
        if (status != REXX_VM_STATUS_OK || option_view.length == 0u)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        option = (char)rexx_builtin_ascii_upper(
            (unsigned char)option_view.bytes[0]);
    }
    if (option != 'C' && option != 'E' && option != 'H' && option != 'L' &&
        option != 'M' && option != 'N' && option != 'R' && option != 'S')
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (vm == NULL) return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
    status = rexx_vm_clock_read_for_clause(vm, &reading);
    if (status != REXX_VM_STATUS_OK) return status;
    context = &vm->clock_context;

    if (option == 'E' || option == 'R') {
        uint64_t elapsed_nanoseconds;
        uint64_t elapsed_seconds;
        uint64_t elapsed_fraction;
        if ((reading.capabilities & REXX_CLOCK_CAP_MONOTONIC) == 0u)
            return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
        if (!context->elapsed_started) {
            context->elapsed_started = 1;
            context->elapsed_start_nanoseconds =
                reading.monotonic_nanoseconds;
            context->elapsed_clause_value_nanoseconds = 0u;
            context->elapsed_clause_value_valid = 1;
        } else if (!context->elapsed_clause_value_valid) {
            if (reading.monotonic_nanoseconds <
                context->elapsed_start_nanoseconds)
                return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
            context->elapsed_clause_value_nanoseconds =
                reading.monotonic_nanoseconds -
                context->elapsed_start_nanoseconds;
            context->elapsed_clause_value_valid = 1;
        }
        elapsed_nanoseconds = context->elapsed_clause_value_nanoseconds;
        elapsed_seconds = elapsed_nanoseconds / UINT64_C(1000000000);
        elapsed_fraction = (elapsed_nanoseconds % UINT64_C(1000000000)) /
                           UINT64_C(1000);
        if (elapsed_seconds > UINT64_C(999999999)) {
            if (option == 'R') {
                context->elapsed_start_nanoseconds =
                    reading.monotonic_nanoseconds;
                context->elapsed_clause_value_nanoseconds = 0u;
                context->elapsed_clause_value_valid = 1;
                context->elapsed_clause_reset = 1;
            }
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        rexx_builtin_append_uint(output, &output_length,
                                 (uint32_t)elapsed_seconds);
        rexx_builtin_append_char(output, &output_length, '.');
        rexx_builtin_append_fixed(output, &output_length,
                                  (uint32_t)elapsed_fraction, 6u);
        if (option == 'R' && !context->elapsed_clause_reset) {
            context->elapsed_start_nanoseconds =
                reading.monotonic_nanoseconds;
            context->elapsed_clause_reset = 1;
        }
        return rexx_value_set_string(result, output, output_length);
    }

    if ((reading.capabilities & REXX_CLOCK_CAP_WALL_CIVIL) == 0u ||
        (reading.wall_interpretation != REXX_CLOCK_WALL_LOCAL_CIVIL &&
         reading.wall_interpretation != REXX_CLOCK_WALL_UTC_FIXED_OFFSET))
        return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
    {
        RexxBuiltinDate local_date;
        if (!rexx_builtin_date_from_parts(reading.year, reading.month,
                                          reading.day, &local_date)) {
            return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
        }
        hour = reading.hour;
        minute = reading.minute;
        second = reading.second;
        if (reading.wall_interpretation ==
            REXX_CLOCK_WALL_UTC_FIXED_OFFSET) {
            int64_t seconds_of_day = (int64_t)hour * 3600 +
                (int64_t)minute * 60 + second + reading.utc_offset_seconds;
            int64_t day_delta = seconds_of_day / 86400;
            int64_t base_day = local_date.base_day;
            if (seconds_of_day < 0 && seconds_of_day % 86400 != 0)
                --day_delta;
            seconds_of_day -= day_delta * 86400;
            base_day += day_delta;
            if (base_day < 0 || base_day >=
                    (int64_t)rexx_builtin_days_before_year(10000u) ||
                !rexx_builtin_date_from_base((uint32_t)base_day, &local_date))
                return REXX_VM_STATUS_CLOCK_UNAVAILABLE;
            hour = (uint32_t)(seconds_of_day / 3600);
            minute = (uint32_t)((seconds_of_day / 60) % 60);
            second = (uint32_t)(seconds_of_day % 60);
        }
    }
    fraction_microseconds = reading.fractional_nanoseconds;
    fraction_microseconds -= fraction_microseconds %
        reading.wall_resolution_nanoseconds;
    fraction_microseconds /= 1000u;
    switch (option) {
    case 'C':
        rexx_builtin_append_uint(output, &output_length,
                                 hour % 12u == 0u ? 12u : hour % 12u);
        rexx_builtin_append_char(output, &output_length, ':');
        rexx_builtin_append_fixed(output, &output_length, minute, 2u);
        memcpy(output + output_length, hour < 12u ? "am" : "pm", 2u);
        output_length += 2u;
        break;
    case 'H':
        rexx_builtin_append_uint(output, &output_length, hour);
        break;
    case 'L':
        rexx_builtin_append_fixed(output, &output_length, hour, 2u);
        rexx_builtin_append_char(output, &output_length, ':');
        rexx_builtin_append_fixed(output, &output_length, minute, 2u);
        rexx_builtin_append_char(output, &output_length, ':');
        rexx_builtin_append_fixed(output, &output_length, second, 2u);
        rexx_builtin_append_char(output, &output_length, '.');
        rexx_builtin_append_fixed(output, &output_length,
                                  fraction_microseconds, 6u);
        break;
    case 'M':
        rexx_builtin_append_uint(output, &output_length, hour * 60u + minute);
        break;
    case 'N':
        rexx_builtin_append_fixed(output, &output_length, hour, 2u);
        rexx_builtin_append_char(output, &output_length, ':');
        rexx_builtin_append_fixed(output, &output_length, minute, 2u);
        rexx_builtin_append_char(output, &output_length, ':');
        rexx_builtin_append_fixed(output, &output_length, second, 2u);
        break;
    case 'S':
        rexx_builtin_append_uint(output, &output_length,
                                 hour * 3600u + minute * 60u + second);
        break;
    default:
        return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    return rexx_value_set_string(result, output, output_length);
}

static const RexxBuiltinDescriptor rexx_builtin_table[] = {
    { "ADDRESS", 0u, 0u, NULL, rexx_builtin_address },
    { "TRACE", 0u, 1u, NULL, rexx_builtin_trace },
    { "LENGTH", 1u, 1u, rexx_builtin_length, NULL },
    { "REVERSE", 1u, 1u, rexx_builtin_reverse, NULL },
    { "SOURCELINE", 0u, 1u, NULL, rexx_builtin_sourceline },
    { "ERRORTEXT", 1u, 1u, rexx_builtin_errortext, NULL },
    { "ABBREV", 2u, 3u, rexx_builtin_abbrev, NULL },
    { "CENTER", 2u, 3u, rexx_builtin_center, NULL },
    { "CENTRE", 2u, 3u, rexx_builtin_center, NULL },
    { "LEFT", 2u, 3u, rexx_builtin_left, NULL },
    { "RIGHT", 2u, 3u, rexx_builtin_right, NULL },
    { "INSERT", 2u, 5u, rexx_builtin_insert, NULL },
    { "OVERLAY", 2u, 5u, rexx_builtin_overlay, NULL },
    { "DELSTR", 2u, 3u, rexx_builtin_delstr, NULL },
    { "DELWORD", 2u, 3u, rexx_builtin_delword, NULL },
    { "SUBWORD", 2u, 3u, rexx_builtin_subword, NULL },
    { "WORDINDEX", 2u, 2u, rexx_builtin_wordindex, NULL },
    { "WORDLENGTH", 2u, 2u, rexx_builtin_wordlength, NULL },
    { "LASTPOS", 2u, 3u, rexx_builtin_lastpos, NULL },
    { "SUBSTR", 2u, 4u, rexx_builtin_substr, NULL },
    { "WORD", 2u, 2u, rexx_builtin_word, NULL },
    { "WORDS", 1u, 1u, rexx_builtin_words, NULL },
    { "STRIP", 1u, 3u, rexx_builtin_strip, NULL },
    { "POS", 2u, 3u, rexx_builtin_pos, NULL },
    { "INDEX", 2u, 3u, rexx_builtin_index, NULL },
    { "TRANSLATE", 1u, 4u, rexx_builtin_translate, NULL },
    { "WORDPOS", 2u, 3u, rexx_builtin_wordpos, NULL },
    { "SPACE", 1u, 3u, rexx_builtin_space, NULL },
    { "COPIES", 2u, 2u, rexx_builtin_copies, NULL },
    { "COMPARE", 2u, 3u, rexx_builtin_compare, NULL },
    { "VERIFY", 2u, 4u, rexx_builtin_verify, NULL },
    { "ABS", 1u, 1u, NULL, rexx_builtin_abs },
    { "SIGN", 1u, 1u, NULL, rexx_builtin_sign },
    { "MAX", 2u, 20u, NULL, rexx_builtin_max },
    { "MIN", 2u, 20u, NULL, rexx_builtin_min },
    { "TRUNC", 1u, 2u, NULL, rexx_builtin_trunc },
    { "FORMAT", 1u, 5u, NULL, rexx_builtin_format },
    { "DATATYPE", 1u, 2u, NULL, rexx_builtin_datatype },
    { "SYMBOL", 1u, 1u, NULL, rexx_builtin_symbol },
    { "XRANGE", 0u, 2u, NULL, rexx_builtin_xrange },
    { "C2D", 1u, 2u, NULL, rexx_builtin_c2d },
    { "C2X", 1u, 1u, rexx_builtin_c2x, NULL },
    { "D2C", 1u, 2u, NULL, rexx_builtin_d2c },
    { "D2X", 1u, 2u, NULL, rexx_builtin_d2x },
    { "X2C", 1u, 1u, rexx_builtin_x2c, NULL },
    { "X2D", 1u, 2u, NULL, rexx_builtin_hex_to_decimal },
    { "B2X", 1u, 1u, rexx_builtin_b2x, NULL },
    { "X2B", 1u, 1u, rexx_builtin_x2b, NULL },
    { "BITAND", 1u, 3u, rexx_builtin_bitand, NULL },
    { "BITOR", 1u, 3u, rexx_builtin_bitor, NULL },
    { "BITXOR", 1u, 3u, rexx_builtin_bitxor, NULL },
    { "RANDOM", 0u, 3u, NULL, rexx_builtin_random },
    { "DATE", 0u, 3u, NULL, rexx_builtin_date },
    { "TIME", 0u, 1u, NULL, rexx_builtin_time }
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
    return rexx_builtin_dispatch_with_vm(NULL, name, name_length, arguments,
                                         argument_count, result);
}

RexxVMStatus rexx_builtin_dispatch_with_vm(RexxVM *vm,
                                          const char *name,
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
    if (descriptor->context_function != NULL)
        status = descriptor->context_function(vm, arguments, argument_count,
                                              result);
    else if (descriptor->function != NULL)
        status = descriptor->function(arguments, argument_count, result);
    else
        status = REXX_VM_STATUS_INVALID_ARGUMENT;
    if (status != REXX_VM_STATUS_OK) {
        rexx_value_clear(result);
    }
    return status;
}
