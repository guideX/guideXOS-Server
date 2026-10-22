#include "kernel/native_elf_alignment.h"

int main() {
    using namespace kernel::native_elf_alignment;

    uint64_t alignment = 0;
    if (!segment_alignment_valid(0x0A40u, 0x201A40u, 0x1000u)) return 1;
    if (segment_alignment_valid(0x0A40u, 0x201A41u, 0x1000u)) return 2;
    if (segment_alignment_valid(0x0A40u, 0x201A40u, 0x1800u)) return 3;

    if (!required_alignment(0x1000u, 0x1000u, &alignment) || alignment != 0x1000u) return 4;
    if (!required_alignment(1u, 0x1000u, &alignment) || alignment != 0x1000u) return 5;
    if (!required_alignment(0x2000u, 0x1000u, &alignment) || alignment != 0x2000u) return 6;
    if (required_alignment(0x1800u, 0x1000u, &alignment)) return 7;

    uint64_t bytes = 0;
    if (!allocation_size(0x16000u, 0x1000u, 0xFFFFFFFFull, &bytes) || bytes != 0x16FFFu) return 8;
    if (allocation_size(0xFFFFFFFFull, 0x1000u, 0xFFFFFFFFull, &bytes)) return 9;

    uintptr_t base = 0;
    if (!image_base(static_cast<uintptr_t>(0x60C1F8u), 0x200000u, 0x1000u, &base) ||
        base != static_cast<uintptr_t>(0x60D000u)) return 10;
    if (((base + static_cast<uintptr_t>(0x2001E0u - 0x200000u)) & 0xFu) != 0u) return 11;
    if (((base - static_cast<uintptr_t>(0x200000u)) & 0xFFFu) != 0u) return 12;

    if (!image_base(static_cast<uintptr_t>(0x100000u), 0x201000u, 0x2000u, &base) ||
        base != static_cast<uintptr_t>(0x101000u)) return 13;
    if (image_base(~static_cast<uintptr_t>(0), 0u, 0x1000u, &base)) return 14;
    return 0;
}
