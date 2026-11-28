#pragma once

#include <stdint.h>
#include "../../../../guideXOSBootLoader/guidexOSBootInfo.h"

namespace kernel {
namespace memory {

// Bound the boot tree walk while allowing the loader's 1,024 tracked tables
// to be audited before any frames in the task arena become allocatable.
static const uint32_t kMaximumBootPageTablePages =
    guideXOS::GUIDEXOS_MAX_BOOT_PAGE_TABLE_PAGES;

enum class BootPageTableValidationFailure : uint32_t {
    None = 0u,
    InvalidInput,
    InvalidTableAddress,
    ProtectedRangeOverlap,
    PageCountLimit,
    PhysicalMappingUnavailable,
    LargePageAtRoot
};

typedef void* (*BootPageTablePhysicalMapper)(void*, uint64_t);

inline bool boot_page_table_tree_avoids_range(
    uint64_t tablePhysical,
    uint32_t level,
    uint64_t protectedBase,
    uint64_t protectedEnd,
    BootPageTablePhysicalMapper mapPhysical,
    void* mapContext,
    uint32_t* tableCount,
    uint32_t maximumTablePages = kMaximumBootPageTablePages,
    BootPageTableValidationFailure* failure = nullptr)
{
    static const uint64_t kPageBytes = 4096u;
    static const uint64_t kPageMask = kPageBytes - 1u;
    static const uint64_t kIdentityMappedPhysicalLimit = 1ull << 47;
    static const uint64_t kPtePresent = 1ull << 0;
    static const uint64_t kPteLarge = 1ull << 7;
    static const uint64_t kPteAddressMask = 0x000FFFFFFFFFF000ull;

    if (failure) *failure = BootPageTableValidationFailure::None;
    if (!tableCount || !mapPhysical || maximumTablePages == 0u) {
        if (failure) *failure = BootPageTableValidationFailure::InvalidInput;
        return false;
    }
    if (level > 3u || (tablePhysical & kPageMask) != 0u ||
        tablePhysical >= kIdentityMappedPhysicalLimit) {
        if (failure) *failure = BootPageTableValidationFailure::InvalidTableAddress;
        return false;
    }
    if (tablePhysical >= protectedBase && tablePhysical < protectedEnd) {
        if (failure) *failure = BootPageTableValidationFailure::ProtectedRangeOverlap;
        return false;
    }
    if (++*tableCount > maximumTablePages) {
        if (failure) *failure = BootPageTableValidationFailure::PageCountLimit;
        return false;
    }

    uint64_t* table = static_cast<uint64_t*>(mapPhysical(mapContext, tablePhysical));
    if (!table) {
        if (failure) *failure = BootPageTableValidationFailure::PhysicalMappingUnavailable;
        return false;
    }
    if (level == 3u) return true;

    for (uint32_t i = 0; i < 512u; ++i) {
        const uint64_t entry = table[i];
        if ((entry & kPtePresent) == 0u) continue;
        if (level == 0u && (entry & kPteLarge) != 0u) {
            if (failure) *failure = BootPageTableValidationFailure::LargePageAtRoot;
            return false;
        }
        if (level > 0u && (entry & kPteLarge) != 0u) continue;
        if (!boot_page_table_tree_avoids_range(
                entry & kPteAddressMask, level + 1u, protectedBase,
                protectedEnd, mapPhysical, mapContext, tableCount,
                maximumTablePages, failure)) return false;
    }
    return true;
}

} // namespace memory
} // namespace kernel
