#pragma once

#include <Uefi.h>
#include <stdint.h>

// Minimal x86_64 paging builder for UEFI->kernel handoff.
// Builds identity-mapped 4KiB pages for a small set of physical ranges.
// Intended for QEMU/OVMF, long mode already enabled by firmware.

namespace guideXOS {
namespace paging
{
    // Page table entry flags
    static constexpr UINT64 PTE_P  = 1ull << 0;   // Present
    static constexpr UINT64 PTE_W  = 1ull << 1;   // Writable
    static constexpr UINT64 PTE_U  = 1ull << 2;   // User (keep 0 for kernel)
    static constexpr UINT64 PTE_PWT= 1ull << 3;
    static constexpr UINT64 PTE_PCD= 1ull << 4;
    static constexpr UINT64 PTE_A  = 1ull << 5;
    static constexpr UINT64 PTE_D  = 1ull << 6;
    static constexpr UINT64 PTE_PS = 1ull << 7;   // Large page (PD/PTD)
    static constexpr UINT64 PTE_G  = 1ull << 8;
    static constexpr UINT64 PTE_NX = 1ull << 63;  // No-execute (if supported/enabled)

    struct PageTables
    {
        EFI_PHYSICAL_ADDRESS Pml4Phys;
    };

    // Build a minimal identity-mapped page table set for the ranges provided.
    // All pages are mapped RW, executable (NX not set) to keep it minimal.
    EFI_STATUS BuildIdentityPageTables(
        EFI_SYSTEM_TABLE* SystemTable,
        const EFI_PHYSICAL_ADDRESS* rangesBegin,
        const EFI_PHYSICAL_ADDRESS* rangesEnd,
        /*in*/ const UINTN* sizesBegin,
        PageTables* out);

    // Helper to add a mapping for [physBase, physBase+size) as identity map
    EFI_STATUS MapIdentityRange(
        EFI_SYSTEM_TABLE* SystemTable,
        EFI_PHYSICAL_ADDRESS pml4Phys,
        EFI_PHYSICAL_ADDRESS physBase,
        UINTN sizeBytes);

    // Helper to add a mapping for [virtBase, virtBase+size) -> physBase
    EFI_STATUS MapRange(
        EFI_SYSTEM_TABLE* SystemTable,
        EFI_PHYSICAL_ADDRESS pml4Phys,
        UINT64 virtBase,
        EFI_PHYSICAL_ADDRESS physBase,
        UINTN sizeBytes);

    // Identity-map all page table pages allocated so far.
    // MUST be called after MapRange() to ensure newly allocated PT pages are accessible.
    EFI_STATUS IdentityMapPageTablePages(
        EFI_SYSTEM_TABLE* SystemTable,
        EFI_PHYSICAL_ADDRESS pml4Phys);
}
}
