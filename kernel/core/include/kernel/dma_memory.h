#pragma once

#include <stdint.h>

// Bounded validation helpers for the x64 UEFI memory map handed to the
// kernel. The descriptor stride is supplied by UEFI so future descriptor
// extensions are skipped without assuming sizeof(EFI_MEMORY_DESCRIPTOR).
namespace kernel {
namespace dma_memory {

static const uint32_t UEFI_DESCRIPTOR_MIN_BYTES = 40u;
static const uint64_t UEFI_PAGE_BYTES = 4096u;
static const uint64_t MAX_MEMORY_MAP_ENTRIES = 4096u;

static const uint32_t UEFI_MEMORY_RESERVED = 0u;
static const uint32_t UEFI_MEMORY_LOADER_CODE = 1u;
static const uint32_t UEFI_MEMORY_LOADER_DATA = 2u;
static const uint32_t UEFI_MEMORY_RUNTIME_CODE = 5u;
static const uint32_t UEFI_MEMORY_RUNTIME_DATA = 6u;
static const uint32_t UEFI_MEMORY_ACPI_RECLAIM = 9u;
static const uint64_t UEFI_MEMORY_RUNTIME_ATTRIBUTE = 1ULL << 63u;

struct MemoryMapView {
    const uint8_t* bytes;
    uint64_t entryCount;
    uint64_t descriptorSize;
};

struct PhysicalRange {
    uint64_t base;
    uint64_t length;
};

enum class MemoryMapCheck : uint8_t {
    Valid = 0,
    InvalidView,
    InvalidDescriptor,
    InvalidAlignment,
    RangeOverflow,
    RangeUncovered,
    OwnershipTypeMismatch,
    AmbiguousOwnership
};

inline uint32_t read_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) |
           (static_cast<uint32_t>(bytes[3]) << 24u);
}

inline uint64_t read_u64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(read_u32(bytes)) |
           (static_cast<uint64_t>(read_u32(bytes + 4u)) << 32u);
}

inline bool valid_view(const MemoryMapView& map)
{
    if (map.bytes == nullptr || map.entryCount == 0u ||
        map.entryCount > MAX_MEMORY_MAP_ENTRIES ||
        map.descriptorSize < UEFI_DESCRIPTOR_MIN_BYTES ||
        map.descriptorSize > UINT32_MAX ||
        map.entryCount > UINT64_MAX / map.descriptorSize) return false;
    const uint64_t byteLength = map.entryCount * map.descriptorSize;
    const uint64_t address = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(map.bytes));
    return address <= UINT64_MAX - byteLength;
}

inline const uint8_t* descriptor(const MemoryMapView& map, uint64_t index)
{
    if (!valid_view(map) || index >= map.entryCount) return nullptr;
    return map.bytes + index * map.descriptorSize;
}

inline bool descriptor_range(const uint8_t* item, uint32_t* typeOut,
                             uint64_t* baseOut, uint64_t* endOut)
{
    if (item == nullptr || typeOut == nullptr || baseOut == nullptr ||
        endOut == nullptr) return false;
    const uint32_t type = read_u32(item);
    const uint64_t base = read_u64(item + 8u);
    const uint64_t pages = read_u64(item + 24u);
    if (pages == 0u || (base & (UEFI_PAGE_BYTES - 1u)) != 0u ||
        pages > UINT64_MAX / UEFI_PAGE_BYTES) return false;
    const uint64_t length = pages * UEFI_PAGE_BYTES;
    if (base > UINT64_MAX - length) return false;
    *typeOut = type;
    *baseOut = base;
    *endOut = base + length;
    return true;
}

inline bool loader_data_type(uint32_t type)
{
    return type == UEFI_MEMORY_LOADER_DATA;
}

inline bool allowed_type(uint32_t type, uint64_t attributes,
                         bool firmwareReserved)
{
    if (!firmwareReserved) return loader_data_type(type);
    if (type == UEFI_MEMORY_RESERVED) return true;
    return (type == UEFI_MEMORY_RUNTIME_CODE ||
            type == UEFI_MEMORY_RUNTIME_DATA) &&
           (attributes & UEFI_MEMORY_RUNTIME_ATTRIBUTE) != 0u;
}

// Prove that [base, base + length) is covered by one or more adjacent UEFI
// descriptors of the required ownership class. Any gap, overlapping
// descriptor, unaligned descriptor, or arithmetic overflow is rejected.
inline MemoryMapCheck range_has_ownership(const MemoryMapView& map,
                                          uint64_t base, uint64_t length,
                                          bool firmwareReserved,
                                          uint64_t requiredAlignment = 1u)
{
    if (!valid_view(map) || length == 0u) return MemoryMapCheck::InvalidView;
    if (requiredAlignment == 0u ||
        (requiredAlignment & (requiredAlignment - 1u)) != 0u ||
        (base & (requiredAlignment - 1u)) != 0u) {
        return MemoryMapCheck::InvalidAlignment;
    }
    if (base > UINT64_MAX - length) return MemoryMapCheck::RangeOverflow;
    const uint64_t end = base + length;
    uint64_t cursor = base;
    while (cursor < end) {
        uint32_t coveringCount = 0u;
        uint64_t next = cursor;
        bool coveringTypeAllowed = false;
        for (uint64_t i = 0u; i < map.entryCount; ++i) {
            const uint8_t* item = descriptor(map, i);
            uint32_t type = 0u;
            uint64_t descriptorBase = 0u;
            uint64_t descriptorEnd = 0u;
            if (!descriptor_range(item, &type, &descriptorBase, &descriptorEnd)) {
                return MemoryMapCheck::InvalidDescriptor;
            }
            if (cursor < descriptorBase || cursor >= descriptorEnd) continue;
            ++coveringCount;
            if (allowed_type(type, read_u64(item + 32u), firmwareReserved)) {
                coveringTypeAllowed = true;
                next = descriptorEnd < end ? descriptorEnd : end;
            }
        }
        if (coveringCount == 0u) return MemoryMapCheck::RangeUncovered;
        if (coveringCount != 1u) return MemoryMapCheck::AmbiguousOwnership;
        if (!coveringTypeAllowed) return MemoryMapCheck::OwnershipTypeMismatch;
        if (next <= cursor) return MemoryMapCheck::InvalidDescriptor;
        cursor = next;
    }
    return MemoryMapCheck::Valid;
}

inline bool ranges_overlap(const PhysicalRange& a, const PhysicalRange& b)
{
    if (a.length == 0u || b.length == 0u ||
        a.base > UINT64_MAX - a.length || b.base > UINT64_MAX - b.length) {
        return false;
    }
    return a.base < b.base + b.length && b.base < a.base + a.length;
}

} // namespace dma_memory
} // namespace kernel
