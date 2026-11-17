#pragma once

namespace gxos {
namespace javascript {

inline void PortableJsonOutputWriter::reset(char* buffer,
    std::size_t capacity)
{
    buffer_ = buffer;
    capacity_ = capacity;
    limit_ = capacity;
    size_ = 0u;
    limit_ = capacity_;
    error_ = (buffer == nullptr && capacity != 0u)
        ? PortableJsonWriteErrorCode::InvalidBuffer
        : PortableJsonWriteErrorCode::None;
}

inline void PortableJsonOutputWriter::setLimit(std::size_t limit)
{
    limit_ = limit <= capacity_ ? limit : capacity_;
    if (size_ > limit_)
        fail(PortableJsonWriteErrorCode::CapacityExceeded);
}

inline void PortableJsonOutputWriter::reset()
{
    size_ = 0u;
    error_ = (buffer_ == nullptr && capacity_ != 0u)
        ? PortableJsonWriteErrorCode::InvalidBuffer
        : PortableJsonWriteErrorCode::None;
}

inline std::size_t PortableJsonOutputWriter::remaining() const
{
    return size_ <= limit_ ? limit_ - size_ : 0u;
}

inline bool PortableJsonOutputWriter::fail(
    PortableJsonWriteErrorCode code)
{
    if (error_ == PortableJsonWriteErrorCode::None) error_ = code;
    return false;
}

inline bool PortableJsonOutputWriter::appendByte(char value)
{
    if (failed()) return false;
    if (size_ >= limit_) return fail(
        PortableJsonWriteErrorCode::CapacityExceeded);
    if (buffer_ == nullptr) return fail(
        PortableJsonWriteErrorCode::InvalidBuffer);
    buffer_[size_++] = value;
    return true;
}

inline bool PortableJsonOutputWriter::appendSpan(const char* bytes,
    std::size_t length)
{
    if (failed()) return false;
    if (bytes == nullptr && length != 0u) return fail(
        PortableJsonWriteErrorCode::InvalidBuffer);
    if (length > remaining()) return fail(
        PortableJsonWriteErrorCode::CapacityExceeded);
    if (length == 0u) return true;
    if (buffer_ == nullptr) return fail(
        PortableJsonWriteErrorCode::InvalidBuffer);
    for (std::size_t index = 0u; index < length; ++index)
        buffer_[size_ + index] = bytes[index];
    size_ += length;
    return true;
}

inline bool PortableJsonOutputWriter::appendQuotedUtf8(SourceView text,
    PortableJsonWriteStepConsumer stepConsumer, void* stepContext)
{
    if (failed()) return false;
    if (text.data == nullptr && text.length != 0u) return fail(
        PortableJsonWriteErrorCode::InvalidBuffer);
    if (!appendByte('"')) return false;

    static const char hexadecimal[] = "0123456789abcdef";
    std::size_t offset = 0u;
    while (offset < text.length) {
        if (stepConsumer != nullptr && !stepConsumer(stepContext))
            return fail(PortableJsonWriteErrorCode::StepBudgetExceeded);
        const std::size_t start = offset;
        std::uint32_t scalar = 0u;
        if (!decodeJsonUtf8Scalar(text.data, text.length, offset, scalar))
            return fail(PortableJsonWriteErrorCode::InvalidUtf8);

        if (scalar == '"') {
            if (!appendSpan("\\\"", 2u)) return false;
        } else if (scalar == '\\') {
            if (!appendSpan("\\\\", 2u)) return false;
        } else if (scalar == '\b') {
            if (!appendSpan("\\b", 2u)) return false;
        } else if (scalar == '\f') {
            if (!appendSpan("\\f", 2u)) return false;
        } else if (scalar == '\n') {
            if (!appendSpan("\\n", 2u)) return false;
        } else if (scalar == '\r') {
            if (!appendSpan("\\r", 2u)) return false;
        } else if (scalar == '\t') {
            if (!appendSpan("\\t", 2u)) return false;
        } else if (scalar < 0x20u) {
            const char escaped[] = {
                '\\', 'u', '0', '0',
                hexadecimal[(scalar >> 4u) & 0x0fu],
                hexadecimal[scalar & 0x0fu]};
            if (!appendSpan(escaped, sizeof(escaped))) return false;
        } else if (!appendSpan(text.data + start, offset - start)) {
            return false;
        }
    }
    return appendByte('"');
}

inline bool PortableJsonOutputWriter::complete(SourceView& output) const
{
    output = SourceView();
    if (failed()) return false;
    if (buffer_ == nullptr && size_ != 0u) return false;
    output = SourceView(buffer_, size_);
    return true;
}

inline const char* portableJsonWriteErrorCodeName(
    PortableJsonWriteErrorCode code)
{
    switch (code) {
    case PortableJsonWriteErrorCode::None: return "None";
    case PortableJsonWriteErrorCode::InvalidBuffer: return "InvalidBuffer";
    case PortableJsonWriteErrorCode::CapacityExceeded:
        return "CapacityExceeded";
    case PortableJsonWriteErrorCode::InvalidUtf8: return "InvalidUtf8";
    case PortableJsonWriteErrorCode::StepBudgetExceeded:
        return "StepBudgetExceeded";
    }
    return "Unknown";
}

} // namespace javascript
} // namespace gxos
