#pragma once

#include <cstddef>

namespace gxos {
namespace javascript {

enum class PortableJsonNumberFormatCode : unsigned char {
    None = 0,
    NonFinite,
    InvalidInput,
    CapacityExceeded,
    InternalInvariantFailure,
};

// Writes the ECMAScript JSON number spelling for one IEEE-754 binary64.
// The caller owns the output storage. Non-finite input is reported separately
// because JSON.stringify writes those values as null.
PortableJsonNumberFormatCode formatPortableJsonBinary64(double value,
    char* output, std::size_t capacity, std::size_t& written);

const char* portableJsonNumberFormatCodeName(
    PortableJsonNumberFormatCode code);

} // namespace javascript
} // namespace gxos

#include "json_number_format.inl"
