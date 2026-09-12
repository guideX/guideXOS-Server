#pragma once

#include "../../../sdk/include/guidexos/development_debug.h"

namespace kernel {
namespace native_elf {

// These limits are debugger limits, independent of compiler source limits.
// The evaluator owns no target memory and uses only fixed-size local storage.
static const uint32_t NATIVE_DEBUG_WATCH_MAX_EXPRESSION_BYTES = 256;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_TOKENS = 96;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_IDENTIFIER_BYTES = 64;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_NUMERIC_LITERAL_BYTES = 32;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_AST_NODES = 64;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_PARSE_DEPTH = 16;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_OPERATORS = 32;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_RESULT_BYTES = 64;
static const uint32_t NATIVE_DEBUG_WATCH_MAX_DIAGNOSTIC_BYTES = 128;

enum class NativeDebugWatchStatus : uint32_t {
    Success = 0,
    SyntaxError,
    UnknownIdentifier,
    NotLive,
    InvalidSelectedFrame,
    StaleGeneration,
    UnsupportedType,
    UnsupportedOperator,
    DivideByZero,
    Overflow,
    TooComplex,
    Running,
    MetadataUnavailable
};

enum class NativeDebugWatchValueType : uint32_t {
    SignedInt32 = 1,
    Pointer = 2,
    Boolean = 3
};

enum class NativeDebugWatchResolveStatus : uint32_t {
    Available = 0,
    UnknownIdentifier,
    NotLive,
    UnsupportedType,
    MetadataUnavailable
};

typedef NativeDebugWatchResolveStatus (*NativeDebugWatchResolveIdentifier)(
    void* context, const char* identifier);

// This is the authenticated Phase 28H query result plus the selected frame's
// identity.  No target address is accepted by the evaluator.
struct NativeDebugWatchFrame {
    bool paused;
    uint32_t frameIndex;
    uint64_t sessionGeneration;
    uint64_t stopGeneration;
    uint64_t instructionPointer;
    uint64_t framePointer;
    const gx_development_debug_variables* variables;
    bool variableMetadataAvailable;
    void* resolverContext;
    NativeDebugWatchResolveIdentifier resolveIdentifier;
};

struct NativeDebugWatchResult {
    NativeDebugWatchStatus status;
    NativeDebugWatchValueType type;
    uint64_t rawValue;
    int64_t signedValue;
    uint64_t unsignedValue;
    bool booleanValue;
    uint32_t frameIndex;
    uint64_t sessionGeneration;
    uint64_t stopGeneration;
    uint32_t tokenCount;
    uint32_t nodeCount;
    uint32_t operatorCount;
    uint32_t diagnosticOffset;
    char formatted[NATIVE_DEBUG_WATCH_MAX_RESULT_BYTES];
    char diagnostic[NATIVE_DEBUG_WATCH_MAX_DIAGNOSTIC_BYTES];
};

const char* native_debug_watch_status_name(NativeDebugWatchStatus status);
const char* native_debug_watch_value_type_name(NativeDebugWatchValueType type);

// Parse and evaluate one bounded, read-only watch against one authenticated
// selected-frame snapshot.  This function never reads target memory, changes
// registers, resumes execution, or changes the selected frame.
bool native_debug_watch_evaluate(const char* expression,
                                 const NativeDebugWatchFrame& frame,
                                 NativeDebugWatchResult* result);

} // namespace native_elf
} // namespace kernel
