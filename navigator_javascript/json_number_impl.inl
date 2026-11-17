#include "json_number.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <float.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <limits>
#define NAN (std::numeric_limits<double>::quiet_NaN())
#define INFINITY (std::numeric_limits<double>::infinity())
#else
#define NAN (__builtin_nan(""))
#define INFINITY (__builtin_inf())
#endif

// The target runtime has no C library. These are complete byte-copy helpers
// for the fixed-size operations used by ffc.h, not imports or no-op stubs.
static void* gxosJsonNumberMemcpy(void* destination, const void* source,
    size_t length)
{
    unsigned char* output = static_cast<unsigned char*>(destination);
    const unsigned char* input = static_cast<const unsigned char*>(source);
    for (size_t index = 0u; index < length; ++index)
        output[index] = input[index];
    return destination;
}

static void* gxosJsonNumberMemmove(void* destination, const void* source,
    size_t length)
{
    const uintptr_t outputAddress = reinterpret_cast<uintptr_t>(destination);
    const uintptr_t inputAddress = reinterpret_cast<uintptr_t>(source);
    unsigned char* output = static_cast<unsigned char*>(destination);
    const unsigned char* input = static_cast<const unsigned char*>(source);
    if (outputAddress <= inputAddress ||
        outputAddress - inputAddress >= length) {
        for (size_t index = 0u; index < length; ++index)
            output[index] = input[index];
    } else {
        for (size_t index = length; index != 0u; --index)
            output[index - 1u] = input[index - 1u];
    }
    return destination;
}

static void* gxosJsonNumberMemset(void* destination, int value,
    size_t length)
{
    unsigned char* output = static_cast<unsigned char*>(destination);
    const unsigned char byte = static_cast<unsigned char>(value);
    for (size_t index = 0u; index < length; ++index)
        output[index] = byte;
    return destination;
}

#define memcpy gxosJsonNumberMemcpy
#define memmove gxosJsonNumberMemmove
#define memset gxosJsonNumberMemset

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define FFC_IMPL
#include "../third_party/ffc/ffc.h"
#undef FFC_IMPL

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#undef memcpy
#undef memmove
#undef memset
#undef NAN
#undef INFINITY

namespace gxos {
namespace javascript {

bool parsePortableJsonBinary64(SourceView token, double& result)
{
    constexpr size_t kMaximumJsonNumberTokenBytes = 64u * 1024u;
    if (token.data == nullptr || token.length == 0u ||
        token.length > kMaximumJsonNumberTokenBytes) {
        return false;
    }

    double converted = 0.0;
    const ffc_result parsed = ffc_parse_double(token.length, token.data,
        &converted);
    if (parsed.ptr != token.data + token.length ||
        (parsed.outcome != FFC_OUTCOME_OK &&
         parsed.outcome != FFC_OUTCOME_OUT_OF_RANGE)) {
        return false;
    }
    result = converted;
    return true;
}

} // namespace javascript
} // namespace gxos
