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
#define REXX_VM_MAX_PARSE_SOURCE_LENGTH ((size_t)16777216u)
#define REXX_VM_MAX_PARSE_WORK ((size_t)16777216u)
#define REXX_VM_DEFAULT_INSTRUCTION_BUDGET ((size_t)1000000u)
#define REXX_VM_DEFAULT_NUMERIC_DIGITS ((uint32_t)9u)
#define REXX_VM_DEFAULT_NUMERIC_WORK_BUDGET ((size_t)10000000u)
#define REXX_VM_MAX_ROUTINE_DEPTH ((size_t)64u)
#define REXX_VM_MAX_ROUTINE_ARGUMENTS ((size_t)256u)
#define REXX_VM_MAX_DO_DEPTH ((size_t)64u)

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
    REXX_VM_STATUS_IO_ERROR,
    REXX_VM_STATUS_UNKNOWN_FUNCTION,
    REXX_VM_STATUS_INVALID_FUNCTION_CALL,
    REXX_VM_STATUS_INSTRUCTION_LIMIT,
    REXX_VM_STATUS_INVALID_NUMERIC_SETTING,
    REXX_VM_STATUS_INVALID_NUMERIC_FUZZ,
    REXX_VM_STATUS_INVALID_NUMERIC_FORM,
    REXX_VM_STATUS_UNKNOWN_ROUTINE,
    REXX_VM_STATUS_ROUTINE_DEPTH_LIMIT,
    REXX_VM_STATUS_INVALID_PROCEDURE,
    REXX_VM_STATUS_MISSING_FUNCTION_RESULT
} RexxVMStatus;

typedef enum RexxNumericForm {
    REXX_NUMERIC_FORM_SCIENTIFIC = 0,
    REXX_NUMERIC_FORM_ENGINEERING = 1
} RexxNumericForm;

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
typedef int32_t (*RexxVMOutputWriter)(void *user_data,
                                      const char *bytes,
                                      size_t length);

typedef struct RexxVM {
    RexxValue *stack;
    size_t stack_capacity;
    size_t sp;            /* Number of live entries; top is stack[sp - 1]. */
    size_t pc;            /* Current bytecode instruction index. */
    size_t instruction_budget; /* Maximum dispatches allowed by one execute. */
    uint32_t numeric_digits; /* Per-VM NUMERIC DIGITS context. */
    uint32_t numeric_fuzz;   /* Per-VM NUMERIC FUZZ context. */
    RexxNumericForm numeric_form; /* Per-VM NUMERIC FORM context. */
    size_t numeric_work_budget;
    size_t numeric_work_remaining;
    char *argument_string; /* Invocation string used by PARSE ARG. */
    size_t argument_string_length;
    void *root_environment; /* Private variable-generation storage. */
    void *current_environment;
    void *routine_frames;
    size_t routine_depth;
    size_t routine_frame_capacity;
    void *do_frames; /* Private bounded storage for active DO instructions. */
    size_t do_depth;
    size_t do_frame_capacity;
    void *current_arguments; /* Private argument vector for active routine. */
    size_t current_argument_count;
    int procedure_pending; /* Set on routine entry; cleared by first opcode. */

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
    char *address_environment; /* NULL means the initial KERNEL environment. */
    size_t address_environment_length;
    RexxVMCommandDispatcher command_dispatcher;
    void *command_dispatcher_user_data;
    RexxVMAddressDispatcher address_dispatcher;
    void *address_dispatcher_user_data;
    RexxVMOutputWriter output_writer;
    void *output_writer_user_data;

    RexxVMStatus status;
    RexxVMError error;
    RexxValue exit_value;
    int exit_requested;
    int exit_has_value;
    int initialized;
} RexxVM;

/* Initialize with the default stack size when stack_capacity is zero. */
RexxVMStatus rexx_vm_init(RexxVM *vm, size_t stack_capacity);
void rexx_vm_free(RexxVM *vm);

/* Set the maximum number of bytecode instructions dispatched by one execute.
 * Zero is rejected so execution always remains bounded. */
RexxVMStatus rexx_vm_set_instruction_budget(RexxVM *vm, size_t budget);

/* Set the command/program invocation string consumed by PARSE ARG. */
RexxVMStatus rexx_vm_set_argument_string(RexxVM *vm,
                                         const char *bytes,
                                         size_t length);

/*
 * Executes the compiler opcodes implemented by this VM. Other opcodes return
 * REXX_VM_STATUS_UNSUPPORTED_OPCODE. Locals/globals persist. ADDRESS and
 * command clauses consume their command string, update `vm->rc`, and expose
 * that return code through the REXX RC variable; they leave no stack value.
 * ADDRESS environment changes the active environment, while ADDRESS
 * environment command dispatches one command without changing it. The
 * default environment is KERNEL. EXIT terminates successfully and stores its
 * optional result in `exit_value`. Each call is bounded by instruction_budget.
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

/* Optional per-VM SAY output. Without a writer hosted builds use stdout;
 * freestanding builds fail the SAY instruction with an I/O error. */
void rexx_vm_set_output_writer(RexxVM *vm,
                               RexxVMOutputWriter writer,
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

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
/* Hosted qualification-only controls. The hook affects VM-owned allocations
 * in rexx_vm.c; it is not compiled into normal or freestanding builds. */
void rexx_vm_test_allocator_fail_on(size_t allocation_attempt);
void rexx_vm_test_allocator_disable(void);
size_t rexx_vm_test_allocator_attempts(void);
size_t rexx_vm_test_allocator_live_blocks(void);
#endif
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
