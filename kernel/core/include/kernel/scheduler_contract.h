// Pure scheduler contract checks shared by freestanding code and host tests.

#pragma once

#include <stdint.h>

static inline bool gxos_scheduler_add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static inline bool gxos_scheduler_stack_layout_valid(uint64_t base, uint64_t size,
                                                      uint64_t top)
{
    uint64_t end = 0;
    return base != 0 && size >= UINT64_C(0x2000) && (base & UINT64_C(0xf)) == 0 &&
           (size & UINT64_C(0xf)) == 0 && gxos_scheduler_add_u64(base, size, &end) &&
           end > base && top == end - UINT64_C(0x10) && (top & UINT64_C(0xf)) == 0;
}

static inline bool gxos_scheduler_entry_valid(const void* entry)
{
    return entry != 0;
}

static inline bool gxos_scheduler_canaries_valid(uint64_t low, uint64_t high,
                                                  uint64_t expected_low,
                                                  uint64_t expected_high)
{
    return low == expected_low && high == expected_high;
}
