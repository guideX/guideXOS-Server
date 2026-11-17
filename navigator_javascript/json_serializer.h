#pragma once

#include "json_number_format.h"
#include "json_writer.h"
#include "object_storage.h"
#include "runtime_string_storage.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

enum class PortableJsonSerializeStatus : std::uint8_t {
    Error = 0,
    Success,
    NoResult,
};

enum class PortableJsonSerializeErrorCode : std::uint8_t {
    None = 0,
    InvalidInput,
    InvalidValue,
    StaleObjectHandle,
    StaleStringHandle,
    OutputCapacityExceeded,
    ArrayLimitExceeded,
    TraversalCapacityExceeded,
    DepthLimitExceeded,
    StepBudgetExceeded,
    CycleDetected,
    UnsupportedCallback,
    HostedOperationRequired,
    UnsupportedPropertyAttributes,
    UnsupportedValue,
    NumericFormattingFailure,
    InvalidStringState,
    ObjectStoreFailure,
    InternalInvariantFailure,
};

struct PortableJsonSerializeError {
    PortableJsonSerializeErrorCode code =
        PortableJsonSerializeErrorCode::None;
    PortableObjectErrorCode objectError = PortableObjectErrorCode::None;
    RuntimeStringErrorCode stringError = RuntimeStringErrorCode::None;
    PortableJsonWriteErrorCode writerError =
        PortableJsonWriteErrorCode::None;
    PortableJsonNumberFormatCode numberError =
        PortableJsonNumberFormatCode::None;
};

struct PortableJsonSerializeStats {
    std::size_t stepsConsumed = 0u;
    std::size_t propertiesVisited = 0u;
    std::size_t arraySlotsVisited = 0u;
    std::size_t propertyStorageInspections = 0u;
    std::size_t containersEntered = 0u;
    std::size_t maximumDepth = 0u;
};

struct PortableJsonSerializeResult {
    PortableJsonSerializeStatus status = PortableJsonSerializeStatus::Error;
    PortableJsonSerializeError error;
    SourceView output;
    PortableJsonSerializeStats stats;
};

using PortableJsonSerializeStepConsumer = bool (*)(void* context);

enum class PortableJsonObjectDisposition : std::uint8_t {
    Supported = 0,
    HostedOperationRequired,
};

using PortableJsonObjectValidator = PortableJsonObjectDisposition (*)(
    void* context, RuntimeObjectId object);

struct PortableJsonTraversalFrame {
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    PortableObjectPropertyCursor propertyCursor;
    std::size_t cursor = 0u;
    std::size_t arrayLength = 0u;
    std::size_t emittedProperties = 0u;
    bool array = false;
};

struct PortableJsonSerializeStorageView {
    PortableJsonTraversalFrame* frames = nullptr;
    std::size_t frameCapacity = 0u;
    char* numberScratch = nullptr;
    std::size_t numberScratchCapacity = 0u;
};

struct PortableJsonSerializeLimits {
    std::size_t maxOutputBytes = 64u * 1024u;
    std::size_t maxNestingDepth = 32u;
    std::size_t maxArrayLength = 1024u;
    std::size_t maxSteps = 100000u;
};

// Serializes callback-free JSON values using the authoritative object and
// string stores and the caller-provided bounded writer/traversal storage.
PortableJsonSerializeResult serializePortableJson(const Value& value,
    PortableObjectStore& objects, RuntimeStringStore& strings,
    PortableJsonOutputWriter& writer,
    PortableJsonSerializeStorageView storage,
    PortableJsonSerializeLimits limits = PortableJsonSerializeLimits(),
    PortableJsonSerializeStepConsumer stepConsumer = nullptr,
    void* stepContext = nullptr, PortableJsonObjectValidator objectValidator =
        nullptr, void* objectValidatorContext = nullptr);

const char* portableJsonSerializeErrorCodeName(
    PortableJsonSerializeErrorCode code);

} // namespace javascript
} // namespace gxos

#include "json_serializer.inl"
