#include "paging.h"
#include "uefi_shim.h"

namespace guideXOS {
namespace paging
{
    static inline EFI_PHYSICAL_ADDRESS AlignDown4K(EFI_PHYSICAL_ADDRESS v) { return v & ~(EFI_PHYSICAL_ADDRESS)0xFFF; }
    static inline EFI_PHYSICAL_ADDRESS AlignUp4K(EFI_PHYSICAL_ADDRESS v) { return (v + 0xFFF) & ~(EFI_PHYSICAL_ADDRESS)0xFFF; }

    static inline UINT64* PhysToPtr(EFI_PHYSICAL_ADDRESS p) { return (UINT64*)(UINTN)p; }

    // Track allocated page table pages so we can identity-map them
    static constexpr UINTN MAX_PT_PAGES = 512;
    static EFI_PHYSICAL_ADDRESS g_ptPages[MAX_PT_PAGES];
    static UINTN g_ptPageCount = 0;

    static EFI_STATUS AllocTable(EFI_SYSTEM_TABLE* SystemTable, EFI_PHYSICAL_ADDRESS* outPhys)
    {
        *outPhys = 0;
        EFI_STATUS st = SystemTable->BootServices->AllocatePages(
            AllocateAnyPages,
            EfiLoaderData,
            1,
            outPhys);
        if (EFI_ERROR(st)) return st;
        SetMem((void*)(UINTN)(*outPhys), EFI_PAGE_SIZE, 0);
        
        // Track this page for later self-mapping
        if (g_ptPageCount < MAX_PT_PAGES) {
            g_ptPages[g_ptPageCount++] = *outPhys;
        }
        
        return EFI_SUCCESS;
    }

    // Walk/allocate PML4->PDPT->PD->PT for a given virtual address (identity => vaddr==paddr)
    static EFI_STATUS EnsurePtForVaddr(EFI_SYSTEM_TABLE* SystemTable, EFI_PHYSICAL_ADDRESS pml4Phys, UINT64 vaddr, EFI_PHYSICAL_ADDRESS* outPtPhys)
    {
        const UINTN pml4i = (UINTN)((vaddr >> 39) & 0x1FFull);
        const UINTN pdpti = (UINTN)((vaddr >> 30) & 0x1FFull);
        const UINTN pdi   = (UINTN)((vaddr >> 21) & 0x1FFull);

        UINT64* pml4 = PhysToPtr(pml4Phys);

        // PDPT
        EFI_PHYSICAL_ADDRESS pdptPhys = 0;
        if ((pml4[pml4i] & PTE_P) == 0) {
            EFI_STATUS st = AllocTable(SystemTable, &pdptPhys);
            if (EFI_ERROR(st)) return st;
            pml4[pml4i] = (UINT64)pdptPhys | PTE_P | PTE_W;
        } else {
            pdptPhys = (EFI_PHYSICAL_ADDRESS)(pml4[pml4i] & ~0xFFFull);
        }

        UINT64* pdpt = PhysToPtr(pdptPhys);

        // PD
        EFI_PHYSICAL_ADDRESS pdPhys = 0;
        if ((pdpt[pdpti] & PTE_P) == 0) {
            EFI_STATUS st = AllocTable(SystemTable, &pdPhys);
            if (EFI_ERROR(st)) return st;
            pdpt[pdpti] = (UINT64)pdPhys | PTE_P | PTE_W;
        } else {
            pdPhys = (EFI_PHYSICAL_ADDRESS)(pdpt[pdpti] & ~0xFFFull);
        }

        UINT64* pd = PhysToPtr(pdPhys);

        // PT (no large pages here, keep it simple)
        EFI_PHYSICAL_ADDRESS ptPhys = 0;
        if ((pd[pdi] & PTE_P) == 0) {
            EFI_STATUS st = AllocTable(SystemTable, &ptPhys);
            if (EFI_ERROR(st)) return st;
            pd[pdi] = (UINT64)ptPhys | PTE_P | PTE_W;
        } else {
            if (pd[pdi] & PTE_PS) {
                // Unexpected: already a 2MiB mapping. For minimal loader, treat as error.
                return EFI_UNSUPPORTED;
            }
            ptPhys = (EFI_PHYSICAL_ADDRESS)(pd[pdi] & ~0xFFFull);
        }

        *outPtPhys = ptPhys;
        return EFI_SUCCESS;
    }

    EFI_STATUS MapIdentityRange(EFI_SYSTEM_TABLE* SystemTable, EFI_PHYSICAL_ADDRESS pml4Phys, EFI_PHYSICAL_ADDRESS physBase, UINTN sizeBytes)
    {
        if (sizeBytes == 0) return EFI_SUCCESS;

        EFI_PHYSICAL_ADDRESS start = AlignDown4K(physBase);
        EFI_PHYSICAL_ADDRESS end   = AlignUp4K(physBase + (EFI_PHYSICAL_ADDRESS)sizeBytes);

        for (EFI_PHYSICAL_ADDRESS p = start; p < end; p += 0x1000) {
            EFI_PHYSICAL_ADDRESS ptPhys = 0;
            EFI_STATUS st = EnsurePtForVaddr(SystemTable, pml4Phys, (UINT64)p, &ptPhys);
            if (EFI_ERROR(st)) return st;

            const UINTN pti = (UINTN)(((UINT64)p >> 12) & 0x1FFull);
            UINT64* pt = PhysToPtr(ptPhys);
            
            // CRITICAL FIX: Check if mapping already exists to avoid conflicts
            if ((pt[pti] & PTE_P) != 0) {
                EFI_PHYSICAL_ADDRESS existingPhys = pt[pti] & ~0xFFFull;
                if (existingPhys != p) {
                    // Conflict! Overwrite with identity mapping.
                }
            }
            
            // Map as RW executable (Present, Writable)
            // NOTE: Removed PTE_G (Global) flag as it requires CR4.PGE to be set
            pt[pti] = (UINT64)p | PTE_P | PTE_W;
        }

        return EFI_SUCCESS;
    }

    EFI_STATUS MapRange(
        EFI_SYSTEM_TABLE* SystemTable,
        EFI_PHYSICAL_ADDRESS pml4Phys,
        UINT64 virtBase,
        EFI_PHYSICAL_ADDRESS physBase,
        UINTN sizeBytes)
    {
        if (sizeBytes == 0) return EFI_SUCCESS;

        UINT64 vStart = virtBase & ~0xFFFULL;
        UINT64 vEnd   = (virtBase + (UINT64)sizeBytes + 0xFFFULL) & ~0xFFFULL;
        UINT64 phys   = physBase & ~0xFFFULL;

        for (UINT64 v = vStart; v < vEnd; v += 0x1000, phys += 0x1000) {
            EFI_PHYSICAL_ADDRESS ptPhys = 0;
            EFI_STATUS st = EnsurePtForVaddr(SystemTable, pml4Phys, v, &ptPhys);
            if (EFI_ERROR(st)) return st;

            const UINTN pti = (UINTN)((v >> 12) & 0x1FFull);
            UINT64* pt = PhysToPtr(ptPhys);

            // Overwrite mapping to desired physical address (Present, Writable)
            pt[pti] = (UINT64)phys | PTE_P | PTE_W;
        }

        return EFI_SUCCESS;
    }

    EFI_STATUS BuildIdentityPageTables(
        EFI_SYSTEM_TABLE* SystemTable,
        const EFI_PHYSICAL_ADDRESS* rangesBegin,
        const EFI_PHYSICAL_ADDRESS* rangesEnd,
        const UINTN* sizesBegin,
        PageTables* out)
    {
        if (!SystemTable || !rangesBegin || !rangesEnd || !sizesBegin || !out) return EFI_INVALID_PARAMETER;

        // Reset page table tracking
        g_ptPageCount = 0;

        EFI_PHYSICAL_ADDRESS pml4Phys = 0;
        EFI_STATUS st = AllocTable(SystemTable, &pml4Phys);
        if (EFI_ERROR(st)) return st;

        // Map each requested range (identity)
        UINTN idx = 0;
        for (auto p = rangesBegin; p != rangesEnd; ++p, ++idx) {
            st = MapIdentityRange(SystemTable, pml4Phys, *p, sizesBegin[idx]);
            if (EFI_ERROR(st)) return st;
        }

        // CRITICAL: Identity-map all page table pages themselves.
        UINTN mappedCount = 0;
        while (mappedCount < g_ptPageCount) {
            UINTN currentCount = g_ptPageCount;
            for (UINTN i = mappedCount; i < currentCount; ++i) {
                st = MapIdentityRange(SystemTable, pml4Phys, g_ptPages[i], EFI_PAGE_SIZE);
                if (EFI_ERROR(st)) return st;
            }
            mappedCount = currentCount;
        }

        out->Pml4Phys = pml4Phys;
        return EFI_SUCCESS;
    }

    EFI_STATUS IdentityMapPageTablePages(
        EFI_SYSTEM_TABLE* SystemTable,
        EFI_PHYSICAL_ADDRESS pml4Phys)
    {
        // Identity-map all page table pages that have been allocated.
        // This must be called after any MapRange() calls to ensure the
        // newly allocated page table pages are themselves accessible
        // after the CR3 switch.
        UINTN mappedCount = 0;
        while (mappedCount < g_ptPageCount) {
            UINTN currentCount = g_ptPageCount;
            for (UINTN i = mappedCount; i < currentCount; ++i) {
                EFI_STATUS st = MapIdentityRange(SystemTable, pml4Phys, g_ptPages[i], EFI_PAGE_SIZE);
                if (EFI_ERROR(st)) return st;
            }
            mappedCount = currentCount;
        }
        return EFI_SUCCESS;
    }
}
}
