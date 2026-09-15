//
// Shared NativeElf bootstrap contract.
//
// This is intentionally small and freestanding so the guest compiler, kernel
// loader, and UEFI bootloader use one address-space contract.
//
#pragma once

#include <stdint.h>

namespace guidexos {
namespace native_elf {

// 0x200000 is occupied by the UEFI handoff stack in the current boot path.
// The lower 0x10000000 choice can overlap the physical kernel allocation in
// the UEFI loader.  This fixed 512-MiB window at 0x50000000 is below the
// high-end kernel allocation and inside the 4-GiB validation guest's
// identity-mapped RAM range.  It is reserved by the bootloader before
// ExitBootServices and reused by the kernel loader for every invocation.
static const uint64_t IMAGE_BASE = 0x50000000ULL;
static const uint64_t REGION_SIZE = 0x20000000ULL;

static const uint32_t PAGE_SIZE = 0x1000U;
// The compiler bootstrap image is bounded by its 64 KiB code, 8 KiB rodata,
// 8 KiB mutable-data, and page-alignment/header budget.
static const uint32_t MAX_ELF_FILE_BYTES = 98304U;
static const uint64_t MAX_MAPPED_BYTES = REGION_SIZE;
static const uint16_t MAX_LOAD_SEGMENTS = 4U;
// Linkers may add bounded non-load metadata headers such as PT_PHDR and
// PT_GNU_STACK. They are ignored by the fixed-address loader; PT_LOAD remains
// capped independently by MAX_LOAD_SEGMENTS.
static const uint16_t MAX_PROGRAM_HEADERS = 8U;

} // namespace native_elf
} // namespace guidexos
