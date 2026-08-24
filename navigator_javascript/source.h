#pragma once

#include <cstddef>

namespace gxos {
namespace javascript {

// Non-owning JavaScript source.  The caller owns the bytes and must keep them
// alive while a SourceView or any token lexeme derived from it is in use.
// length is authoritative; the input does not need to be null terminated.
struct SourceView {
    const char* data = nullptr;
    std::size_t length = 0;

    constexpr SourceView() = default;
    constexpr SourceView(const char* bytes, std::size_t byteCount)
        : data(bytes), length(byteCount) {}

    constexpr bool empty() const { return length == 0; }
};

} // namespace javascript
} // namespace gxos
