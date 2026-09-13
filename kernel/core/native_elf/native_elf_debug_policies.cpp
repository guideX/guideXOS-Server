#include "native_elf_debug_policies.h"

namespace kernel {
namespace native_elf {

bool native_debug_hit_count_policy_valid(uint32_t policy, uint64_t threshold)
{
    switch (policy) {
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_NONE:
        return true;
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_EQUAL:
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE:
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_AT_LEAST:
        return threshold != 0;
    default:
        return false;
    }
}

bool native_debug_hit_count_policy_matches(uint32_t policy, uint64_t threshold,
                                           uint64_t rawHitCount)
{
    if (!native_debug_hit_count_policy_valid(policy, threshold)) return false;
    switch (policy) {
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_NONE:
        return true;
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_EQUAL:
        return rawHitCount == threshold;
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE:
        return rawHitCount % threshold == 0;
    case GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_AT_LEAST:
        return rawHitCount >= threshold;
    default:
        return false;
    }
}

uint64_t native_debug_saturating_increment(uint64_t value)
{
    const uint64_t maximum = ~static_cast<uint64_t>(0);
    return value == maximum ? maximum : value + 1ULL;
}

} // namespace native_elf
} // namespace kernel
