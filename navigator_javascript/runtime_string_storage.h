#pragma once

#include "source.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

enum class RuntimeStringErrorCode : std::uint8_t {
    None = 0,
    InvalidStorage,
    InvalidAlignment,
    InvalidInput,
    InvalidReference,
    StaleGeneration,
    CapacityExceeded,
    SizeOverflow,
    InternalInvariantFailure,
};

struct RuntimeStringError {
    RuntimeStringErrorCode code = RuntimeStringErrorCode::None;
};

struct RuntimeStringRecord {
    std::size_t offset = 0u;
    std::size_t length = 0u;
};

struct RuntimeStringStorageView {
    RuntimeStringRecord* records = nullptr;
    std::size_t recordCapacity = 0u;
    char* bytes = nullptr;
    std::size_t byteCapacity = 0u;
};

struct RuntimeStringMark {
    std::size_t count = 0u;
    std::size_t bytesUsed = 0u;
    RuntimeGeneration generation = kInvalidRuntimeGeneration;
};

// Caller-backed, append-only UTF-8 byte storage. Handles refer to record
// slots and carry the realm generation; record and byte addresses never move
// during the configured lifetime.
class RuntimeStringStore {
public:
    bool configure(RuntimeStringStorageView storage,
        RuntimeGeneration generation, RuntimeStringError& error);
    void reset(RuntimeGeneration generation);

    bool create(SourceView text, std::size_t maximumLength,
        RuntimeStringId& result, RuntimeStringError& error);
    // Decode the bounded string escape subset accepted by the lexer/parser
    // directly into caller-backed storage, without a temporary std::string.
    bool createDecodedStringLiteral(SourceView token,
        std::size_t maximumLength, RuntimeStringId& result,
        RuntimeStringError& error);
    bool createConcatenated(SourceView left, SourceView right,
        std::size_t maximumLength, RuntimeStringId& result,
        RuntimeStringError& error);
    bool view(RuntimeStringId id, SourceView& result,
        RuntimeStringError& error) const;
    RuntimeStringMark mark() const
    {
        return RuntimeStringMark{count_, bytesUsed_, generation_};
    }
    bool restore(RuntimeStringMark mark, RuntimeStringError& error);

    std::size_t count() const { return count_; }
    std::size_t bytesUsed() const { return bytesUsed_; }
    RuntimeGeneration generation() const { return generation_; }
    std::size_t recordCapacity() const { return storage_.recordCapacity; }
    std::size_t byteCapacity() const { return storage_.byteCapacity; }

private:
    RuntimeStringStorageView storage_;
    RuntimeGeneration generation_ = kInvalidRuntimeGeneration;
    std::size_t count_ = 0u;
    std::size_t bytesUsed_ = 0u;
    bool valid_ = false;
};

const char* runtimeStringErrorCodeName(RuntimeStringErrorCode code);

} // namespace javascript
} // namespace gxos
