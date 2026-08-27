// Shared bounded retry logic for secure-random word sources.
//
// The production CPU provider uses this helper for RDSEED/RDRAND.  Keeping the
// policy independent of the instruction wrappers makes transient failure,
// exhaustion, zero-length, and no-partial-success behavior host-testable.

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace secure_random {

typedef bool (*WordReader)(uint32_t* word);

inline void zero_random_bytes(uint8_t* bytes, size_t len)
{
    if (!bytes) return;
    for (size_t i = 0; i < len; ++i) bytes[i] = 0;
}

inline bool fill_from_word_source(void* buffer,
                                  size_t len,
                                  WordReader reader,
                                  uint32_t retryLimit)
{
    if (len == 0) return true;
    if (!buffer || !reader || retryLimit == 0) {
        if (buffer) zero_random_bytes(static_cast<uint8_t*>(buffer), len);
        return false;
    }

    uint8_t* const start = static_cast<uint8_t*>(buffer);
    uint8_t* out = start;
    size_t remaining = len;
    while (remaining != 0) {
        uint32_t word = 0;
        bool received = false;
        for (uint32_t attempt = 0; attempt < retryLimit; ++attempt) {
            if (reader(&word)) {
                received = true;
                break;
            }
        }

        if (!received) {
            // The caller must never observe a prefix that could be mistaken
            // for a complete successful request.
            zero_random_bytes(start, len);
            return false;
        }

        const size_t copyLength = remaining < sizeof(word) ? remaining : sizeof(word);
        for (size_t i = 0; i < copyLength; ++i) {
            out[i] = static_cast<uint8_t>(word >> (i * 8));
        }
        out += copyLength;
        remaining -= copyLength;
    }

    return true;
}

} // namespace secure_random
} // namespace kernel
