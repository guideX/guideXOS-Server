#ifndef GUIDEXOS_REXX_VM_H
#define GUIDEXOS_REXX_VM_H

/*
 * Small, dependency-free execution environment for the native REXX compiler.
 * Values own their string storage. VM tables and evaluation-stack slots own
 * copies of values, and must only be released with the helpers below.
 */

#include "rexx_compiler.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REXX_VM_DEFAULT_STACK_CAPACITY ((size_t)256u)
#define REXX_VM_MAX_STACK_CAPACITY ((size_t)1048576u)
#define REXX_VM_MAX_TABLE_ENTRIES ((size_t)65536u)

typedef enum RexxValueType {
    REXX_VALUE_UNSET = 0,
    REXX_VALUE_NUMBER,
    REXX_VALUE_STRING,
    REXX_VALUE_BOOLEAN
} RexxValueType;

typedef struct RexxValue {
    RexxValueType type;
    union {
        double number;
        int boolean;
        struct {
            char *bytes;
            size_t length;
        } string;
    } as;
} RexxValue;

/* Use this initializer before calling a value setter or clear on a new value. */
#define REXX_VALUE_INIT { 0 }

typedef enum RexxVMStatus {
    REXX_VM_STATUS_OK = 0,
    REXX_VM_STATUS_INVALID_ARGUMENT,
    REXX_VM_STATUS_OUT_OF_MEMORY,
    REXX_VM_STATUS_RESOURCE_LIMIT,
    REXX_VM_STATUS_STACK_OVERFLOW,
    REXX_VM_STATUS_STACK_UNDERFLOW,
    REXX_VM_STATUS_INVALID_BYTECODE,
    REXX_VM_STATUS_NUMERIC_ERROR,
    REXX_VM_STATUS_UNSUPPORTED_OPCODE,
    REXX_VM_STATUS_IO_ERROR
} RexxVMStatus;

typedef struct RexxVMError {
    RexxVMStatus status;
    size_t pc;
    const char *message; /* Static string; caller must not free it. */
} RexxVMError;

typedef struct RexxVMEntry {
    char *name;           /* Canonical ASCII-uppercase key. */
    size_t name_length;
    RexxValue value;
} RexxVMEntry;

typedef struct RexxVMStemEntry {
    char *stem;           /* Includes the period, for example "ITEM.". */
    size_t stem_length;
    char *tail;           /* Remaining compound-variable spelling. */
    size_t tail_length;
    RexxValue value;
} RexxVMStemEntry;

struct RexxVM;
typedef int32_t (*RexxVMCommandDispatcher)(void *user_data,
                                          const char *command,
                                          size_t command_length);
typedef int32_t (*RexxVMAddressDispatcher)(void *user_data,
                                          const char *environment,
                                          size_t environment_length,
                                          const char *command,
                                          size_t command_length);

typedef struct RexxVM {
    RexxValue *stack;
    size_t stack_capacity;
    size_t sp;            /* Number of live entries; top is stack[sp - 1]. */
    size_t pc;            /* Current bytecode instruction index. */

    RexxVMEntry *locals;
    size_t local_count;
    size_t local_capacity;
    RexxVMEntry *globals;
    size_t global_count;
    size_t global_capacity;
    RexxVMStemEntry *stems;
    size_t stem_count;
    size_t stem_capacity;

    int32_t rc;           /* Most recent guideXOS command return code. */
    RexxVMCommandDispatcher command_dispatcher;
    void *command_dispatcher_user_data;
    RexxVMAddressDispatcher address_dispatcher;
    void *address_dispatcher_user_data;

    RexxVMStatus status;
    RexxVMError error;
    int initialized;
} RexxVM;

/* Initialize with the default stack size when stack_capacity is zero. */
RexxVMStatus rexx_vm_init(RexxVM *vm, size_t stack_capacity);
void rexx_vm_free(RexxVM *vm);

/*
 * Executes the implemented compiler opcodes (constants, variable/stem access,
 * ADD/SUB/MULTIPLY, concatenation, GREATER, SAY, plain DO blocks, jumps,
 * stack basics, and ADDRESS). Other opcodes return
 * REXX_VM_STATUS_UNSUPPORTED_OPCODE. Locals/globals persist between calls.
 * OP_ADDRESS with a value consumes that command string and leaves its numeric
 * RC on the evaluation stack; environment-aware dispatch receives the
 * explicit environment or KERNEL by default. `vm->rc` records the latest RC.
 */
RexxVMStatus rexx_vm_execute(RexxVM *vm,
                             const RexxBytecodeChunk *chunk);

/* Optional legacy command-only hook. The environment-aware hook takes
 * precedence; without either hook, ADDRESS returns the safe RC 127 stub. */
void rexx_vm_set_command_dispatcher(RexxVM *vm,
                                    RexxVMCommandDispatcher dispatcher,
                                    void *user_data);

/* Optional environment-aware hook. It takes precedence over the legacy
 * command-only hook; omitted ADDRESS environments are reported as KERNEL. */
void rexx_vm_set_address_dispatcher(RexxVM *vm,
                                    RexxVMAddressDispatcher dispatcher,
                                    void *user_data);

/* The built-in dispatcher never invokes a host shell; it returns RC 127. */
int32_t rexx_vm_guidexos_command_stub(const char *command,
                                      size_t command_length);

/*
 * Convenience API for embedding a VM and supplying predefined variables.
 * Initialize standalone RexxValue objects with REXX_VALUE_INIT before use.
 */
void rexx_value_clear(RexxValue *value);
RexxVMStatus rexx_value_set_number(RexxValue *value, double number);
RexxVMStatus rexx_value_set_boolean(RexxValue *value, int boolean_value);
RexxVMStatus rexx_value_set_string(RexxValue *value,
                                   const char *bytes,
                                   size_t length);
RexxVMStatus rexx_value_copy(RexxValue *destination,
                             const RexxValue *source);
RexxVMStatus rexx_vm_set_local(RexxVM *vm,
                               const char *name,
                               size_t name_length,
                               const RexxValue *value);
RexxVMStatus rexx_vm_set_global(RexxVM *vm,
                                const char *name,
                                size_t name_length,
                                const RexxValue *value);

const char *rexx_vm_status_name(RexxVMStatus status);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_VM_H */
