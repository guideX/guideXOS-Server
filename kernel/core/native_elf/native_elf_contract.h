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
// The first replacement choice at 0x04000000 was inside the kernel's
// identity-visible virtual span.  This fixed two-MiB window at 0x10000000 is
// outside the kernel span measured during the Phase 27C audit.  It is reserved
// by the bootloader before ExitBootServices and reused by the kernel loader
// for every invocation.
static const uint64_t IMAGE_BASE = 0x10000000ULL;
static const uint64_t REGION_SIZE = 0x00200000ULL;

static const uint32_t PAGE_SIZE = 0x1000U;
static const uint32_t MAX_ELF_FILE_BYTES = 8192U;
static const uint64_t MAX_MAPPED_BYTES = 0x00100000ULL;
static const uint16_t MAX_LOAD_SEGMENTS = 4U;

} // namespace native_elf
} // namespace guidexos
