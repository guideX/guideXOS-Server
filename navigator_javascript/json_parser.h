#pragma once

#include "object_storage.h"
#include "json_number.h"
#include "runtime_string_storage.h"
#include "source.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

enum class PortableJsonParseErrorCode : unsigned char {
    None = 0,
    InvalidInput,
    InputLimitExceeded,
    SyntaxError,
    InvalidUnicode,
    InvalidNumber,
    NumericConversionUnavailable,
    NestingLimitExceeded,
    StringLimitExceeded,
    ObjectStoreFailure,
    StringStoreFailure,
    ArrayLimitExceeded,
    ExecutionBudgetExceeded,
    InternalInvariantFailure,
};

struct PortableJsonParseError {
    PortableJsonParseErrorCode code = PortableJsonParseErrorCode::None;
    PortableObjectErrorCode objectError = PortableObjectErrorCode::None;
    RuntimeStringErrorCode stringError = RuntimeStringErrorCode::None;
};

using PortableJsonStepConsumer = bool (*)(void* context);
using PortableJsonNumberConverter = bool (*)(SourceView input,
    double& result);

struct PortableJsonParseLimits {
    std::size_t maxInputBytes = 64u * 1024u;
    std::size_t maxStringLength = 64u * 1024u;
    std::size_t maxStringValues = 4096u;
    std::size_t maxStringBytes = 256u * 1024u;
    std::size_t maxNestingDepth = 32u;
    std::size_t maxArrayLength = 1024u;
    std::size_t maxArrayElements = 1024u;
    std::size_t maxTotalArrayElements = 4096u;
    std::size_t maxSteps = 100000u;
    std::uint8_t propertyAttributes = 0x07u;
    PortableJsonNumberConverter numberConverter = nullptr;
    RuntimeObjectId objectPrototype = kInvalidRuntimeObjectId;
    RuntimeObjectId arrayPrototype = kInvalidRuntimeObjectId;
};

struct PortableJsonParseStats {
    std::size_t objectsCreated = 0u;
    std::size_t arrayElementsCreated = 0u;
    std::size_t stringsCreated = 0u;
    std::size_t stringBytesCreated = 0u;
    std::size_t stepsConsumed = 0u;
};

// Parses strict JSON directly into the shared Value, RuntimeStringStore, and
// PortableObjectStore representations. All decoding scratch is caller-owned.
// A failed parse restores the object and string stores to their entry marks.
bool parsePortableJson(SourceView input, PortableObjectStore& objects,
    RuntimeStringStore& strings, PortableJsonParseLimits limits,
    char* decodedScratch, std::size_t decodedScratchCapacity,
    char* keyScratch, std::size_t keyScratchCapacity,
    PortableJsonStepConsumer stepConsumer, void* stepContext,
    Value& result, PortableJsonParseError& error,
    PortableJsonParseStats* stats = nullptr);

// UTF-8 validation is shared with hosted stringify. The accepted binary64
// converter is supplied by an adapter because charconv is hosted-only.
bool decodeJsonUtf8Scalar(const char* bytes, std::size_t length,
    std::size_t& offset, std::uint32_t& scalar);

const char* portableJsonParseErrorCodeName(
    PortableJsonParseErrorCode code);

} // namespace javascript
} // namespace gxos

#include "json_parser.inl"
