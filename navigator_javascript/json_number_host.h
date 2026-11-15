#pragma once

#include "source.h"

#include <charconv>
#include <cstddef>
#include <limits>

namespace gxos {
namespace javascript {
namespace {

inline bool hostedJsonIsAsciiDigit(char value)
{
    return value >= '0' && value <= '9';
}

} // namespace

// Hosted adapter for the accepted conversion path. The portable JSON parser
// receives this function by pointer and does not depend on <charconv>.
inline bool parseHostedJsonBinary64(SourceView input, double& result)
{
    if (input.data == nullptr || input.length == 0u) return false;
    const char* const begin = input.data;
    const char* const end = input.data + input.length;
    const auto conversion = std::from_chars(begin, end, result,
        std::chars_format::general);
    if (conversion.ec == std::errc() && conversion.ptr == end) return true;
    if (conversion.ec != std::errc::result_out_of_range ||
        conversion.ptr != end) return false;

    bool negative = false;
    std::size_t cursor = 0u;
    if (input.data[cursor] == '-') {
        negative = true;
        ++cursor;
    }
    std::size_t integerDigits = 0u;
    std::size_t digitPosition = 0u;
    std::size_t firstNonZero =
        (std::numeric_limits<std::size_t>::max)();
    for (; cursor < input.length && input.data[cursor] != '.' &&
            input.data[cursor] != 'e' && input.data[cursor] != 'E'; ++cursor) {
        if (hostedJsonIsAsciiDigit(input.data[cursor])) {
            if (input.data[cursor] != '0' && firstNonZero ==
                    (std::numeric_limits<std::size_t>::max)()) {
                firstNonZero = digitPosition;
            }
            ++digitPosition;
            ++integerDigits;
        }
    }
    if (cursor < input.length && input.data[cursor] == '.') {
        ++cursor;
        for (; cursor < input.length && input.data[cursor] != 'e' &&
                input.data[cursor] != 'E'; ++cursor) {
            if (hostedJsonIsAsciiDigit(input.data[cursor])) {
                if (input.data[cursor] != '0' && firstNonZero ==
                        (std::numeric_limits<std::size_t>::max)()) {
                    firstNonZero = digitPosition;
                }
                ++digitPosition;
            }
        }
    }
    if (firstNonZero == (std::numeric_limits<std::size_t>::max)()) {
        result = negative ? -0.0 : 0.0;
        return true;
    }
    long long explicitExponent = 0LL;
    if (cursor < input.length) {
        ++cursor;
        bool exponentNegative = false;
        if (cursor < input.length && (input.data[cursor] == '+' ||
                input.data[cursor] == '-')) {
            exponentNegative = input.data[cursor] == '-';
            ++cursor;
        }
        for (; cursor < input.length &&
                hostedJsonIsAsciiDigit(input.data[cursor]); ++cursor) {
            if (explicitExponent < 1000000LL) {
                explicitExponent = explicitExponent * 10LL +
                    static_cast<long long>(input.data[cursor] - '0');
                if (explicitExponent > 1000000LL)
                    explicitExponent = 1000000LL;
            }
        }
        if (exponentNegative) explicitExponent = -explicitExponent;
    }
    const long long decimalOrder = static_cast<long long>(integerDigits) -
        static_cast<long long>(firstNonZero) - 1LL + explicitExponent;
    if (decimalOrder >= 0LL) {
        result = negative ? -(std::numeric_limits<double>::infinity)()
            : (std::numeric_limits<double>::infinity)();
    } else {
        result = negative ? -0.0 : 0.0;
    }
    return true;
}

} // namespace javascript
} // namespace gxos
