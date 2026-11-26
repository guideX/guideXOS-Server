#pragma once

#include <stdint.h>

namespace guideXOS {
namespace ramdisk_diagnostic {

static const uint32_t kSectorBytes = 512u;
static const uint64_t kPageBytes = 4096u;
static const uint64_t kPageMask = kPageBytes - 1u;
static const uint64_t kAddressMask = 0x000FFFFFFFFFF000ull;
static const uint64_t kPhysicalIdentityLimit = 1ull << 47;
// Keep the RAMDISK's physical BootInfo range separate from the relocated
// kernel's low virtual image, which can overlap the same numeric address.
static const uint64_t kKernelRamdiskVirtualBase = 0x0000400000000000ull;

enum class MappingStatus : uint8_t {
    Mapped4K = 0,
    Mapped2M,
    Mapped1G,
    NotPresent,
    PageTableUnavailable,
    Invalid
};

struct Mapping {
    MappingStatus status;
    uint64_t physicalAddress;
    uint64_t leafEntry;
    uint8_t pageShift;
    bool writable;
    bool user;
    bool executable;
};

typedef const volatile uint64_t* (*PageTableReader)(void* context,
                                                     uint64_t physicalAddress);

struct SectorZero {
    uint64_t fnv1a64;
    uint16_t signature;
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t fatCount;
};

static inline bool observe_sector_zero(const void* sector, SectorZero* out)
{
    if (!sector || !out) return false;
    const uint8_t* bytes = static_cast<const uint8_t*>(sector);
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t index = 0; index < kSectorBytes; ++index) {
        hash = (hash ^ bytes[index]) * 1099511628211ull;
    }
    out->fnv1a64 = hash;
    out->signature = static_cast<uint16_t>(bytes[510]) |
                     (static_cast<uint16_t>(bytes[511]) << 8);
    out->bytesPerSector = static_cast<uint16_t>(bytes[11]) |
                          (static_cast<uint16_t>(bytes[12]) << 8);
    out->sectorsPerCluster = bytes[13];
    out->reservedSectors = static_cast<uint16_t>(bytes[14]) |
                           (static_cast<uint16_t>(bytes[15]) << 8);
    out->fatCount = bytes[16];
    return true;
}

static inline MappingStatus translate(uint64_t rootPhysical,
                                      uint64_t virtualAddress,
                                      PageTableReader readPageTable,
                                      void* context,
                                      Mapping* out)
{
    if (!out || !readPageTable || rootPhysical == 0u ||
        (rootPhysical & kPageMask) != 0u ||
        rootPhysical >= kPhysicalIdentityLimit ||
        virtualAddress >= kPhysicalIdentityLimit) {
        if (out) {
            out->status = MappingStatus::Invalid;
            out->physicalAddress = 0u;
            out->leafEntry = 0u;
            out->pageShift = 0u;
            out->writable = false;
            out->user = false;
            out->executable = false;
        }
        return MappingStatus::Invalid;
    }

    const uint64_t indices[4] = {
        (virtualAddress >> 39) & 0x1FFu,
        (virtualAddress >> 30) & 0x1FFu,
        (virtualAddress >> 21) & 0x1FFu,
        (virtualAddress >> 12) & 0x1FFu
    };
    const volatile uint64_t* table = readPageTable(context, rootPhysical);
    if (!table) {
        out->status = MappingStatus::PageTableUnavailable;
        return out->status;
    }

    bool writable = true;
    bool user = false;
    bool executable = true;
    for (uint32_t level = 0; level < 4u; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & 1u) == 0u) {
            out->status = MappingStatus::NotPresent;
            out->physicalAddress = 0u;
            out->leafEntry = entry;
            out->pageShift = 0u;
            out->writable = false;
            out->user = false;
            out->executable = false;
            return out->status;
        }
        writable = writable && ((entry & (1ull << 1)) != 0u);
        user = user || ((entry & (1ull << 2)) != 0u);
        executable = executable && ((entry & (1ull << 63)) == 0u);

        const bool large = level > 0u && level < 3u &&
                           (entry & (1ull << 7)) != 0u;
        if (level == 3u || large) {
            const uint8_t shift = level == 3u ? 12u :
                                 (level == 1u ? 30u : 21u);
            const uint64_t pageMask = (1ull << shift) - 1u;
            const uint64_t physicalBase = (entry & kAddressMask) & ~pageMask;
            out->status = level == 3u ? MappingStatus::Mapped4K :
                          (level == 1u ? MappingStatus::Mapped1G :
                                         MappingStatus::Mapped2M);
            out->physicalAddress = physicalBase | (virtualAddress & pageMask);
            out->leafEntry = entry;
            out->pageShift = shift;
            out->writable = writable;
            out->user = user;
            out->executable = executable;
            return out->status;
        }

        const uint64_t nextPhysical = entry & kAddressMask;
        table = readPageTable(context, nextPhysical);
        if (!table) {
            out->status = MappingStatus::PageTableUnavailable;
            out->physicalAddress = 0u;
            out->leafEntry = entry;
            out->pageShift = 0u;
            out->writable = false;
            out->user = false;
            out->executable = false;
            return out->status;
        }
    }

    out->status = MappingStatus::Invalid;
    return out->status;
}

static inline bool identity_range_is_mapped(uint64_t rootPhysical,
                                            uint64_t virtualBase,
                                            uint64_t length,
                                            PageTableReader readPageTable,
                                            void* context,
                                            uint64_t* pagesVerified,
                                            uint64_t* firstMismatchVirtual)
{
    if (pagesVerified) *pagesVerified = 0u;
    if (firstMismatchVirtual) *firstMismatchVirtual = 0u;
    if (length == 0u || virtualBase >= kPhysicalIdentityLimit ||
        virtualBase > UINT64_MAX - length ||
        virtualBase + length > kPhysicalIdentityLimit || !readPageTable ||
        rootPhysical == 0u || (rootPhysical & kPageMask) != 0u ||
        rootPhysical >= kPhysicalIdentityLimit) {
        if (firstMismatchVirtual) *firstMismatchVirtual = virtualBase;
        return false;
    }

    const uint64_t firstPage = virtualBase & ~kPageMask;
    const uint64_t lastPage = (virtualBase + length - 1u) & ~kPageMask;
    for (uint64_t page = firstPage;; page += kPageBytes) {
        Mapping mapping{};
        const MappingStatus status = translate(rootPhysical, page,
                                               readPageTable, context, &mapping);
        if (pagesVerified) ++*pagesVerified;
        if ((status != MappingStatus::Mapped4K &&
             status != MappingStatus::Mapped2M &&
             status != MappingStatus::Mapped1G) ||
            mapping.physicalAddress != page) {
            if (firstMismatchVirtual) *firstMismatchVirtual = page;
            return false;
        }
        if (page == lastPage) return true;
    }
}

static inline bool range_maps_to_physical(uint64_t rootPhysical,
                                          uint64_t virtualBase,
                                          uint64_t physicalBase,
                                          uint64_t length,
                                          PageTableReader readPageTable,
                                          void* context,
                                          uint64_t* pagesVerified,
                                          uint64_t* firstMismatchVirtual)
{
    if (pagesVerified) *pagesVerified = 0u;
    if (firstMismatchVirtual) *firstMismatchVirtual = 0u;
    if (length == 0u || virtualBase >= kPhysicalIdentityLimit ||
        physicalBase >= kPhysicalIdentityLimit ||
        virtualBase > UINT64_MAX - length || physicalBase > UINT64_MAX - length ||
        virtualBase + length > kPhysicalIdentityLimit ||
        physicalBase + length > kPhysicalIdentityLimit ||
        (virtualBase & kPageMask) != (physicalBase & kPageMask) ||
        !readPageTable || rootPhysical == 0u ||
        (rootPhysical & kPageMask) != 0u ||
        rootPhysical >= kPhysicalIdentityLimit) {
        if (firstMismatchVirtual) *firstMismatchVirtual = virtualBase;
        return false;
    }

    const uint64_t firstVirtualPage = virtualBase & ~kPageMask;
    const uint64_t firstPhysicalPage = physicalBase & ~kPageMask;
    const uint64_t lastVirtualPage = (virtualBase + length - 1u) & ~kPageMask;
    for (uint64_t page = firstVirtualPage;; page += kPageBytes) {
        Mapping mapping{};
        const MappingStatus status = translate(rootPhysical, page,
                                               readPageTable, context, &mapping);
        if (pagesVerified) ++*pagesVerified;
        const uint64_t expectedPhysical = firstPhysicalPage +
            (page - firstVirtualPage);
        if ((status != MappingStatus::Mapped4K &&
             status != MappingStatus::Mapped2M &&
             status != MappingStatus::Mapped1G) ||
            mapping.physicalAddress != expectedPhysical) {
            if (firstMismatchVirtual) *firstMismatchVirtual = page;
            return false;
        }
        if (page == lastVirtualPage) return true;
    }
}

static inline const char* mapping_status_name(MappingStatus status)
{
    switch (status) {
    case MappingStatus::Mapped4K: return "mapped-4k";
    case MappingStatus::Mapped2M: return "mapped-2m";
    case MappingStatus::Mapped1G: return "mapped-1g";
    case MappingStatus::NotPresent: return "not-present";
    case MappingStatus::PageTableUnavailable: return "page-table-unavailable";
    default: return "invalid";
    }
}

} // namespace ramdisk_diagnostic
} // namespace guideXOS
