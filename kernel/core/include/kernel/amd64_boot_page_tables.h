#pragma once

#include "../../../../guideXOSBootLoader/amd64_boot_paging.h"

namespace kernel {
namespace memory {

enum class BootPageTableStatus { Ok, Invalid, ArenaOverlap, CapacityExceeded };

// Called on the active boot tree. The loader guarantees an identity mapping
// for each table page. The mapper also lets hosted tests walk the same tree.
inline BootPageTableStatus check_boot_page_tables(
    uint64_t tablePhysical, uint64_t arenaBase, uint64_t arenaEnd,
    void* (*mapPhysical)(void*, uint64_t), void* context,
    uint32_t& count, uint32_t level = 0)
{
    using namespace guideXOS::amd64_boot_paging;
    constexpr uint64_t kPresent = 1u << 0;
    constexpr uint64_t kLarge = 1u << 7;
    if (level > 3 || tablePhysical == 0 || (tablePhysical & kPageMask) != 0 ||
        tablePhysical >= kIdentityLimit) return BootPageTableStatus::Invalid;
    if (tablePhysical >= arenaBase && tablePhysical < arenaEnd) {
        return BootPageTableStatus::ArenaOverlap;
    }
    if (count >= kMaxTablePages) return BootPageTableStatus::CapacityExceeded;
    ++count;

    const auto* table = static_cast<const uint64_t*>(mapPhysical(context, tablePhysical));
    if (!table) return BootPageTableStatus::Invalid;
    if (level == 3) return BootPageTableStatus::Ok;

    for (uint32_t i = 0; i < 512; ++i) {
        const uint64_t entry = table[i];
        if ((entry & kPresent) == 0) continue;
        if ((entry & kLarge) != 0) {
            if (level == 0) return BootPageTableStatus::Invalid;
            continue;
        }
        const auto status = check_boot_page_tables(entry & kAddressMask,
            arenaBase, arenaEnd, mapPhysical, context, count, level + 1);
        if (status != BootPageTableStatus::Ok) return status;
    }
    return BootPageTableStatus::Ok;
}

}
}
