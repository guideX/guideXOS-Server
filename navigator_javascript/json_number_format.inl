#include "../third_party/ryu/d2s.c"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {
namespace {

bool append(char value, char* output, std::size_t capacity,
    std::size_t& written)
{
    if (output == nullptr || written >= capacity) return false;
    output[written++] = value;
    return true;
}

bool appendUnsigned(unsigned value, char* output, std::size_t capacity,
    std::size_t& written)
{
    char digits[10] = {};
    std::size_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) {
        if (!append(digits[--count], output, capacity, written)) return false;
    }
    return true;
}

} // namespace

inline PortableJsonNumberFormatCode formatPortableJsonBinary64(double value,
    char* output, std::size_t capacity, std::size_t& written)
{
    written = 0u;
    if (output == nullptr || capacity == 0u)
        return PortableJsonNumberFormatCode::InvalidInput;

    char scientific[32] = {};
    const int rawLength = d2s_buffered_n(value, scientific);
    if (rawLength <= 0 || static_cast<std::size_t>(rawLength) >=
            sizeof(scientific))
        return PortableJsonNumberFormatCode::InternalInvariantFailure;
    if (scientific[0] == 'N' || scientific[0] == 'I' ||
        (scientific[0] == '-' && rawLength > 1 && scientific[1] == 'I'))
        return PortableJsonNumberFormatCode::NonFinite;

    std::size_t cursor = 0u;
    const bool negative = scientific[cursor] == '-';
    if (negative) ++cursor;
    char digits[20] = {};
    std::size_t digitCount = 0u;
    bool decimalPointSeen = false;
    while (cursor < static_cast<std::size_t>(rawLength) &&
            scientific[cursor] != 'E') {
        const char current = scientific[cursor++];
        if (current == '.') {
            if (decimalPointSeen) return
                PortableJsonNumberFormatCode::InternalInvariantFailure;
            decimalPointSeen = true;
            continue;
        }
        if (current < '0' || current > '9' || digitCount >= sizeof(digits))
            return PortableJsonNumberFormatCode::InternalInvariantFailure;
        digits[digitCount++] = current;
    }
    if (digitCount == 0u || cursor >= static_cast<std::size_t>(rawLength) ||
        scientific[cursor] != 'E')
        return PortableJsonNumberFormatCode::InternalInvariantFailure;
    ++cursor;

    bool exponentNegative = false;
    if (cursor < static_cast<std::size_t>(rawLength) &&
            (scientific[cursor] == '+' || scientific[cursor] == '-')) {
        exponentNegative = scientific[cursor] == '-';
        ++cursor;
    }
    if (cursor >= static_cast<std::size_t>(rawLength))
        return PortableJsonNumberFormatCode::InternalInvariantFailure;
    int exponentMagnitude = 0;
    for (; cursor < static_cast<std::size_t>(rawLength); ++cursor) {
        const char current = scientific[cursor];
        if (current < '0' || current > '9' || exponentMagnitude > 1000)
            return PortableJsonNumberFormatCode::InternalInvariantFailure;
        exponentMagnitude = exponentMagnitude * 10 + (current - '0');
    }
    const int exponent = exponentNegative ? -exponentMagnitude
        : exponentMagnitude;

    bool nonzero = false;
    for (std::size_t index = 0u; index < digitCount; ++index)
        nonzero = nonzero || digits[index] != '0';
    if (!nonzero) {
        if (!append('0', output, capacity, written))
            return PortableJsonNumberFormatCode::CapacityExceeded;
        return PortableJsonNumberFormatCode::None;
    }

    const int decimalPosition = exponent + 1;
    if (negative && !append('-', output, capacity, written))
        return PortableJsonNumberFormatCode::CapacityExceeded;

    if (decimalPosition > 0 && decimalPosition <= 21) {
        if (digitCount <= static_cast<std::size_t>(decimalPosition)) {
            for (std::size_t index = 0u; index < digitCount; ++index) {
                if (!append(digits[index], output, capacity, written))
                    return PortableJsonNumberFormatCode::CapacityExceeded;
            }
            for (std::size_t index = digitCount;
                    index < static_cast<std::size_t>(decimalPosition); ++index) {
                if (!append('0', output, capacity, written))
                    return PortableJsonNumberFormatCode::CapacityExceeded;
            }
        } else {
            for (int index = 0; index < decimalPosition; ++index) {
                if (!append(digits[static_cast<std::size_t>(index)], output,
                        capacity, written))
                    return PortableJsonNumberFormatCode::CapacityExceeded;
            }
            if (!append('.', output, capacity, written))
                return PortableJsonNumberFormatCode::CapacityExceeded;
            for (std::size_t index =
                    static_cast<std::size_t>(decimalPosition);
                    index < digitCount; ++index) {
                if (!append(digits[index], output, capacity, written))
                    return PortableJsonNumberFormatCode::CapacityExceeded;
            }
        }
        return PortableJsonNumberFormatCode::None;
    }

    if (decimalPosition <= 0 && decimalPosition > -6) {
        if (!append('0', output, capacity, written) ||
            !append('.', output, capacity, written))
            return PortableJsonNumberFormatCode::CapacityExceeded;
        for (int index = 0; index < -decimalPosition; ++index) {
            if (!append('0', output, capacity, written))
                return PortableJsonNumberFormatCode::CapacityExceeded;
        }
        for (std::size_t index = 0u; index < digitCount; ++index) {
            if (!append(digits[index], output, capacity, written))
                return PortableJsonNumberFormatCode::CapacityExceeded;
        }
        return PortableJsonNumberFormatCode::None;
    }

    if (!append(digits[0], output, capacity, written))
        return PortableJsonNumberFormatCode::CapacityExceeded;
    if (digitCount > 1u) {
        if (!append('.', output, capacity, written))
            return PortableJsonNumberFormatCode::CapacityExceeded;
        for (std::size_t index = 1u; index < digitCount; ++index) {
            if (!append(digits[index], output, capacity, written))
                return PortableJsonNumberFormatCode::CapacityExceeded;
        }
    }
    if (!append('e', output, capacity, written))
        return PortableJsonNumberFormatCode::CapacityExceeded;
    const int scientificExponent = decimalPosition - 1;
    if (scientificExponent >= 0) {
        if (!append('+', output, capacity, written) ||
            !appendUnsigned(static_cast<unsigned>(scientificExponent), output,
                capacity, written))
            return PortableJsonNumberFormatCode::CapacityExceeded;
    } else {
        if (!append('-', output, capacity, written) ||
            !appendUnsigned(static_cast<unsigned>(-scientificExponent), output,
                capacity, written))
            return PortableJsonNumberFormatCode::CapacityExceeded;
    }
    return PortableJsonNumberFormatCode::None;
}

const char* portableJsonNumberFormatCodeName(
    PortableJsonNumberFormatCode code)
{
    switch (code) {
    case PortableJsonNumberFormatCode::None: return "None";
    case PortableJsonNumberFormatCode::NonFinite: return "NonFinite";
    case PortableJsonNumberFormatCode::InvalidInput: return "InvalidInput";
    case PortableJsonNumberFormatCode::CapacityExceeded:
        return "CapacityExceeded";
    case PortableJsonNumberFormatCode::InternalInvariantFailure:
        return "InternalInvariantFailure";
    }
    return "Unknown";
}

} // namespace javascript
} // namespace gxos
