#pragma once

#include "../../../sdk/include/guidexos/development_debug.h"

namespace kernel {
namespace native_elf {

// Phase 28L keeps hit-count policy evaluation separate from the breakpoint
// patch owner. These helpers are deliberately closed and allocation-free.
bool native_debug_hit_count_policy_valid(uint32_t policy, uint64_t threshold);
bool native_debug_hit_count_policy_matches(uint32_t policy, uint64_t threshold,
                                           uint64_t rawHitCount);
uint64_t native_debug_saturating_increment(uint64_t value);

} // namespace native_elf
} // namespace kernel
