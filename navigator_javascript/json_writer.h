#pragma once

#include "json_parser.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

enum class PortableJsonWriteErrorCode : std::uint8_t {
    None = 0,
    InvalidBuffer,
    CapacityExceeded,
    InvalidUtf8,
    StepBudgetExceeded,
};

using PortableJsonWriteStepConsumer = bool (*)(void* context);

// A bounded caller-buffer writer shared by hosted and freestanding JSON
// output paths. A failed writer is sticky and can never complete successfully.
class PortableJsonOutputWriter {
public:
    PortableJsonOutputWriter() = default;
    PortableJsonOutputWriter(char* buffer, std::size_t capacity)
    {
        reset(buffer, capacity);
    }

    void reset(char* buffer, std::size_t capacity);
    void reset();

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    std::size_t remaining() const;
    bool failed() const
    {
        return error_ != PortableJsonWriteErrorCode::None;
    }
    PortableJsonWriteErrorCode error() const { return error_; }

    bool appendByte(char value);
    bool appendSpan(const char* bytes, std::size_t length);
    bool appendQuotedUtf8(SourceView text,
        PortableJsonWriteStepConsumer stepConsumer = nullptr,
        void* stepContext = nullptr);
    bool complete(SourceView& output) const;

private:
    bool fail(PortableJsonWriteErrorCode code);

    char* buffer_ = nullptr;
    std::size_t capacity_ = 0u;
    std::size_t size_ = 0u;
    PortableJsonWriteErrorCode error_ = PortableJsonWriteErrorCode::None;
};

const char* portableJsonWriteErrorCodeName(
    PortableJsonWriteErrorCode code);

} // namespace javascript
} // namespace gxos

#include "json_writer.inl"
