#include "rexx_vm.h"
#include "rexx_builtin.h"
#include "rexx_numeric.h"

#include <stdlib.h>
#include <string.h>

#if !defined(GXOS_BARE_METAL)
#include <stdio.h>
#endif

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static size_t test_allocator_fail_on;
static size_t test_allocator_attempt_count;
static size_t test_allocator_live_blocks;

static int test_allocator_should_fail(void)
{
    ++test_allocator_attempt_count;
    return test_allocator_fail_on != 0u &&
           test_allocator_attempt_count == test_allocator_fail_on;
}

static void *vm_malloc(size_t size)
{
    void *result;
    if (test_allocator_should_fail()) return NULL;
    result = malloc(size);
    if (result != NULL) ++test_allocator_live_blocks;
    return result;
}

static void *vm_calloc(size_t count, size_t size)
{
    void *result;
    if (test_allocator_should_fail()) return NULL;
    result = calloc(count, size);
    if (result != NULL) ++test_allocator_live_blocks;
    return result;
}

static void *vm_realloc(void *pointer, size_t size)
{
    void *result;
    if (test_allocator_should_fail()) return NULL;
    result = realloc(pointer, size);
    if (result != NULL && pointer == NULL) ++test_allocator_live_blocks;
    return result;
}

static void vm_free(void *pointer)
{
    if (pointer != NULL && test_allocator_live_blocks != 0u)
        --test_allocator_live_blocks;
    free(pointer);
}

void rexx_vm_test_allocator_fail_on(size_t allocation_attempt)
{
    test_allocator_attempt_count = 0u;
    test_allocator_fail_on = allocation_attempt;
}

void rexx_vm_test_allocator_disable(void)
{
    test_allocator_fail_on = 0u;
}

size_t rexx_vm_test_allocator_attempts(void)
{
    return test_allocator_attempt_count;
}

size_t rexx_vm_test_allocator_live_blocks(void)
{
    return test_allocator_live_blocks;
}
#else
#define vm_malloc malloc
#define vm_calloc calloc
#define vm_realloc realloc
#define vm_free free
#endif

#define REXX_VM_MAX_NAME_LENGTH ((size_t)65535u)

static const char *const vm_messages[] = {
    "ok", "invalid VM argument", "out of memory", "VM resource limit exceeded",
    "evaluation stack overflow", "evaluation stack underflow",
    "invalid or malformed bytecode", "value is not a valid finite number",
    "opcode is not implemented by this VM", "standard output error",
    "unknown REXX built-in function", "invalid arguments to REXX built-in function",
    "instruction budget exhausted", "invalid NUMERIC DIGITS setting"
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

static RexxVMStatus value_set_int32(RexxValue *value, int32_t number);

static char *copy_name(const char *name, size_t length)
{
    char *copy;
    size_t i;
    if (name == NULL || length == 0 || length > REXX_VM_MAX_NAME_LENGTH ||
        length == SIZE_MAX) return NULL;
    copy = (char *)vm_malloc(length + 1);
    if (copy == NULL) return NULL;
    for (i = 0; i < length; ++i) copy[i] = (char)ascii_upper((unsigned char)name[i]);
    copy[length] = '\0';
    return copy;
}

void rexx_value_clear(RexxValue *value)
{
    if (value == NULL) return;
    if (value->type == REXX_VALUE_STRING) vm_free(value->as.string.bytes);
    memset(value, 0, sizeof(*value));
}

RexxVMStatus rexx_value_set_number(RexxValue *value, double number)
{
    if (value == NULL || !rexx_numeric_is_finite(number)) return REXX_VM_STATUS_INVALID_ARGUMENT;
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
    copy = (char *)vm_malloc(length + 1);
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
                            status <= REXX_VM_STATUS_INVALID_NUMERIC_SETTING
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
            vm_free(key);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        grown = (RexxVMEntry *)vm_realloc(*entries, next * sizeof(**entries));
        if (grown == NULL) {
            vm_free(key);
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
    tail = tail_length == 0 ? (char *)vm_calloc(1, 1)
                            : copy_name(name + dot + 1, tail_length);
    if (stem == NULL || tail == NULL) {
        vm_free(stem);
        vm_free(tail);
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    }
    memset(&copied, 0, sizeof(copied));
    result = rexx_value_copy(&copied, value);
    if (result != REXX_VM_STATUS_OK) {
        vm_free(stem);
        vm_free(tail);
        return result;
    }
    for (i = 0; i < vm->stem_count; ++i) {
        RexxVMStemEntry *entry = &vm->stems[i];
        if (entry->stem_length == dot + 1 && entry->tail_length == tail_length &&
            memcmp(entry->stem, stem, dot + 1) == 0 &&
            memcmp(entry->tail, tail, tail_length) == 0) {
            rexx_value_clear(&entry->value);
            entry->value = copied;
            vm_free(stem);
            vm_free(tail);
            return REXX_VM_STATUS_OK;
        }
    }
    if (vm->stem_count >= REXX_VM_MAX_TABLE_ENTRIES) {
        vm_free(stem);
        vm_free(tail);
        rexx_value_clear(&copied);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    if (vm->stem_count == vm->stem_capacity) {
        size_t next = vm->stem_capacity == 0 ? 8 : vm->stem_capacity * 2;
        RexxVMStemEntry *grown;
        if (next < vm->stem_capacity || next > REXX_VM_MAX_TABLE_ENTRIES)
            next = REXX_VM_MAX_TABLE_ENTRIES;
        if (next <= vm->stem_capacity || next > SIZE_MAX / sizeof(*vm->stems)) {
            vm_free(stem);
            vm_free(tail);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        grown = (RexxVMStemEntry *)vm_realloc(vm->stems, next * sizeof(*vm->stems));
        if (grown == NULL) {
            vm_free(stem);
            vm_free(tail);
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
    vm->stack = (RexxValue *)vm_calloc(stack_capacity, sizeof(*vm->stack));
    if (vm->stack == NULL) return vm_fail(vm, REXX_VM_STATUS_OUT_OF_MEMORY, 0);
    vm->stack_capacity = stack_capacity;
    vm->instruction_budget = REXX_VM_DEFAULT_INSTRUCTION_BUDGET;
    vm->numeric_digits = REXX_VM_DEFAULT_NUMERIC_DIGITS;
    vm->numeric_fuzz = 0u;
    vm->numeric_form = REXX_NUMERIC_FORM_SCIENTIFIC;
    vm->numeric_work_budget = REXX_VM_DEFAULT_NUMERIC_WORK_BUDGET;
    vm->numeric_work_remaining = vm->numeric_work_budget;
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
        vm_free(vm->locals[i].name);
        rexx_value_clear(&vm->locals[i].value);
    }
    for (i = 0; vm->globals != NULL && i < vm->global_count &&
                i < vm->global_capacity; ++i) {
        vm_free(vm->globals[i].name);
        rexx_value_clear(&vm->globals[i].value);
    }
    for (i = 0; vm->stems != NULL && i < vm->stem_count &&
                i < vm->stem_capacity; ++i) {
        vm_free(vm->stems[i].stem);
        vm_free(vm->stems[i].tail);
        rexx_value_clear(&vm->stems[i].value);
    }
    rexx_value_clear(&vm->exit_value);
    vm_free(vm->stack);
    vm_free(vm->locals);
    vm_free(vm->globals);
    vm_free(vm->stems);
    vm_free(vm->address_environment);
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

void rexx_vm_set_output_writer(RexxVM *vm,
                               RexxVMOutputWriter writer,
                               void *user_data)
{
    if (vm == NULL || !vm->initialized) return;
    vm->output_writer = writer;
    vm->output_writer_user_data = writer != NULL ? user_data : NULL;
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
    vm_free(vm->address_environment);
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

    status = value_set_int32(&rc_value, vm->rc);
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

static RexxVMStatus value_decimal(const RexxValue *value, RexxDecimal *decimal)
{
    char number[64];
    const char *bytes = NULL;
    size_t length = 0u;
    int written;
    RexxNumericStatus status;

    if (value == NULL || decimal == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    switch (value->type) {
    case REXX_VALUE_STRING:
        if (value->as.string.bytes == NULL && value->as.string.length != 0u)
            return REXX_VM_STATUS_INVALID_ARGUMENT;
        bytes = value->as.string.bytes;
        length = value->as.string.length;
        break;
    case REXX_VALUE_BOOLEAN:
        bytes = value->as.boolean ? "1" : "0";
        length = 1u;
        break;
    case REXX_VALUE_NUMBER:
        if (!rexx_numeric_is_finite(value->as.number))
            return REXX_VM_STATUS_NUMERIC_ERROR;
        written = rexx_numeric_format(number, sizeof(number), value->as.number);
        if (written < 0) return REXX_VM_STATUS_NUMERIC_ERROR;
        bytes = number;
        length = (size_t)written;
        break;
    default:
        return REXX_VM_STATUS_NUMERIC_ERROR;
    }
    status = rexx_decimal_parse(bytes, length, decimal);
    if (status == REXX_NUMERIC_OK) return REXX_VM_STATUS_OK;
    if (status == REXX_NUMERIC_RESOURCE_LIMIT) return REXX_VM_STATUS_RESOURCE_LIMIT;
    return REXX_VM_STATUS_NUMERIC_ERROR;
}

static size_t numeric_text_cost(const RexxValue *value)
{
    if (value->type == REXX_VALUE_STRING) return value->as.string.length;
    if (value->type == REXX_VALUE_NUMBER) return 32u;
    return 1u;
}

static RexxVMStatus charge_numeric_work(RexxVM *vm, size_t cost)
{
    if (cost > vm->numeric_work_remaining) return REXX_VM_STATUS_RESOURCE_LIMIT;
    vm->numeric_work_remaining -= cost;
    return REXX_VM_STATUS_OK;
}

static size_t decimal_operation_cost(const RexxDecimal *left,
                                    const RexxDecimal *right,
                                    RexxOpcode opcode,
                                    size_t precision)
{
    size_t cost = left->length + right->length;
    if (opcode == OP_MULTIPLY) return cost + left->length * right->length;
    {
        int64_t scale_gap = (int64_t)left->scale - right->scale;
        int exponent_gap = ((int)left->length - left->scale - 1) -
                           ((int)right->length - right->scale - 1);
        if (scale_gap < 0) scale_gap = -scale_gap;
        if (exponent_gap < 0) exponent_gap = -exponent_gap;
        if ((size_t)exponent_gap <= precision + 2u)
            cost += (size_t)scale_gap;
    }
    return cost;
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
        if (!rexx_numeric_is_finite(value->as.number)) return REXX_VM_STATUS_NUMERIC_ERROR;
        written = rexx_numeric_format(number, sizeof(number), value->as.number);
        if (written < 0 || (size_t)written >= sizeof(number)) return REXX_VM_STATUS_NUMERIC_ERROR;
        source = number;
        count = (size_t)written;
        break;
    default: return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    if (count == SIZE_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
    *bytes = (char *)vm_malloc(count + 1);
    if (*bytes == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    if (count != 0) memcpy(*bytes, source, count);
    (*bytes)[count] = '\0';
    *length = count;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus value_truthy(const RexxValue *value, int *truthy)
{
    RexxDecimal number;
    RexxVMStatus status;
    if (value == NULL || truthy == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    switch (value->type) {
    case REXX_VALUE_UNSET: *truthy = 0; return REXX_VM_STATUS_OK;
    case REXX_VALUE_BOOLEAN: *truthy = value->as.boolean != 0; return REXX_VM_STATUS_OK;
    case REXX_VALUE_STRING:
    case REXX_VALUE_NUMBER:
        status = value_decimal(value, &number);
        if (status == REXX_VM_STATUS_RESOURCE_LIMIT) return status;
        *truthy = status == REXX_VM_STATUS_OK
                      ? number.sign != 0
                      : (value->type == REXX_VALUE_STRING &&
                         value->as.string.length != 0u);
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
        return rexx_value_set_string(value, constant->text, constant->length);
    }
    case REXX_CONSTANT_STRING: {
        char quote, *decoded;
        size_t out = 0;
        if (constant->length < 2) return REXX_VM_STATUS_INVALID_BYTECODE;
        quote = constant->text[0];
        if ((quote != '\'' && quote != '"') || constant->text[constant->length - 1] != quote)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        decoded = (char *)vm_malloc(constant->length);
        if (decoded == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
        i = 1;
        while (i + 1 < constant->length) {
            char c = constant->text[i++];
            if (c == quote) {
                if (i + 1 >= constant->length || constant->text[i] != quote) {
                    vm_free(decoded);
                    return REXX_VM_STATUS_INVALID_BYTECODE;
                }
                ++i;
            }
            decoded[out++] = c;
        }
        decoded[out] = '\0';
        {
            RexxVMStatus result = rexx_value_set_string(value, decoded, out);
            vm_free(decoded);
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
        decoded = (char *)vm_calloc(out_length + 1, 1);
        if (decoded == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
        if (is_hex) {
            size_t digit_index = 0;
            int high_nibble = -1;
            for (i = start; i < end; ++i) {
                int nibble;
                unsigned char c = (unsigned char)constant->text[i];
                if (ascii_space(c)) continue;
                nibble = hex_value(c);
                if (nibble < 0) { vm_free(decoded); return REXX_VM_STATUS_INVALID_BYTECODE; }
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
                vm_free(decoded);
                return REXX_VM_STATUS_INVALID_BYTECODE;
            }
        } else {
            size_t bit = out_length * 8 - digits;
            for (i = start; i < end; ++i) {
                unsigned char c = (unsigned char)constant->text[i];
                if (ascii_space(c)) continue;
                if (c != '0' && c != '1') { vm_free(decoded); return REXX_VM_STATUS_INVALID_BYTECODE; }
                if (c == '1') decoded[bit / 8] |= (char)(1u << (7u - (unsigned)(bit % 8)));
                ++bit;
            }
        }
        {
            RexxVMStatus result = rexx_value_set_string(value, decoded, out_length);
            vm_free(decoded);
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

static RexxVMStatus decimal_status_to_vm(RexxNumericStatus status)
{
    if (status == REXX_NUMERIC_OK) return REXX_VM_STATUS_OK;
    if (status == REXX_NUMERIC_RESOURCE_LIMIT) return REXX_VM_STATUS_RESOURCE_LIMIT;
    return REXX_VM_STATUS_NUMERIC_ERROR;
}

static RexxVMStatus decimal_value_to_rexx(const RexxDecimal *decimal,
                                          size_t precision,
                                          RexxValue *result)
{
    char output[REXX_DECIMAL_MAX_DIGITS + 64u];
    int length = rexx_decimal_format(output, sizeof(output), decimal, precision);
    if (length < 0) return REXX_VM_STATUS_RESOURCE_LIMIT;
    return rexx_value_set_string(result, output, (size_t)length);
}

static RexxVMStatus binary_decimal(RexxVM *vm, RexxOpcode opcode)
{
    RexxValue left, right, result;
    RexxDecimal a, b, answer;
    RexxNumericStatus numeric_status = REXX_NUMERIC_INVALID;
    RexxVMStatus status;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&result, 0, sizeof(result));
    status = pop_move(vm, &right);
    if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &left);
    if (status == REXX_VM_STATUS_OK) status = value_decimal(&left, &a);
    if (status == REXX_VM_STATUS_OK) status = value_decimal(&right, &b);
    if (status == REXX_VM_STATUS_OK) {
        size_t cost = numeric_text_cost(&left) + numeric_text_cost(&right) +
                      decimal_operation_cost(&a, &b, opcode,
                                             vm->numeric_digits);
        status = charge_numeric_work(vm, cost);
    }
    if (status == REXX_VM_STATUS_OK) {
        switch (opcode) {
        case OP_ADD:
            numeric_status = rexx_decimal_add(&a, &b, vm->numeric_digits, &answer);
            break;
        case OP_SUB:
            numeric_status = rexx_decimal_subtract(&a, &b, vm->numeric_digits, &answer);
            break;
        case OP_MULTIPLY:
            numeric_status = rexx_decimal_multiply(&a, &b, vm->numeric_digits, &answer);
            break;
        default:
            status = REXX_VM_STATUS_UNSUPPORTED_OPCODE;
            break;
        }
        if (status == REXX_VM_STATUS_OK)
            status = decimal_status_to_vm(numeric_status);
        if (status == REXX_VM_STATUS_OK)
            status = decimal_value_to_rexx(&answer, vm->numeric_digits, &result);
        if (status == REXX_VM_STATUS_OK) status = push_move(vm, &result);
    }
    rexx_value_clear(&left);
    rexx_value_clear(&right);
    rexx_value_clear(&result);
    return status;
}

static RexxVMStatus unary_decimal(RexxVM *vm, RexxOpcode opcode)
{
    RexxValue source, result;
    RexxDecimal number;
    RexxVMStatus status;
    RexxNumericStatus numeric_status;
    memset(&source, 0, sizeof(source));
    memset(&result, 0, sizeof(result));
    status = pop_move(vm, &source);
    if (status == REXX_VM_STATUS_OK) status = value_decimal(&source, &number);
    if (status == REXX_VM_STATUS_OK) {
        status = charge_numeric_work(vm, numeric_text_cost(&source) + number.length);
    }
    if (status == REXX_VM_STATUS_OK) {
        if (opcode == OP_NEGATE && number.sign != 0) number.sign = (int8_t)-number.sign;
        numeric_status = rexx_decimal_round(&number, vm->numeric_digits);
        status = decimal_status_to_vm(numeric_status);
    }
    if (status == REXX_VM_STATUS_OK)
        status = decimal_value_to_rexx(&number, vm->numeric_digits, &result);
    if (status == REXX_VM_STATUS_OK) status = push_move(vm, &result);
    rexx_value_clear(&source);
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
            joined = (char *)vm_malloc(alen + separator_length + blen + 1);
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
    vm_free(a);
    vm_free(b);
    vm_free(joined);
    rexx_value_clear(&left);
    rexx_value_clear(&right);
    rexx_value_clear(&result);
    return status;
}

static int comparison_is_strict(RexxOpcode opcode)
{
    return opcode == OP_STRICT_EQUAL || opcode == OP_STRICT_NOT_EQUAL ||
           opcode == OP_STRICT_GREATER || opcode == OP_STRICT_LESS ||
           opcode == OP_STRICT_GREATER_EQUAL || opcode == OP_STRICT_LESS_EQUAL ||
           opcode == OP_STRICT_NOT_GREATER || opcode == OP_STRICT_NOT_LESS;
}

static int comparison_result(RexxOpcode opcode, int comparison)
{
    switch (opcode) {
    case OP_EQUAL: case OP_STRICT_EQUAL: return comparison == 0;
    case OP_NOT_EQUAL: case OP_STRICT_NOT_EQUAL: return comparison != 0;
    case OP_GREATER: case OP_STRICT_GREATER: return comparison > 0;
    case OP_LESS: case OP_STRICT_LESS: return comparison < 0;
    case OP_GREATER_EQUAL: case OP_STRICT_GREATER_EQUAL: return comparison >= 0;
    case OP_LESS_EQUAL: case OP_STRICT_LESS_EQUAL: return comparison <= 0;
    case OP_NOT_GREATER: case OP_STRICT_NOT_GREATER: return comparison <= 0;
    case OP_NOT_LESS: case OP_STRICT_NOT_LESS: return comparison >= 0;
    default: return 0;
    }
}

static RexxVMStatus compare_values(RexxVM *vm,
                                   const RexxValue *left,
                                   const RexxValue *right,
                                   RexxOpcode opcode,
                                   int *matches)
{
    RexxDecimal a, b, difference;
    RexxVMStatus left_status, right_status;
    RexxNumericStatus numeric_status;
    int comparison;

    if (vm == NULL || left == NULL || right == NULL || matches == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (comparison_is_strict(opcode)) {
        left_status = right_status = REXX_VM_STATUS_NUMERIC_ERROR;
    } else {
        left_status = value_decimal(left, &a);
        right_status = value_decimal(right, &b);
    }
    if (!comparison_is_strict(opcode) &&
        (left_status == REXX_VM_STATUS_RESOURCE_LIMIT ||
        right_status == REXX_VM_STATUS_RESOURCE_LIMIT)
       )
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    if (!comparison_is_strict(opcode) &&
        left_status == REXX_VM_STATUS_OK && right_status == REXX_VM_STATUS_OK) {
        size_t cost = numeric_text_cost(left) + numeric_text_cost(right) +
                      decimal_operation_cost(&a, &b, OP_SUB,
                                             vm->numeric_digits - vm->numeric_fuzz);
        RexxVMStatus charge_status = charge_numeric_work(vm, cost);
        if (charge_status != REXX_VM_STATUS_OK) return charge_status;
        numeric_status = rexx_decimal_subtract(&a, &b,
                                               vm->numeric_digits - vm->numeric_fuzz,
                                               &difference);
        if (numeric_status != REXX_NUMERIC_OK)
            return decimal_status_to_vm(numeric_status);
        comparison = difference.sign;
    } else {
        char *left_text = NULL, *right_text = NULL;
        size_t left_length = 0u, right_length = 0u, count, index;
        RexxVMStatus status = value_string(left, &left_text, &left_length);
        if (status == REXX_VM_STATUS_OK)
            status = value_string(right, &right_text, &right_length);
        if (status != REXX_VM_STATUS_OK) {
            vm_free(left_text); vm_free(right_text); return status;
        }
        count = comparison_is_strict(opcode)
                    ? (left_length < right_length ? left_length : right_length)
                    : (left_length > right_length ? left_length : right_length);
        comparison = 0;
        for (index = 0u; index < count; ++index) {
            unsigned char ca = index < left_length ? (unsigned char)left_text[index] : (unsigned char)' ';
            unsigned char cb = index < right_length ? (unsigned char)right_text[index] : (unsigned char)' ';
            if (ca != cb) { comparison = ca < cb ? -1 : 1; break; }
        }
        if (comparison == 0 && comparison_is_strict(opcode) &&
            left_length != right_length)
            comparison = left_length < right_length ? -1 : 1;
        vm_free(left_text);
        vm_free(right_text);
    }
    *matches = comparison_result(opcode, comparison);
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus value_set_uint32(RexxValue *value, uint32_t number)
{
    char reverse[10], output[10];
    size_t count = 0u, i;
    do {
        reverse[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0u);
    for (i = 0u; i < count; ++i) output[i] = reverse[count - i - 1u];
    return rexx_value_set_string(value, output, count);
}

static RexxVMStatus value_set_int32(RexxValue *value, int32_t number)
{
    char output[11];
    uint32_t magnitude;
    size_t offset = 0u;
    if (number < 0) {
        output[offset++] = '-';
        magnitude = (uint32_t)(-(int64_t)number);
    } else {
        magnitude = (uint32_t)number;
    }
    {
        RexxValue digits = REXX_VALUE_INIT;
        RexxVMStatus status = value_set_uint32(&digits, magnitude);
        if (status != REXX_VM_STATUS_OK) return status;
        if (digits.as.string.length > sizeof(output) - offset) {
            rexx_value_clear(&digits);
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        memcpy(output + offset, digits.as.string.bytes,
               digits.as.string.length);
        offset += digits.as.string.length;
        rexx_value_clear(&digits);
    }
    return rexx_value_set_string(value, output, offset);
}

static RexxVMStatus execute_numeric_digits(RexxVM *vm,
                                           uint8_t flags,
                                           RexxValue *operand)
{
    RexxDecimal decimal;
    uint32_t precision;
    RexxVMStatus status;
    RexxNumericStatus numeric_status;

    if ((flags & (uint8_t)~REXX_INSTRUCTION_HAS_VALUE) != 0u)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if ((flags & REXX_INSTRUCTION_HAS_VALUE) == 0u) {
        vm->numeric_digits = REXX_VM_DEFAULT_NUMERIC_DIGITS;
        return REXX_VM_STATUS_OK;
    }
    status = value_decimal(operand, &decimal);
    if (status != REXX_VM_STATUS_OK) return REXX_VM_STATUS_INVALID_NUMERIC_SETTING;
    numeric_status = rexx_decimal_to_uint32(&decimal, &precision);
    if (numeric_status != REXX_NUMERIC_OK || precision == 0u ||
        precision > REXX_NUMERIC_MAX_DIGITS || precision <= vm->numeric_fuzz)
        return REXX_VM_STATUS_INVALID_NUMERIC_SETTING;
    vm->numeric_digits = precision;
    return REXX_VM_STATUS_OK;
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

    vm->numeric_work_remaining = vm->numeric_work_budget;

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
            status = binary_decimal(vm, (RexxOpcode)ins->opcode);
            break;

        case OP_POSITIVE:
        case OP_NEGATE:
            status = unary_decimal(vm, (RexxOpcode)ins->opcode);
            break;

        case OP_CONCAT:
        case OP_CONCAT_BLANK:
        case OP_CONCAT_ABUTTAL:
            status = concat_values(vm, (RexxOpcode)ins->opcode);
            break;

        case OP_EQUAL:
        case OP_STRICT_EQUAL:
        case OP_NOT_EQUAL:
        case OP_STRICT_NOT_EQUAL:
        case OP_GREATER:
        case OP_LESS:
        case OP_GREATER_EQUAL:
        case OP_LESS_EQUAL:
        case OP_NOT_GREATER:
        case OP_NOT_LESS:
        case OP_STRICT_GREATER:
        case OP_STRICT_LESS:
        case OP_STRICT_GREATER_EQUAL:
        case OP_STRICT_LESS_EQUAL:
        case OP_STRICT_NOT_GREATER:
        case OP_STRICT_NOT_LESS: {
            RexxValue left, right;
            int matches = 0;
            memset(&left, 0, sizeof(left));
            memset(&right, 0, sizeof(right));
            status = pop_move(vm, &right);
            if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &left);
            if (status == REXX_VM_STATUS_OK)
                status = compare_values(vm, &left, &right,
                                        (RexxOpcode)ins->opcode, &matches);
            if (status == REXX_VM_STATUS_OK)
                status = rexx_value_set_boolean(&value, matches);
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
            if (status == REXX_VM_STATUS_OK) {
                static const char newline = '\n';
                if (vm->output_writer != NULL) {
                    if ((length != 0u &&
                         vm->output_writer(vm->output_writer_user_data,
                                           text, length) == 0) ||
                        vm->output_writer(vm->output_writer_user_data,
                                          &newline, 1u) == 0) {
                        status = REXX_VM_STATUS_IO_ERROR;
                    }
                } else {
#if !defined(GXOS_BARE_METAL)
                    if ((length != 0u && fwrite(text, 1u, length, stdout) != length) ||
                        fputc('\n', stdout) == EOF) {
                        status = REXX_VM_STATUS_IO_ERROR;
                    }
#else
                    status = REXX_VM_STATUS_IO_ERROR;
#endif
                }
            }
            vm_free(text);
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
            vm_free(command);
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
                if (ascii_equal(function->name, function->length, "DIGITS", 6u)) {
                    if (argument_count != 0u)
                        status = REXX_VM_STATUS_INVALID_FUNCTION_CALL;
                    else
                        status = value_set_uint32(&value, vm->numeric_digits);
                } else {
                    status = rexx_builtin_dispatch(
                        function->name, function->length,
                        argument_count == 0u ? NULL : &vm->stack[argument_start],
                        argument_count, &value);
                }
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

        case OP_NUMERIC_DIGITS:
            if ((ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0u)
                status = pop_move(vm, &value);
            if (status == REXX_VM_STATUS_OK)
                status = execute_numeric_digits(vm, ins->flags, &value);
            rexx_value_clear(&value);
            break;

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
                vm_free(command);
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
        status > REXX_VM_STATUS_INVALID_NUMERIC_SETTING)
        return "unknown VM status";
    return vm_messages[status];
}
