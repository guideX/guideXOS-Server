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
    "instruction budget exhausted", "invalid NUMERIC DIGITS setting",
    "invalid NUMERIC FUZZ setting", "invalid NUMERIC FORM setting",
    "unknown REXX routine", "internal routine depth limit exceeded",
    "PROCEDURE is invalid here",
    "internal function returned without a result",
    "unknown SIGNAL label", "invalid SIGNAL destination label",
    "invalid subsidiary PROCEDURE EXPOSE list", "condition control transfer",
    "syntax error in interpreted source"
};

typedef struct RexxVMEnvironment RexxVMEnvironment;
typedef struct RexxVMExpose RexxVMExpose;
typedef struct RexxVMArgument RexxVMArgument;
typedef struct RexxVMCallFrame RexxVMCallFrame;
typedef struct RexxVMDoFrame RexxVMDoFrame;

struct RexxVMExpose {
    char *name;
    size_t name_length;
    int stem_prefix;
    RexxVMExpose *next;
};

struct RexxVMEnvironment {
    RexxVMEntry *locals;
    size_t local_count;
    size_t local_capacity;
    RexxVMStemEntry *stems;
    size_t stem_count;
    size_t stem_capacity;
    RexxVMEnvironment *parent;
    RexxVMExpose *exposes;
    RexxVMExpose *exposes_tail;
    size_t expose_count;
    size_t exposed_name_bytes;
    int root;
    int transparent_parent;
};

struct RexxVMArgument {
    char *bytes;
    size_t length;
    int present;
};

struct RexxVMDoFrame {
    uint32_t flags;
    uint32_t control_symbol;
    RexxDecimal control_value;
    RexxDecimal limit;
    RexxDecimal step;
    uint32_t remaining;
    int has_limit;
    int has_remaining;
    int counted;
    int controlled;
};

struct RexxVMCallFrame {
    size_t return_pc;
    const RexxBytecodeChunk *return_chunk;
    size_t return_interpret_depth;
    size_t stack_base;
    size_t do_depth;
    int function_call;
    RexxVMEnvironment *caller_environment;
    RexxVMEnvironment *callee_environment;
    RexxVMArgument *caller_arguments;
    size_t caller_argument_count;
    RexxVMArgument *arguments;
    size_t argument_count;
    uint32_t numeric_digits;
    uint32_t numeric_fuzz;
    RexxNumericForm numeric_form;
    char *address_environment;
    size_t address_environment_length;
    int procedure_entered;
    int first_instruction;
    int owns_environment;
    int expose_setup_open;
    uint8_t condition_states[REXX_CONDITION_COUNT];
    uint8_t condition_actions[REXX_CONDITION_COUNT];
    uint32_t condition_targets[REXX_CONDITION_COUNT];
    char *condition_target_names[REXX_CONDITION_COUNT];
    size_t condition_target_name_lengths[REXX_CONDITION_COUNT];
    uint32_t current_condition;
    uint8_t condition_invocation;
    char *condition_description;
    size_t condition_description_length;
    int condition_information_active;
    int condition_call;
};

typedef struct RexxVMInterpretFrame {
    const RexxBytecodeChunk *parent_chunk;
    size_t parent_pc;
    RexxBytecodeChunk *chunk;
    char *source;
    size_t source_length;
    size_t source_line;
    size_t routine_depth;
    size_t stack_base;
    size_t do_depth;
    size_t bytecode_bytes;
    size_t instruction_count;
} RexxVMInterpretFrame;

enum {
    REXX_CONDITION_STATE_OFF = 0,
    REXX_CONDITION_STATE_ON = 1,
    REXX_CONDITION_STATE_DELAY = 2,
    REXX_CONDITION_ACTION_SIGNAL = 0,
    REXX_CONDITION_ACTION_CALL = 1,
    REXX_CONDITION_INVOKED_SIGNAL = 0,
    REXX_CONDITION_INVOKED_CALL = 1
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

static RexxVMStatus environment_load(RexxVM *vm,
                                     RexxVMEnvironment *environment,
                                     const RexxSymbol *symbol,
                                     int stem,
                                     RexxValue *value);
static RexxVMStatus value_string(const RexxValue *value, char **bytes,
                                size_t *length);
static RexxVMStatus resolve_expose_name(RexxVM *vm,
                                        RexxVMEnvironment *environment,
                                        const char *name, size_t name_length,
                                        char **resolved_name,
                                        size_t *resolved_length);
static int resolved_name_uses_stem_table(const char *name, size_t length);
static int expose_name_has_dynamic_tail(const char *name, size_t length);
static RexxVMStatus load_variable_value(RexxVM *vm,
                                        RexxVMEnvironment *environment,
                                        const RexxSymbol *symbol,
                                        RexxValue *value);

static RexxVMStatus value_set_int32(RexxValue *value, int32_t number);
static RexxVMStatus raise_condition(RexxVM *vm,
                                    const RexxBytecodeChunk *chunk,
                                    RexxConditionType condition,
                                    const char *description,
                                    size_t description_length,
                                    size_t source_line,
                                    int32_t syntax_code,
                                    int *transferred);
static RexxVMStatus execute_condition_registration(
    RexxVM *vm, const RexxBytecodeChunk *chunk,
    const RexxInstruction *instruction);
static RexxVMStatus execute_condition_builtin(
    RexxVM *vm, const RexxValue *arguments, size_t argument_count,
    RexxValue *result);
static void unwind_routines(RexxVM *vm);
static void truncate_interpret_contexts(RexxVM *vm, size_t minimum_routine_depth,
                                       int restore_parent);
static void copy_saved_condition_context(RexxVMCallFrame *destination,
                                         RexxVMCallFrame *source);

static size_t physical_source_line(const RexxVM *vm,
                                   const RexxBytecodeChunk *chunk,
                                   size_t source_line)
{
    const RexxVMInterpretFrame *frames;
    size_t index;
    if (vm == NULL || chunk == vm->root_chunk) return source_line;
    frames = (const RexxVMInterpretFrame *)vm->interpret_frames;
    for (index = vm->interpret_depth; index != 0u; --index) {
        const RexxVMInterpretFrame *frame = &frames[index - 1u];
        if (frame->chunk == chunk) return frame->source_line;
    }
    return source_line;
}

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

static char *copy_text(const char *text, size_t length)
{
    char *copy;
    if (length == 0u) return NULL;
    if (text == NULL || length > REXX_VM_MAX_PARSE_SOURCE_LENGTH ||
        length == SIZE_MAX) return NULL;
    copy = (char *)vm_malloc(length + 1u);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void free_condition_target_names(char **names)
{
    size_t index;
    if (names == NULL) return;
    for (index = 0u; index < REXX_CONDITION_COUNT; ++index) {
        vm_free(names[index]);
        names[index] = NULL;
    }
}

static void free_condition_snapshot(RexxVMCallFrame *frame)
{
    if (frame == NULL) return;
    free_condition_target_names(frame->condition_target_names);
    vm_free(frame->condition_description);
    frame->condition_description = NULL;
    frame->condition_description_length = 0u;
}

static RexxVMStatus save_condition_context(RexxVM *vm,
                                           RexxVMCallFrame *frame)
{
    size_t index;
    if (vm == NULL || frame == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;
    memcpy(frame->condition_states, vm->condition_states,
           sizeof(frame->condition_states));
    memcpy(frame->condition_actions, vm->condition_actions,
           sizeof(frame->condition_actions));
    memcpy(frame->condition_targets, vm->condition_targets,
           sizeof(frame->condition_targets));
    for (index = 0u; index < REXX_CONDITION_COUNT; ++index) {
        frame->condition_target_name_lengths[index] =
            vm->condition_target_name_lengths[index];
        if (vm->condition_target_names[index] != NULL) {
            frame->condition_target_names[index] = copy_text(
                vm->condition_target_names[index],
                vm->condition_target_name_lengths[index]);
            if (frame->condition_target_names[index] == NULL) {
                free_condition_snapshot(frame);
                return REXX_VM_STATUS_OUT_OF_MEMORY;
            }
        }
    }
    frame->current_condition = vm->current_condition;
    frame->condition_invocation = vm->condition_invocation;
    frame->condition_information_active = vm->condition_information_active;
    frame->condition_description_length = vm->condition_description_length;
    if (vm->condition_description_length != 0u) {
        frame->condition_description = copy_text(
            vm->condition_description, vm->condition_description_length);
        if (frame->condition_description == NULL) {
            free_condition_snapshot(frame);
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        }
    }
    return REXX_VM_STATUS_OK;
}

static void restore_condition_context(RexxVM *vm, RexxVMCallFrame *frame)
{
    size_t index;
    vm_free(vm->condition_description);
    free_condition_target_names(vm->condition_target_names);
    memcpy(vm->condition_states, frame->condition_states,
           sizeof(vm->condition_states));
    memcpy(vm->condition_actions, frame->condition_actions,
           sizeof(vm->condition_actions));
    memcpy(vm->condition_targets, frame->condition_targets,
           sizeof(vm->condition_targets));
    for (index = 0u; index < REXX_CONDITION_COUNT; ++index) {
        vm->condition_target_names[index] =
            frame->condition_target_names[index];
        vm->condition_target_name_lengths[index] =
            frame->condition_target_name_lengths[index];
        frame->condition_target_names[index] = NULL;
        frame->condition_target_name_lengths[index] = 0u;
    }
    vm->current_condition = frame->current_condition;
    vm->condition_invocation = frame->condition_invocation;
    vm->condition_information_active =
        frame->condition_information_active;
    vm->condition_description = frame->condition_description;
    vm->condition_description_length = frame->condition_description_length;
    frame->condition_description = NULL;
    frame->condition_description_length = 0u;
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
                            status <= REXX_VM_STATUS_DYNAMIC_SYNTAX
                                ? vm_messages[status] : "unknown VM error";
    }
    return status;
}

static int find_entry(const RexxVMEntry *entries, size_t count,
                      const char *name, size_t length);

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

static void environment_sync_root(RexxVM *vm, RexxVMEnvironment *environment)
{
    if (vm == NULL || environment == NULL || !environment->root) return;
    vm->locals = environment->locals;
    vm->local_count = environment->local_count;
    vm->local_capacity = environment->local_capacity;
    vm->stems = environment->stems;
    vm->stem_count = environment->stem_count;
    vm->stem_capacity = environment->stem_capacity;
}

static RexxVMStatus stem_set(RexxVM *vm, RexxVMEnvironment *environment,
                             const char *name, size_t name_length,
                             const RexxValue *value)
{
    size_t dot = 0, tail_length, i;
    char *stem = NULL, *tail = NULL;
    RexxValue copied;
    RexxVMStatus result;
    if (vm == NULL || environment == NULL || name == NULL || value == NULL ||
        name_length == 0)
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
    for (i = 0; i < environment->stem_count; ++i) {
        RexxVMStemEntry *entry = &environment->stems[i];
        if (entry->stem_length == dot + 1 && entry->tail_length == tail_length &&
            memcmp(entry->stem, stem, dot + 1) == 0 &&
            memcmp(entry->tail, tail, tail_length) == 0) {
            rexx_value_clear(&entry->value);
            entry->value = copied;
            vm_free(stem);
            vm_free(tail);
            environment_sync_root(vm, environment);
            return REXX_VM_STATUS_OK;
        }
    }
    if (environment->stem_count >= REXX_VM_MAX_TABLE_ENTRIES) {
        vm_free(stem);
        vm_free(tail);
        rexx_value_clear(&copied);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    if (environment->stem_count == environment->stem_capacity) {
        size_t next = environment->stem_capacity == 0 ? 8 : environment->stem_capacity * 2;
        RexxVMStemEntry *grown;
        if (next < environment->stem_capacity || next > REXX_VM_MAX_TABLE_ENTRIES)
            next = REXX_VM_MAX_TABLE_ENTRIES;
        if (next <= environment->stem_capacity || next > SIZE_MAX / sizeof(*environment->stems)) {
            vm_free(stem);
            vm_free(tail);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        grown = (RexxVMStemEntry *)vm_realloc(environment->stems, next * sizeof(*environment->stems));
        if (grown == NULL) {
            vm_free(stem);
            vm_free(tail);
            rexx_value_clear(&copied);
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        }
        environment->stems = grown;
        environment->stem_capacity = next;
    }
    environment->stems[environment->stem_count].stem = stem;
    environment->stems[environment->stem_count].stem_length = dot + 1;
    environment->stems[environment->stem_count].tail = tail;
    environment->stems[environment->stem_count].tail_length = tail_length;
    environment->stems[environment->stem_count].value = copied;
    ++environment->stem_count;
    environment_sync_root(vm, environment);
    return REXX_VM_STATUS_OK;
}

static RexxVMEnvironment *environment_new(RexxVM *vm,
                                           RexxVMEnvironment *parent,
                                           int root)
{
    RexxVMEnvironment *environment = (RexxVMEnvironment *)vm_calloc(
        1u, sizeof(*environment));
    if (environment != NULL) {
        environment->parent = parent;
        environment->root = root;
    }
    (void)vm;
    return environment;
}

static void environment_free(RexxVMEnvironment *environment)
{
    size_t i;
    RexxVMExpose *expose;
    if (environment == NULL) return;
    for (i = 0u; i < environment->local_count; ++i) {
        vm_free(environment->locals[i].name);
        rexx_value_clear(&environment->locals[i].value);
    }
    for (i = 0u; i < environment->stem_count; ++i) {
        vm_free(environment->stems[i].stem);
        vm_free(environment->stems[i].tail);
        rexx_value_clear(&environment->stems[i].value);
    }
    expose = environment->exposes;
    while (expose != NULL) {
        RexxVMExpose *next = expose->next;
        vm_free(expose->name);
        vm_free(expose);
        expose = next;
    }
    vm_free(environment->locals);
    vm_free(environment->stems);
    vm_free(environment);
}

static RexxVMExpose *environment_find_expose(RexxVMEnvironment *environment,
                                              const char *name,
                                              size_t name_length)
{
    RexxVMExpose *expose;
    for (expose = environment != NULL ? environment->exposes : NULL;
         expose != NULL; expose = expose->next) {
        if (ascii_equal(expose->name, expose->name_length, name, name_length) ||
            (expose->stem_prefix && name_length >= expose->name_length &&
             ascii_equal(expose->name, expose->name_length, name,
                         expose->name_length))) return expose;
    }
    return NULL;
}

static RexxVMExpose *environment_find_exact_expose(
    RexxVMEnvironment *environment, const char *name, size_t name_length)
{
    RexxVMExpose *expose;
    for (expose = environment != NULL ? environment->exposes : NULL;
         expose != NULL; expose = expose->next) {
        if (ascii_equal(expose->name, expose->name_length,
                        name, name_length)) return expose;
    }
    return NULL;
}

static RexxVMStatus environment_add_expose_name(
    RexxVM *vm, RexxVMEnvironment *environment,
    const char *name, size_t name_length)
{
    RexxVMExpose *expose;
    size_t dot = 0u;
    if (environment == NULL || environment->root || environment->parent == NULL ||
        name == NULL || name_length == 0u)
        return REXX_VM_STATUS_INVALID_PROCEDURE;
    if (environment_find_exact_expose(environment, name, name_length) != NULL)
        return REXX_VM_STATUS_OK;
    if (name_length > REXX_VM_MAX_NAME_LENGTH ||
        environment->expose_count >= REXX_VM_MAX_EXPOSES ||
        name_length > REXX_VM_MAX_EXPOSED_NAME_BYTES -
                          environment->exposed_name_bytes)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    expose = (RexxVMExpose *)vm_calloc(1u, sizeof(*expose));
    if (expose == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    expose->name = copy_name(name, name_length);
    if (expose->name == NULL) {
        vm_free(expose);
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    }
    expose->name_length = name_length;
    while (dot < name_length && name[dot] != '.') ++dot;
    expose->stem_prefix = dot + 1u == name_length && dot < name_length;
    if (environment->exposes_tail == NULL) environment->exposes = expose;
    else environment->exposes_tail->next = expose;
    environment->exposes_tail = expose;
    ++environment->expose_count;
    environment->exposed_name_bytes += name_length;
    (void)vm;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus environment_add_expose(RexxVM *vm,
                                            RexxVMEnvironment *environment,
                                            const RexxSymbol *symbol)
{
    char *resolved_name = NULL;
    size_t resolved_length = 0u;
    RexxVMStatus status;
    if (symbol == NULL || symbol->name == NULL)
        return REXX_VM_STATUS_INVALID_PROCEDURE;
    if (!expose_name_has_dynamic_tail(symbol->name, symbol->length))
        return environment_add_expose_name(vm, environment, symbol->name,
                                           symbol->length);
    status = resolve_expose_name(vm, environment, symbol->name,
                                 symbol->length, &resolved_name,
                                 &resolved_length);
    if (status == REXX_VM_STATUS_OK)
        status = environment_add_expose_name(
            vm, environment, resolved_name, resolved_length);
    vm_free(resolved_name);
    return status;
}

static int subsidiary_name_start(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '!' || c == '?' || c == '_' || c == '@' || c == '#' ||
           c == '$';
}

static int subsidiary_name_part(unsigned char c)
{
    return subsidiary_name_start(c) || (c >= '0' && c <= '9') || c == '.';
}

static int subsidiary_name_valid(const char *name, size_t length)
{
    size_t index;
    if (name == NULL || length == 0u || length > REXX_VM_MAX_NAME_LENGTH ||
        !subsidiary_name_start((unsigned char)name[0])) return 0;
    for (index = 1u; index < length; ++index) {
        if (!subsidiary_name_part((unsigned char)name[index])) return 0;
    }
    return 1;
}

static int resolved_name_uses_stem_table(const char *name, size_t length)
{
    size_t index;
    for (index = 0u; index < length; ++index)
        if (name[index] == '.') return 1;
    return 0;
}

static int expose_name_has_dynamic_tail(const char *name, size_t length)
{
    size_t index;
    for (index = 0u; index + 1u < length; ++index)
        if (name[index] == '.') return 1;
    return 0;
}

static RexxVMStatus resolved_name_append(char **buffer, size_t *length,
                                         size_t *capacity,
                                         const char *text,
                                         size_t text_length)
{
    size_t required;
    size_t next;
    char *grown;
    if (text_length > REXX_VM_MAX_NAME_LENGTH - *length)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    required = *length + text_length + 1u;
    if (required <= *capacity) {
        if (text_length != 0u)
            memcpy(*buffer + *length, text, text_length);
        *length += text_length;
        (*buffer)[*length] = '\0';
        return REXX_VM_STATUS_OK;
    }
    next = *capacity == 0u ? 32u : *capacity;
    while (next < required) {
        if (next > (REXX_VM_MAX_NAME_LENGTH + 1u) / 2u) {
            next = REXX_VM_MAX_NAME_LENGTH + 1u;
            break;
        }
        next *= 2u;
    }
    if (next < required || next > SIZE_MAX / sizeof(char))
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    grown = (char *)vm_realloc(*buffer, next);
    if (grown == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    *buffer = grown;
    *capacity = next;
    if (text_length != 0u)
        memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
    return REXX_VM_STATUS_OK;
}

/* Resolve each symbolic compound tail in order, using the environment as it
   exists at this EXPOSE item. Numeric tails remain literal. */
static RexxVMStatus resolve_expose_name(RexxVM *vm,
                                        RexxVMEnvironment *environment,
                                        const char *name, size_t name_length,
                                        char **resolved_name,
                                        size_t *resolved_length)
{
    size_t dot = 0u;
    size_t cursor;
    size_t length = 0u;
    size_t capacity = 0u;
    char *buffer = NULL;
    RexxVMStatus status;
    if (name == NULL || name_length == 0u || resolved_name == NULL ||
        resolved_length == NULL || name_length > REXX_VM_MAX_NAME_LENGTH)
        return REXX_VM_STATUS_INVALID_EXPOSE_LIST;
    while (dot < name_length && name[dot] != '.') ++dot;
    if (dot == name_length || dot + 1u == name_length) {
        status = resolved_name_append(&buffer, &length, &capacity,
                                      name, name_length);
        if (status != REXX_VM_STATUS_OK) {
            vm_free(buffer);
            return status;
        }
    } else {
        status = resolved_name_append(&buffer, &length, &capacity,
                                      name, dot + 1u);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        cursor = dot + 1u;
        while (cursor <= name_length) {
            size_t end = cursor;
            size_t part_length;
            int numeric = 1;
            size_t index;
            while (end < name_length && name[end] != '.') ++end;
            part_length = end - cursor;
            if (part_length != 0u) {
                for (index = 0u; index < part_length; ++index) {
                    if (name[cursor + index] < '0' ||
                        name[cursor + index] > '9') {
                        numeric = 0;
                        break;
                    }
                }
                if (!numeric && subsidiary_name_start(
                                    (unsigned char)name[cursor])) {
                    RexxSymbol segment_symbol;
                    char *segment_name = copy_name(name + cursor,
                                                   part_length);
                    RexxValue segment_value = REXX_VALUE_INIT;
                    char *segment_text = NULL;
                    size_t segment_text_length = 0u;
                    if (segment_name == NULL) {
                        status = REXX_VM_STATUS_OUT_OF_MEMORY;
                        goto cleanup;
                    }
                    segment_symbol.name = segment_name;
                    segment_symbol.length = part_length;
                    segment_symbol.kind = REXX_SYMBOL_VARIABLE;
                    status = environment_load(vm, environment,
                                              &segment_symbol, 0,
                                              &segment_value);
                    if (status == REXX_VM_STATUS_OK &&
                        segment_value.type == REXX_VALUE_STRING &&
                        segment_value.as.string.length >
                            REXX_VM_MAX_NAME_LENGTH - length)
                        status = REXX_VM_STATUS_RESOURCE_LIMIT;
                    if (status == REXX_VM_STATUS_OK)
                        status = value_string(&segment_value, &segment_text,
                                              &segment_text_length);
                    rexx_value_clear(&segment_value);
                    vm_free(segment_name);
                    if (status == REXX_VM_STATUS_OK)
                        status = resolved_name_append(
                            &buffer, &length, &capacity, segment_text,
                            segment_text_length);
                    vm_free(segment_text);
                    if (status != REXX_VM_STATUS_OK) goto cleanup;
                } else {
                    status = resolved_name_append(&buffer, &length,
                                                  &capacity, name + cursor,
                                                  part_length);
                    if (status != REXX_VM_STATUS_OK) goto cleanup;
                }
            }
            if (end == name_length) break;
            status = resolved_name_append(&buffer, &length, &capacity, ".", 1u);
            if (status != REXX_VM_STATUS_OK) goto cleanup;
            cursor = end + 1u;
        }
    }
    if (!subsidiary_name_valid(buffer, length)) {
        status = REXX_VM_STATUS_INVALID_EXPOSE_LIST;
        goto cleanup;
    }
    *resolved_name = buffer;
    *resolved_length = length;
    return REXX_VM_STATUS_OK;

cleanup:
    vm_free(buffer);
    return status;
}

static RexxVMStatus execute_expose_subsidiary(
    RexxVM *vm, RexxVMEnvironment *environment, const RexxSymbol *symbol)
{
    RexxValue list_value = REXX_VALUE_INIT;
    char *list = NULL;
    char *resolved_list_name = NULL;
    size_t length = 0u;
    size_t resolved_list_length = 0u;
    size_t cursor = 0u;
    RexxVMStatus status;

    /* The controlling variable is exposed before its value is read. */
    status = resolve_expose_name(vm, environment, symbol->name,
                                 symbol->length, &resolved_list_name,
                                 &resolved_list_length);
    if (status == REXX_VM_STATUS_OK)
        status = environment_add_expose_name(
            vm, environment, resolved_list_name, resolved_list_length);
    if (status == REXX_VM_STATUS_OK) {
        RexxSymbol resolved_symbol;
        size_t dot = 0u;
        while (dot < resolved_list_length &&
               resolved_list_name[dot] != '.') ++dot;
        resolved_symbol.name = resolved_list_name;
        resolved_symbol.length = resolved_list_length;
        resolved_symbol.kind = dot < resolved_list_length
            ? REXX_SYMBOL_STEM : REXX_SYMBOL_VARIABLE;
        status = load_variable_value(vm, environment, &resolved_symbol,
                                     &list_value);
    }
    if (status == REXX_VM_STATUS_OK) {
        if (list_value.type == REXX_VALUE_STRING &&
            list_value.as.string.length >
                REXX_VM_MAX_SUBSIDIARY_LIST_LENGTH)
            status = REXX_VM_STATUS_RESOURCE_LIMIT;
        else
            status = value_string(&list_value, &list, &length);
    }
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    if (length > REXX_VM_MAX_SUBSIDIARY_LIST_LENGTH) {
        status = REXX_VM_STATUS_RESOURCE_LIMIT;
        goto cleanup;
    }

    while (cursor < length) {
        size_t start;
        while (cursor < length && list[cursor] == ' ') ++cursor;
        if (cursor == length) break;
        start = cursor;
        while (cursor < length && list[cursor] != ' ') ++cursor;
        if (!subsidiary_name_valid(list + start, cursor - start)) {
            status = REXX_VM_STATUS_INVALID_EXPOSE_LIST;
            goto cleanup;
        }
        {
            char *resolved_name = NULL;
            size_t resolved_length = 0u;
            status = resolve_expose_name(vm, environment, list + start,
                                         cursor - start, &resolved_name,
                                         &resolved_length);
            if (status == REXX_VM_STATUS_OK)
                status = environment_add_expose_name(
                    vm, environment, resolved_name, resolved_length);
            vm_free(resolved_name);
        }
        if (status != REXX_VM_STATUS_OK) goto cleanup;
    }

cleanup:
    vm_free(list);
    vm_free(resolved_list_name);
    rexx_value_clear(&list_value);
    return status;
}

static RexxVMStatus environment_set_resolved(
    RexxVM *vm, RexxVMEnvironment *environment,
    const char *name, size_t name_length, int stem, const RexxValue *value)
{
    RexxVMExpose *expose = environment_find_expose(environment, name,
                                                    name_length);
    RexxVMStatus status;
    if (expose != NULL) {
        if (environment->parent == NULL) return REXX_VM_STATUS_INVALID_BYTECODE;
        return environment_set_resolved(vm, environment->parent, name,
                                        name_length, stem, value);
    }
    if (stem) status = stem_set(vm, environment, name, name_length, value);
    else {
        status = table_set(&environment->locals, &environment->local_count,
                           &environment->local_capacity, name, name_length,
                           value);
        environment_sync_root(vm, environment);
    }
    return status;
}

static RexxVMStatus environment_set(RexxVM *vm, RexxVMEnvironment *environment,
                                    const char *name, size_t name_length,
                                    int stem, const RexxValue *value)
{
    char *resolved_name = NULL;
    size_t resolved_length = 0u;
    RexxVMStatus status;
    if (name != NULL && name_length != 0u &&
        !expose_name_has_dynamic_tail(name, name_length))
        return environment_set_resolved(
            vm, environment, name, name_length,
            resolved_name_uses_stem_table(name, name_length), value);
    status = resolve_expose_name(
        vm, environment, name, name_length, &resolved_name, &resolved_length);
    if (status == REXX_VM_STATUS_OK)
        status = environment_set_resolved(vm, environment, resolved_name,
                                          resolved_length,
                                          resolved_name_uses_stem_table(
                                              resolved_name, resolved_length),
                                          value);
    vm_free(resolved_name);
    (void)stem;
    return status;
}

static RexxVMStatus environment_drop_resolved(
    RexxVM *vm, RexxVMEnvironment *environment,
    const char *name, size_t name_length, int stem)
{
    RexxVMExpose *expose = environment_find_expose(environment, name,
                                                    name_length);
    size_t index;
    if (expose != NULL) {
        if (environment->parent == NULL) return REXX_VM_STATUS_INVALID_BYTECODE;
        return environment_drop_resolved(vm, environment->parent, name,
                                         name_length, stem);
    }
    if (!stem) {
        int found = find_entry(environment->locals, environment->local_count,
                               name, name_length);
        if (found >= 0) {
            size_t at = (size_t)found;
            vm_free(environment->locals[at].name);
            rexx_value_clear(&environment->locals[at].value);
            if (at + 1u < environment->local_count)
                memmove(&environment->locals[at], &environment->locals[at + 1u],
                        (environment->local_count - at - 1u) *
                            sizeof(*environment->locals));
            --environment->local_count;
        }
        environment_sync_root(vm, environment);
        return REXX_VM_STATUS_OK;
    }
    {
        size_t dot = 0u;
        int prefix_drop;
        while (dot < name_length && name[dot] != '.') ++dot;
        if (dot == 0u || dot == name_length) return REXX_VM_STATUS_INVALID_BYTECODE;
        prefix_drop = dot + 1u == name_length;
        index = 0u;
        while (index < environment->stem_count) {
            RexxVMStemEntry *entry = &environment->stems[index];
            int match = prefix_drop
                ? ascii_equal(entry->stem, entry->stem_length, name, dot + 1u)
                : ascii_equal(entry->stem, entry->stem_length, name, dot + 1u) &&
                  ascii_equal(entry->tail, entry->tail_length,
                              name + dot + 1u, name_length - dot - 1u);
            if (match) {
                vm_free(entry->stem);
                vm_free(entry->tail);
                rexx_value_clear(&entry->value);
                if (index + 1u < environment->stem_count)
                    memmove(entry, entry + 1u,
                            (environment->stem_count - index - 1u) *
                                sizeof(*environment->stems));
                --environment->stem_count;
            } else ++index;
        }
    }
    environment_sync_root(vm, environment);
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus environment_drop(RexxVM *vm,
                                     RexxVMEnvironment *environment,
                                     const char *name, size_t name_length,
                                     int stem)
{
    char *resolved_name = NULL;
    size_t resolved_length = 0u;
    RexxVMStatus status;
    if (name != NULL && name_length != 0u &&
        !expose_name_has_dynamic_tail(name, name_length))
        return environment_drop_resolved(
            vm, environment, name, name_length,
            resolved_name_uses_stem_table(name, name_length));
    status = resolve_expose_name(
        vm, environment, name, name_length, &resolved_name, &resolved_length);
    if (status == REXX_VM_STATUS_OK)
        status = environment_drop_resolved(
            vm, environment, resolved_name, resolved_length,
            resolved_name_uses_stem_table(resolved_name, resolved_length));
    vm_free(resolved_name);
    (void)stem;
    return status;
}

static RexxVMStatus execute_drop_subsidiary(RexxVM *vm,
                                            const RexxSymbol *symbol)
{
    RexxVMEnvironment *environment =
        (RexxVMEnvironment *)vm->current_environment;
    RexxValue list_value = REXX_VALUE_INIT;
    char *resolved_list_name = NULL;
    char *list = NULL;
    size_t resolved_list_length = 0u;
    size_t list_length = 0u;
    size_t cursor = 0u;
    RexxSymbol resolved_symbol;
    RexxVMStatus status;

    status = resolve_expose_name(vm, environment, symbol->name,
                                 symbol->length, &resolved_list_name,
                                 &resolved_list_length);
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    resolved_symbol.name = resolved_list_name;
    resolved_symbol.length = resolved_list_length;
    resolved_symbol.kind = resolved_name_uses_stem_table(
        resolved_list_name, resolved_list_length)
        ? REXX_SYMBOL_STEM : REXX_SYMBOL_VARIABLE;
    status = load_variable_value(vm, environment, &resolved_symbol,
                                 &list_value);
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    if (list_value.type == REXX_VALUE_STRING &&
        list_value.as.string.length > REXX_VM_MAX_SUBSIDIARY_LIST_LENGTH) {
        status = REXX_VM_STATUS_RESOURCE_LIMIT;
        goto cleanup;
    }
    status = value_string(&list_value, &list, &list_length);
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    if (list_length > REXX_VM_MAX_SUBSIDIARY_LIST_LENGTH) {
        status = REXX_VM_STATUS_RESOURCE_LIMIT;
        goto cleanup;
    }

    while (cursor < list_length) {
        size_t start;
        while (cursor < list_length &&
               ascii_space((unsigned char)list[cursor])) ++cursor;
        if (cursor == list_length) break;
        start = cursor;
        while (cursor < list_length &&
               !ascii_space((unsigned char)list[cursor])) ++cursor;
        if (!subsidiary_name_valid(list + start, cursor - start)) {
            status = REXX_VM_STATUS_INVALID_EXPOSE_LIST;
            goto cleanup;
        }
        status = environment_drop(
            vm, environment, list + start, cursor - start,
            resolved_name_uses_stem_table(list + start, cursor - start));
        if (status != REXX_VM_STATUS_OK) goto cleanup;
    }

cleanup:
    vm_free(resolved_list_name);
    vm_free(list);
    rexx_value_clear(&list_value);
    return status;
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
    vm->interpret_compile_budget =
        REXX_VM_DEFAULT_INTERPRET_COMPILE_BUDGET;
    vm->interpret_compile_remaining = vm->interpret_compile_budget;
    vm->initialized = 1;
    vm->root_environment = environment_new(vm, NULL, 1);
    if (vm->root_environment == NULL) {
        rexx_vm_free(vm);
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    }
    vm->current_environment = vm->root_environment;
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

RexxVMStatus rexx_vm_set_argument_string(RexxVM *vm,
                                         const char *bytes,
                                         size_t length)
{
    char *copy;
    if (vm == NULL || !vm->initialized ||
        (bytes == NULL && length != 0u) || length == SIZE_MAX)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (length > REXX_VM_MAX_PARSE_SOURCE_LENGTH)
        return vm_fail(vm, REXX_VM_STATUS_RESOURCE_LIMIT, vm->pc);
    copy = (char *)vm_malloc(length + 1u);
    if (copy == NULL)
        return vm_fail(vm, REXX_VM_STATUS_OUT_OF_MEMORY, vm->pc);
    if (length != 0u) memcpy(copy, bytes, length);
    copy[length] = '\0';
    vm_free(vm->argument_string);
    vm->argument_string = copy;
    vm->argument_string_length = length;
    return REXX_VM_STATUS_OK;
}

void rexx_vm_free(RexxVM *vm)
{
    size_t i, live;
    RexxVMCallFrame *frames;
    RexxVMEnvironment *root;
    if (vm == NULL) return;
    live = vm->sp < vm->stack_capacity ? vm->sp : vm->stack_capacity;
    for (i = 0; vm->stack != NULL && i < live; ++i) rexx_value_clear(&vm->stack[i]);
    for (i = 0; vm->globals != NULL && i < vm->global_count &&
                i < vm->global_capacity; ++i) {
        vm_free(vm->globals[i].name);
        rexx_value_clear(&vm->globals[i].value);
    }
    frames = (RexxVMCallFrame *)vm->routine_frames;
    for (i = 0u; frames != NULL && i < vm->routine_depth; ++i) {
        size_t argument;
        for (argument = 0u; argument < frames[i].argument_count; ++argument)
            vm_free(frames[i].arguments[argument].bytes);
        vm_free(frames[i].arguments);
        vm_free(frames[i].address_environment);
        free_condition_snapshot(&frames[i]);
        if (frames[i].owns_environment)
            environment_free(frames[i].callee_environment);
    }
    root = (RexxVMEnvironment *)vm->root_environment;
    environment_free(root);
    rexx_value_clear(&vm->exit_value);
    vm_free(vm->stack);
    vm_free(vm->globals);
    vm_free(vm->routine_frames);
    {
        RexxVMInterpretFrame *interpret_frames =
            (RexxVMInterpretFrame *)vm->interpret_frames;
        for (i = 0u; interpret_frames != NULL &&
                    i < vm->interpret_depth; ++i) {
            rexx_bytecode_chunk_free(interpret_frames[i].chunk);
            vm_free(interpret_frames[i].source);
        }
    }
    vm_free(vm->interpret_frames);
    vm_free(vm->do_frames);
    vm_free(vm->address_environment);
    vm_free(vm->argument_string);
    vm_free(vm->condition_description);
    free_condition_target_names(vm->condition_target_names);
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
        status = environment_set(vm,
                                 (RexxVMEnvironment *)vm->current_environment,
                                 "RC", 2u, 0, &rc_value);
    }
    rexx_value_clear(&rc_value);
    return status;
}

RexxVMStatus rexx_vm_set_local(RexxVM *vm, const char *name, size_t name_length,
                               const RexxValue *value)
{
    RexxVMStatus result;
    if (vm == NULL || !vm->initialized) return REXX_VM_STATUS_INVALID_ARGUMENT;
    result = environment_set(vm,
                             (RexxVMEnvironment *)vm->current_environment,
                             name, name_length, 0, value);
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

static RexxVMStatus environment_load(RexxVM *vm,
                                     RexxVMEnvironment *environment,
                                     const RexxSymbol *symbol,
                                     int stem,
                                     RexxValue *value)
{
    RexxVMExpose *expose = environment_find_expose(
        environment, symbol->name, symbol->length);
    int index;
    size_t i;
    if (expose != NULL) {
        if (environment->parent == NULL) return REXX_VM_STATUS_INVALID_BYTECODE;
        return environment_load(vm, environment->parent, symbol, stem, value);
    }
    if (stem) {
        size_t dot = 0u;
        size_t tail_length;
        while (dot < symbol->length && symbol->name[dot] != '.') ++dot;
        if (dot == 0u || dot == symbol->length)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        tail_length = symbol->length - dot - 1u;
        for (i = 0u; i < environment->stem_count; ++i) {
            RexxVMStemEntry *entry = &environment->stems[i];
            if (entry->stem_length == dot + 1u &&
                entry->tail_length == tail_length &&
                ascii_equal(entry->stem, entry->stem_length,
                            symbol->name, dot + 1u) &&
                ascii_equal(entry->tail, entry->tail_length,
                            symbol->name + dot + 1u, tail_length))
                return rexx_value_copy(value, &entry->value);
        }
        /* An empty stem tail supplies the default for every compound tail. */
        for (i = 0u; i < environment->stem_count; ++i) {
            RexxVMStemEntry *entry = &environment->stems[i];
            if (entry->stem_length == dot + 1u && entry->tail_length == 0u &&
                ascii_equal(entry->stem, entry->stem_length,
                            symbol->name, dot + 1u))
                return rexx_value_copy(value, &entry->value);
        }
        if (environment->transparent_parent && environment->parent != NULL)
            return environment_load(vm, environment->parent, symbol, stem,
                                    value);
    } else {
        index = find_entry(environment->locals, environment->local_count,
                           symbol->name, symbol->length);
        if (index >= 0)
            return rexx_value_copy(value, &environment->locals[index].value);
        if (environment->root) {
            index = find_entry(vm->globals, vm->global_count,
                               symbol->name, symbol->length);
            if (index >= 0)
                return rexx_value_copy(value, &vm->globals[index].value);
        }
        if (environment->transparent_parent && environment->parent != NULL)
            return environment_load(vm, environment->parent, symbol, stem,
                                    value);
    }
    return rexx_value_set_string(value, symbol->name, symbol->length);
}

static RexxVMStatus environment_load_variable(
    RexxVM *vm, RexxVMEnvironment *environment,
    const RexxSymbol *symbol, RexxValue *value)
{
    char *resolved_name = NULL;
    size_t resolved_length = 0u;
    RexxSymbol resolved_symbol;
    RexxVMStatus status;
    if (!expose_name_has_dynamic_tail(symbol->name, symbol->length))
        return environment_load(vm, environment, symbol,
                                resolved_name_uses_stem_table(
                                    symbol->name, symbol->length), value);
    status = resolve_expose_name(
        vm, environment, symbol->name, symbol->length,
        &resolved_name, &resolved_length);
    if (status == REXX_VM_STATUS_OK) {
        resolved_symbol.name = resolved_name;
        resolved_symbol.length = resolved_length;
        resolved_symbol.kind = resolved_name_uses_stem_table(
            resolved_name, resolved_length)
            ? REXX_SYMBOL_STEM : REXX_SYMBOL_VARIABLE;
        status = environment_load(vm, environment, &resolved_symbol,
                                  resolved_symbol.kind == REXX_SYMBOL_STEM,
                                  value);
    }
    vm_free(resolved_name);
    return status;
}

static int environment_has_variable(RexxVM *vm,
                                    RexxVMEnvironment *environment,
                                    const char *name,
                                    size_t name_length)
{
    RexxVMExpose *expose = environment_find_expose(
        environment, name, name_length);
    size_t dot = 0u;
    size_t i;
    int index;
    if (expose != NULL)
        return environment->parent != NULL &&
               environment_has_variable(vm, environment->parent, name,
                                        name_length);
    if (name[name_length - 1u] == '.') {
        for (i = 0u; i < environment->stem_count; ++i) {
            const RexxVMStemEntry *entry = &environment->stems[i];
            if (entry->stem_length == name_length &&
                entry->tail_length == 0u &&
                ascii_equal(entry->stem, entry->stem_length,
                            name, name_length))
                return 1;
        }
        if (environment->transparent_parent && environment->parent != NULL)
            return environment_has_variable(vm, environment->parent, name,
                                            name_length);
        return 0;
    }
    while (dot < name_length && name[dot] != '.') ++dot;
    if (dot < name_length) {
        size_t stem_length = dot + 1u;
        size_t tail_length = name_length - stem_length;
        int has_default = 0;
        for (i = 0u; i < environment->stem_count; ++i) {
            const RexxVMStemEntry *entry = &environment->stems[i];
            if (entry->stem_length != stem_length ||
                !ascii_equal(entry->stem, entry->stem_length,
                             name, stem_length))
                continue;
            if (entry->tail_length == tail_length &&
                ascii_equal(entry->tail, entry->tail_length,
                            name + stem_length, tail_length))
                return 1;
            if (entry->tail_length == 0u) has_default = 1;
        }
        if (has_default) return 1;
        if (environment->transparent_parent && environment->parent != NULL)
            return environment_has_variable(vm, environment->parent, name,
                                            name_length);
        return 0;
    }
    index = find_entry(environment->locals, environment->local_count,
                       name, name_length);
    if (index >= 0) return 1;
    if (environment->root) {
        index = find_entry(vm->globals, vm->global_count, name, name_length);
        if (index >= 0) return 1;
    }
    if (environment->transparent_parent && environment->parent != NULL)
        return environment_has_variable(vm, environment->parent, name,
                                        name_length);
    return 0;
}

static RexxVMStatus load_variable_value(RexxVM *vm,
                                        RexxVMEnvironment *environment,
                                        const RexxSymbol *symbol,
                                        RexxValue *value)
{
    const char *resolved_name;
    size_t resolved_length;
    char *owned_resolved_name = NULL;
    RexxVMStatus result;
    if (vm == NULL || environment == NULL || symbol == NULL ||
        symbol->name == NULL || value == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    resolved_name = symbol->name;
    resolved_length = symbol->length;
    if (vm->condition_states[REXX_CONDITION_NOVALUE] ==
        REXX_CONDITION_STATE_ON &&
        expose_name_has_dynamic_tail(symbol->name, symbol->length)) {
        result = resolve_expose_name(vm, environment, symbol->name,
                                     symbol->length, &owned_resolved_name,
                                     &resolved_length);
        if (result != REXX_VM_STATUS_OK) return result;
        resolved_name = owned_resolved_name;
    }
    if (vm->condition_states[REXX_CONDITION_NOVALUE] ==
            REXX_CONDITION_STATE_ON &&
        !environment_has_variable(vm, environment, resolved_name,
                                  resolved_length)) {
        int transferred = 0;
        result = raise_condition(vm, (const RexxBytecodeChunk *)vm->active_chunk,
                                 REXX_CONDITION_NOVALUE,
                                 resolved_name, resolved_length,
                                 vm->current_source_line, 0, &transferred);
        vm_free(owned_resolved_name);
        if (result != REXX_VM_STATUS_OK || transferred) return result;
    } else {
        vm_free(owned_resolved_name);
    }
    return environment_load_variable(vm, environment, symbol, value);
}

static RexxVMEnvironment *variable_access_environment(RexxVM *vm)
{
    if (vm->active_parse_overlay != NULL)
        return (RexxVMEnvironment *)vm->active_parse_overlay;
    return (RexxVMEnvironment *)vm->current_environment;
}

static RexxVMStatus load_variable(RexxVM *vm, const RexxSymbol *symbol, int stem)
{
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus result = load_variable_value(
        vm, variable_access_environment(vm), symbol, &value);
    if (result == REXX_VM_STATUS_OK) result = push_move(vm, &value);
    rexx_value_clear(&value);
    (void)stem;
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

static RexxVMStatus parse_get_variable_symbol(
    const RexxBytecodeChunk *chunk,
    uint32_t operand,
    const RexxSymbol **symbol)
{
    RexxSymbolKind kind;
    if (chunk->symbols == NULL || operand >= chunk->symbol_count)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    kind = chunk->symbols[operand].kind;
    if (kind != REXX_SYMBOL_VARIABLE && kind != REXX_SYMBOL_STEM)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    return get_symbol(chunk, operand, kind, symbol);
}

static RexxVMStatus parse_load_variable(RexxVM *vm,
                                        const RexxBytecodeChunk *chunk,
                                        uint32_t operand,
                                        RexxValue *value)
{
    const RexxSymbol *symbol = NULL;
    RexxVMStatus status = parse_get_variable_symbol(chunk, operand, &symbol);
    if (status == REXX_VM_STATUS_OK)
        status = load_variable(vm, symbol, symbol->kind == REXX_SYMBOL_STEM);
    if (status == REXX_VM_STATUS_OK) status = pop_move(vm, value);
    return status;
}

static RexxVMStatus parse_store_variable(RexxVM *vm,
                                         const RexxBytecodeChunk *chunk,
                                         uint32_t operand,
                                         const char *bytes,
                                         size_t length)
{
    const RexxSymbol *symbol = NULL;
    RexxVMEnvironment *environment = variable_access_environment(vm);
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus status = parse_get_variable_symbol(chunk, operand, &symbol);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&value, bytes, length);
    if (status == REXX_VM_STATUS_OK) {
        status = environment_set(vm, environment,
                                 symbol->name, symbol->length,
                                 symbol->kind == REXX_SYMBOL_STEM, &value);
    }
    rexx_value_clear(&value);
    return status;
}

static RexxVMStatus parse_pattern_string(RexxVM *vm,
                                         const RexxBytecodeChunk *chunk,
                                         const RexxParseTemplateItem *item,
                                         char **bytes,
                                         size_t *length)
{
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus status;
    if (item->dynamic) {
        status = parse_load_variable(vm, chunk, item->operand, &value);
    } else {
        if (chunk->constants == NULL || item->operand >= chunk->constant_count)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        status = decode_constant(&chunk->constants[item->operand], &value);
    }
    if (status == REXX_VM_STATUS_OK)
        status = value_string(&value, bytes, length);
    rexx_value_clear(&value);
    return status;
}

static RexxVMStatus parse_pattern_position(RexxVM *vm,
                                           const RexxBytecodeChunk *chunk,
                                           const RexxParseTemplateItem *item,
                                           uint32_t *position)
{
    RexxValue value = REXX_VALUE_INIT;
    RexxDecimal decimal;
    RexxNumericStatus numeric_status;
    RexxVMStatus status;

    if (item->dynamic) {
        status = parse_load_variable(vm, chunk, item->operand, &value);
        if (status == REXX_VM_STATUS_OK) status = value_decimal(&value, &decimal);
        rexx_value_clear(&value);
        if (status != REXX_VM_STATUS_OK) return status;
        numeric_status = rexx_decimal_to_uint32(&decimal, position);
        if (numeric_status == REXX_NUMERIC_RESOURCE_LIMIT)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        return numeric_status == REXX_NUMERIC_OK
            ? REXX_VM_STATUS_OK : REXX_VM_STATUS_NUMERIC_ERROR;
    }
    *position = item->position;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus parse_find_pattern(const char *source,
                                       size_t source_length,
                                       size_t start,
                                       const char *pattern,
                                       size_t pattern_length,
                                       size_t *work_remaining,
                                       size_t *match_out)
{
    size_t position;
    size_t last_position;
    if (start > source_length) start = source_length;
    if (work_remaining == NULL || match_out == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (pattern_length == 0u) {
        *match_out = source_length;
        return REXX_VM_STATUS_OK;
    }
    if (pattern_length > source_length - start) {
        *match_out = source_length;
        return REXX_VM_STATUS_OK;
    }
    last_position = source_length - pattern_length;
    for (position = start; position <= last_position; ++position) {
        size_t pattern_index;
        for (pattern_index = 0u; pattern_index < pattern_length;
             ++pattern_index) {
            if (*work_remaining == 0u)
                return REXX_VM_STATUS_RESOURCE_LIMIT;
            --*work_remaining;
            if (source[position + pattern_index] != pattern[pattern_index])
                break;
        }
        if (pattern_index == pattern_length) {
            *match_out = position;
            return REXX_VM_STATUS_OK;
        }
    }
    *match_out = source_length;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus parse_assign_fields(
    RexxVM *vm,
    const RexxBytecodeChunk *chunk,
    const RexxParseTemplateItem *items,
    size_t item_start,
    size_t item_end,
    const char *source,
    size_t source_length,
    size_t begin,
    size_t end)
{
    size_t item_index;
    size_t field_count = 0u;
    size_t cursor;
    size_t field_number = 0u;

    if (begin > source_length) begin = source_length;
    if (end > source_length) end = source_length;
    if (end < begin) end = begin;
    for (item_index = item_start; item_index < item_end; ++item_index) {
        if (items[item_index].kind == REXX_PARSE_ITEM_VARIABLE ||
            items[item_index].kind == REXX_PARSE_ITEM_PLACEHOLDER)
            ++field_count;
    }
    if (field_count == 0u) return REXX_VM_STATUS_OK;

    cursor = begin;
    for (item_index = item_start; item_index < item_end; ++item_index) {
        const RexxParseTemplateItem *item = &items[item_index];
        size_t value_start;
        size_t value_end;
        RexxVMStatus status;
        if (item->kind != REXX_PARSE_ITEM_VARIABLE &&
            item->kind != REXX_PARSE_ITEM_PLACEHOLDER)
            return REXX_VM_STATUS_INVALID_BYTECODE;

        if (field_count == 1u) {
            value_start = begin;
            value_end = end;
        } else if (field_number + 1u == field_count) {
            if (cursor < end && source[cursor] == ' ') ++cursor;
            value_start = cursor;
            value_end = end;
        } else {
            while (cursor < end && source[cursor] == ' ') ++cursor;
            value_start = cursor;
            while (cursor < end && source[cursor] != ' ') ++cursor;
            value_end = cursor;
        }

        if (item->kind == REXX_PARSE_ITEM_VARIABLE) {
            status = parse_store_variable(vm, chunk, item->operand,
                                          source + value_start,
                                          value_end - value_start);
            if (status != REXX_VM_STATUS_OK) return status;
        }
        ++field_number;
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus execute_parse_template(RexxVM *vm,
                                           const RexxBytecodeChunk *chunk,
                                           const RexxParseTemplate *template_part,
                                           const char *source,
                                           size_t source_length,
                                           size_t *parse_work_remaining)
{
    size_t first = template_part->first_item;
    size_t count = template_part->item_count;
    size_t field_start = first;
    size_t section_start = 0u;
    size_t cursor = 0u;
    size_t last_match = 0u;
    int last_match_was_string = 0;
    size_t index;

    if (first > chunk->parse_item_count ||
        count > chunk->parse_item_count - first)
        return REXX_VM_STATUS_INVALID_BYTECODE;

    for (index = first; index < first + count; ++index) {
        const RexxParseTemplateItem *item = &chunk->parse_items[index];
        size_t boundary;
        RexxVMStatus status;

        if (item->dynamic > 1u || item->reserved != 0u)
            return REXX_VM_STATUS_INVALID_BYTECODE;

        if (item->kind == REXX_PARSE_ITEM_VARIABLE ||
            item->kind == REXX_PARSE_ITEM_PLACEHOLDER)
            continue;

        if (item->kind == REXX_PARSE_ITEM_STRING_PATTERN) {
            char *pattern = NULL;
            size_t pattern_length = 0u;
            size_t match;
            status = parse_pattern_string(vm, chunk, item,
                                          &pattern, &pattern_length);
            if (status != REXX_VM_STATUS_OK) {
                vm_free(pattern);
                return status;
            }
            status = parse_find_pattern(source, source_length, cursor,
                                        pattern, pattern_length,
                                        parse_work_remaining, &match);
            vm_free(pattern);
            if (status != REXX_VM_STATUS_OK) return status;
            boundary = match;
            status = parse_assign_fields(vm, chunk, chunk->parse_items,
                                         field_start, index, source,
                                         source_length, section_start,
                                         boundary);
            if (status != REXX_VM_STATUS_OK) return status;
            if (match < source_length || pattern_length == 0u) {
                size_t matched_length = pattern_length;
                if (matched_length > source_length - match)
                    matched_length = source_length - match;
                cursor = match + matched_length;
                last_match = match;
                last_match_was_string = 1;
            } else {
                cursor = source_length;
                last_match = source_length;
                last_match_was_string = 0;
            }
            section_start = cursor;
        } else if (item->kind == REXX_PARSE_ITEM_POSITION_PATTERN) {
            uint32_t position;
            size_t target;
            size_t split_start = section_start;
            size_t boundary;
            status = parse_pattern_position(vm, chunk, item, &position);
            if (status != REXX_VM_STATUS_OK) return status;
            if (item->position_kind != REXX_PARSE_POSITION_ABSOLUTE &&
                last_match_was_string && field_start < index)
                split_start = last_match;
            if (item->position_kind == REXX_PARSE_POSITION_ABSOLUTE) {
                target = position == 0u ? 0u : (size_t)position - 1u;
                if (target > source_length) target = source_length;
            } else if (item->position_kind == REXX_PARSE_POSITION_FORWARD) {
                size_t base = last_match > source_length
                    ? source_length : last_match;
                target = (size_t)position > source_length - base
                    ? source_length : base + (size_t)position;
            } else if (item->position_kind == REXX_PARSE_POSITION_BACKWARD) {
                size_t base = last_match > source_length
                    ? source_length : last_match;
                target = (size_t)position > base
                    ? 0u : base - (size_t)position;
            } else {
                return REXX_VM_STATUS_INVALID_BYTECODE;
            }
            boundary = target <= split_start ? source_length : target;
            status = parse_assign_fields(vm, chunk, chunk->parse_items,
                                         field_start, index, source,
                                         source_length, split_start,
                                         boundary);
            if (status != REXX_VM_STATUS_OK) return status;
            cursor = target;
            last_match = target;
            last_match_was_string = 0;
            section_start = target;
        } else {
            return REXX_VM_STATUS_INVALID_BYTECODE;
        }
        field_start = index + 1u;
    }

    return parse_assign_fields(vm, chunk, chunk->parse_items,
                               field_start, first + count, source,
                               source_length, section_start, source_length);
}

static RexxVMStatus commit_parse_overlay(
    RexxVM *vm, RexxVMEnvironment *overlay,
    RexxVMEnvironment *destination)
{
    size_t index;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    for (index = 0u; index < overlay->local_count; ++index) {
        RexxVMEntry *entry = &overlay->locals[index];
        status = environment_set_resolved(
            vm, destination, entry->name, entry->name_length, 0,
            &entry->value);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    for (index = 0u; index < overlay->stem_count; ++index) {
        RexxVMStemEntry *entry = &overlay->stems[index];
        size_t name_length;
        char *name;
        if (entry->stem_length > REXX_VM_MAX_NAME_LENGTH -
                                     entry->tail_length ||
            entry->stem_length + entry->tail_length == SIZE_MAX)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        name_length = entry->stem_length + entry->tail_length;
        name = (char *)vm_malloc(name_length + 1u);
        if (name == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
        memcpy(name, entry->stem, entry->stem_length);
        if (entry->tail_length != 0u)
            memcpy(name + entry->stem_length, entry->tail,
                   entry->tail_length);
        name[name_length] = '\0';
        status = environment_set_resolved(vm, destination, name,
                                          name_length, 1, &entry->value);
        vm_free(name);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus execute_parse(RexxVM *vm,
                                  const RexxBytecodeChunk *chunk,
                                  uint32_t plan_index)
{
    static const char empty_string[] = "";
    const RexxParsePlan *plan;
    RexxVMEnvironment *destination = NULL;
    RexxVMEnvironment *overlay = NULL;
    void *previous_overlay;
    RexxValue input = REXX_VALUE_INIT;
    const char *source = empty_string;
    size_t source_length = 0u;
    char *source_copy = NULL;
    size_t parse_work_remaining = REXX_VM_MAX_PARSE_WORK;
    size_t group_index;
    RexxVMStatus status = REXX_VM_STATUS_OK;

    if (chunk->parse_plans == NULL || plan_index >= chunk->parse_plan_count)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    plan = &chunk->parse_plans[plan_index];
    if (plan->source_kind > REXX_PARSE_SOURCE_VALUE ||
        plan->uppercase > 1u ||
        plan->reserved != 0u ||
        plan->first_template > chunk->parse_template_count ||
        plan->template_count >
            chunk->parse_template_count - plan->first_template)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if (plan->source_kind == REXX_PARSE_SOURCE_VAR) {
        const RexxSymbol *source_symbol = NULL;
        if (parse_get_variable_symbol(chunk, plan->source_symbol,
                                      &source_symbol) != REXX_VM_STATUS_OK)
            return REXX_VM_STATUS_INVALID_BYTECODE;
    } else if (plan->source_symbol != REXX_NO_SYMBOL) {
        return REXX_VM_STATUS_INVALID_BYTECODE;
    }

    destination = vm->active_parse_overlay != NULL
        ? (RexxVMEnvironment *)vm->active_parse_overlay
        : (RexxVMEnvironment *)vm->current_environment;
    overlay = environment_new(vm, destination, 0);
    if (overlay == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    overlay->transparent_parent = 1;
    previous_overlay = vm->active_parse_overlay;
    vm->active_parse_overlay = overlay;

    if (plan->source_kind == REXX_PARSE_SOURCE_ARG) {
        RexxVMArgument *arguments = (RexxVMArgument *)vm->current_arguments;
        if (vm->routine_depth != 0u) {
            for (group_index = 0u;
                 group_index < plan->template_count; ++group_index) {
                const RexxParseTemplate *template_part =
                    &chunk->parse_templates[plan->first_template + group_index];
                const char *group_source = empty_string;
                size_t group_length = 0u;
                char *uppercase_copy = NULL;
                size_t index;
                if (group_index < vm->current_argument_count &&
                    arguments[group_index].present) {
                    group_source = arguments[group_index].bytes;
                    group_length = arguments[group_index].length;
                }
                if (group_length > REXX_VM_MAX_PARSE_SOURCE_LENGTH) {
                    status = REXX_VM_STATUS_RESOURCE_LIMIT;
                    break;
                }
                if (plan->uppercase && group_length != 0u) {
                    uppercase_copy = (char *)vm_malloc(group_length + 1u);
                    if (uppercase_copy == NULL) {
                        status = REXX_VM_STATUS_OUT_OF_MEMORY;
                        break;
                    }
                    memcpy(uppercase_copy, group_source, group_length);
                    uppercase_copy[group_length] = '\0';
                    for (index = 0u; index < group_length; ++index)
                        uppercase_copy[index] = (char)ascii_upper(
                            (unsigned char)uppercase_copy[index]);
                    group_source = uppercase_copy;
                }
                status = execute_parse_template(
                    vm, chunk, template_part, group_source, group_length,
                    &parse_work_remaining);
                vm_free(uppercase_copy);
                if (status != REXX_VM_STATUS_OK) break;
            }
            if (status == REXX_VM_STATUS_OK)
                status = commit_parse_overlay(vm, overlay, destination);
            goto cleanup;
        }
        source = vm->argument_string != NULL ? vm->argument_string
                                             : empty_string;
        source_length = vm->argument_string_length;
    } else if (plan->source_kind == REXX_PARSE_SOURCE_VAR) {
        const RexxSymbol *symbol = NULL;
        status = parse_get_variable_symbol(chunk, plan->source_symbol,
                                           &symbol);
        if (status == REXX_VM_STATUS_OK)
            status = load_variable(vm, symbol,
                                   symbol->kind == REXX_SYMBOL_STEM);
        if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &input);
        if (status == REXX_VM_STATUS_OK)
            status = value_string(&input, &source_copy, &source_length);
        if (status == REXX_VM_STATUS_OK) source = source_copy;
    } else {
        status = pop_move(vm, &input);
        if (status == REXX_VM_STATUS_OK)
            status = value_string(&input, &source_copy, &source_length);
        if (status == REXX_VM_STATUS_OK) source = source_copy;
    }

    if (status == REXX_VM_STATUS_OK &&
        source_length > REXX_VM_MAX_PARSE_SOURCE_LENGTH)
        status = REXX_VM_STATUS_RESOURCE_LIMIT;
    if (status == REXX_VM_STATUS_OK && source_copy == NULL) {
        if (source_length == SIZE_MAX) {
            status = REXX_VM_STATUS_RESOURCE_LIMIT;
        } else {
            source_copy = (char *)vm_malloc(source_length + 1u);
            if (source_copy == NULL) {
                status = REXX_VM_STATUS_OUT_OF_MEMORY;
            } else {
                if (source_length != 0u)
                    memcpy(source_copy, source, source_length);
                source_copy[source_length] = '\0';
                source = source_copy;
            }
        }
    }

    if (status == REXX_VM_STATUS_OK && plan->uppercase) {
        size_t index;
        for (index = 0u; index < source_length; ++index)
            source_copy[index] = (char)ascii_upper(
                (unsigned char)source_copy[index]);
    }

    for (group_index = 0u;
         status == REXX_VM_STATUS_OK && group_index < plan->template_count;
         ++group_index) {
        const RexxParseTemplate *template_part =
            &chunk->parse_templates[plan->first_template + group_index];
        const char *group_source = group_index == 0u
            ? source : empty_string;
        size_t group_source_length = group_index == 0u ? source_length : 0u;
        status = execute_parse_template(vm, chunk, template_part,
                                        group_source,
                                        group_source_length,
                                        &parse_work_remaining);
    }

    if (status == REXX_VM_STATUS_OK)
        status = commit_parse_overlay(vm, overlay, destination);

cleanup:
    vm->active_parse_overlay = previous_overlay;
    environment_free(overlay);
    vm_free(source_copy);
    rexx_value_clear(&input);
    return status;
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

static RexxVMStatus decimal_value_to_rexx(RexxVM *vm,
                                          const RexxDecimal *decimal,
                                          size_t precision,
                                          RexxValue *result)
{
    char output[REXX_DECIMAL_MAX_DIGITS + 64u];
    RexxDecimalFormat form = vm->numeric_form == REXX_NUMERIC_FORM_ENGINEERING
                                 ? REXX_DECIMAL_FORMAT_ENGINEERING
                                 : REXX_DECIMAL_FORMAT_SCIENTIFIC;
    int length = rexx_decimal_format_with_form(
        output, sizeof(output), decimal, precision, form);
    if (length < 0) return REXX_VM_STATUS_RESOURCE_LIMIT;
    {
        RexxVMStatus status = charge_numeric_work(vm, (size_t)length);
        if (status != REXX_VM_STATUS_OK) return status;
    }
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
        size_t cost = numeric_text_cost(&left) + numeric_text_cost(&right);
        if (opcode == OP_ADD || opcode == OP_SUB || opcode == OP_MULTIPLY) {
            cost += decimal_operation_cost(&a, &b, opcode,
                                           vm->numeric_digits);
        } else {
            cost += a.length + b.length;
        }
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
        case OP_DIVIDE:
            numeric_status = rexx_decimal_divide(
                &a, &b, vm->numeric_digits, &vm->numeric_work_remaining,
                &answer);
            break;
        case OP_INTEGER_DIVIDE:
            numeric_status = rexx_decimal_integer_divide(
                &a, &b, vm->numeric_digits, &vm->numeric_work_remaining,
                &answer);
            break;
        case OP_REMAINDER:
            numeric_status = rexx_decimal_remainder(
                &a, &b, vm->numeric_digits, &vm->numeric_work_remaining,
                &answer);
            break;
        case OP_POWER:
            numeric_status = rexx_decimal_power(
                &a, &b, vm->numeric_digits, &vm->numeric_work_remaining,
                &answer);
            break;
        default:
            status = REXX_VM_STATUS_UNSUPPORTED_OPCODE;
            break;
        }
        if (status == REXX_VM_STATUS_OK &&
            numeric_status != REXX_NUMERIC_OK && b.sign == 0 &&
            (opcode == OP_DIVIDE || opcode == OP_INTEGER_DIVIDE ||
             opcode == OP_REMAINDER))
            vm->syntax_error_number = 42;
        else if (numeric_status != REXX_NUMERIC_OK)
            vm->syntax_error_number = 0;
        if (status == REXX_VM_STATUS_OK)
            status = decimal_status_to_vm(numeric_status);
        if (status == REXX_VM_STATUS_OK)
            status = decimal_value_to_rexx(vm, &answer, vm->numeric_digits,
                                           &result);
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
        status = decimal_value_to_rexx(vm, &number, vm->numeric_digits,
                                       &result);
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
        size_t comparison_precision =
            vm->numeric_digits - vm->numeric_fuzz;
        size_t cost = numeric_text_cost(left) + numeric_text_cost(right) +
                      decimal_operation_cost(&a, &b, OP_SUB,
                                             comparison_precision);
        RexxVMStatus charge_status = charge_numeric_work(vm, cost);
        if (charge_status != REXX_VM_STATUS_OK) return charge_status;
        /* Comparison operands are first represented at the temporary
         * DIGITS-FUZZ precision. The subtraction then uses that same context,
         * as classic REXX does; otherwise exact cancellation in the decimal
         * backing type incorrectly makes FUZZ ineffective. */
        numeric_status = rexx_decimal_round(&a, comparison_precision);
        if (numeric_status == REXX_NUMERIC_OK)
            numeric_status = rexx_decimal_round(&b, comparison_precision);
        if (numeric_status == REXX_NUMERIC_OK)
            numeric_status = rexx_decimal_subtract(
                &a, &b, comparison_precision, &difference);
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

/* Classic REXX logical operands must evaluate numerically to 0 or 1. */
static RexxVMStatus value_logical(const RexxValue *value, int *logical)
{
    RexxDecimal number;
    RexxDecimal one;
    RexxVMStatus status;

    if (value == NULL || logical == NULL)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    status = value_decimal(value, &number);
    if (status != REXX_VM_STATUS_OK) return status;
    if (number.sign == 0) {
        *logical = 0;
        return REXX_VM_STATUS_OK;
    }

    memset(&one, 0, sizeof(one));
    one.digits[0] = '1';
    one.length = 1u;
    one.sign = 1;
    if (rexx_decimal_compare(&number, &one) != 0)
        return REXX_VM_STATUS_NUMERIC_ERROR;
    *logical = 1;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus binary_logical(RexxVM *vm, RexxOpcode opcode)
{
    RexxValue left = REXX_VALUE_INIT;
    RexxValue right = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    size_t work = 0u;
    int left_value = 0;
    int right_value = 0;
    int result_value = 0;

    if (vm == NULL || (opcode != OP_LOGICAL_AND &&
                       opcode != OP_LOGICAL_OR &&
                       opcode != OP_LOGICAL_XOR))
        return REXX_VM_STATUS_INVALID_ARGUMENT;

    status = pop_move(vm, &right);
    if (status == REXX_VM_STATUS_OK) status = pop_move(vm, &left);
    if (status == REXX_VM_STATUS_OK) {
        work = numeric_text_cost(&left);
        if (numeric_text_cost(&right) > SIZE_MAX - work)
            status = REXX_VM_STATUS_RESOURCE_LIMIT;
        else
            work += numeric_text_cost(&right);
    }
    if (status == REXX_VM_STATUS_OK) status = charge_numeric_work(vm, work);
    if (status == REXX_VM_STATUS_OK)
        status = value_logical(&left, &left_value);
    if (status == REXX_VM_STATUS_OK)
        status = value_logical(&right, &right_value);
    if (status == REXX_VM_STATUS_OK) {
        if (opcode == OP_LOGICAL_AND)
            result_value = left_value && right_value;
        else if (opcode == OP_LOGICAL_OR)
            result_value = left_value || right_value;
        else
            result_value = left_value != right_value;
        status = rexx_value_set_boolean(&result, result_value);
    }
    if (status == REXX_VM_STATUS_OK) status = push_move(vm, &result);

    rexx_value_clear(&left);
    rexx_value_clear(&right);
    rexx_value_clear(&result);
    return status;
}

static RexxVMStatus unary_logical_not(RexxVM *vm)
{
    RexxValue operand = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int logical = 0;

    if (vm == NULL) return REXX_VM_STATUS_INVALID_ARGUMENT;

    status = pop_move(vm, &operand);
    if (status == REXX_VM_STATUS_OK)
        status = charge_numeric_work(vm, numeric_text_cost(&operand));
    if (status == REXX_VM_STATUS_OK)
        status = value_logical(&operand, &logical);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_boolean(&result, !logical);
    if (status == REXX_VM_STATUS_OK) status = push_move(vm, &result);

    rexx_value_clear(&operand);
    rexx_value_clear(&result);
    return status;
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
        if (REXX_VM_DEFAULT_NUMERIC_DIGITS <= vm->numeric_fuzz)
            return REXX_VM_STATUS_INVALID_NUMERIC_SETTING;
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

static RexxVMStatus execute_numeric_fuzz(RexxVM *vm,
                                         uint8_t flags,
                                         RexxValue *operand)
{
    RexxDecimal decimal;
    uint32_t fuzz;
    RexxNumericStatus numeric_status;
    RexxVMStatus status;

    if ((flags & (uint8_t)~REXX_INSTRUCTION_HAS_VALUE) != 0u)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if ((flags & REXX_INSTRUCTION_HAS_VALUE) == 0u) {
        vm->numeric_fuzz = 0u;
        return REXX_VM_STATUS_OK;
    }
    status = value_decimal(operand, &decimal);
    if (status != REXX_VM_STATUS_OK)
        return status == REXX_VM_STATUS_RESOURCE_LIMIT
                   ? status : REXX_VM_STATUS_INVALID_NUMERIC_FUZZ;
    numeric_status = rexx_decimal_to_uint32(&decimal, &fuzz);
    if (numeric_status == REXX_NUMERIC_RESOURCE_LIMIT)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    if (numeric_status != REXX_NUMERIC_OK || fuzz >= vm->numeric_digits)
        return REXX_VM_STATUS_INVALID_NUMERIC_FUZZ;
    vm->numeric_fuzz = fuzz;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus execute_numeric_form(RexxVM *vm,
                                         uint8_t flags,
                                         uint32_t form,
                                         RexxValue *operand)
{
    RexxNumericForm selected;

    if ((flags & (uint8_t)~REXX_INSTRUCTION_HAS_VALUE) != 0u)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if ((flags & REXX_INSTRUCTION_HAS_VALUE) == 0u) {
        if (form > (uint32_t)REXX_NUMERIC_FORM_ENGINEERING)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        selected = (RexxNumericForm)form;
    } else {
        if (form != 0u || operand == NULL ||
            operand->type != REXX_VALUE_STRING ||
            (operand->as.string.bytes == NULL &&
             operand->as.string.length != 0u))
            return REXX_VM_STATUS_INVALID_NUMERIC_FORM;
        if (ascii_equal(operand->as.string.bytes,
                        operand->as.string.length, "SCIENTIFIC", 10u)) {
            selected = REXX_NUMERIC_FORM_SCIENTIFIC;
        } else if (ascii_equal(operand->as.string.bytes,
                               operand->as.string.length,
                               "ENGINEERING", 11u)) {
            selected = REXX_NUMERIC_FORM_ENGINEERING;
        } else {
            return REXX_VM_STATUS_INVALID_NUMERIC_FORM;
        }
    }
    vm->numeric_form = selected;
    return REXX_VM_STATUS_OK;
}

static void free_arguments(RexxVMArgument *arguments, size_t count)
{
    size_t index;
    for (index = 0u; arguments != NULL && index < count; ++index)
        vm_free(arguments[index].bytes);
    vm_free(arguments);
}

static RexxVMStatus ensure_routine_frame(RexxVM *vm)
{
    size_t next;
    RexxVMCallFrame *grown;
    if (vm->routine_depth >= REXX_VM_MAX_ROUTINE_DEPTH)
        return REXX_VM_STATUS_ROUTINE_DEPTH_LIMIT;
    if (vm->routine_depth < vm->routine_frame_capacity) return REXX_VM_STATUS_OK;
    next = vm->routine_frame_capacity == 0u ? 4u :
           vm->routine_frame_capacity * 2u;
    if (next > REXX_VM_MAX_ROUTINE_DEPTH) next = REXX_VM_MAX_ROUTINE_DEPTH;
    if (next <= vm->routine_frame_capacity ||
        next > SIZE_MAX / sizeof(*grown)) return REXX_VM_STATUS_RESOURCE_LIMIT;
    grown = (RexxVMCallFrame *)vm_realloc(vm->routine_frames,
                                          next * sizeof(*grown));
    if (grown == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    memset(grown + vm->routine_frame_capacity, 0,
           (next - vm->routine_frame_capacity) * sizeof(*grown));
    vm->routine_frames = grown;
    vm->routine_frame_capacity = next;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus find_internal_label(const RexxBytecodeChunk *chunk,
                                        const char *name, size_t name_length,
                                        int case_insensitive,
                                        size_t *target, int *found)
{
    size_t index;
    *found = 0;
    *target = 0u;
    for (index = 0u; index < chunk->instruction_count; ++index) {
        const RexxInstruction *instruction = &chunk->instructions[index];
        const RexxSymbol *label;
        RexxVMStatus status;
        if (instruction->opcode != OP_LABEL) continue;
        status = get_symbol(chunk, instruction->operand, REXX_SYMBOL_LABEL,
                            &label);
        if (status != REXX_VM_STATUS_OK) return status;
        if ((case_insensitive &&
             ascii_equal(label->name, label->length, name, name_length)) ||
            (!case_insensitive && label->length == name_length &&
             memcmp(label->name, name, name_length) == 0)) {
            *found = 1;
            *target = index + 1u;
            return REXX_VM_STATUS_OK;
        }
    }
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus set_sigl(RexxVM *vm,
                             RexxVMEnvironment *environment,
                             size_t source_line)
{
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus status;
    if (source_line == 0u) return REXX_VM_STATUS_INVALID_BYTECODE;
    status = rexx_value_set_number(&value, (double)source_line);
    if (status == REXX_VM_STATUS_OK)
        status = environment_set(vm, environment, "SIGL", 4u, 0, &value);
    rexx_value_clear(&value);
    return status;
}

static RexxVMStatus execute_arg_builtin(RexxVM *vm,
                                        const RexxValue *arguments,
                                        size_t argument_count,
                                        RexxValue *result)
{
    size_t count;
    uint32_t position = 0u;
    int present = 0;
    const char *bytes = "";
    size_t length = 0u;
    RexxVMArgument *routine_arguments =
        (RexxVMArgument *)vm->current_arguments;
    if (argument_count == 0u) {
        if (vm->routine_depth != 0u) count = vm->current_argument_count;
        else count = vm->argument_string_length != 0u ? 1u : 0u;
        if (count > UINT32_MAX) return REXX_VM_STATUS_RESOURCE_LIMIT;
        return value_set_uint32(result, (uint32_t)count);
    }
    if (argument_count > 2u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    {
        RexxDecimal decimal;
        RexxVMStatus decimal_status = value_decimal(&arguments[0], &decimal);
        RexxNumericStatus numeric_status;
        if (decimal_status == REXX_VM_STATUS_RESOURCE_LIMIT)
            return decimal_status;
        if (decimal_status != REXX_VM_STATUS_OK)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        numeric_status = rexx_decimal_to_uint32(&decimal, &position);
        if (numeric_status == REXX_NUMERIC_RESOURCE_LIMIT)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        if (numeric_status != REXX_NUMERIC_OK || position == 0u)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    if (position != 0u) {
        if (vm->routine_depth != 0u) {
            if ((size_t)position <= vm->current_argument_count) {
                const RexxVMArgument *argument = &routine_arguments[position - 1u];
                present = argument->present;
                bytes = argument->bytes;
                length = argument->length;
            }
        } else if (position == 1u && vm->argument_string_length != 0u) {
            present = 1;
            bytes = vm->argument_string;
            length = vm->argument_string_length;
        }
    }
    if (argument_count == 2u) {
        char *option = NULL;
        size_t option_length = 0u;
        RexxVMStatus status = value_string(&arguments[1], &option,
                                           &option_length);
        int is_exists;
        int is_omitted;
        if (status != REXX_VM_STATUS_OK) return status;
        is_exists = option_length != 0u &&
                    ascii_upper((unsigned char)option[0]) == 'E';
        is_omitted = option_length != 0u &&
                     ascii_upper((unsigned char)option[0]) == 'O';
        vm_free(option);
        if (!is_exists && !is_omitted)
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        return rexx_value_set_string(result,
                                     ((present != 0) == is_exists) ? "1" : "0",
                                     1u);
    }
    return rexx_value_set_string(result, bytes, length);
}

static RexxVMStatus copy_routine_arguments(RexxVM *vm,
                                            size_t argument_start,
                                            size_t argument_count,
                                            RexxVMArgument **arguments_out)
{
    RexxVMArgument *arguments;
    size_t index;
    size_t total = 0u;
    size_t allocated_count = argument_count == 0u ? 1u : argument_count;
    if (argument_count > REXX_VM_MAX_ROUTINE_ARGUMENTS ||
        allocated_count > SIZE_MAX / sizeof(*arguments))
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    arguments = (RexxVMArgument *)vm_calloc(allocated_count,
                                             sizeof(*arguments));
    if (arguments == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    for (index = 0u; index < argument_count; ++index) {
        const RexxValue *source = &vm->stack[argument_start + index];
        if (source->type == REXX_VALUE_UNSET) {
            arguments[index].bytes = (char *)vm_calloc(1u, 1u);
            if (arguments[index].bytes == NULL) {
                free_arguments(arguments, argument_count);
                return REXX_VM_STATUS_OUT_OF_MEMORY;
            }
            arguments[index].present = 0;
        } else {
            RexxVMStatus status = value_string(source, &arguments[index].bytes,
                                               &arguments[index].length);
            if (status != REXX_VM_STATUS_OK) {
                free_arguments(arguments, argument_count);
                return status;
            }
            arguments[index].present = 1;
        }
        if (arguments[index].length > REXX_VM_MAX_PARSE_SOURCE_LENGTH - total) {
            free_arguments(arguments, argument_count);
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        }
        total += arguments[index].length;
    }
    *arguments_out = arguments;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus execute_call(RexxVM *vm,
                                 const RexxBytecodeChunk *chunk,
                                 const RexxInstruction *instruction,
                                 RexxValue *result,
                                 const char *forced_name,
                                 size_t forced_name_length)
{
    const RexxSymbol *routine_symbol = NULL;
    const char *name = NULL;
    size_t name_length = 0u;
    size_t argument_count = (size_t)instruction->auxiliary;
    size_t argument_start;
    size_t target = 0u;
    int found = 0;
    int quoted = (instruction->flags & REXX_INSTRUCTION_CALL_QUOTED) != 0u;
    int subroutine =
        (instruction->flags & REXX_INSTRUCTION_CALL_SUBROUTINE) != 0u;
    RexxValue name_value = REXX_VALUE_INIT;
    RexxVMStatus status;

    if ((instruction->flags & (uint8_t)~(REXX_INSTRUCTION_CALL_SUBROUTINE |
                                         REXX_INSTRUCTION_CALL_QUOTED)) != 0u ||
        argument_count > vm->sp) return REXX_VM_STATUS_INVALID_BYTECODE;
    argument_start = vm->sp - argument_count;
    if (forced_name != NULL) {
        name = forced_name;
        name_length = forced_name_length;
        quoted = 0;
    } else if (quoted) {
        char *owned_name = NULL;
        if (instruction->operand >= chunk->constant_count ||
            chunk->constants[instruction->operand].kind != REXX_CONSTANT_STRING)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        status = decode_constant(&chunk->constants[instruction->operand],
                                 &name_value);
        if (status == REXX_VM_STATUS_OK)
            status = value_string(&name_value, &owned_name, &name_length);
        if (status != REXX_VM_STATUS_OK) {
            rexx_value_clear(&name_value);
            return status;
        }
        name = owned_name;
    } else {
        status = get_symbol(chunk, instruction->operand,
                            REXX_SYMBOL_ROUTINE, &routine_symbol);
        if (status != REXX_VM_STATUS_OK) return status;
        name = routine_symbol->name;
        name_length = routine_symbol->length;
    }

    if (!quoted) {
        const RexxBytecodeChunk *label_chunk = vm->root_chunk != NULL
            ? vm->root_chunk : chunk;
        status = find_internal_label(label_chunk, name, name_length, 1,
                                     &target, &found);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
    }
    if (found) {
        RexxVMCallFrame *frames;
        RexxVMCallFrame *frame;
        RexxVMArgument *arguments = NULL;
        char *saved_address = NULL;
        RexxVMCallFrame condition_snapshot;
        memset(&condition_snapshot, 0, sizeof(condition_snapshot));
        status = ensure_routine_frame(vm);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        status = copy_routine_arguments(vm, argument_start, argument_count,
                                        &arguments);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        if (vm->address_environment != NULL) {
            if (vm->address_environment_length == SIZE_MAX) {
                free_arguments(arguments, argument_count);
                status = REXX_VM_STATUS_RESOURCE_LIMIT;
                goto cleanup;
            }
            saved_address = (char *)vm_malloc(
                vm->address_environment_length + 1u);
            if (saved_address == NULL) {
                free_arguments(arguments, argument_count);
                status = REXX_VM_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            memcpy(saved_address, vm->address_environment,
                   vm->address_environment_length);
            saved_address[vm->address_environment_length] = '\0';
        }
        status = save_condition_context(vm, &condition_snapshot);
        if (status != REXX_VM_STATUS_OK) {
            free_arguments(arguments, argument_count);
            vm_free(saved_address);
            goto cleanup;
        }
        status = set_sigl(
            vm, (RexxVMEnvironment *)vm->current_environment,
            physical_source_line(vm, chunk, instruction->source_line));
        if (status != REXX_VM_STATUS_OK) {
            free_arguments(arguments, argument_count);
            vm_free(saved_address);
            free_condition_snapshot(&condition_snapshot);
            goto cleanup;
        }
        frames = (RexxVMCallFrame *)vm->routine_frames;
        frame = &frames[vm->routine_depth];
        memset(frame, 0, sizeof(*frame));
        frame->return_pc = vm->pc;
        frame->return_chunk = vm->active_chunk != NULL
            ? (const RexxBytecodeChunk *)vm->active_chunk : chunk;
        frame->return_interpret_depth = vm->interpret_depth;
        frame->stack_base = argument_start;
        frame->do_depth = vm->do_depth;
        frame->function_call = !subroutine;
        frame->caller_environment =
            (RexxVMEnvironment *)vm->current_environment;
        frame->callee_environment = frame->caller_environment;
        frame->caller_arguments = (RexxVMArgument *)vm->current_arguments;
        frame->caller_argument_count = vm->current_argument_count;
        frame->arguments = arguments;
        frame->argument_count = argument_count;
        frame->numeric_digits = vm->numeric_digits;
        frame->numeric_fuzz = vm->numeric_fuzz;
        frame->numeric_form = vm->numeric_form;
        frame->address_environment = saved_address;
        frame->address_environment_length = vm->address_environment_length;
        frame->first_instruction = 1;
        frame->condition_call = vm->condition_call_pending;
        copy_saved_condition_context(frame, &condition_snapshot);
        ++vm->routine_depth;
        while (vm->sp > argument_start)
            rexx_value_clear(&vm->stack[--vm->sp]);
        vm->current_arguments = arguments;
        vm->current_argument_count = argument_count;
        vm->active_chunk = vm->root_chunk != NULL ? vm->root_chunk : chunk;
        vm->pc = target;
        vm->procedure_pending = 1;
        status = REXX_VM_STATUS_OK;
        goto cleanup;
    }

    if (ascii_equal(name, name_length, "CONDITION", 9u)) {
        status = execute_condition_builtin(
            vm, argument_count == 0u ? NULL : &vm->stack[argument_start],
            argument_count, result);
    } else if (ascii_equal(name, name_length, "ARG", 3u)) {
        status = execute_arg_builtin(vm, argument_count == 0u ? NULL :
                                     &vm->stack[argument_start],
                                     argument_count, result);
    } else if (ascii_equal(name, name_length, "DIGITS", 6u)) {
        status = argument_count == 0u
            ? value_set_uint32(result, vm->numeric_digits)
            : REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    } else if (ascii_equal(name, name_length, "FUZZ", 4u)) {
        status = argument_count == 0u
            ? value_set_uint32(result, vm->numeric_fuzz)
            : REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    } else if (ascii_equal(name, name_length, "FORM", 4u)) {
        static const char scientific[] = "SCIENTIFIC";
        static const char engineering[] = "ENGINEERING";
        if (argument_count != 0u) status = REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        else if (vm->numeric_form == REXX_NUMERIC_FORM_ENGINEERING)
            status = rexx_value_set_string(result, engineering,
                                           sizeof(engineering) - 1u);
        else status = rexx_value_set_string(result, scientific,
                                            sizeof(scientific) - 1u);
    } else {
        status = rexx_builtin_dispatch(
            name, name_length,
            argument_count == 0u ? NULL : &vm->stack[argument_start],
            argument_count, result);
        if (status == REXX_VM_STATUS_UNKNOWN_FUNCTION &&
            (subroutine || quoted))
            status = REXX_VM_STATUS_UNKNOWN_ROUTINE;
    }
    if (status == REXX_VM_STATUS_OK) {
        while (vm->sp > argument_start)
            rexx_value_clear(&vm->stack[--vm->sp]);
        if (vm->condition_call_pending) {
            rexx_value_clear(result);
        } else if (subroutine) {
            status = environment_set(
                vm, (RexxVMEnvironment *)vm->current_environment,
                "RESULT", 6u, 0, result);
            rexx_value_clear(result);
        } else {
            status = push_move(vm, result);
        }
    }

cleanup:
    if (quoted) vm_free((void *)name);
    rexx_value_clear(&name_value);
    return status;
}

static void clear_stack_to(RexxVM *vm, size_t base)
{
    while (vm->sp > base) rexx_value_clear(&vm->stack[--vm->sp]);
}

static void unwind_routines(RexxVM *vm);

static void truncate_do_frames(RexxVM *vm, size_t base)
{
    RexxVMDoFrame *frames = (RexxVMDoFrame *)vm->do_frames;
    while (vm->do_depth > base) {
        --vm->do_depth;
        memset(&frames[vm->do_depth], 0, sizeof(*frames));
    }
}

static RexxVMStatus ensure_interpret_frame(RexxVM *vm)
{
    size_t next;
    RexxVMInterpretFrame *grown;
    if (vm->interpret_depth >= REXX_VM_MAX_INTERPRET_DEPTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    if (vm->interpret_depth < vm->interpret_frame_capacity)
        return REXX_VM_STATUS_OK;
    next = vm->interpret_frame_capacity == 0u ? 4u :
           vm->interpret_frame_capacity * 2u;
    if (next > REXX_VM_MAX_INTERPRET_DEPTH)
        next = REXX_VM_MAX_INTERPRET_DEPTH;
    if (next <= vm->interpret_frame_capacity ||
        next > SIZE_MAX / sizeof(*grown))
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    grown = (RexxVMInterpretFrame *)vm_realloc(
        vm->interpret_frames, next * sizeof(*grown));
    if (grown == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    memset(grown + vm->interpret_frame_capacity, 0,
           (next - vm->interpret_frame_capacity) * sizeof(*grown));
    vm->interpret_frames = grown;
    vm->interpret_frame_capacity = next;
    return REXX_VM_STATUS_OK;
}

static void pop_interpret_context(RexxVM *vm, int restore_parent)
{
    RexxVMInterpretFrame *frames;
    RexxVMInterpretFrame *frame;
    if (vm == NULL || vm->interpret_depth == 0u) return;
    frames = (RexxVMInterpretFrame *)vm->interpret_frames;
    frame = &frames[vm->interpret_depth - 1u];
    if (restore_parent) {
        while (vm->sp > frame->stack_base)
            rexx_value_clear(&vm->stack[--vm->sp]);
        vm->do_depth = frame->do_depth;
        vm->active_chunk = frame->parent_chunk;
        vm->pc = frame->parent_pc;
    }
    vm->dynamic_source_bytes =
        frame->source_length + 1u <= vm->dynamic_source_bytes
            ? vm->dynamic_source_bytes - (frame->source_length + 1u) : 0u;
    vm->dynamic_bytecode_bytes = frame->bytecode_bytes <=
        vm->dynamic_bytecode_bytes
            ? vm->dynamic_bytecode_bytes - frame->bytecode_bytes : 0u;
    vm->dynamic_instruction_count = frame->instruction_count <=
        vm->dynamic_instruction_count
            ? vm->dynamic_instruction_count - frame->instruction_count : 0u;
    rexx_bytecode_chunk_free(frame->chunk);
    vm_free(frame->source);
    memset(frame, 0, sizeof(*frame));
    --vm->interpret_depth;
}

static void truncate_interpret_contexts(RexxVM *vm,
                                        size_t minimum_routine_depth,
                                        int restore_parent)
{
    RexxVMInterpretFrame *frames;
    if (vm == NULL || vm->interpret_depth == 0u) return;
    frames = (RexxVMInterpretFrame *)vm->interpret_frames;
    while (vm->interpret_depth != 0u &&
           frames[vm->interpret_depth - 1u].routine_depth >=
               minimum_routine_depth) {
        pop_interpret_context(vm, restore_parent);
        frames = (RexxVMInterpretFrame *)vm->interpret_frames;
    }
}

static int ast_clause_list_has_label(const RexxAstClause *clauses)
{
    const RexxAstClause *clause;
    for (clause = clauses; clause != NULL; clause = clause->next) {
        if (clause->kind == REXX_AST_CLAUSE_LABEL) return 1;
        switch (clause->kind) {
        case REXX_AST_CLAUSE_IF:
            if (ast_clause_list_has_label(clause->as.if_clause.then_clause) ||
                ast_clause_list_has_label(clause->as.if_clause.else_clause))
                return 1;
            break;
        case REXX_AST_CLAUSE_DO:
            if (ast_clause_list_has_label(clause->as.do_clause.body)) return 1;
            break;
        case REXX_AST_CLAUSE_SELECT: {
            const RexxAstWhen *when_clause;
            for (when_clause = clause->as.select_clause.when_clauses;
                 when_clause != NULL; when_clause = when_clause->next) {
                if (ast_clause_list_has_label(when_clause->body)) return 1;
            }
            if (ast_clause_list_has_label(
                    clause->as.select_clause.otherwise_clause)) return 1;
            break;
        }
        default:
            break;
        }
    }
    return 0;
}

static int checked_size_add(size_t *total, size_t amount)
{
    if (total == NULL || amount > SIZE_MAX - *total) return 0;
    *total += amount;
    return 1;
}

static int checked_size_array(size_t *total, size_t count, size_t element_size)
{
    if (element_size != 0u && count > SIZE_MAX / element_size) return 0;
    return checked_size_add(total, count * element_size);
}

static int bytecode_storage_size(const RexxBytecodeChunk *chunk,
                                size_t *size_out)
{
    size_t total = sizeof(*chunk);
    size_t index;
    if (chunk == NULL || size_out == NULL ||
        !checked_size_array(&total, chunk->instruction_capacity,
                            sizeof(*chunk->instructions)) ||
        !checked_size_array(&total, chunk->constant_capacity,
                            sizeof(*chunk->constants)) ||
        !checked_size_array(&total, chunk->symbol_capacity,
                            sizeof(*chunk->symbols)) ||
        !checked_size_array(&total, chunk->parse_item_capacity,
                            sizeof(*chunk->parse_items)) ||
        !checked_size_array(&total, chunk->parse_template_capacity,
                            sizeof(*chunk->parse_templates)) ||
        !checked_size_array(&total, chunk->parse_plan_capacity,
                            sizeof(*chunk->parse_plans)))
        return 0;
    for (index = 0u; index < chunk->constant_count; ++index) {
        if (chunk->constants[index].length == SIZE_MAX ||
            !checked_size_add(&total, chunk->constants[index].length + 1u))
            return 0;
    }
    for (index = 0u; index < chunk->symbol_count; ++index) {
        if (chunk->symbols[index].length == SIZE_MAX ||
            !checked_size_add(&total, chunk->symbols[index].length + 1u))
            return 0;
    }
    *size_out = total;
    return 1;
}

static RexxVMStatus charge_interpret_compile_work(RexxVM *vm, size_t amount)
{
    if (amount > vm->interpret_compile_remaining)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    vm->interpret_compile_remaining -= amount;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus compile_interpret_source(
    RexxVM *vm, const char *source, size_t source_length,
    RexxBytecodeChunk **chunk_out, const char **syntax_message)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;
    const RexxParserError *parser_error;
    RexxVMStatus status;

    if (chunk_out == NULL || syntax_message == NULL || vm == NULL ||
        (source == NULL && source_length != 0u))
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    *chunk_out = NULL;
    *syntax_message = NULL;
    if (source_length > REXX_VM_MAX_INTERPRET_SOURCE_LENGTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    status = charge_interpret_compile_work(vm,
                                           source_length == SIZE_MAX
                                               ? SIZE_MAX : source_length + 1u);
    if (status != REXX_VM_STATUS_OK) return status;
    if (rexx_lexer_init(&lexer, source, source_length) !=
        REXX_LEXER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        parser_error = rexx_parser_get_error(&parser);
        if (parser_status == REXX_PARSER_STATUS_OUT_OF_MEMORY)
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        if (parser_status == REXX_PARSER_STATUS_RESOURCE_LIMIT)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        if (parser_status == REXX_PARSER_STATUS_SYNTAX_ERROR ||
            parser_status == REXX_PARSER_STATUS_LEXER_ERROR) {
            *syntax_message = parser_error != NULL &&
                parser_error->message != NULL
                    ? parser_error->message : "invalid interpreted source";
            return REXX_VM_STATUS_DYNAMIC_SYNTAX;
        }
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    status = charge_interpret_compile_work(vm, parser.allocated_nodes);
    if (status != REXX_VM_STATUS_OK) {
        rexx_parser_free_ast(program);
        return status;
    }
    if (ast_clause_list_has_label(program->clauses)) {
        rexx_parser_free_ast(program);
        *syntax_message = "labels are not permitted in INTERPRET source";
        return REXX_VM_STATUS_DYNAMIC_SYNTAX;
    }
    compiler_status = rexx_compiler_compile(program, &chunk,
                                             &compiler_error);
    rexx_parser_free_ast(program);
    if (compiler_status != REXX_COMPILER_STATUS_OK) {
        if (compiler_status == REXX_COMPILER_STATUS_OUT_OF_MEMORY)
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        if (compiler_status == REXX_COMPILER_STATUS_RESOURCE_LIMIT)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        if (compiler_status == REXX_COMPILER_STATUS_MALFORMED_AST ||
            compiler_status == REXX_COMPILER_STATUS_UNSUPPORTED_AST) {
            *syntax_message = compiler_error.message != NULL
                ? compiler_error.message : "invalid interpreted source";
            return REXX_VM_STATUS_DYNAMIC_SYNTAX;
        }
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    }
    status = charge_interpret_compile_work(vm, chunk->instruction_count);
    if (status != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return status;
    }
    *chunk_out = chunk;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus execute_interpret(RexxVM *vm,
                                     const RexxBytecodeChunk *parent_chunk)
{
    RexxValue expression = REXX_VALUE_INIT;
    RexxBytecodeChunk *dynamic_chunk = NULL;
    const char *syntax_message = NULL;
    char *source = NULL;
    size_t source_length = 0u;
    size_t bytecode_bytes = 0u;
    size_t source_allocation;
    size_t next_total;
    size_t pc = vm->pc;
    int transferred = 0;
    RexxVMStatus status;

    status = pop_move(vm, &expression);
    if (status == REXX_VM_STATUS_OK)
        status = value_string(&expression, &source, &source_length);
    rexx_value_clear(&expression);
    if (status != REXX_VM_STATUS_OK) return status;
    if (source_length > REXX_VM_MAX_INTERPRET_SOURCE_LENGTH ||
        source_length == SIZE_MAX) {
        vm_free(source);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    source_allocation = source_length + 1u;
    if (vm->interpret_depth >= REXX_VM_MAX_INTERPRET_DEPTH ||
        source_allocation > REXX_VM_MAX_DYNAMIC_SOURCE_BYTES ||
        vm->dynamic_source_bytes > REXX_VM_MAX_DYNAMIC_SOURCE_BYTES ||
        source_allocation > REXX_VM_MAX_DYNAMIC_SOURCE_BYTES -
                                vm->dynamic_source_bytes) {
        vm_free(source);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    status = ensure_interpret_frame(vm);
    if (status != REXX_VM_STATUS_OK) {
        vm_free(source);
        return status;
    }
    status = compile_interpret_source(vm, source, source_length,
                                      &dynamic_chunk, &syntax_message);
    if (status == REXX_VM_STATUS_DYNAMIC_SYNTAX) {
        RexxVMStatus trap_status = raise_condition(
            vm, parent_chunk, REXX_CONDITION_SYNTAX,
            syntax_message != NULL ? syntax_message :
                "invalid interpreted source",
            strlen(syntax_message != NULL ? syntax_message :
                   "invalid interpreted source"),
            vm->current_source_line, 40, &transferred);
        vm_free(source);
        if (trap_status == REXX_VM_STATUS_CONTROL_TRANSFER && transferred)
            return REXX_VM_STATUS_OK;
        if (trap_status != REXX_VM_STATUS_OK) return trap_status;
        return REXX_VM_STATUS_DYNAMIC_SYNTAX;
    }
    if (status != REXX_VM_STATUS_OK) {
        vm_free(source);
        return status;
    }
    if (!bytecode_storage_size(dynamic_chunk, &bytecode_bytes) ||
        bytecode_bytes > REXX_VM_MAX_DYNAMIC_BYTECODE_BYTES ||
        bytecode_bytes > REXX_VM_MAX_DYNAMIC_BYTECODE_BYTES -
                             vm->dynamic_bytecode_bytes ||
        dynamic_chunk->instruction_count >
            REXX_VM_MAX_DYNAMIC_INSTRUCTIONS ||
        dynamic_chunk->instruction_count >
            REXX_VM_MAX_DYNAMIC_INSTRUCTIONS -
                vm->dynamic_instruction_count) {
        rexx_bytecode_chunk_free(dynamic_chunk);
        vm_free(source);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    if (source_allocation > SIZE_MAX - vm->dynamic_source_bytes) {
        rexx_bytecode_chunk_free(dynamic_chunk);
        vm_free(source);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }
    next_total = vm->dynamic_source_bytes + source_allocation;
    if (next_total > REXX_VM_MAX_DYNAMIC_SOURCE_BYTES) {
        rexx_bytecode_chunk_free(dynamic_chunk);
        vm_free(source);
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    }

    {
        RexxVMInterpretFrame *frames =
            (RexxVMInterpretFrame *)vm->interpret_frames;
        RexxVMInterpretFrame *frame = &frames[vm->interpret_depth];
        memset(frame, 0, sizeof(*frame));
        frame->parent_chunk = parent_chunk;
        frame->parent_pc = pc;
        frame->chunk = dynamic_chunk;
        frame->source = source;
        frame->source_length = source_length;
        frame->source_line = vm->current_source_line;
        frame->routine_depth = vm->routine_depth;
        frame->stack_base = vm->sp;
        frame->do_depth = vm->do_depth;
        frame->bytecode_bytes = bytecode_bytes;
        frame->instruction_count = dynamic_chunk->instruction_count;
        ++vm->interpret_depth;
    }
    vm->dynamic_source_bytes = next_total;
    vm->dynamic_bytecode_bytes += bytecode_bytes;
    vm->dynamic_instruction_count += dynamic_chunk->instruction_count;
    vm->active_chunk = dynamic_chunk;
    vm->pc = 0u;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus execute_signal(RexxVM *vm,
                                   const RexxBytecodeChunk *chunk,
                                   const RexxInstruction *instruction,
                                   const char *forced_name,
                                   size_t forced_name_length)
{
    const char *name = NULL;
    size_t name_length = 0u;
    size_t target = 0u;
    size_t stack_base = 0u;
    size_t do_base = 0u;
    int found = 0;
    int case_insensitive = 0;
    char *owned_name = NULL;
    RexxValue destination = REXX_VALUE_INIT;
    RexxVMStatus status;

    if (instruction->auxiliary != 0u || instruction->source_line == 0u)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if (forced_name != NULL) {
        name = forced_name;
        name_length = forced_name_length;
        case_insensitive = 1;
    } else if (instruction->flags == REXX_INSTRUCTION_SIGNAL_LABEL) {
        const RexxSymbol *label = NULL;
        status = get_symbol(chunk, instruction->operand, REXX_SYMBOL_LABEL,
                            &label);
        if (status != REXX_VM_STATUS_OK) return status;
        name = label->name;
        name_length = label->length;
        case_insensitive = 1;
    } else if (instruction->flags == REXX_INSTRUCTION_HAS_VALUE &&
               instruction->operand == 0u) {
        status = pop_move(vm, &destination);
        if (status == REXX_VM_STATUS_OK &&
            destination.type == REXX_VALUE_STRING &&
            destination.as.string.length > REXX_VM_MAX_NAME_LENGTH)
            status = REXX_VM_STATUS_INVALID_LABEL;
        if (status == REXX_VM_STATUS_OK)
            status = value_string(&destination, &owned_name, &name_length);
        rexx_value_clear(&destination);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        name = owned_name;
        if (!subsidiary_name_valid(name, name_length)) {
            status = REXX_VM_STATUS_INVALID_LABEL;
            goto cleanup;
        }
    } else {
        return REXX_VM_STATUS_INVALID_BYTECODE;
    }

    status = find_internal_label(vm->root_chunk != NULL
                                     ? vm->root_chunk : chunk,
                                 name, name_length,
                                 case_insensitive, &target, &found);
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    if (!found) {
        status = REXX_VM_STATUS_UNKNOWN_LABEL;
        goto cleanup;
    }
    /* Label lookup is program-wide, but SIGNAL does not create or remove an
       internal-call activation. A destination outside the routine's source
       region still runs with that activation's arguments and variables; an
       explicit RETURN there uses the original CALL continuation. */
    if (vm->routine_depth != 0u) {
        RexxVMCallFrame *frames = (RexxVMCallFrame *)vm->routine_frames;
        RexxVMCallFrame *frame = &frames[vm->routine_depth - 1u];
        stack_base = frame->stack_base;
        do_base = frame->do_depth;
    }
    if (stack_base > vm->sp || do_base > vm->do_depth) {
        status = REXX_VM_STATUS_INVALID_BYTECODE;
        goto cleanup;
    }
    status = set_sigl(vm, (RexxVMEnvironment *)vm->current_environment,
                      physical_source_line(vm, chunk,
                                           instruction->source_line));
    if (status != REXX_VM_STATUS_OK) goto cleanup;

    /* Abandon pending expressions and DO loops in the current routine only.
       Caller stack values and loops remain suspended in their CALL frame. */
    clear_stack_to(vm, stack_base);
    truncate_do_frames(vm, do_base);
    truncate_interpret_contexts(vm, vm->routine_depth, 0);
    vm->active_chunk = vm->root_chunk != NULL ? vm->root_chunk : chunk;
    vm->pc = target;
    status = REXX_VM_STATUS_OK;

cleanup:
    vm_free(owned_name);
    return status;
}

static const char *condition_name(RexxConditionType condition)
{
    static const char *const names[REXX_CONDITION_COUNT] = {
        "ERROR", "FAILURE", "HALT", "NOTREADY", "NOVALUE", "SYNTAX"
    };
    return condition >= 0 && condition < REXX_CONDITION_COUNT
        ? names[condition] : "";
}

static const char *condition_state_name(uint8_t state)
{
    if (state == REXX_CONDITION_STATE_ON) return "ON";
    if (state == REXX_CONDITION_STATE_DELAY) return "DELAY";
    return "OFF";
}

static RexxVMStatus set_condition_rc(RexxVM *vm, int32_t code)
{
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus status = value_set_int32(&value, code);
    if (status == REXX_VM_STATUS_OK)
        status = environment_set(
            vm, (RexxVMEnvironment *)vm->current_environment,
            "RC", 2u, 0, &value);
    rexx_value_clear(&value);
    if (status == REXX_VM_STATUS_OK) vm->rc = code;
    return status;
}

static RexxVMStatus replace_condition_information_owned(
    RexxVM *vm, RexxConditionType condition, uint8_t invocation,
    char *description, size_t description_length)
{
    vm_free(vm->condition_description);
    vm->condition_description = description;
    vm->condition_description_length = description_length;
    vm->current_condition = (uint32_t)condition;
    vm->condition_invocation = invocation;
    vm->condition_information_active = 1;
    return REXX_VM_STATUS_OK;
}

static void copy_saved_condition_context(RexxVMCallFrame *destination,
                                         RexxVMCallFrame *source)
{
    size_t index;
    memcpy(destination->condition_states, source->condition_states,
           sizeof(destination->condition_states));
    memcpy(destination->condition_actions, source->condition_actions,
           sizeof(destination->condition_actions));
    memcpy(destination->condition_targets, source->condition_targets,
           sizeof(destination->condition_targets));
    for (index = 0u; index < REXX_CONDITION_COUNT; ++index) {
        destination->condition_target_names[index] =
            source->condition_target_names[index];
        destination->condition_target_name_lengths[index] =
            source->condition_target_name_lengths[index];
        source->condition_target_names[index] = NULL;
        source->condition_target_name_lengths[index] = 0u;
    }
    destination->current_condition = source->current_condition;
    destination->condition_invocation = source->condition_invocation;
    destination->condition_information_active =
        source->condition_information_active;
    destination->condition_description = source->condition_description;
    destination->condition_description_length =
        source->condition_description_length;
    source->condition_description = NULL;
    source->condition_description_length = 0u;
}

static RexxVMStatus raise_condition(RexxVM *vm,
                                    const RexxBytecodeChunk *chunk,
                                    RexxConditionType condition,
                                    const char *description,
                                    size_t description_length,
                                    size_t source_line,
                                    int32_t syntax_code,
                                    int *transferred)
{
    uint8_t state;
    char *owned_description = NULL;
    RexxVMStatus status;
    if (transferred != NULL) *transferred = 0;
    if (vm == NULL || chunk == NULL || condition >= REXX_CONDITION_COUNT ||
        (description == NULL && description_length != 0u))
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    state = vm->condition_states[condition];
    if (state == REXX_CONDITION_STATE_OFF ||
        state == REXX_CONDITION_STATE_DELAY)
        return REXX_VM_STATUS_OK;

    if (description_length != 0u) {
        owned_description = copy_text(description, description_length);
        if (owned_description == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    }

    if (vm->condition_actions[condition] ==
        REXX_CONDITION_ACTION_SIGNAL) {
        RexxInstruction signal_instruction;
        const char *target_name = vm->condition_target_names[condition];
        size_t target_name_length =
            vm->condition_target_name_lengths[condition];
        if (target_name == NULL || target_name_length == 0u) {
            vm_free(owned_description);
            return REXX_VM_STATUS_INVALID_BYTECODE;
        }
        if (condition == REXX_CONDITION_SYNTAX) {
            status = set_condition_rc(vm, syntax_code);
            if (status != REXX_VM_STATUS_OK) {
                vm_free(owned_description);
                return status;
            }
        }
        memset(&signal_instruction, 0, sizeof(signal_instruction));
        signal_instruction.opcode = OP_SIGNAL;
        signal_instruction.flags = REXX_INSTRUCTION_SIGNAL_LABEL;
        signal_instruction.operand = 0u;
        signal_instruction.source_line = source_line;
        vm->condition_states[condition] = REXX_CONDITION_STATE_OFF;
        status = execute_signal(vm, chunk, &signal_instruction,
                                target_name, target_name_length);
        (void)replace_condition_information_owned(
            vm, condition, REXX_CONDITION_INVOKED_SIGNAL,
            owned_description, description_length);
        if (status == REXX_VM_STATUS_OK) {
            vm->condition_control_transfer = 1;
            if (transferred != NULL) *transferred = 1;
            return REXX_VM_STATUS_CONTROL_TRANSFER;
        }
        return status;
    }

    if (vm->condition_actions[condition] == REXX_CONDITION_ACTION_CALL) {
        RexxVMCallFrame previous_context;
        RexxInstruction call_instruction;
        size_t previous_depth = vm->routine_depth;
        const char *target_name = vm->condition_target_names[condition];
        size_t target_name_length =
            vm->condition_target_name_lengths[condition];
        if (target_name == NULL || target_name_length == 0u) {
            vm_free(owned_description);
            return REXX_VM_STATUS_INVALID_BYTECODE;
        }
        memset(&previous_context, 0, sizeof(previous_context));
        status = save_condition_context(vm, &previous_context);
        if (status != REXX_VM_STATUS_OK) {
            vm_free(owned_description);
            return status;
        }
        vm->condition_states[condition] = REXX_CONDITION_STATE_DELAY;
        (void)replace_condition_information_owned(
            vm, condition, REXX_CONDITION_INVOKED_CALL,
            owned_description, description_length);

        memset(&call_instruction, 0, sizeof(call_instruction));
        call_instruction.opcode = OP_CALL;
        call_instruction.flags = REXX_INSTRUCTION_CALL_SUBROUTINE;
        call_instruction.operand = 0u;
        call_instruction.source_line = source_line;
        vm->condition_call_pending = 1;
        status = execute_call(vm, chunk, &call_instruction,
                              &(RexxValue){0}, target_name,
                              target_name_length);
        vm->condition_call_pending = 0;
        if (status != REXX_VM_STATUS_OK) {
            restore_condition_context(vm, &previous_context);
            free_condition_snapshot(&previous_context);
            return status;
        }
        if (vm->routine_depth > previous_depth) {
            RexxVMCallFrame *frames = (RexxVMCallFrame *)vm->routine_frames;
            RexxVMCallFrame *frame = &frames[previous_depth];
            free_condition_snapshot(frame);
            copy_saved_condition_context(frame, &previous_context);
            frame->condition_call = 1;
        } else {
            restore_condition_context(vm, &previous_context);
        }
        free_condition_snapshot(&previous_context);
        vm->condition_control_transfer = 1;
        if (transferred != NULL) *transferred = 1;
        return REXX_VM_STATUS_CONTROL_TRANSFER;
    }

    vm_free(owned_description);
    return REXX_VM_STATUS_INVALID_BYTECODE;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
RexxVMStatus rexx_vm_test_raise_condition(
    RexxVM *vm, const RexxBytecodeChunk *chunk, RexxConditionType condition,
    const char *description, size_t description_length, size_t source_line,
    int *transferred)
{
    return raise_condition(vm, chunk, condition, description,
                           description_length, source_line, 0, transferred);
}
#endif

static RexxVMStatus execute_condition_registration(
    RexxVM *vm, const RexxBytecodeChunk *chunk,
    const RexxInstruction *instruction)
{
    uint8_t allowed = REXX_INSTRUCTION_CONDITION_ON |
                      REXX_INSTRUCTION_CONDITION_CALL;
    RexxConditionType condition;
    RexxSymbolKind target_kind;
    const RexxSymbol *target = NULL;
    char *target_name = NULL;
    int enable;
    int call_action;
    if ((instruction->flags & (uint8_t)~allowed) != 0u ||
        instruction->operand >= REXX_CONDITION_COUNT ||
        instruction->source_line == 0u)
        return REXX_VM_STATUS_INVALID_BYTECODE;
    condition = (RexxConditionType)instruction->operand;
    enable = (instruction->flags & REXX_INSTRUCTION_CONDITION_ON) != 0u;
    call_action =
        (instruction->flags & REXX_INSTRUCTION_CONDITION_CALL) != 0u;
    if (call_action &&
        (condition == REXX_CONDITION_NOVALUE ||
         condition == REXX_CONDITION_SYNTAX))
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if (enable) {
        target_kind = call_action ? REXX_SYMBOL_ROUTINE : REXX_SYMBOL_LABEL;
        if (instruction->auxiliary == REXX_NO_SYMBOL)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        if (get_symbol(chunk, instruction->auxiliary, target_kind,
                       &target) != REXX_VM_STATUS_OK)
            return REXX_VM_STATUS_INVALID_BYTECODE;
        target_name = copy_text(target->name, target->length);
        if (target_name == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    } else if (instruction->auxiliary != REXX_NO_SYMBOL) {
        return REXX_VM_STATUS_INVALID_BYTECODE;
    }
    vm_free(vm->condition_target_names[condition]);
    vm->condition_target_names[condition] = target_name;
    vm->condition_target_name_lengths[condition] = enable
        ? target->length : 0u;
    vm->condition_states[condition] = enable
        ? REXX_CONDITION_STATE_ON : REXX_CONDITION_STATE_OFF;
    vm->condition_actions[condition] = call_action
        ? REXX_CONDITION_ACTION_CALL : REXX_CONDITION_ACTION_SIGNAL;
    vm->condition_targets[condition] = REXX_NO_SYMBOL;
    (void)target;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus execute_condition_builtin(
    RexxVM *vm, const RexxValue *arguments, size_t argument_count,
    RexxValue *result)
{
    char *option_text = NULL;
    size_t option_length = 0u;
    char option = 'I';
    const char *answer = "";
    RexxVMStatus status;
    if (argument_count > 1u) return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    if (argument_count == 1u) {
        status = value_string(&arguments[0], &option_text, &option_length);
        if (status != REXX_VM_STATUS_OK) return status;
        if (option_length == 0u) {
            vm_free(option_text);
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
        }
        option = (char)ascii_upper((unsigned char)option_text[0]);
        vm_free(option_text);
        if (option != 'C' && option != 'D' && option != 'I' &&
            option != 'S')
            return REXX_VM_STATUS_INVALID_FUNCTION_CALL;
    }
    if (vm->condition_information_active &&
        vm->current_condition < REXX_CONDITION_COUNT) {
        switch (option) {
        case 'C':
            answer = condition_name(
                (RexxConditionType)vm->current_condition);
            break;
        case 'D':
            answer = vm->condition_description != NULL
                ? vm->condition_description : "";
            break;
        case 'I':
            answer = vm->condition_invocation ==
                    REXX_CONDITION_INVOKED_CALL ? "CALL" : "SIGNAL";
            break;
        case 'S':
            answer = condition_state_name(
                vm->condition_states[vm->current_condition]);
            break;
        default:
            break;
        }
    }
    return rexx_value_set_string(result, answer, strlen(answer));
}

static RexxVMStatus raise_command_condition(
    RexxVM *vm, const RexxBytecodeChunk *chunk, const char *command,
    size_t command_length, size_t source_line)
{
    RexxConditionType condition;
    int32_t rc = vm->rc;
    int transferred = 0;
    if (rc > 0) {
        condition = REXX_CONDITION_ERROR;
    } else if (rc < 0) {
        if (vm->condition_states[REXX_CONDITION_FAILURE] !=
            REXX_CONDITION_STATE_OFF)
            condition = REXX_CONDITION_FAILURE;
        else
            condition = REXX_CONDITION_ERROR;
    } else {
        return REXX_VM_STATUS_OK;
    }
    return raise_condition(vm, chunk, condition, command, command_length,
                           source_line, 0, &transferred);
}

static int syntax_error_is_catchable(RexxVMStatus status)
{
    switch (status) {
    case REXX_VM_STATUS_NUMERIC_ERROR:
    case REXX_VM_STATUS_INVALID_FUNCTION_CALL:
    case REXX_VM_STATUS_INVALID_NUMERIC_SETTING:
    case REXX_VM_STATUS_INVALID_NUMERIC_FUZZ:
    case REXX_VM_STATUS_INVALID_NUMERIC_FORM:
    case REXX_VM_STATUS_UNKNOWN_FUNCTION:
    case REXX_VM_STATUS_UNKNOWN_ROUTINE:
    case REXX_VM_STATUS_MISSING_FUNCTION_RESULT:
    case REXX_VM_STATUS_UNKNOWN_LABEL:
    case REXX_VM_STATUS_INVALID_LABEL:
    case REXX_VM_STATUS_INVALID_PROCEDURE:
        return 1;
    default:
        return 0;
    }
}

static int32_t syntax_error_code(RexxVMStatus status, int32_t detail)
{
    if (detail >= 3 && detail <= 49) return detail;
    switch (status) {
    case REXX_VM_STATUS_NUMERIC_ERROR:
        return 41; /* Bad arithmetic conversion; divide by zero sets 42. */
    case REXX_VM_STATUS_INVALID_NUMERIC_SETTING:
    case REXX_VM_STATUS_INVALID_NUMERIC_FUZZ:
    case REXX_VM_STATUS_INVALID_NUMERIC_FORM:
        return 26;
    case REXX_VM_STATUS_UNKNOWN_LABEL:
    case REXX_VM_STATUS_INVALID_LABEL:
        return 16;
    case REXX_VM_STATUS_UNKNOWN_FUNCTION:
    case REXX_VM_STATUS_UNKNOWN_ROUTINE:
        return 43;
    case REXX_VM_STATUS_INVALID_PROCEDURE:
        return 24;
    default:
        return 40;
    }
}

static const char *syntax_error_description(RexxVMStatus status,
                                            int32_t detail)
{
    if (detail == 42) return "Arithmetic overflow/underflow";
    switch (status) {
    case REXX_VM_STATUS_NUMERIC_ERROR:
        return "Bad arithmetic conversion";
    case REXX_VM_STATUS_INVALID_NUMERIC_SETTING:
    case REXX_VM_STATUS_INVALID_NUMERIC_FUZZ:
    case REXX_VM_STATUS_INVALID_NUMERIC_FORM:
        return "Invalid NUMERIC setting";
    case REXX_VM_STATUS_UNKNOWN_LABEL:
    case REXX_VM_STATUS_INVALID_LABEL:
        return "Label not found";
    case REXX_VM_STATUS_UNKNOWN_FUNCTION:
    case REXX_VM_STATUS_UNKNOWN_ROUTINE:
        return "Routine not found";
    case REXX_VM_STATUS_INVALID_PROCEDURE:
        return "Invalid PROCEDURE instruction";
    default:
        return "Language processing error";
    }
}

static RexxVMStatus ensure_do_frame(RexxVM *vm)
{
    size_t next;
    RexxVMDoFrame *grown;
    if (vm->do_depth >= REXX_VM_MAX_DO_DEPTH)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    if (vm->do_depth < vm->do_frame_capacity) return REXX_VM_STATUS_OK;
    next = vm->do_frame_capacity == 0u ? 4u : vm->do_frame_capacity * 2u;
    if (next > REXX_VM_MAX_DO_DEPTH) next = REXX_VM_MAX_DO_DEPTH;
    if (next <= vm->do_frame_capacity ||
        next > SIZE_MAX / sizeof(*grown))
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    grown = (RexxVMDoFrame *)vm_realloc(vm->do_frames,
                                        next * sizeof(*grown));
    if (grown == NULL) return REXX_VM_STATUS_OUT_OF_MEMORY;
    memset(grown + vm->do_frame_capacity, 0,
           (next - vm->do_frame_capacity) * sizeof(*grown));
    vm->do_frames = grown;
    vm->do_frame_capacity = next;
    return REXX_VM_STATUS_OK;
}

static RexxVMStatus do_control_store(RexxVM *vm,
                                     const RexxBytecodeChunk *chunk,
                                     RexxVMDoFrame *frame)
{
    const RexxSymbol *symbol = NULL;
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus status = parse_get_variable_symbol(
        chunk, frame->control_symbol, &symbol);
    if (status == REXX_VM_STATUS_OK)
        status = decimal_value_to_rexx(vm, &frame->control_value,
                                       vm->numeric_digits, &value);
    if (status == REXX_VM_STATUS_OK)
        status = environment_set(
            vm, (RexxVMEnvironment *)vm->current_environment,
            symbol->name, symbol->length,
            symbol->kind == REXX_SYMBOL_STEM, &value);
    rexx_value_clear(&value);
    return status;
}

static RexxVMStatus do_control_load(RexxVM *vm,
                                    const RexxBytecodeChunk *chunk,
                                    RexxVMDoFrame *frame)
{
    const RexxSymbol *symbol = NULL;
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus status = parse_get_variable_symbol(
        chunk, frame->control_symbol, &symbol);
    if (status == REXX_VM_STATUS_OK)
        status = environment_load_variable(
            vm, (RexxVMEnvironment *)vm->current_environment, symbol, &value);
    if (status == REXX_VM_STATUS_OK)
        status = value_decimal(&value, &frame->control_value);
    rexx_value_clear(&value);
    return status;
}

static RexxVMStatus execute_do_setup(RexxVM *vm,
                                     const RexxBytecodeChunk *chunk,
                                     const RexxInstruction *instruction)
{
    const uint32_t valid_flags = REXX_DO_FLAG_CONTROLLED |
        REXX_DO_FLAG_COUNTED | REXX_DO_FLAG_HAS_TO | REXX_DO_FLAG_HAS_BY |
        REXX_DO_FLAG_HAS_FOR;
    const uint32_t flags = instruction->auxiliary;
    const int controlled = (flags & REXX_DO_FLAG_CONTROLLED) != 0u;
    const int counted = (flags & REXX_DO_FLAG_COUNTED) != 0u;
    RexxValue initial = REXX_VALUE_INIT, to = REXX_VALUE_INIT;
    RexxValue by = REXX_VALUE_INIT, for_value = REXX_VALUE_INIT;
    RexxVMDoFrame *frame;
    RexxVMStatus status;

    if (instruction->flags != 0u || (flags & ~valid_flags) != 0u ||
        (controlled && counted) ||
        ((flags & (REXX_DO_FLAG_HAS_TO | REXX_DO_FLAG_HAS_BY |
                   REXX_DO_FLAG_HAS_FOR)) != 0u && !controlled && !counted) ||
        (counted && (flags & (REXX_DO_FLAG_HAS_TO |
                              REXX_DO_FLAG_HAS_BY)) != 0u) ||
        ((!controlled && !counted) && instruction->operand != REXX_NO_SYMBOL) ||
        (counted && instruction->operand != REXX_NO_SYMBOL))
        return REXX_VM_STATUS_INVALID_BYTECODE;
    if (controlled) {
        const RexxSymbol *symbol = NULL;
        status = parse_get_variable_symbol(chunk, instruction->operand,
                                           &symbol);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    status = ensure_do_frame(vm);
    if (status != REXX_VM_STATUS_OK) return status;
    if ((flags & REXX_DO_FLAG_HAS_FOR) != 0u)
        status = pop_move(vm, &for_value);
    if (status == REXX_VM_STATUS_OK &&
        (flags & REXX_DO_FLAG_HAS_BY) != 0u)
        status = pop_move(vm, &by);
    if (status == REXX_VM_STATUS_OK &&
        (flags & REXX_DO_FLAG_HAS_TO) != 0u)
        status = pop_move(vm, &to);
    if (status == REXX_VM_STATUS_OK && (controlled || counted))
        status = pop_move(vm, &initial);
    if (status != REXX_VM_STATUS_OK) goto cleanup;

    frame = &((RexxVMDoFrame *)vm->do_frames)[vm->do_depth];
    memset(frame, 0, sizeof(*frame));
    frame->flags = flags;
    frame->control_symbol = instruction->operand;
    frame->controlled = controlled;
    frame->counted = counted;
    if (controlled || counted) {
        RexxDecimal initial_decimal;
        status = value_decimal(&initial, &initial_decimal);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        if (controlled) frame->control_value = initial_decimal;
        else {
            RexxNumericStatus numeric_status = rexx_decimal_to_uint32(
                &initial_decimal, &frame->remaining);
            if (numeric_status != REXX_NUMERIC_OK) {
                status = numeric_status == REXX_NUMERIC_RESOURCE_LIMIT
                    ? REXX_VM_STATUS_RESOURCE_LIMIT
                    : REXX_VM_STATUS_NUMERIC_ERROR;
                goto cleanup;
            }
        }
    }
    if ((flags & REXX_DO_FLAG_HAS_TO) != 0u) {
        status = value_decimal(&to, &frame->limit);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        frame->has_limit = 1;
    }
    if ((flags & REXX_DO_FLAG_HAS_BY) != 0u) {
        status = value_decimal(&by, &frame->step);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
    } else {
        status = decimal_status_to_vm(
            rexx_decimal_parse("1", 1u, &frame->step));
    }
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    if (frame->step.sign == 0) {
        status = REXX_VM_STATUS_NUMERIC_ERROR;
        goto cleanup;
    }
    if ((flags & REXX_DO_FLAG_HAS_FOR) != 0u) {
        RexxDecimal iterations;
        uint32_t for_count = 0u;
        RexxNumericStatus numeric_status;
        status = value_decimal(&for_value, &iterations);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        numeric_status = rexx_decimal_to_uint32(&iterations, &for_count);
        if (numeric_status != REXX_NUMERIC_OK) {
            status = numeric_status == REXX_NUMERIC_RESOURCE_LIMIT
                ? REXX_VM_STATUS_RESOURCE_LIMIT : REXX_VM_STATUS_NUMERIC_ERROR;
            goto cleanup;
        }
        if (counted && for_count < frame->remaining)
            frame->remaining = for_count;
        else if (controlled) {
            frame->remaining = for_count;
            frame->has_remaining = 1;
        }
    }
    if (controlled) {
        status = do_control_store(vm, chunk, frame);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
    }
    ++vm->do_depth;
cleanup:
    rexx_value_clear(&initial);
    rexx_value_clear(&to);
    rexx_value_clear(&by);
    rexx_value_clear(&for_value);
    return status;
}

static RexxVMStatus execute_do_test(RexxVM *vm,
                                    const RexxBytecodeChunk *chunk)
{
    int matches = 1;
    RexxVMDoFrame *frame;
    RexxValue value = REXX_VALUE_INIT;
    RexxVMStatus status;
    if (vm->do_depth == 0u) return REXX_VM_STATUS_INVALID_BYTECODE;
    frame = &((RexxVMDoFrame *)vm->do_frames)[vm->do_depth - 1u];
    if (frame->counted || frame->has_remaining)
        matches = frame->remaining != 0u;
    if (matches && frame->controlled && frame->has_limit) {
        int comparison;
        status = do_control_load(vm, chunk, frame);
        if (status != REXX_VM_STATUS_OK) return status;
        status = charge_numeric_work(
            vm, decimal_operation_cost(&frame->control_value, &frame->limit,
                                       OP_SUB, vm->numeric_digits));
        if (status != REXX_VM_STATUS_OK) return status;
        comparison = rexx_decimal_compare(&frame->control_value, &frame->limit);
        matches = frame->step.sign > 0 ? comparison <= 0 : comparison >= 0;
    }
    {
        RexxVMStatus status = rexx_value_set_boolean(&value, matches);
        if (status == REXX_VM_STATUS_OK) status = push_move(vm, &value);
        rexx_value_clear(&value);
        return status;
    }
}

static RexxVMStatus execute_do_next(RexxVM *vm,
                                    const RexxBytecodeChunk *chunk)
{
    RexxVMDoFrame *frame;
    RexxDecimal next;
    RexxVMStatus status;
    RexxNumericStatus numeric_status;
    if (vm->do_depth == 0u) return REXX_VM_STATUS_INVALID_BYTECODE;
    frame = &((RexxVMDoFrame *)vm->do_frames)[vm->do_depth - 1u];
    if (frame->counted) {
        if (frame->remaining != 0u) --frame->remaining;
        return REXX_VM_STATUS_OK;
    }
    if (!frame->controlled) return REXX_VM_STATUS_INVALID_BYTECODE;
    status = do_control_load(vm, chunk, frame);
    if (status != REXX_VM_STATUS_OK) return status;
    status = charge_numeric_work(
        vm, decimal_operation_cost(&frame->control_value, &frame->step,
                                   OP_ADD, vm->numeric_digits));
    if (status != REXX_VM_STATUS_OK) return status;
    numeric_status = rexx_decimal_add(&frame->control_value, &frame->step,
                                      vm->numeric_digits, &next);
    status = decimal_status_to_vm(numeric_status);
    if (status != REXX_VM_STATUS_OK) return status;
    frame->control_value = next;
    if (frame->has_remaining && frame->remaining != 0u)
        --frame->remaining;
    return do_control_store(vm, chunk, frame);
}

static RexxVMStatus return_from_routine(RexxVM *vm, int has_value,
                                        RexxValue *returned)
{
    RexxValue result = REXX_VALUE_INIT;
    truncate_interpret_contexts(vm, vm->routine_depth, 0);
    if (vm->routine_depth == 0u) {
        vm->exit_has_value = has_value;
        if (has_value && returned != NULL) {
            vm->exit_value = *returned;
            memset(returned, 0, sizeof(*returned));
        }
        vm->exit_requested = 1;
        return REXX_VM_STATUS_OK;
    }
    {
        RexxVMCallFrame *frames = (RexxVMCallFrame *)vm->routine_frames;
        RexxVMCallFrame *frame = &frames[vm->routine_depth - 1u];
        RexxVMEnvironment *caller = frame->caller_environment;
        RexxVMEnvironment *callee = frame->callee_environment;
        RexxVMArgument *caller_arguments = frame->caller_arguments;
        size_t caller_argument_count = frame->caller_argument_count;
        size_t stack_base = frame->stack_base;
        size_t do_depth = frame->do_depth;
        size_t return_pc = frame->return_pc;
        const RexxBytecodeChunk *return_chunk = frame->return_chunk;
        int function_call = frame->function_call;
        int condition_call = frame->condition_call;
        int owns_environment = frame->owns_environment;
        uint32_t numeric_digits = frame->numeric_digits;
        uint32_t numeric_fuzz = frame->numeric_fuzz;
        RexxNumericForm numeric_form = frame->numeric_form;
        char *saved_address = frame->address_environment;
        size_t saved_address_length = frame->address_environment_length;
        RexxVMArgument *arguments = frame->arguments;
        size_t argument_count = frame->argument_count;
        RexxVMStatus status = REXX_VM_STATUS_OK;
        if (function_call && !has_value)
            return REXX_VM_STATUS_MISSING_FUNCTION_RESULT;
        if (has_value && returned != NULL) {
            result = *returned;
            memset(returned, 0, sizeof(*returned));
        }
        clear_stack_to(vm, stack_base);
        vm->do_depth = do_depth;
        vm->current_environment = caller;
        if (owns_environment) environment_free(callee);
        vm->current_arguments = caller_arguments;
        vm->current_argument_count = caller_argument_count;
        vm->numeric_digits = numeric_digits;
        vm->numeric_fuzz = numeric_fuzz;
        vm->numeric_form = numeric_form;
        restore_condition_context(vm, frame);
        vm_free(vm->address_environment);
        vm->address_environment = saved_address;
        vm->address_environment_length = saved_address_length;
        vm->active_chunk = return_chunk;
        vm->pc = return_pc;
        free_arguments(arguments, argument_count);
        frame->arguments = NULL;
        frame->address_environment = NULL;
        memset(frame, 0, sizeof(*frame));
        --vm->routine_depth;
        vm->procedure_pending = 0;
        if (function_call) status = push_move(vm, &result);
        else if (condition_call)
            rexx_value_clear(&result);
        else if (has_value)
            status = environment_set(vm, caller, "RESULT", 6u, 0, &result);
        else
            status = environment_drop(vm, caller, "RESULT", 6u, 0);
        rexx_value_clear(&result);
        return status;
    }
}

static void unwind_routines(RexxVM *vm)
{
    RexxVMCallFrame *frames = (RexxVMCallFrame *)vm->routine_frames;
    while (vm->routine_depth != 0u) {
        RexxVMCallFrame *frame = &frames[vm->routine_depth - 1u];
        vm->current_environment = frame->caller_environment;
        vm->do_depth = frame->do_depth;
        vm->current_arguments = frame->caller_arguments;
        vm->current_argument_count = frame->caller_argument_count;
        vm->numeric_digits = frame->numeric_digits;
        vm->numeric_fuzz = frame->numeric_fuzz;
        vm->numeric_form = frame->numeric_form;
        restore_condition_context(vm, frame);
        vm_free(vm->address_environment);
        vm->address_environment = frame->address_environment;
        vm->address_environment_length = frame->address_environment_length;
        frame->address_environment = NULL;
        free_arguments(frame->arguments, frame->argument_count);
        frame->arguments = NULL;
        if (frame->owns_environment)
            environment_free(frame->callee_environment);
        memset(frame, 0, sizeof(*frame));
        --vm->routine_depth;
    }
    vm->procedure_pending = 0;
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
        chunk->parse_item_count > REXX_COMPILER_MAX_POOL_ENTRIES ||
        chunk->parse_template_count > REXX_COMPILER_MAX_POOL_ENTRIES ||
        chunk->parse_plan_count > REXX_COMPILER_MAX_POOL_ENTRIES ||
        (chunk->constant_count != 0 && chunk->constants == NULL) ||
        (chunk->symbol_count != 0 && chunk->symbols == NULL) ||
        (chunk->parse_item_count != 0 && chunk->parse_items == NULL) ||
        (chunk->parse_template_count != 0 &&
         chunk->parse_templates == NULL) ||
        (chunk->parse_plan_count != 0 && chunk->parse_plans == NULL))
        return vm_fail(vm, REXX_VM_STATUS_INVALID_BYTECODE, 0);

    vm->active_chunk = chunk;
    vm->root_chunk = chunk;
    vm->active_parse_overlay = NULL;

    if (vm->routine_depth != 0u) unwind_routines(vm);
    vm->current_environment = vm->root_environment;
    vm->current_arguments = NULL;
    vm->current_argument_count = 0u;
    vm->do_depth = 0u;

    vm_free(vm->condition_description);
    vm->condition_description = NULL;
    vm->condition_description_length = 0u;
    vm->condition_information_active = 0;
    vm->current_condition = 0u;
    vm->condition_invocation = REXX_CONDITION_INVOKED_SIGNAL;
    vm->condition_control_transfer = 0;
    vm->syntax_error_number = 0;
    memset(vm->condition_states, REXX_CONDITION_STATE_OFF,
           sizeof(vm->condition_states));
    memset(vm->condition_actions, REXX_CONDITION_ACTION_SIGNAL,
           sizeof(vm->condition_actions));
    free_condition_target_names(vm->condition_target_names);
    memset(vm->condition_target_name_lengths, 0,
           sizeof(vm->condition_target_name_lengths));
    {
        size_t condition_index;
        for (condition_index = 0u;
             condition_index < REXX_CONDITION_COUNT; ++condition_index)
            vm->condition_targets[condition_index] = REXX_NO_SYMBOL;
    }

    vm->numeric_work_remaining = vm->numeric_work_budget;
    vm->interpret_compile_remaining = vm->interpret_compile_budget;
    vm->dynamic_source_bytes = 0u;
    vm->dynamic_bytecode_bytes = 0u;
    vm->dynamic_instruction_count = 0u;
    truncate_interpret_contexts(vm, 0u, 0);

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

    while (vm->active_chunk != NULL &&
           vm->pc < ((const RexxBytecodeChunk *)vm->active_chunk)
                         ->instruction_count) {
        if (dispatched >= vm->instruction_budget) {
            size_t exhausted_pc = vm->pc;
            unwind_routines(vm);
            truncate_interpret_contexts(vm, 0u, 0);
            vm->do_depth = 0u;
            clear_stack(vm);
            vm->active_chunk = NULL;
            vm->root_chunk = NULL;
            return vm_fail(vm, REXX_VM_STATUS_INSTRUCTION_LIMIT,
                           exhausted_pc);
        }
        ++dispatched;

        chunk = (const RexxBytecodeChunk *)vm->active_chunk;
        size_t current_pc = vm->pc;
        /* Dispatch may finish INTERPRET and free its current chunk. Keep the
         * instruction record in this stack frame until post-dispatch checks. */
        RexxInstruction instruction = chunk->instructions[vm->pc++];
        const RexxInstruction *ins = &instruction;
        RexxVMStatus status = REXX_VM_STATUS_OK;
        RexxValue value;
        vm->current_source_line = physical_source_line(vm, chunk,
                                                        ins->source_line);
        if (vm->routine_depth != 0u) {
            RexxVMCallFrame *frames = (RexxVMCallFrame *)vm->routine_frames;
            RexxVMCallFrame *frame = &frames[vm->routine_depth - 1u];
            if (frame->first_instruction && ins->opcode != OP_PROCEDURE)
                frame->first_instruction = 0;
            if (frame->expose_setup_open && ins->opcode != OP_EXPOSE &&
                ins->opcode != OP_EXPOSE_SUBSIDIARY &&
                ins->opcode != OP_PROCEDURE)
                frame->expose_setup_open = 0;
        }
        memset(&value, 0, sizeof(value));

        switch ((RexxOpcode)ins->opcode) {
        case OP_HALT:
            if (vm->interpret_depth != 0u) {
                RexxVMInterpretFrame *frames =
                    (RexxVMInterpretFrame *)vm->interpret_frames;
                if (frames[vm->interpret_depth - 1u].chunk == chunk) {
                    pop_interpret_context(vm, 1);
                    break;
                }
            }
            if (vm->routine_depth != 0u) {
                status = return_from_routine(vm, 0, NULL);
                break;
            }
            unwind_routines(vm);
            truncate_interpret_contexts(vm, 0u, 0);
            clear_stack(vm);
            vm->status = REXX_VM_STATUS_OK;
            vm->error.status = REXX_VM_STATUS_OK;
            vm->error.pc = current_pc;
            vm->error.message = "ok";
            vm->active_chunk = NULL;
            vm->root_chunk = NULL;
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
            if (status == REXX_VM_STATUS_OK)
                status = environment_set(
                    vm, (RexxVMEnvironment *)vm->current_environment,
                    symbol->name, symbol->length,
                    ins->opcode == OP_STORE_STEM, &value);
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
        case OP_DIVIDE:
        case OP_INTEGER_DIVIDE:
        case OP_REMAINDER:
        case OP_POWER:
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

        case OP_LOGICAL_AND:
        case OP_LOGICAL_OR:
        case OP_LOGICAL_XOR:
            status = binary_logical(vm, (RexxOpcode)ins->opcode);
            break;

        case OP_LOGICAL_NOT:
            status = unary_logical_not(vm);
            break;

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
                if (status == REXX_VM_STATUS_OK)
                    status = raise_command_condition(
                        vm, chunk, command, command_length,
                        vm->current_source_line);
            }
            vm_free(command);
            break;
        }

        case OP_CALL:
            status = execute_call(vm, chunk, ins, &value, NULL, 0u);
            break;

        case OP_INTERPRET:
            if (ins->flags != 0u || ins->operand != 0u ||
                ins->auxiliary != 0u) {
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            } else {
                status = execute_interpret(vm, chunk);
            }
            break;

        case OP_RETURN:
            if ((ins->flags & (uint8_t)~REXX_INSTRUCTION_HAS_VALUE) != 0u ||
                ins->operand != 0u || ins->auxiliary != 0u) {
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            } else {
                int has_value =
                    (ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0u;
                if (has_value) status = pop_move(vm, &value);
                if (status == REXX_VM_STATUS_OK)
                    status = return_from_routine(vm, has_value, &value);
            }
            break;

        case OP_PROCEDURE:
            if (ins->flags != 0u || ins->operand != 0u ||
                ins->auxiliary != 0u || vm->routine_depth == 0u) {
                status = REXX_VM_STATUS_INVALID_PROCEDURE;
            } else {
                RexxVMCallFrame *frames =
                    (RexxVMCallFrame *)vm->routine_frames;
                RexxVMCallFrame *frame = &frames[vm->routine_depth - 1u];
                RexxVMEnvironment *environment;
                if (!frame->first_instruction || frame->procedure_entered) {
                    status = REXX_VM_STATUS_INVALID_PROCEDURE;
                } else {
                    environment = environment_new(
                        vm, (RexxVMEnvironment *)vm->current_environment, 0);
                    if (environment == NULL) {
                        status = REXX_VM_STATUS_OUT_OF_MEMORY;
                    } else {
                        frame->callee_environment = environment;
                        frame->owns_environment = 1;
                        frame->procedure_entered = 1;
                        frame->first_instruction = 0;
                        frame->expose_setup_open = 1;
                        vm->current_environment = environment;
                        vm->procedure_pending = 0;
                    }
                }
            }
            break;

        case OP_EXPOSE:
        case OP_EXPOSE_SUBSIDIARY: {
            const RexxSymbol *symbol = NULL;
            RexxVMEnvironment *environment =
                (RexxVMEnvironment *)vm->current_environment;
            if (ins->flags != 0u || ins->auxiliary != 0u ||
                vm->routine_depth == 0u || environment == NULL ||
                environment->root ||
                !((RexxVMCallFrame *)vm->routine_frames)
                    [vm->routine_depth - 1u].expose_setup_open) {
                status = REXX_VM_STATUS_INVALID_PROCEDURE;
            } else {
                status = parse_get_variable_symbol(chunk, ins->operand,
                                                   &symbol);
                if (status == REXX_VM_STATUS_OK) {
                    if (ins->opcode == OP_EXPOSE_SUBSIDIARY)
                        status = execute_expose_subsidiary(
                            vm, environment, symbol);
                    else
                        status = environment_add_expose(vm, environment,
                                                       symbol);
                }
            }
            break;
        }

        case OP_SIGNAL:
            status = execute_signal(vm, chunk, ins, NULL, 0u);
            break;

        case OP_CONDITION:
            status = execute_condition_registration(vm, chunk, ins);
            break;

        case OP_DROP:
        case OP_DROP_SUBSIDIARY: {
            const RexxSymbol *symbol = NULL;
            if (ins->flags != 0u || ins->auxiliary != 0u) {
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            } else {
                status = parse_get_variable_symbol(chunk, ins->operand,
                                                   &symbol);
                if (status == REXX_VM_STATUS_OK &&
                    ins->opcode == OP_DROP_SUBSIDIARY) {
                    status = execute_drop_subsidiary(vm, symbol);
                } else if (status == REXX_VM_STATUS_OK) {
                    status = environment_drop(
                        vm, (RexxVMEnvironment *)vm->current_environment,
                        symbol->name, symbol->length,
                        symbol->kind == REXX_SYMBOL_STEM);
                }
            }
            break;
        }

        case OP_NUMERIC_DIGITS:
            if (ins->operand != 0u || ins->auxiliary != 0u)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            if ((ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0u)
                if (status == REXX_VM_STATUS_OK)
                    status = pop_move(vm, &value);
            if (status == REXX_VM_STATUS_OK)
                status = execute_numeric_digits(vm, ins->flags, &value);
            rexx_value_clear(&value);
            break;

        case OP_NUMERIC_FUZZ:
            if (ins->operand != 0u || ins->auxiliary != 0u)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            if ((ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0u &&
                status == REXX_VM_STATUS_OK)
                status = pop_move(vm, &value);
            if (status == REXX_VM_STATUS_OK)
                status = execute_numeric_fuzz(vm, ins->flags, &value);
            rexx_value_clear(&value);
            break;

        case OP_NUMERIC_FORM:
            if (ins->auxiliary != 0u)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            if ((ins->flags & REXX_INSTRUCTION_HAS_VALUE) != 0u &&
                status == REXX_VM_STATUS_OK)
                status = pop_move(vm, &value);
            if (status == REXX_VM_STATUS_OK)
                status = execute_numeric_form(vm, ins->flags, ins->operand,
                                              &value);
            rexx_value_clear(&value);
            break;

        case OP_PARSE:
            if (ins->flags != 0u || ins->auxiliary != 0u)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            if (status == REXX_VM_STATUS_OK)
                status = execute_parse(vm, chunk, ins->operand);
            break;

        case OP_DO_SETUP:
            status = execute_do_setup(vm, chunk, ins);
            break;
        case OP_DO_TEST:
            if (ins->flags != 0u || ins->operand != 0u ||
                ins->auxiliary != 0u)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            else
                status = execute_do_test(vm, chunk);
            break;
        case OP_DO_NEXT:
            if (ins->flags != 0u || ins->operand != 0u ||
                ins->auxiliary != 0u)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            else
                status = execute_do_next(vm, chunk);
            break;
        case OP_DO_END:
            if (ins->flags != 0u || ins->operand != 0u ||
                ins->auxiliary != 0u || vm->do_depth == 0u)
                status = REXX_VM_STATUS_INVALID_BYTECODE;
            else
                --vm->do_depth;
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
                    if (status == REXX_VM_STATUS_OK)
                        status = raise_command_condition(
                            vm, chunk, command, command_length,
                            vm->current_source_line);
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
        if (status == REXX_VM_STATUS_CONTROL_TRANSFER) {
            vm->condition_control_transfer = 0;
            continue;
        }
        if (status != REXX_VM_STATUS_OK) {
            if (syntax_error_is_catchable(status) &&
                vm->condition_states[REXX_CONDITION_SYNTAX] ==
                    REXX_CONDITION_STATE_ON) {
                int transferred = 0;
                int32_t syntax_code = syntax_error_code(
                    status, vm->syntax_error_number);
                const char *description = syntax_error_description(
                    status, syntax_code);
                RexxVMStatus trap_status = raise_condition(
                    vm, chunk, REXX_CONDITION_SYNTAX,
                    description, strlen(description), vm->current_source_line,
                    syntax_code, &transferred);
                if (trap_status == REXX_VM_STATUS_CONTROL_TRANSFER &&
                    transferred) {
                    status = REXX_VM_STATUS_OK;
                    vm->syntax_error_number = 0;
                } else if (trap_status != REXX_VM_STATUS_OK) {
                    status = trap_status;
                }
            }
        }
        if (status != REXX_VM_STATUS_OK) {
            vm->pc = current_pc;
            unwind_routines(vm);
            truncate_interpret_contexts(vm, 0u, 0);
            vm->do_depth = 0u;
            clear_stack(vm);
            vm->active_chunk = NULL;
            vm->root_chunk = NULL;
            return vm_fail(vm, status, current_pc);
        }

        if (vm->condition_control_transfer) {
            vm->condition_control_transfer = 0;
            continue;
        }

        if ((RexxOpcode)ins->opcode == OP_EXIT && vm->exit_requested) {
            unwind_routines(vm);
            truncate_interpret_contexts(vm, 0u, 0);
        }
        if (((RexxOpcode)ins->opcode == OP_EXIT ||
             (RexxOpcode)ins->opcode == OP_RETURN) && vm->exit_requested) {
            vm->do_depth = 0u;
            clear_stack(vm);
            vm->status = REXX_VM_STATUS_OK;
            vm->error.status = REXX_VM_STATUS_OK;
            vm->error.pc = vm->pc;
            vm->error.message = "ok";
            vm->active_chunk = NULL;
            vm->root_chunk = NULL;
            return REXX_VM_STATUS_OK;
        }
    }
    truncate_interpret_contexts(vm, 0u, 0);
    clear_stack(vm);
    vm->status = REXX_VM_STATUS_OK;
    vm->error.status = REXX_VM_STATUS_OK;
    vm->error.pc = vm->pc;
    vm->error.message = "ok";
    vm->active_chunk = NULL;
    vm->root_chunk = NULL;
    return REXX_VM_STATUS_OK;
}

const char *rexx_vm_status_name(RexxVMStatus status)
{
    if (status < REXX_VM_STATUS_OK ||
        status > REXX_VM_STATUS_DYNAMIC_SYNTAX)
        return "unknown VM status";
    return vm_messages[status];
}
