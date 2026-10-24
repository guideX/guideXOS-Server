#ifndef GUIDEXOS_REXX_VM_H
#define GUIDEXOS_REXX_VM_H

/*
 * Small, dependency-free execution environment for the native REXX compiler.
 * Values own their string storage. VM tables and evaluation-stack slots own
 * copies of values, and must only be released with the helpers below.
 */

#include "rexx_compiler.h"
#include "rexx_data_stack.h"
#include "rexx_output_capability.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REXX_VM_DEFAULT_STACK_CAPACITY ((size_t)256u)
#define REXX_VM_DEFAULT_ADDRESS_ENVIRONMENT "KERNEL"
#define REXX_VM_DEFAULT_TRACE_ACTION 'N'
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
#define REXX_VM_MAX_EXPOSES ((size_t)4096u)
#define REXX_VM_MAX_EXPOSED_NAME_BYTES ((size_t)1048576u)
#define REXX_VM_MAX_SUBSIDIARY_LIST_LENGTH ((size_t)65536u)
#define REXX_VM_MAX_INTERPRET_SOURCE_LENGTH ((size_t)65536u)
#define REXX_VM_MAX_INTERPRET_DEPTH ((size_t)16u)
#define REXX_VM_MAX_DYNAMIC_SOURCE_BYTES ((size_t)262144u)
#define REXX_VM_MAX_DYNAMIC_BYTECODE_BYTES ((size_t)4194304u)
#define REXX_VM_MAX_DYNAMIC_INSTRUCTIONS ((size_t)65536u)
#define REXX_VM_DEFAULT_INTERPRET_COMPILE_BUDGET ((size_t)8388608u)
#define REXX_VM_BYTE_STREAM_STATUS_CAPACITY ((size_t)16u)
#define REXX_VM_BYTE_STREAM_IDENTITY_MAX_BYTES ((size_t)128u)

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
    REXX_VM_STATUS_MISSING_FUNCTION_RESULT,
    REXX_VM_STATUS_UNKNOWN_LABEL,
    REXX_VM_STATUS_INVALID_LABEL,
    REXX_VM_STATUS_INVALID_EXPOSE_LIST,
    REXX_VM_STATUS_CONTROL_TRANSFER,
    REXX_VM_STATUS_DYNAMIC_SYNTAX,
    REXX_VM_STATUS_CLOCK_UNAVAILABLE,
    REXX_VM_STATUS_INVALID_TRACE_REQUEST,
    REXX_VM_STATUS_UNSUPPORTED_TRACE_CAPABILITY,
    REXX_VM_STATUS_INPUT_UNAVAILABLE,
    REXX_VM_STATUS_INPUT_NOT_READY,
    REXX_VM_STATUS_INPUT_END_OF_FILE,
    REXX_VM_STATUS_INPUT_PROVIDER_ERROR,
    REXX_VM_STATUS_INPUT_PENDING_UNSUPPORTED,
    REXX_VM_STATUS_INPUT_RESULT_INVALID,
    REXX_VM_STATUS_BYTE_STREAM_UNAVAILABLE,
    REXX_VM_STATUS_BYTE_STREAM_NOT_READY,
    REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE,
    REXX_VM_STATUS_BYTE_STREAM_UNKNOWN,
    REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR,
    REXX_VM_STATUS_BYTE_STREAM_PENDING_UNSUPPORTED,
    REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION,
    REXX_VM_STATUS_BYTE_STREAM_RESULT_INVALID
} RexxVMStatus;

enum {
    REXX_CLOCK_CAP_WALL_CIVIL = 1u << 0,
    REXX_CLOCK_CAP_MONOTONIC = 1u << 1
};

typedef enum RexxClockWallInterpretation {
    REXX_CLOCK_WALL_UNSPECIFIED = 0,
    REXX_CLOCK_WALL_LOCAL_CIVIL,
    REXX_CLOCK_WALL_UTC_FIXED_OFFSET
} RexxClockWallInterpretation;

typedef struct RexxVMClockReading {
    uint32_t capabilities;
    RexxClockWallInterpretation wall_interpretation;
    int32_t utc_offset_seconds;
    uint32_t wall_resolution_nanoseconds;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t reserved;
    uint32_t fractional_nanoseconds;
    uint64_t monotonic_nanoseconds;
    uint32_t monotonic_resolution_nanoseconds;
    uint32_t reserved2;
} RexxVMClockReading;

typedef enum RexxVMClockReadStatus {
    REXX_VM_CLOCK_READ_OK = 0,
    REXX_VM_CLOCK_READ_UNAVAILABLE,
    REXX_VM_CLOCK_READ_INVALID
} RexxVMClockReadStatus;

typedef RexxVMClockReadStatus (*RexxVMClockReader)(
    void *user_data, RexxVMClockReading *reading);

/* Per-VM clock state. A copy is saved in routine frames so internal calls
 * inherit the timer but cannot change the caller's timer or clause snapshot. */
typedef struct RexxVMClockContext {
    const void *clause_chunk;
    uint32_t clause_id;
    int clause_identified;
    int reading_attempted;
    int reading_valid;
    RexxVMClockReading reading;
    int elapsed_started;
    uint64_t elapsed_start_nanoseconds;
    int elapsed_clause_value_valid;
    uint64_t elapsed_clause_value_nanoseconds;
    int elapsed_clause_reset;
} RexxVMClockContext;

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

/* A provider supplies one immediate line as an immutable borrowed byte span.
 * The callback owns its bytes; the VM copies them before parsing and never
 * frees the provider's pointer. A NULL pointer with zero length is a valid
 * empty line. */
typedef enum RexxVMInputOwnership {
    REXX_VM_INPUT_OWNERSHIP_UNSPECIFIED = 0,
    REXX_VM_INPUT_OWNERSHIP_BORROWED = 1
} RexxVMInputOwnership;

typedef struct RexxVMInputLine {
    const unsigned char *bytes;
    size_t length;
    RexxVMInputOwnership ownership;
} RexxVMInputLine;

typedef enum RexxVMInputReadStatus {
    REXX_VM_INPUT_READ_OK = 0,
    REXX_VM_INPUT_READ_UNAVAILABLE,
    REXX_VM_INPUT_READ_END_OF_INPUT,
    REXX_VM_INPUT_READ_ERROR,
    REXX_VM_INPUT_READ_PENDING
} RexxVMInputReadStatus;

typedef RexxVMInputReadStatus (*RexxVMInputReader)(
    void *user_data, RexxVMInputLine *line);

/* Byte streams are resolved by identity through an installed per-VM provider.
 * The embedding owns the provider context and keeps it alive until the
 * provider is cleared or the VM is freed. `identity` is an opaque callback
 * handle and is never retained by the VM. `stable_identity` is a length-bearing
 * byte key borrowed only for the resolver call; the VM copies it into a
 * bounded condition table (up to 16 streams, 128 identity bytes each). A
 * zero-length stable identity is valid only when its pointer is non-NULL.
 * `identity_generation` is nonzero and changes when the provider reuses the
 * same stable identity for a different logical stream lifetime. Replacing the
 * provider also clears all state.
 * The provider owns each stream cursor and advances it only by the byte count
 * returned with READ_OK; non-OK callbacks leave it unchanged. Read spans are
 * borrowed until the next provider callback. The VM copies them immediately
 * and never frees a handle, buffer, context, or provider resource. */
typedef enum RexxVMByteStreamResolveStatus {
    REXX_VM_BYTE_STREAM_RESOLVE_OK = 0,
    REXX_VM_BYTE_STREAM_RESOLVE_UNKNOWN,
    REXX_VM_BYTE_STREAM_RESOLVE_UNAVAILABLE,
    REXX_VM_BYTE_STREAM_RESOLVE_ERROR
} RexxVMByteStreamResolveStatus;

enum {
    REXX_VM_BYTE_STREAM_CAP_READABLE = 1u << 0,
    REXX_VM_BYTE_STREAM_CAP_SEEKABLE = 1u << 1
};

typedef enum RexxVMByteStreamIdentityOwnership {
    REXX_VM_BYTE_STREAM_IDENTITY_UNSPECIFIED = 0,
    REXX_VM_BYTE_STREAM_IDENTITY_BORROWED = 1
} RexxVMByteStreamIdentityOwnership;

typedef struct RexxVMByteStreamInfo {
    const void *identity; /* Opaque callback handle; never retained by VM. */
    const unsigned char *stable_identity;
    size_t stable_identity_length;
    uint32_t capabilities;
    RexxVMByteStreamIdentityOwnership ownership;
    uint64_t identity_generation;
} RexxVMByteStreamInfo;

typedef enum RexxVMByteStreamConditionState {
    REXX_VM_BYTE_STREAM_CONDITION_UNKNOWN = 0,
    REXX_VM_BYTE_STREAM_CONDITION_RESOLVED,
    REXX_VM_BYTE_STREAM_CONDITION_READY,
    REXX_VM_BYTE_STREAM_CONDITION_ERROR
} RexxVMByteStreamConditionState;

typedef enum RexxVMByteStreamOperation {
    REXX_VM_BYTE_STREAM_OPERATION_NONE = 0,
    REXX_VM_BYTE_STREAM_OPERATION_RESOLVE,
    REXX_VM_BYTE_STREAM_OPERATION_READ,
    REXX_VM_BYTE_STREAM_OPERATION_SEEK,
    REXX_VM_BYTE_STREAM_OPERATION_ZERO_LENGTH_READ
} RexxVMByteStreamOperation;

typedef enum RexxVMByteStreamErrorClass {
    REXX_VM_BYTE_STREAM_ERROR_NONE = 0,
    REXX_VM_BYTE_STREAM_ERROR_SHORT_READ,
    REXX_VM_BYTE_STREAM_ERROR_END_OF_STREAM,
    REXX_VM_BYTE_STREAM_ERROR_NOT_READY,
    REXX_VM_BYTE_STREAM_ERROR_PROVIDER,
    REXX_VM_BYTE_STREAM_ERROR_PENDING_UNSUPPORTED,
    REXX_VM_BYTE_STREAM_ERROR_UNSUPPORTED_OPERATION,
    REXX_VM_BYTE_STREAM_ERROR_INVALID_METADATA,
    REXX_VM_BYTE_STREAM_ERROR_INVALID_POSITION,
    REXX_VM_BYTE_STREAM_ERROR_RESULT_COPY,
    REXX_VM_BYTE_STREAM_ERROR_UNAVAILABLE
} RexxVMByteStreamErrorClass;

/* A caller-owned, fixed-size snapshot. `valid` is zero for an unknown stream.
 * The identity bytes are copied into this value; no provider pointer escapes. */
typedef struct RexxVMByteStreamConditionStatus {
    uint8_t valid;
    RexxVMByteStreamConditionState state;
    RexxVMByteStreamOperation last_operation;
    RexxVMByteStreamErrorClass error_class;
    uint8_t eof_after_last_read;
    uint64_t provider_generation;
    uint64_t identity_generation;
    RexxVMStatus last_status;
    uint32_t diagnostic_code;
    size_t identity_length;
    size_t requested_length;
    size_t returned_length;
    unsigned char identity[REXX_VM_BYTE_STREAM_IDENTITY_MAX_BYTES];
} RexxVMByteStreamConditionStatus;

typedef enum RexxVMByteStreamReadStatus {
    REXX_VM_BYTE_STREAM_READ_OK = 0,
    REXX_VM_BYTE_STREAM_READ_EOF,
    REXX_VM_BYTE_STREAM_READ_UNAVAILABLE,
    REXX_VM_BYTE_STREAM_READ_ERROR,
    REXX_VM_BYTE_STREAM_READ_PENDING,
    REXX_VM_BYTE_STREAM_READ_UNSUPPORTED
} RexxVMByteStreamReadStatus;

typedef enum RexxVMByteStreamSeekStatus {
    REXX_VM_BYTE_STREAM_SEEK_OK = 0,
    REXX_VM_BYTE_STREAM_SEEK_UNSUPPORTED,
    REXX_VM_BYTE_STREAM_SEEK_UNAVAILABLE,
    REXX_VM_BYTE_STREAM_SEEK_ERROR,
    REXX_VM_BYTE_STREAM_SEEK_INVALID_POSITION
} RexxVMByteStreamSeekStatus;

typedef struct RexxVMByteStreamRead {
    const unsigned char *bytes;
    size_t length;
    RexxVMInputOwnership ownership;
    /* Nonzero means this successful read reached EOF after these bytes. */
    uint8_t eof_after_read;
} RexxVMByteStreamRead;

typedef RexxVMByteStreamResolveStatus (*RexxVMByteStreamResolver)(
    void *user_data, int use_default, const unsigned char *name,
    size_t name_length, RexxVMByteStreamInfo *stream);
typedef RexxVMByteStreamReadStatus (*RexxVMByteStreamReader)(
    void *user_data, const void *identity, size_t requested,
    RexxVMByteStreamRead *read_result);
typedef RexxVMByteStreamSeekStatus (*RexxVMByteStreamSeeker)(
    void *user_data, const void *identity, uint64_t one_based_position);

typedef struct RexxVMByteStreamProvider {
    RexxVMByteStreamResolver resolve;
    RexxVMByteStreamReader read;
    RexxVMByteStreamSeeker seek;
} RexxVMByteStreamProvider;

typedef struct RexxVM {
    RexxValue *stack;
    size_t stack_capacity;
    size_t sp;            /* Number of live entries; top is stack[sp - 1]. */
    RexxDataStack data_stack; /* Per-VM REXX external data stack. */
    size_t pc;            /* Current bytecode instruction index. */
    size_t current_source_line; /* Source line of the active instruction. */
    const RexxBytecodeChunk *active_chunk; /* Valid only during execute. */
    const RexxBytecodeChunk *root_chunk; /* Original program; owns label space. */
    size_t instruction_budget; /* Maximum dispatches allowed by one execute. */
    uint32_t numeric_digits; /* Per-VM NUMERIC DIGITS context. */
    uint32_t numeric_fuzz;   /* Per-VM NUMERIC FUZZ context. */
    RexxNumericForm numeric_form; /* Per-VM NUMERIC FORM context. */
    /* Program-level RANDOM state. Deliberately not saved in routine frames. */
    uint64_t random_state;
    uint8_t random_initialized;
    size_t numeric_work_budget;
    size_t numeric_work_remaining;
    char *argument_string; /* Invocation string used by PARSE ARG. */
    size_t argument_string_length;
    void *root_environment; /* Private variable-generation storage. */
    void *current_environment;
    void *active_parse_overlay; /* Private tentative writes during PARSE. */
    void *routine_frames;
    size_t routine_depth;
    size_t routine_frame_capacity;
    void *interpret_frames; /* Private bounded dynamic-code continuations. */
    size_t interpret_depth;
    size_t interpret_frame_capacity;
    size_t dynamic_source_bytes;
    size_t dynamic_bytecode_bytes;
    size_t dynamic_instruction_count;
    size_t interpret_compile_budget;
    size_t interpret_compile_remaining;
    RexxVMClockReader clock_reader;
    void *clock_reader_user_data;
    RexxVMClockContext clock_context;
    RexxVMInputReader input_reader;
    void *input_reader_user_data;
    RexxVMByteStreamProvider byte_stream_provider;
    void *byte_stream_provider_user_data;
    uint8_t byte_stream_callback_active;
    uint64_t byte_stream_provider_generation;
    size_t byte_stream_status_count;
    RexxVMByteStreamConditionStatus
        byte_stream_statuses[REXX_VM_BYTE_STREAM_STATUS_CAPACITY];
    void *do_frames; /* Private bounded storage for active DO instructions. */
    size_t do_depth;
    size_t do_frame_capacity;
    void *current_arguments; /* Private argument vector for active routine. */
    size_t current_argument_count;
    int procedure_pending; /* Set on routine entry; cleared by first opcode. */

    /* Classic condition-trap context. The arrays are per-VM and bounded by
     * REXX_CONDITION_COUNT; descriptions are owned by this VM. */
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
    int condition_control_transfer;
    int condition_call_pending;
    int32_t syntax_error_number;

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
    char trace_action; /* Operationally supported actions: E, F, N, and O. */
    uint8_t trace_enabled; /* Explicit TRACE option requested in this VM. */
    RexxVMCommandDispatcher command_dispatcher;
    void *command_dispatcher_user_data;
    RexxVMAddressDispatcher address_dispatcher;
    void *address_dispatcher_user_data;
    RexxVMOutputWriter output_writer;
    void *output_writer_user_data;
    RexxVMOutputCapabilityReader output_capability_reader;
    uint64_t output_provider_generation;
    uint8_t output_capability_callback_active;

    RexxVMStatus status;
    RexxVMError error;
    RexxValue exit_value;
    int exit_requested;
    int exit_has_value;
    uint8_t execution_active; /* Reject callback replacement/reentry while executing. */
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

/* Install a read-only destination query for the same per-VM context used by
 * the output writer. The caller owns that context and keeps it alive until
 * both callbacks are cleared/replaced and any active callback has returned.
 * The VM retains no snapshot; every query calls the provider. */
RexxVMStatus rexx_vm_set_output_capability_reader(
    RexxVM *vm, RexxVMOutputCapabilityReader reader);
RexxVMOutputCapabilityQueryStatus rexx_vm_get_output_capability(
    RexxVM *vm, RexxVMOutputCapability *capability);

/* Return the active environment identity as a borrowed, length-bearing view.
 * The initial environment is REXX_VM_DEFAULT_ADDRESS_ENVIRONMENT. */
RexxVMStatus rexx_vm_get_address_environment(const RexxVM *vm,
                                             const char **environment,
                                             size_t *environment_length);

/* Install a clock source for this VM only. Passing NULL restores the
 * deterministic unavailable-clock behavior. */
void rexx_vm_set_clock_reader(RexxVM *vm,
                              RexxVMClockReader reader,
                              void *user_data);

/* Install an optional per-VM PULL input provider. Passing NULL clears it.
 * The callback must return immediately; PENDING is reported as unsupported.
 * Its borrowed bytes must remain valid until the next callback or until the
 * reader is replaced while the VM is quiescent. This setter rejects changes
 * made reentrantly during execution. Concurrent VM access is unsupported. */
RexxVMStatus rexx_vm_set_input_reader(RexxVM *vm,
                                      RexxVMInputReader reader,
                                      void *user_data);

/* Install or clear the optional per-VM RX33 byte-stream provider. Callback
 * tables are copied into the VM; the host owns user_data and every identity.
 * Replacement is permitted only while the VM is quiescent. */
RexxVMStatus rexx_vm_set_byte_stream_provider(
    RexxVM *vm, const RexxVMByteStreamProvider *provider, void *user_data);

/* Read-only query by the stable, provider-resolved identity bytes. It never
 * calls the provider or changes the cursor. Unknown identities return OK with
 * `status->valid == 0` and state UNKNOWN. The output is fully caller-owned. */
RexxVMStatus rexx_vm_query_byte_stream_status(
    const RexxVM *vm, const unsigned char *stable_identity,
    size_t stable_identity_length,
    RexxVMByteStreamConditionStatus *status);

/* Resolve a caller-facing stream name for observation without reading, seeking,
 * or allocating a status-table entry. */
RexxVMStatus rexx_vm_resolve_byte_stream_status(
    RexxVM *vm, int use_default, const unsigned char *name, size_t name_length,
    RexxVMByteStreamConditionStatus *status);

/* Execute one bounded CHARIN byte read. `use_default` distinguishes an
 * omitted name from an explicit zero-length name. The provider owns cursor
 * advancement; the VM only asks it to seek or read. */
RexxVMStatus rexx_vm_charin(RexxVM *vm, int use_default,
                            const unsigned char *name, size_t name_length,
                            int has_start, uint64_t one_based_start,
                            size_t requested_length, RexxValue *result);

/* Snapshot access is cached per executed REXX clause. */
RexxVMStatus rexx_vm_clock_read_for_clause(
    RexxVM *vm, RexxVMClockReading *reading);

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
/* Allocate an owned string of exactly `length` bytes for direct filling.
 * On success, `bytes_out` points to writable storage with a trailing NUL. */
RexxVMStatus rexx_value_allocate_string(RexxValue *value,
                                        size_t length,
                                        char **bytes_out);
RexxVMStatus rexx_value_copy(RexxValue *destination,
                             const RexxValue *source);

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
/* Hosted qualification-only controls. The hook affects VM-owned allocations
 * in rexx_vm.c; it is not compiled into normal or freestanding builds. */
void rexx_vm_test_allocator_fail_on(size_t allocation_attempt);
void rexx_vm_test_allocator_disable(void);
size_t rexx_vm_test_allocator_attempts(void);
size_t rexx_vm_test_allocator_live_blocks(void);
/* Inject a failure at a CHARIN borrowed-buffer copy point. These controls are
 * hosted test-only and are absent from normal and freestanding builds. */
void rexx_vm_test_byte_stream_copy_fail_on(size_t copy_attempt);
void rexx_vm_test_byte_stream_copy_disable(void);
size_t rexx_vm_test_byte_stream_copy_attempts(void);
RexxVMStatus rexx_vm_test_raise_condition(
    RexxVM *vm, const RexxBytecodeChunk *chunk, RexxConditionType condition,
    const char *description, size_t description_length, size_t source_line,
    int *transferred);
#endif
RexxVMStatus rexx_vm_set_local(RexxVM *vm,
                               const char *name,
                               size_t name_length,
                               const RexxValue *value);
RexxVMStatus rexx_vm_set_global(RexxVM *vm,
                                const char *name,
                                size_t name_length,
                                const RexxValue *value);

/* Read-only SYMBOL() support. The name must already be lexically valid.
 * Compound tails are resolved in the active variable generation without
 * creating variables or raising NOVALUE. */
RexxVMStatus rexx_vm_symbol_is_initialized(RexxVM *vm,
                                           const char *name,
                                           size_t name_length,
                                           int *initialized);

/* Read a lexically validated variable symbol from the active environment.
 * An uninitialized symbol returns its resolved uppercase name without
 * raising NOVALUE. When new_value is non-NULL, store it after reading and
 * return the prior value. old_value must be initialized and distinct from
 * new_value. */
RexxVMStatus rexx_vm_value_access(RexxVM *vm,
                                 const char *name,
                                 size_t name_length,
                                 const RexxValue *new_value,
                                 RexxValue *old_value);

const char *rexx_vm_status_name(RexxVMStatus status);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_VM_H */
