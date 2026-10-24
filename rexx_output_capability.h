#ifndef GUIDEXOS_REXX_OUTPUT_CAPABILITY_H
#define GUIDEXOS_REXX_OUTPUT_CAPABILITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REXX_VM_OUTPUT_CAPABILITY_MAX_WIDTH ((uint32_t)1048576u)

typedef enum RexxVMOutputDestinationKind {
    REXX_VM_OUTPUT_DESTINATION_NONE = 0,
    REXX_VM_OUTPUT_DESTINATION_INTERACTIVE,
    REXX_VM_OUTPUT_DESTINATION_REDIRECTED,
    REXX_VM_OUTPUT_DESTINATION_BACKGROUND,
    REXX_VM_OUTPUT_DESTINATION_RECORD
} RexxVMOutputDestinationKind;

typedef enum RexxVMOutputCapabilityState {
    REXX_VM_OUTPUT_CAPABILITY_KNOWN = 0,
    REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH,
    REXX_VM_OUTPUT_CAPABILITY_DESTINATION_NOT_INSTALLED,
    REXX_VM_OUTPUT_CAPABILITY_TEMPORARILY_UNKNOWN,
    REXX_VM_OUTPUT_CAPABILITY_STALE
} RexxVMOutputCapabilityState;

typedef enum RexxVMOutputWidthUnit {
    REXX_VM_OUTPUT_WIDTH_NONE = 0,
    REXX_VM_OUTPUT_WIDTH_CHARACTER_COLUMNS,
    REXX_VM_OUTPUT_WIDTH_OUTPUT_BYTES,
    REXX_VM_OUTPUT_WIDTH_RECORD_BYTES,
    REXX_VM_OUTPUT_WIDTH_PIXELS
} RexxVMOutputWidthUnit;

typedef enum RexxVMOutputWrapPolicy {
    REXX_VM_OUTPUT_WRAP_UNKNOWN = 0,
    REXX_VM_OUTPUT_WRAP_NONE,
    REXX_VM_OUTPUT_WRAP_EXPLICIT_NEWLINES,
    REXX_VM_OUTPUT_WRAP_DESTINATION_WIDTH,
    REXX_VM_OUTPUT_WRAP_RECORD_BOUNDARY,
    REXX_VM_OUTPUT_WRAP_STORAGE_LIMIT
} RexxVMOutputWrapPolicy;

/* A by-value, pointer-free view of the currently installed destination. A
 * known width is valid only for this destination generation and the wrap
 * policy/unit pair reported here. */
typedef struct RexxVMOutputCapability {
    uint32_t struct_size;
    RexxVMOutputCapabilityState state;
    RexxVMOutputDestinationKind destination_kind;
    RexxVMOutputWidthUnit width_unit;
    RexxVMOutputWrapPolicy wrap_policy;
    uint32_t logical_line_width;
    uint32_t record_length;
    uint32_t reserved;
    uint64_t destination_identity;
    uint64_t destination_generation;
} RexxVMOutputCapability;

typedef enum RexxVMOutputCapabilityReadStatus {
    REXX_VM_OUTPUT_CAPABILITY_READ_OK = 0,
    REXX_VM_OUTPUT_CAPABILITY_READ_FAILURE,
    REXX_VM_OUTPUT_CAPABILITY_READ_UNSUPPORTED
} RexxVMOutputCapabilityReadStatus;

typedef RexxVMOutputCapabilityReadStatus (*RexxVMOutputCapabilityReader)(
    void *user_data, RexxVMOutputCapability *capability);

typedef enum RexxVMOutputCapabilityQueryStatus {
    REXX_VM_OUTPUT_CAPABILITY_QUERY_OK = 0,
    REXX_VM_OUTPUT_CAPABILITY_QUERY_INVALID_ARGUMENT,
    REXX_VM_OUTPUT_CAPABILITY_QUERY_NO_PROVIDER,
    REXX_VM_OUTPUT_CAPABILITY_QUERY_PROVIDER_FAILURE,
    REXX_VM_OUTPUT_CAPABILITY_QUERY_UNSUPPORTED,
    REXX_VM_OUTPUT_CAPABILITY_QUERY_INVALID_METADATA,
    REXX_VM_OUTPUT_CAPABILITY_QUERY_STALE
} RexxVMOutputCapabilityQueryStatus;

#ifdef __cplusplus
}
#endif

#endif
