#include "rexx_vm.h"
#include "rexx_builtin.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REXX_VM_MAX_NAME_LENGTH ((size_t)65535u)

static const char *const vm_messages[] = {
    "ok", "invalid VM argument", "out of memory", "VM resource limit exceeded",
    "evaluation stack overflow", "evaluation stack underflow",
    "invalid or malformed bytecode", "value is not a valid finite number",
    "opcode is not implemented by this VM", "standard output error",
    "unknown REXX built-in function", "invalid arguments to REXX built-in function",
    "instruction budget exhausted"
};

static int ascii_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static unsigned char ascii_upper(unsigned char c)
{
    return c >= 'a' && c <= 'z' ? (unsigned char)(c - 'a' + 'A') : c;
}

static int ascii_equal(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t i;
    if (alen != blen) return 0;
    for (i = 0; i < alen; ++i) {
        if (ascii_upper((unsigned char)a[i]) != ascii_upper((unsigned char)b[i])) return 0;
    }
    return 1;
}

static char *copy_name(const char *name, size_t length)
{
    char *copy;
    size_t i;
    if (name == NULL || length == 0 || length > REXX_VM_MAX_NAME_LENGTH ||
        length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    for (i = 0; i < length; ++i) copy[i] = (char)ascii_upper((unsigned char)name[i]);
    copy[length] = '\0';
    return copy;
}

void rexx_value_clear(RexxValue *value)
{
    if (value == NULL) return;
    if (value->type == REXX_VALUE_STRING) free(value->as.string.bytes);
    memset(value, 0, sizeof(*value));
}

RexxVMStatus rexx_value_set_number(RexxValue *value, double number)
{
    if (value == NULL || !isfinite(number)) return REXX_VM_STATUS_INVALID_ARGUMENT;
    rexx_value_clear(value);
    value->type = REXX_VALUE_NUMBER;
    value->as.number = number;
    return REXX_VM_STATUS_OK;
}

RexxVMStatus rexx_value_set_boolean(RexxValue *value, int boolean_value)
{
    if (value == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    rexx_value_clear(value);
    value->type = REXX_VALUE_BOOLEAN;
    value->as.boolean = boolean_value != 0;
    return REXX_VM_STATUS_OK;
}

RexxVMStatus rexx_value_set_string(RexxValue *value, const char *bytes, size_t length)
{
    char *copy;
    if (value == NULL || (bytes == NULL && length != 0) || length == SIZE_MAX)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    if (length != 0) memcpy(copy, bytes, length);
    copy[length] = '\0';
    rexx_value_clear(value);
    value->type = REXX_VALUE_STRING;
    value->as.string.bytes = copy;
    value->as.string.length = length;
    return REXX_VM_STATUS_OK;
}

RexxVMStatus rexx_value_copy(RexxValue *destination, const RexxValue *source)
{
    if (destination == NULL || source == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (destination == source) return REXX_VM_STATUS_OK;
    switch (source->type) {
    case REXX_VALUE_UNSET:
        rexx_value_clear(destination);
        return REXX_VM_STATUS_OK;
    case REXX_VALUE_NUMBER:
        return rexx_value_set_number(destination, source->as.number);
    case REXX_VALUE_BOOLEAN:
        return rexx_value_set_boolean(destination, source->as.boolean);
    case REXX_VALUE_STRING:
        if (source->as.string.bytes == NULL && source->as.string.length != 0)
            return REXX_VM_STATUS_INVALID_ARGUMENT;
        return rexx_value_set_string(destination, source->as.string.bytes,
                                     source->as.string.length);
    default:
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
}

static RexxVMStatus vm_fail(RexxVM *vm, RexxVMStatus status, size_t pc)
{
    if (vm != NULL) {
        vm->status = status;
        vm->error.status = status;
        vm->error.pc = pc;
        vm->error.message = status >= REXX_VM_STATUS_OK &&
                            status <= REXX_VM_STATUS_INSTRUCTION_LIMIT
                                ? vm_messages[status] : "unknown VM error";
    }
    return status;
}

static RexxVMStatus table_set(RexxVMEntry **entries, size_t *count, size_t *capacity,
                              const char *name, size_t name_length,
                              const RexxValue *value)
{
    size_t i;
    char *key;
    RexxValue copied;
    RexxVMStatus result;
    if (entries == NULL || count == NULL || capacity == NULL || name == NULL ||
        value == NULL || name_length == 0) return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (name_length > REXX_VM_MAX_NAME_LENGTH) return REXX_VM_STATUS_RESOURCE_LIMIT;
    memset(&copied, 0, sizeof(copied));
    result = rexx_value_copy(&copied, value);
    if (result != REXX_VM_STATUS_OK) return result;
    for (i = 0; i < *count; ++i) {
        if (ascii_equal((*entries)[i].name, (*entries)[i].name_length, name, name_length)) {
            rexx_value_clear(&(*entries)[i].value);
            (*entries)[i].value = copied;
            return REXX_VM_STATUS_OK;
        }
    }
    if (*count >= REXX_VM_MAX_TABLE_ENTRIES) {
        rexx_value_clear(&copied);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    key = copy_name(name, name_length);
    if (key == NULL) {
        rexx_value_clear(&copied);
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    }
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 8 : *capacity * 2;
        RexxVMEntry *grown;
        if (next < *capacity || next > REXX_VM_MAX_TABLE_ENTRIES)
            next = REXX_VM_MAX_TABLE_ENTRIES;
        if (next <= *capacity || next > SIZE_MAX / sizeof(**entries)) {
            free(key);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        grown = (RexxVMEntry *)realloc(*entries, next * sizeof(**entries));
        if (grown == NULL) {
            free(key);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        }
        *entries = grown;
        *capacity = next;
    }
    (*entries)[*count].name = key;
    (*entries)[*count].name_length = name_length;
    (*entries)[*count].value = copied;
    ++*count;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus stem_set(RexxVM *vm, const char *name, size_t name_length,
                             const RexxValue *value)
{
    size_t dot = 0, tail_length, i;
    char *stem = NULL, *tail = NULL;
    RexxValue copied;
    RexxVMStatus result;
    if (name == NULL || value == NULL || name_length == 0)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (name_length > REXX_VM_MAX_NAME_LENGTH) return REXX_VM_STATUS_RESOURCE_LIMIT;
    while (dot < name_length && name[dot] != '.') ++dot;
    if (dot == 0 || dot == name_length) return REXX_VM_STATUS_INVALID_BYTECODE;
    tail_length = name_length - dot - 1;
    stem = copy_name(name, dot + 1);
    tail = tail_length == 0 ? (char *)calloc(1, 1)
                            : copy_name(name + dot + 1, tail_length);
    if (stem == NULL || tail == NULL) {
        free(stem);
        free(tail);
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    }
    memset(&copied, 0, sizeof(copied));
    result = rexx_value_copy(&copied, value);
    if (result != REXX_VM_STATUS_OK) {
        free(stem);
        free(tail);
        return result;
    }
    for (i = 0; i < vm->stem_count; ++i) {
        RexxVMStemEntry *entry = &vm->stems[i];
        if (entry->stem_length == dot + 1 && entry->tail_length == tail_length &&
            memcmp(entry->stem, stem, dot + 1) == 0 &&
            memcmp(entry->tail, tail, tail_length) == 0) {
            rexx_value_clear(&entry->value);
            entry->value = copied;
            free(stem);
            free(tail);
            return REXX_VM_STATUS_OK;
        }
    }
    if (vm->stem_count >= REXX_VM_MAX_TABLE_ENTRIES) {
        free(stem);
        free(tail);
        rexx_value_clear(&copied);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    if (vm->stem_count == vm->stem_capacity) {
        size_t next = vm->stem_capacity == 0 ? 8 : vm->stem_capacity * 2;
        RexxVMStemEntry *grown;
        if (next < vm->stem_capacity || next > REXX_VM_MAX_TABLE_ENTRIES)
            next = REXX_VM_MAX_TABLE_ENTRIES;
        if (next <= vm->stem_capacity || next > SIZE_MAX / sizeof(*vm->stems)) {
            free(stem);
            free(tail);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        grown = (RexxVMStemEntry *)realloc(vm->stems, next * sizeof(*vm->stems));
        if (grown == NULL) {
            free(stem);
            free(tail);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        }
        vm->stems = grown;
        vm->stem_capacity = next;
    }
    vm->stems[vm->stem_count].stem = stem;
    vm->stems[vm->stem_count].stem_length = dot + 1;
    vm->stems[vm->stem_count].tail = tail;
    vm->stems[vm->stem_count].tail_length = tail_length;
    vm->stems[vm->stem_count].value = copied;
    ++vm->stem_count;
    return REXX_VM_STATUS_OK;
}

RexxVMStatus rexx_vm_init(RexxVM *vm, size_t stack_capacity)
{
    if (vm == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    memset(vm, 0, sizeof(*vm));
    if (stack_capacity == 0) stack_capacity = REXX_VM_DEFAULT_STACK_CAPACITY;
    if (stack_capacity > REXX_VM_MAX_STACK_CAPACITY ||
        stack_capacity > SIZE_MAX / sizeof(*vm->stack))
        return vm_fail(vm, REXX_VM_STATUS_RESOURCE_LIMIT, 0);
    vm->stack = (RexxValue *)calloc(stack_capacity, sizeof(*vm->stack));
    if (vm->stack == NULL) return vm_fail(vm, REXX_VM_STATUS_OUT_OF_MEMORY, 0);
    vm->stack_capacity = stack_capacity;
    vm->instruction_budget = REXX_VM_DEFAULT_INSTRUCTION_BUDGET;
    vm->initialized = 1;
    vm->status = REXX_VM_STATUS_OK;
    vm->error.status = REXX_VM_STATUS_OK;
    vm->error.message = "ok";
    return REXX_VM_STATUS_OK;
}

RexxVMStatus rexx_vm_set_instruction_budget(RexxVM *vm, size_t budget)
{
    if (vm == NULL || !vm->initialized || budget == 0u)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    vm->instruction_budget = budget;
    return REXX_VM_STATUS_OK;
}

void rexx_vm_free(RexxVM *vm)
{
    size_t i, live;
    if (vm == NULL) return;
    live = vm->sp < vm->stack_capacity ? vm->sp : vm->stack_capacity;
    for (i = 0; vm->stack != NULL && i < live; ++i) rexx_value_clear(&vm->stack[i]);
    for (i = 0; vm->locals != NULL && i < vm->local_count &&
                i < vm->local_capacity; ++i) {
        free(vm->locals[i].name);
        rexx_value_clear(&vm->locals[i].value);
    }
    for (i = 0; vm->globals != NULL && i < vm->global_count &&
                i < vm->global_capacity; ++i) {
        free(vm->globals[i].name);
        rexx_value_clear(&vm->globals[i].value);
    }
    for (i = 0; vm->stems != NULL && i < vm->stem_count &&
                i < vm->stem_capacity; ++i) {
        free(vm->stems[i].stem);
        free(vm->stems[i].tail);
        rexx_value_clear(&vm->stems[i].value);
    }
    rexx_value_clear(&vm->exit_value);
    free(vm->stack);
    free(vm->locals);
    free(vm->globals);
    free(vm->stems);
    free(vm->address_environment);
    memset(vm, 0, sizeof(*vm));
}

void rexx_vm_set_command_dispatcher(RexxVM *vm,
                                    RexxVMCommandDispatcher dispatcher,
                                    void *user_data)
{
    if (vm == NULL || !vm->initialized) return;
    vm->command_dispatcher = dispatcher;
    vm->command_dispatcher_user_data = user_data;
}

void rexx_vm_set_address_dispatcher(RexxVM *vm,
                                    RexxVMAddressDispatcher dispatcher,
                                    void *user_data)
{
    if (vm == NULL || !vm->initialized) return;
    vm->address_dispatcher = dispatcher;
    vm->address_dispatcher_user_data = dispatcher != NULL ? user_data : NULL;
}

int32_t rexx_vm_guidexos_command_stub(const char *command, size_t command_length)
{
    (void)command;
    (void)command_length;
    return INT32_C(127); /* Dispatcher unavailable; never invokes a host shell. */
}

static const char *current_address_environment(const RexxVM *vm,
                                               size_t *length)
{
    static const char default_environment[] = "KERNEL";
    if (vm->address_environment != NULL) {
        *length = vm->address_environment_length;
        return vm->address_environment;
    }
    *length = sizeof(default_environment) - 1u;
    return default_environment;
}

static RexxVMStatus set_address_environment(RexxVM *vm,
                                             const char *environment,
                                             size_t environment_length)
{
    char *copy;
    if (environment == NULL || environment_length == 0u)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if (environment_length > REXX_VM_MAX_NAME_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    copy = copy_name(environment, environment_length);
    if (copy == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    free(vm->address_environment);
    vm->address_environment = copy;
    vm->address_environment_length = environment_length;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus dispatch_command(RexxVM *vm,
                                    const char *environment,
                                    size_t environment_length,
                                    const char *command,
                                    size_t command_length)
{
    RexxValue rc_value = REXX_VALUE_INIT;
    RexxVMStatus status;

    if (environment == NULL || environment_length == 0u ||
        (command == NULL && command_length != 0u))
        return REXX_VM_STATUS_INVALID_BYTECODE;

    if (vm->address_dispatcher != NULL) {
        vm->rc = vm->address_dispatcher(
            vm->address_dispatcher_user_data, environment,
            environment_length, command, command_length);
    } else if (vm->command_dispatcher != NULL) {
        /* Preserve the legacy command-only embedding API. */
        vm->rc = vm->command_dispatcher(
            vm->command_dispatcher_user_data, command, command_length);
    } else {
        vm->rc = rexx_vm_guidexos_command_stub(command, command_length);
    }

    status = rexx_value_set_number(&rc_value, (double)vm->rc);
    if (status == REXX_VM_STATUS_OK) {
        status = table_set(&vm->locals, &vm->local_count, &vm->local_capacity,
                           "RC", 2u, &rc_value);
    }
    rexx_value_clear(&rc_value);
    return status;
}

RexxVMStatus rexx_vm_set_local(RexxVM *vm, const char *name, size_t name_length,
                               const RexxValue *value)
{
    RexxVMStatus result;
    if (vm == NULL || !vm->initialized) return REXX_VM_STATUS_INVALID_ARGUMENT;
    result = table_set(&vm->locals, &vm->local_count, &vm->local_capacity,
                       name, name_length, value);
    if (result != REXX_VM_STATUS_OK) (void)vm_fail(vm, result, vm->pc);
    return result;
}

RexxVMStatus rexx_vm_set_global(RexxVM *vm, const char *name, size_t name_length,
                                const RexxValue *value)
{
    RexxVMStatus result;
    if (vm == NULL || !vm->initialized) return REXX_VM_STATUS_INVALID_ARGUMENT;
    result = table_set(&vm->globals, &vm->global_count, &vm->global_capacity,
                       name, name_length, value);
    if (result != REXX_VM_STATUS_OK) (void)vm_fail(vm, result, vm->pc);
    return result;
}

static RexxVMStatus push_move(RexxVM *vm, RexxValue *value)
{
    if (vm->sp >= vm->stack_capacity) return REXX_VM_STATUS_STACK_OVERFLOW;
    vm->stack[vm->sp++] = *value;
    memset(value, 0, sizeof(*value));
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus pop_move(RexxVM *vm, RexxValue *value)
{
    if (vm->sp == 0 || vm->sp > vm->stack_capacity)
        return REXX_VM_STATUS_STACK_UNDERFLOW;
    *value = vm->stack[--vm->sp];
    memset(&vm->stack[vm->sp], 0, sizeof(vm->stack[vm->sp]));
    return REXX_VM_STATUS_OK;
}

static void clear_stack(RexxVM *vm)
{
    size_t i, live = vm->sp < vm->stack_capacity ? vm->sp : vm->stack_capacity;
    for (i = 0; i < live; ++i) rexx_value_clear(&vm->stack[i]);
    vm->sp = 0;
}

/* Returns 1 for a valid number, 0 for nonnumeric input, and -1 on allocation failure. */
static int parse_number(const char *bytes, size_t length, double *number)
{
    char *copy, *start, *end;
    size_t used;
    double parsed;
    if (number == NULL || (bytes == NULL && length != 0) || length == SIZE_MAX) return 0;
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return -1;
    if (length != 0) memcpy(copy, bytes, length);
    copy[length] = '\0';
    start = copy;
    while (*start != '\0' && ascii_space((unsigned char)*start)) ++start;
    end = copy + length;
    while (end > start && ascii_space((unsigned char)end[-1])) --end;
    *end = '\0';
    used = (size_t)(end - start);
    if (used == 0) {
        free(copy);
        *number = 0.0;
        return 1;
    }
    errno = 0;
    parsed = strtod(start, &end);
    if (errno == ERANGE || end == start || (size_t)(end - start) != used ||
        !isfinite(parsed)) {
        free(copy);
        return 0;
    }
    free(copy);
    *number = parsed;
    return 1;
}

static RexxVMStatus value_number(const RexxValue *value, double *number)
{
    int parsed;
    if (value == NULL || number == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    switch (value->type) {
    case REXX_VALUE_UNSET: *number = 0.0; return REXX_VM_STATUS_OK;
    case REXX_VALUE_NUMBER:
        if (!isfinite(value->as.number)) return REXX_VM_STATUS_NUMERIC_ERROR;
        *number = value->as.number;
        return REXX_VM_STATUS_OK;
    case REXX_VALUE_BOOLEAN: *number = value->as.boolean ? 1.0 : 0.0; return REXX_VM_STATUS_OK;
    case REXX_VALUE_STRING:
        parsed = parse_number(value->as.string.bytes, value->as.string.length, number);
        return parsed > 0 ? REXX_VM_STATUS_OK :
               parsed < 0 ? REXX_VM_STATUS_OUT_OF_MEMORY : REXX_VM_STATUS_NUMERIC_ERROR;
    default: return REXX_VM_STATUS_NUMERIC_ERROR;
    }
}

static RexxVMStatus value_string(const RexxValue *value, char **bytes, size_t *length)
{
    char number[64];
    const char *source;
    size_t count;
    int written;
    if (value == NULL || bytes == NULL || length == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    *bytes = NULL;
    *length = 0;
    switch (value->type) {
    case REXX_VALUE_UNSET: source = ""; count = 0; break;
    case REXX_VALUE_STRING:
        if (value->as.string.bytes == NULL && value->as.string.length != 0)
            return REXX_VM_STATUS_INVALID_ARGUMENT;
        source = value->as.string.bytes;
        count = value->as.string.length;
        break;
    case REXX_VALUE_BOOLEAN: source = value->as.boolean ? "1" : "0"; count = 1; break;
    case REXX_VALUE_NUMBER:
        if (!isfinite(value->as.number)) return REXX_VM_STATUS_NUMERIC_ERROR;
        written = snprintf(number, sizeof(number), "%.15g", value->as.number);
        if (written < 0 || (size_t)written >= sizeof(number)) return REXX_VM_STATUS_NUMERIC_ERROR;
        source = number;
        count = (size_t)written;
        break;
    default: return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    if (count == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
    *bytes = (char *)malloc(count + 1);
    if (*bytes == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    if (count != 0) memcpy(*bytes, source, count);
    (*bytes)[count] = '\0';
    *length = count;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus value_truthy(const RexxValue *value, int *truthy)
{
    double number;
    int parsed;
    if (value == NULL || truthy == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    switch (value->type) {
    case REXX_VALUE_UNSET: *truthy = 0; return REXX_VM_STATUS_OK;
    case REXX_VALUE_BOOLEAN: *truthy = value->as.boolean != 0; return REXX_VM_STATUS_OK;
    case REXX_VALUE_NUMBER: *truthy = value->as.number != 0.0; return REXX_VM_STATUS_OK;
    case REXX_VALUE_STRING:
        parsed = parse_number(value->as.string.bytes, value->as.string.length, &number);
        if (parsed < 0) return REXX_VM_STATUS_OUT_OF_MEMORY;
        *truthy = parsed > 0 ? number != 0.0 : value->as.string.length != 0;
        return REXX_VM_STATUS_OK;
    default: return REXX_VM_STATUS_INVALID_BYTECODE;
    }
}

static int hex_value(unsigned char c)
{
    c = ascii_upper(c);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static RexxVMStatus decode_constant(const RexxConstant *constant, RexxValue *value)
{
    size_t i;
    if (constant == NULL || value == NULL || constant->text == NULL)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    switch (constant->kind) {
    case REXX_CONSTANT_NUMBER: {
        double number;
        int parsed = parse_number(constant->text, constant->length, &number);
        if (parsed < 0) return REXX_VM_STATUS_OUT_OF_MEMORY;
        if (parsed == 0) return REXX_VM_STATUS_INVALID_BYTECODE;
        return rexx_value_set_number(value, number);
    }
    case REXX_CONSTANT_STRING: {
        char quote, *decoded;
        size_t out = 0;
        if (constant->length < 2) return REXX_VM_STATUS_INVALID_BYTECODE;
        quote = constant->text[0];
        if ((quote != '\'' && quote != '"') || constant->text[constant->length - 1] != quote)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        decoded = (char *)malloc(constant->length);
        if (decoded == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
        i = 1;
        while (i + 1 < constant->length) {
            char c = constant->text[i++];
            if (c == quote) {
                if (i + 1 >= constant->length || constant->text[i] != quote) {
                    free(decoded);
                    return REXX_VM_STATUS_INVALID_BYTECODE;
                }
                ++i;
            }
            decoded[out++] = c;
        }
        decoded[out] = '\0';
        {
            RexxVMStatus result = rexx_value_set_string(value, decoded, out);
            free(decoded);
            return result;
        }
    }
    case REXX_CONSTANT_HEX_STRING:
    case REXX_CONSTANT_BINARY_STRING: {
        int is_hex = constant->kind == REXX_CONSTANT_HEX_STRING;
        size_t start = 2, end, digits = 0, out_length, out = 0;
        char *decoded;
        if (constant->length < 3 ||
            ascii_upper((unsigned char)constant->text[0]) != (is_hex ? 'X' : 'B') ||
            (constant->text[1] != '\'' && constant->text[1] != '"') ||
            constant->text[constant->length - 1] != constant->text[1])
            return REXX_VM_STATUS_INVALID_BYTECODE;
        end = constant->length - 1;
        for (i = start; i < end; ++i)
            if (!ascii_space((unsigned char)constant->text[i])) ++digits;
        if (is_hex) {
            if (digits == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
            out_length = (digits + 1) / 2;
        } else {
            if (digits > SIZE_MAX - 7) return REXX_VM_STATUS_RESOURCE_LIMIT;
            out_length = (digits + 7) / 8;
            if (out_length > SIZE_MAX / 8) return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        if (out_length == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
        decoded = (char *)calloc(out_length + 1, 1);
        if (decoded == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
        if (is_hex) {
            size_t digit_index = 0;
            int high_nibble = -1;
            for (i = start; i < end; ++i) {
                int nibble;
                unsigned char c = (unsigned char)constant->text[i];
                if (ascii_space(c)) continue;
                nibble = hex_value(c);
                if (nibble < 0) { free(decoded); return REXX_VM_STATUS_INVALID_BYTECODE; }
                if ((digits & 1u) != 0u && digit_index == 0) {
                    decoded[out++] = (char)nibble; /* Odd payload: pad a leading zero nibble. */
                } else if (high_nibble < 0) {
                    high_nibble = nibble;
                } else {
                    decoded[out++] = (char)((high_nibble << 4) | nibble);
                    high_nibble = -1;
                }
                ++digit_index;
            }
            if (high_nibble >= 0 || out != out_length) {
                free(decoded);
                return REXX_VM_STATUS_INVALID_BYTECODE;
            }
        } else {
            size_t bit = out_length * 8 - digits;
            for (i = start; i < end; ++i) {
                unsigned char c = (unsigned char)constant->text[i];
                if (ascii_space(c)) continue;
                if (c != '0' && c != '1') { free(decoded); return REXX_VM_STATUS_INVALID_BYTECODE; }
                if (c == '1') decoded[bit / 8] |= (char)(1u << (7u - (unsigned)(bit % 8)));
                ++bit;
            }
        }
        {
            RexxVMStatus result = rexx_value_set_string(value, decoded, out_length);
            free(decoded);
            return result;
        }
    }
    default:
        return REXX_VM_STATUS_INVALID_BYTECODE;
    }
}

static int find_entry(const RexxVMEntry *entries, size_t count,
                      const char *name, size_t length)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (ascii_equal(entries[i].name, entries[i].name_length, name, length))
            return (int)i;
    return -1;
}

static RexxVMStatus load_variable(RexxVM *vm, const RexxSymbol *symbol, int stem)
{
    RexxValue value;
    RexxVMStatus result;
    int index;
    memset(&value, 0, sizeof(value));
    if (stem) {
        size_t dot = 0, tail_length, i;
        while (dot < symbol->length && symbol->name[dot] != '.') ++dot;
        if (dot == 0 || dot == symbol->length) return REXX_VM_STATUS_INVALID_BYTECODE;
        tail_length = symbol->length - dot - 1;
        for (i = 0; i < vm->stem_count; ++i) {
            RexxVMStemEntry *entry = &vm->stems[i];
            if (entry->stem_length == dot + 1 && entry->tail_length == tail_length &&
                ascii_equal(entry->stem, entry->stem_length, symbol->name, dot + 1) &&
                ascii_equal(entry->tail, entry->tail_length,
                            symbol->name + dot + 1, tail_length)) {
                result = rexx_value_copy(&value, &entry->value);
                if (result == REXX_VM_STATUS_OK) result = push_move(vm, &value);
                rexx_value_clear(&value);
                return result;
            }
        }
        /* A stem's empty tail supplies the default for every compound tail. */
        for (i = 0; i < vm->stem_count; ++i) {
            RexxVMStemEntry *entry = &vm->stems[i];
            if (entry->stem_length == dot + 1 && entry->tail_length == 0 &&
                ascii_equal(entry->stem, entry->stem_length, symbol->name, dot + 1)) {
                result = rexx_value_copy(&value, &entry->value);
                if (result == REXX_VM_STATUS_OK) result = push_move(vm, &value);
                rexx_value_clear(&value);
                return result;
            }
        }
    } else {
        index = find_entry(vm->locals, vm->local_count, symbol->name, symbol->length);
        if (index >= 0) {
            result = rexx_value_copy(&value, &vm->locals[index].value);
            if (result == REXX_VM_STATUS_OK) result = push_move(vm, &value);
            rexx_value_clear(&value);
            return result;
        }
        index = find_entry(vm->globals, vm->global_count, symbol->name, symbol->length);
        if (index >= 0) {
            result = rexx_value_copy(&value, &vm->globals[index].value);
            if (result == REXX_VM_STATUS_OK) result = push_move(vm, &value);
            rexx_value_clear(&value);
            return result;
        }
    }
    result = rexx_value_set_string(&value, symbol->name, symbol->length);
    if (result == REXX_VM_STATUS_OK) result = push_move(vm, &value);
    rexx_value_clear(&value);
    return result;
}

static RexxVMStatus get_symbol(const RexxBytecodeChunk *chunk, uint32_t operand,
                               RexxSymbolKind expected, const RexxSymbol **symbol)
{
    if (chunk->symbols == NULL || operand >= chunk->symbol_count)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    *symbol = &chunk->symbols[operand];
    if ((*symbol)->name == NULL || (*symbol)->length == 0 ||
        (*symbol)->length > REXX_VM_MAX_NAME_LENGTH || (*symbol)->kind != expected)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus load_constant(RexxVM *vm, const RexxBytecodeChunk *chunk,
                                  uint32_t operand)
{
    RexxValue value;
    RexxVMStatus result;
    if (chunk->constants == NULL || operand >= chunk->constant_count)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    memset(&value, 0, sizeof(value));
    result = decode_constant(&chunk->constants[operand], &value);
    if (result == REXX_VM_STATUS_OK) result = push_move(vm, &value);
    rexx_value_clear(&value);
    return result;
}

static RexxVMStatus binary_number(RexxVM *vm, RexxOpcode opcode)
{
    RexxValue left, right, result;
    double a = 0.0, b = 0.0, answer;
    RexxVMStatus status;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&result, 0, sizeof(result));
    status = pop_move(vm, &right);
    if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &left);
    if (status == REXX_VM_STATUS_OK) status = value_number(&left, &a);
    if (status == REXX_VM_STATUS_OK) status = value_number(&right, &b);
    if (status == REXX_VM_STATUS_OK) {
        switch (opcode) {
        case OP_ADD: answer = a + b; break;
        case OP_SUB: answer = a - b; break;
        case OP_MULTIPLY: answer = a * b; break;
        default: status = REXX_VM_STATUS_UNSUPPORTED_OPCODE; answer = 0.0; break;
        }
        if (!isfinite(answer)) status = REXX_VM_STATUS_NUMERIC_ERROR;
    }
    rexx_value_clear(&left);
    rexx_value_clear(&right);
    if (status == REXX_VM_STATUS_OK) status = rexx_value_set_number(&result, answer);
    if (status == REXX_VM_STATUS_OK) status = push_move(vm, &result);
    rexx_value_clear(&result);
    return status;
}

static RexxVMStatus concat_values(RexxVM *vm, RexxOpcode opcode)
{
    RexxValue left, right, result;
    char *a = NULL, *b = NULL, *joined = NULL;
    size_t alen = 0, blen = 0, separator_length = 0;
    RexxVMStatus status;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&result, 0, sizeof(result));
    status = pop_move(vm, &right);
    if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &left);
    if (status == REXX_VM_STATUS_OK) status = value_string(&left, &a, &alen);
    if (status == REXX_VM_STATUS_OK) status = value_string(&right, &b, &blen);
    if (status == REXX_VM_STATUS_OK) {
        if (opcode == OP_CONCAT_BLANK && alen != 0 && blen != 0)
            separator_length = 1;
        if (alen > SIZE_MAX - separator_length - 1 ||
            blen > SIZE_MAX - alen - separator_length - 1)
            status = REXX_VM_STATUS_RESOURCE_LIMIT;
        else {
            joined = (char *)malloc(alen + separator_length + blen + 1);
            if (joined == NULL) status = REXX_VM_STATUS_OUT_OF_MEMORY;
            else {
                if (alen != 0) memcpy(joined, a, alen);
                if (separator_length != 0) joined[alen] = ' ';
                if (blen != 0)
                    memcpy(joined + alen + separator_length, b, blen);
                joined[alen + separator_length + blen] = '\0';
                status = rexx_value_set_string(
                    &result, joined, alen + separator_length + blen);
            }
        }
    }
    if (status == REXX_VM_STATUS_OK) status = push_move(vm, &result);
    free(a);
    free(b);
    free(joined);
    rexx_value_clear(&left);
    rexx_value_clear(&right);
    rexx_value_clear(&result);
    return status;
}

/* Compare numerically when both operands are numeric; otherwise compare their
 * rendered byte strings. This is sufficient for the ordered comparisons used
 * by the currently implemented execution subset and avoids locale dependence. */
static RexxVMStatus compare_values(const RexxValue *left,
                                   const RexxValue *right,
                                   int *comparison)
{
    double left_number = 0.0, right_number = 0.0;
    RexxVMStatus left_status, right_status;

    if (left == NULL || right == NULL || comparison == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;

    left_status = value_number(left, &left_number);
    right_status = value_number(right, &right_number);
    if (left_status == REXX_VM_STATUS_OUT_OF_MEMORY ||
        right_status == REXX_VM_STATUS_OUT_OF_MEMORY)
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    if (left_status == REXX_VM_STATUS_OK && right_status == REXX_VM_STATUS_OK) {
        *comparison = left_number < right_number ? -1 :
                      left_number > right_number ? 1 : 0;
        return REXX_VM_STATUS_OK;
    }

    {
        char *left_text = NULL, *right_text = NULL;
        size_t left_length = 0, right_length = 0, common_length, index;
        RexxVMStatus status = value_string(left, &left_text, &left_length);
        if (status == REXX_VM_STATUS_OK)
            status = value_string(right, &right_text, &right_length);
        if (status == REXX_VM_STATUS_OK) {
            common_length = left_length < right_length ? left_length : right_length;
            for (index = 0; index < common_length; ++index) {
                unsigned char a = (unsigned char)left_text[index];
                unsigned char b = (unsigned char)right_text[index];
                if (a != b) {
                    *comparison = a < b ? -1 : 1;
                    break;
                }
            }
            if (index == common_length)
                *comparison = left_length < right_length ? -1 :
                              left_length > right_length ? 1 : 0;
        }
        free(left_text);
        free(right_text);
        return status;
    }
}

RexxVMStatus rexx_vm_execute(RexxVM *vm, const RexxBytecodeChunk *chunk)
{
    size_t dispatched = 0u;

    if (vm == NULL || !vm->initialized || chunk == NULL)
        return vm_fail(vm, REXX_VM_STATUS_INVALID_ARGUMENT, 0);
    if ((chunk->instruction_count != 0 && chunk->instructions == NULL) ||
        chunk->instruction_count > REXX_COMPILER_MAX_INSTRUCTIONS ||
        chunk->constant_count > REXX_COMPILER_MAX_POOL_ENTRIES ||
        chunk->symbol_count > REXX_COMPILER_MAX_POOL_ENTRIES ||
        (chunk->constant_count != 0 && chunk->constants == NULL) ||
        (chunk->symbol_count != 0 && chunk->symbols == NULL))
        return vm_fail(vm, REXX_VM_STATUS_INVALID_BYTECODE, 0);

    /* Each call starts with an empty evaluation stack; variable tables persist. */
    clear_stack(vm);
    rexx_value_clear(&vm->exit_value);
    vm->exit_requested = 0;
    vm->exit_has_value = 0;
    vm->pc = 0;
    vm->status = REXX_VM_STATUS_OK;
    vm->error.status = REXX_VM_STATUS_OK;
    vm->error.pc = 0;
    vm->error.message = "ok";

    while (vm->pc < chunk->instruction_count) {
        if (dispatched >= vm->instruction_budget) {
            size_t exhausted_pc = vm->pc;
            clear_stack(vm);
            return vm_fail(vm, REXX_VM_STATUS_INSTRUCTION_LIMIT,
                           exhausted_pc);
        }
        ++dispatched;

        size_t current_pc = vm->pc;
        const RexxInstruction *ins = &chunk->instructions[vm->pc++];
        RexxVMStatus status = REXX_VM_STATUS_OK;
        RexxValue value;
        memset(&value, 0, sizeof(value));

        switch ((RexxOpcode)ins->opcode) {
        case OP_HALT:
            clear_stack(vm);
            vm->status = REXX_VM_STATUS_OK;
            vm->error.status = REXX_VM_STATUS_OK;
            vm->error.pc = current_pc;
            vm->error.message = "ok";
            return REXX_VM_STATUS_OK;

        case OP_NOP:
        case OP_LABEL:
            break;

        case OP_LOAD_CONST:
            status = load_constant(vm, chunk, ins->operand);
            break;

        case OP_LOAD_UNSET:
            status = push_move(vm, &value);
            break;

        case OP_LOAD_VAR:
        case OP_LOAD_STEM: {
            const RexxSymbol *symbol = NULL;
            RexxSymbolKind kind = ins->opcode == OP_LOAD_STEM
                                      ? REXX_SYMBOL_STEM : REXX_SYMBOL_VARIABLE;
            status = get_symbol(chunk, ins->operand, kind, &symbol);
            if (status == REXX_VM_STATUS_OK)
                status = load_variable(vm, symbol, ins->opcode == OP_LOAD_STEM);
            break;
        }

        case OP_STORE_VAR:
        case OP_STORE_STEM: {
            const RexxSymbol *symbol = NULL;
            RexxSymbolKind kind = ins->opcode == OP_STORE_STEM
                                      ? REXX_SYMBOL_STEM : REXX_SYMBOL_VARIABLE;
            status = get_symbol(chunk, ins->operand, kind, &symbol);
            if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &value);
            if (status == REXX_VM_STATUS_OK && ins->opcode == OP_STORE_VAR)
                status = table_set(&vm->locals, &vm->local_count, &vm->local_capacity,
                                   symbol->name, symbol->length, &value);
            else if (status == REXX_VM_STATUS_OK)
                status = stem_set(vm, symbol->name, symbol->length, &value);
            rexx_value_clear(&value);
            break;
        }

        case OP_POP:
            status = pop_move(vm, &value);
            rexx_value_clear(&value);
            break;

        case OP_ADD:
        case OP_SUB:
        case OP_MULTIPLY:
            status = binary_number(vm, (RexxOpcode)ins->opcode);
            break;

        case OP_CONCAT:
        case OP_CONCAT_BLANK:
        case OP_CONCAT_ABUTTAL:
            status = concat_values(vm, (RexxOpcode)ins->opcode);
            break;

        case OP_GREATER: {
            RexxValue left, right;
            int comparison = 0;
            memset(&left, 0, sizeof(left));
            memset(&right, 0, sizeof(right));
            status = pop_move(vm, &right);
            if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &left);
            if (status == REXX_VM_STATUS_OK)
                status = compare_values(&left, &right, &comparison);
            if (status == REXX_VM_STATUS_OK)
                status = rexx_value_set_boolean(&value, comparison > 0);
            if (status == REXX_VM_STATUS_OK) status = push_move(vm, &value);
            rexx_value_clear(&left);
            rexx_value_clear(&right);
            break;
        }

        case OP_SAY: {
            char *text = NULL;
            size_t length = 0;
            if ((ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0) {
                status = pop_move(vm, &value);
                if (status == REXX_VM_STATUS_OK)
                    status = value_string(&value, &text, &length);
            }
            if (status == REXX_VM_STATUS_OK &&
                ((length != 0u && fwrite(text, 1u, length, stdout) != length) ||
                 fputc('\n', stdout) == EOF)) {
                status = REXX_VM_STATUS_IO_ERROR;
            }
            free(text);
            rexx_value_clear(&value);
            break;
        }

        case OP_COMMAND: {
            char *command = NULL;
            size_t command_length = 0u;
            size_t environment_length = 0u;
            const char *environment = NULL;

            status = pop_move(vm, &value);
            if (status == REXX_VM_STATUS_OK)
                status = value_string(&value, &command, &command_length);
            rexx_value_clear(&value);
            if (status == REXX_VM_STATUS_OK) {
                environment = current_address_environment(vm,
                                                          &environment_length);
                status = dispatch_command(vm, environment,
                                          environment_length,
                                          command, command_length);
            }
            free(command);
            break;
        }

        case OP_CALL: {
            const RexxSymbol *function = NULL;
            size_t argument_count = (size_t)ins->auxiliary;
            size_t argument_start;
            size_t argument_index;

            status = get_symbol(chunk, ins->operand, REXX_SYMBOL_ROUTINE,
                                &function);
            if (status == REXX_VM_STATUS_OK && argument_count > vm->sp) {
                status = REXX_VM_STATUS_STACK_UNDERFLOW;
            }
            if (status == REXX_VM_STATUS_OK) {
                argument_start = vm->sp - argument_count;
                status = rexx_builtin_dispatch(
                    function->name, function->length,
                    argument_count == 0u ? NULL : &vm->stack[argument_start],
                    argument_count, &value);
                for (argument_index = argument_start; argument_index < vm->sp;
                     ++argument_index) {
                    rexx_value_clear(&vm->stack[argument_index]);
                }
                vm->sp = argument_start;
                if (status == REXX_VM_STATUS_OK) {
                    status = push_move(vm, &value);
                }
            }
            break;
        }

        /* A plain DO group only needs a structural frame. Counted and
         * controlled loops remain unsupported until their opcodes are added. */
        case OP_DO_SETUP:
            if (ins->operand != REXX_NO_SYMBOL || ins->auxiliary != 0u)
                status = REXX_VM_STATUS_UNSUPPORTED_OPCODE;
            break;
        case OP_DO_END:
            break;

        case OP_EXIT:
            if ((ins->flags & (uint8_t)~REXX_INSTRUCTION_HAS_VALUE) != 0u) {
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            } else {
                vm->exit_has_value =
                    (ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0u;
                if (vm->exit_has_value) {
                    status = pop_move(vm, &value);
                    if (status == REXX_VM_STATUS_OK) {
                        rexx_value_clear(&vm->exit_value);
                        vm->exit_value = value;
                        memset(&value, 0, sizeof(value));
                    }
                }
                if (status == REXX_VM_STATUS_OK) vm->exit_requested = 1;
            }
            break;

        case OP_JUMP:
            if ((size_t)ins->operand > chunk->instruction_count)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            else
                vm->pc = (size_t)ins->operand;
            break;

        case OP_JUMP_IF_FALSE:
            status = pop_move(vm, &value);
            if (status == REXX_VM_STATUS_OK) {
                int truthy = 0;
                status = value_truthy(&value, &truthy);
                rexx_value_clear(&value);
                if (status == REXX_VM_STATUS_OK && !truthy) {
                    if ((size_t)ins->operand > chunk->instruction_count)
                        status = REXX_VM_STATUS_INVALID_BYTECODE;
                    else
                        vm->pc = (size_t)ins->operand;
                }
            }
            break;

        case OP_ADDRESS: {
            const char *environment = "KERNEL";
            size_t environment_length = 0u;
            const RexxSymbol *environment_symbol = NULL;

            if ((ins->flags & (uint8_t)~(REXX_INSTRUCTION_HAS_VALUE |
                                         REXX_INSTRUCTION_HAS_ENVIRONMENT)) != 0u) {
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            }
            if ((ins->flags & REXX_INSTRUCTION_HAS_ENVIRONMENT) != 0u) {
                if (status == REXX_VM_STATUS_OK)
                    status = get_symbol(chunk, ins->operand,
                                        REXX_SYMBOL_VARIABLE,
                                        &environment_symbol);
                if (status == REXX_VM_STATUS_OK) {
                    environment = environment_symbol->name;
                    environment_length = environment_symbol->length;
                }
            } else {
                environment = current_address_environment(vm,
                                                          &environment_length);
            }
            if (status == REXX_VM_STATUS_OK &&
                (ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0u) {
                char *command = NULL;
                size_t command_length = 0;
                status = pop_move(vm, &value);
                if (status == REXX_VM_STATUS_OK)
                    status = value_string(&value, &command, &command_length);
                rexx_value_clear(&value);
                if (status == REXX_VM_STATUS_OK) {
                    status = dispatch_command(vm, environment,
                                              environment_length,
                                              command, command_length);
                }
                free(command);
                rexx_value_clear(&value);
            } else if (status == REXX_VM_STATUS_OK &&
                       (ins->flags & REXX_INSTRUCTION_HAS_ENVIRONMENT) != 0u) {
                /* ADDRESS environment changes the default for later commands. */
                status = set_address_environment(vm, environment,
                                                 environment_length);
            }
            break;
        }

        default:
            status = REXX_VM_STATUS_UNSUPPORTED_OPCODE;
            break;
        }

        rexx_value_clear(&value);
        if (status != REXX_VM_STATUS_OK) {
            vm->pc = current_pc;
            clear_stack(vm);
            return vm_fail(vm, status, current_pc);
        }

        if ((RexxOpcode)ins->opcode == OP_EXIT) {
            clear_stack(vm);
            vm->status = REXX_VM_STATUS_OK;
            vm->error.status = REXX_VM_STATUS_OK;
            vm->error.pc = vm->pc;
            vm->error.message = "ok";
            return REXX_VM_STATUS_OK;
        }
    }
    clear_stack(vm);
    vm->status = REXX_VM_STATUS_OK;
    vm->error.status = REXX_VM_STATUS_OK;
    vm->error.pc = vm->pc;
    vm->error.message = "ok";
    return REXX_VM_STATUS_OK;
}

const char *rexx_vm_status_name(RexxVMStatus status)
{
    if (status < REXX_VM_STATUS_OK ||
        status > REXX_VM_STATUS_INSTRUCTION_LIMIT)
        return "unknown VM status";
    return vm_messages[status];
}
