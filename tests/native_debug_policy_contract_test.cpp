#include <cassert>
#include <cstdint>
#include <cstdio>

#include "guidexos/development_debug.h"
#include "kernel/core/native_elf/native_elf_debug_policies.h"

using kernel::native_elf::native_debug_hit_count_policy_matches;
using kernel::native_elf::native_debug_hit_count_policy_valid;
using kernel::native_elf::native_debug_saturating_increment;

int main()
{
    assert(native_debug_hit_count_policy_valid(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_NONE, 0));
    assert(native_debug_hit_count_policy_valid(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_EQUAL, 3));
    assert(native_debug_hit_count_policy_valid(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE, 2));
    assert(native_debug_hit_count_policy_valid(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_AT_LEAST, 3));
    assert(!native_debug_hit_count_policy_valid(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_EQUAL, 0));
    assert(!native_debug_hit_count_policy_valid(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE, 0));
    assert(!native_debug_hit_count_policy_valid(99, 1));

    assert(native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_NONE, 0, 1));
    assert(!native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_EQUAL, 3, 2));
    assert(native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_EQUAL, 3, 3));
    assert(native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE, 2, 2));
    assert(native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE, 2, 4));
    assert(!native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE, 2, 5));
    assert(native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_AT_LEAST, 3, 3));
    assert(native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_AT_LEAST, 3, 99));
    const bool conditionPasses = true;
    const bool conditionFails = false;
    const bool hitPolicyPasses = native_debug_hit_count_policy_matches(
        GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_EQUAL, 3, 3);
    assert(conditionPasses && hitPolicyPasses);
    assert(!(conditionFails && hitPolicyPasses));

    const uint64_t maximum = ~static_cast<uint64_t>(0);
    assert(native_debug_saturating_increment(0) == 1);
    assert(native_debug_saturating_increment(maximum - 1) == maximum);
    assert(native_debug_saturating_increment(maximum) == maximum);

    gx_development_debug_breakpoint record = {};
    record.action = GX_DEVELOPMENT_DEBUG_BREAKPOINT_ACTION_LOG;
    record.hitCountPolicy = GX_DEVELOPMENT_DEBUG_HIT_COUNT_POLICY_MULTIPLE;
    record.hitCountThreshold = 2;
    record.rawHitCount = 4;
    record.logTemplatePresent = 1;
    record.logTemplateLength = 31;
    assert(record.action == GX_DEVELOPMENT_DEBUG_BREAKPOINT_ACTION_LOG);
    assert(record.rawHitCount == 4);

    std::printf("Native debug policy contract test PASS\n");
    return 0;
}
