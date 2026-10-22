#ifndef KERNEL_NATIVE_ELF_ALIGNMENT_H
#define KERNEL_NATIVE_ELF_ALIGNMENT_H

#include <stdint.h>

namespace kernel {
namespace native_elf_alignment {

inline bool is_power_of_two(uint64_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

inline bool segment_alignment_valid(uint64_t fileOffset, uint64_t virtualAddress,
                                    uint64_t segmentAlignment) {
    if (segmentAlignment <= 1u) return true;
    if (!is_power_of_two(segmentAlignment)) return false;
    const uint64_t mask = segmentAlignment - 1u;
    return (fileOffset & mask) == (virtualAddress & mask);
}

inline bool required_alignment(uint64_t segmentAlignment, uint64_t minimumAlignment,
                               uint64_t* result) {
    if (!result || !is_power_of_two(minimumAlignment) ||
        (segmentAlignment > 1u && !is_power_of_two(segmentAlignment))) return false;
    *result = segmentAlignment > minimumAlignment ? segmentAlignment : minimumAlignment;
    return true;
}

inline bool allocation_size(uint64_t imageBytes, uint64_t alignment,
                            uint64_t maximumAllocationBytes, uint64_t* result) {
    if (!result || imageBytes == 0u || !is_power_of_two(alignment) ||
        alignment - 1u > maximumAllocationBytes ||
        imageBytes > maximumAllocationBytes - (alignment - 1u)) return false;
    *result = imageBytes + alignment - 1u;
    return true;
}

inline bool image_base(uintptr_t allocationAddress, uint64_t minimumVirtualAddress,
                       uint64_t alignment, uintptr_t* result) {
    if (!result || !is_power_of_two(alignment)) return false;
    const uint64_t wideMask = alignment - 1u;
    const uintptr_t mask = static_cast<uintptr_t>(wideMask);
    if (static_cast<uint64_t>(mask) != wideMask) return false;

    const uintptr_t wantedResidue = static_cast<uintptr_t>(minimumVirtualAddress & wideMask);
    const uintptr_t currentResidue = allocationAddress & mask;
    const uintptr_t padding = (wantedResidue - currentResidue) & mask;
    const uintptr_t maximumAddress = ~static_cast<uintptr_t>(0);
    if (allocationAddress > maximumAddress - padding) return false;
    *result = allocationAddress + padding;
    return true;
}

} // namespace native_elf_alignment
} // namespace kernel

#endif
