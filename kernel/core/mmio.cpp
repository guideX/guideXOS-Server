//
// Kernel MMIO Mapping Implementation
//
// QEMU-only x86_64 runtime MMIO mapping for diagnostic VirtIO probing.
// The active path installs supervisor-only, NX, UC-style mappings in a
// reserved high virtual MMIO window and keeps the unmap path conservative.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/mmio.h"
#include "include/kernel/arch.h"

#if defined(_MSC_VER)
#define GXOS_ALIGN_4096 __declspec(align(4096))
#else
#define GXOS_ALIGN_4096 __attribute__((aligned(4096)))
#endif

namespace kernel {
namespace mmio {

namespace {

#if defined(ARCH_AMD64) && defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)

static const uint64_t PAGE_SHIFT = 12ULL;
static const uint64_t PAGE_MASK = PAGE_SIZE_BYTES - 1ULL;
static const uint64_t PTE_PRESENT = (1ULL << 0);
static const uint64_t PTE_WRITABLE = (1ULL << 1);
static const uint64_t PTE_USER = (1ULL << 2);
static const uint64_t PTE_PWT = (1ULL << 3);
static const uint64_t PTE_PCD = (1ULL << 4);
static const uint64_t PTE_PS = (1ULL << 7);
static const uint64_t PTE_NX = (1ULL << 63);
static const uint64_t PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;
static const uint64_t SUPPORTED_FLAG_MASK =
    MAP_FLAG_NON_USER | MAP_FLAG_NO_EXEC | MAP_FLAG_UNCACHED |
    MAP_FLAG_WRITE_THROUGH | MAP_FLAG_WRITE_COMBINING;

struct PageRecord {
    bool active;
    uint64_t physicalPage;
    uint64_t virtualPage;
    uint32_t refCount;
};

static PageRecord s_windowPages[MMIO_WINDOW_PAGE_COUNT];
static uint64_t s_nextWindowPage = 0;

static const uint64_t kPageTablePoolPages = 64;
GXOS_ALIGN_4096 static uint8_t s_pageTablePool[kPageTablePoolPages][PAGE_SIZE_BYTES];
static bool s_pageTablePoolUsed[kPageTablePoolPages];

static void memzero(void* dst, size_t len)
{
    uint8_t* bytes = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        bytes[i] = 0;
    }
}

static uint64_t* current_cr3_root()
{
    return reinterpret_cast<uint64_t*>(kernel::arch::read_cr3() & ~0xFFFULL);
}

static uint64_t* allocate_page_table_page()
{
    for (uint64_t i = 0; i < kPageTablePoolPages; ++i) {
        if (s_pageTablePoolUsed[i]) {
            continue;
        }

        s_pageTablePoolUsed[i] = true;
        uint8_t* page = &s_pageTablePool[i][0];
        memzero(page, PAGE_SIZE_BYTES);
        return reinterpret_cast<uint64_t*>(page);
    }

    return nullptr;
}

static uint64_t page_base(uint64_t addr)
{
    return addr & ~PAGE_MASK;
}

static uint64_t page_offset(uint64_t addr)
{
    return addr & PAGE_MASK;
}

static uint64_t slot_to_virtual_page(uint64_t slot)
{
    return MMIO_WINDOW_BASE + (slot * PAGE_SIZE_BYTES);
}

static int find_page_record(uint64_t physicalPage)
{
    for (uint64_t i = 0; i < MMIO_WINDOW_PAGE_COUNT; ++i) {
        const PageRecord& record = s_windowPages[i];
        if (record.active && record.refCount != 0 && record.physicalPage == physicalPage) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

static bool ensure_table_entry(uint64_t* table, uint64_t index, uint64_t** childOut,
                               const char** reasonOut)
{
    if (table == nullptr || childOut == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO page-table walk received a null table pointer";
        }
        return false;
    }

    uint64_t& entry = table[index];
    if ((entry & PTE_PRESENT) != 0u) {
        if ((entry & PTE_PS) != 0u) {
            if (reasonOut != nullptr) {
                *reasonOut = "conflicting huge-page mapping already occupies the MMIO window";
            }
            return false;
        }

        const uint64_t childPhys = entry & PTE_ADDR_MASK;
        *childOut = reinterpret_cast<uint64_t*>(childPhys);
        return true;
    }

    uint64_t* child = allocate_page_table_page();
    if (child == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO page-table page pool exhausted";
        }
        return false;
    }

    const uint64_t childPhys = reinterpret_cast<uint64_t>(child);
    entry = (childPhys & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE;
    *childOut = child;
    return true;
}

static bool walk_to_pt(uint64_t virtualAddress, uint64_t** ptOut, const char** reasonOut)
{
    if (ptOut == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO page-table walk received a null output pointer";
        }
        return false;
    }

    uint64_t* pml4 = current_cr3_root();
    if (pml4 == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO CR3 root page table is unavailable";
        }
        return false;
    }

    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1FFULL;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1FFULL;
    const uint64_t pdIndex = (virtualAddress >> 21) & 0x1FFULL;

    uint64_t* pdpt = nullptr;
    if (!ensure_table_entry(pml4, pml4Index, &pdpt, reasonOut)) {
        return false;
    }

    uint64_t* pd = nullptr;
    if (!ensure_table_entry(pdpt, pdptIndex, &pd, reasonOut)) {
        return false;
    }

    uint64_t* pt = nullptr;
    if (!ensure_table_entry(pd, pdIndex, &pt, reasonOut)) {
        return false;
    }

    *ptOut = pt;
    return true;
}

static uint64_t make_mmio_leaf(uint64_t physicalPage)
{
    return (physicalPage & PTE_ADDR_MASK) |
           PTE_PRESENT |
           PTE_WRITABLE |
           PTE_PCD |
           PTE_PWT |
           PTE_NX;
}

static bool map_single_page(uint64_t physicalPage, uint64_t* mappedVirtualPageOut,
                            bool* createdNewOut, const char** reasonOut)
{
    if (mappedVirtualPageOut == nullptr || createdNewOut == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO single-page mapper received a null output pointer";
        }
        return false;
    }

    const int existingIndex = find_page_record(physicalPage);
    if (existingIndex >= 0) {
        PageRecord& record = s_windowPages[existingIndex];
        if (record.virtualPage < MMIO_WINDOW_BASE || record.virtualPage >= MMIO_WINDOW_LIMIT) {
            if (reasonOut != nullptr) {
                *reasonOut = "conflicting MMIO mapping already occupies the window slot";
            }
            return false;
        }

        uint64_t* pt = nullptr;
        if (!walk_to_pt(record.virtualPage, &pt, reasonOut)) {
            return false;
        }

        const uint64_t ptIndex = (record.virtualPage >> 12) & 0x1FFULL;
        const uint64_t expectedLeaf = make_mmio_leaf(physicalPage);
        const uint64_t currentLeaf = pt[ptIndex];
        if ((currentLeaf & PTE_PRESENT) != 0u) {
            if ((currentLeaf & PTE_ADDR_MASK) != (expectedLeaf & PTE_ADDR_MASK) ||
                (currentLeaf & (PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT | PTE_NX | PTE_USER)) !=
                    (expectedLeaf & (PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT | PTE_NX))) {
                if (reasonOut != nullptr) {
                    *reasonOut = "conflicting MMIO mapping already occupies the window slot";
                }
                return false;
            }
        } else {
            pt[ptIndex] = expectedLeaf;
        }

        kernel::arch::invalidate_tlb_entry(record.virtualPage);
        record.refCount += 1;
        *mappedVirtualPageOut = record.virtualPage;
        *createdNewOut = false;
        return true;
    }

    if (s_nextWindowPage >= MMIO_WINDOW_PAGE_COUNT) {
        if (reasonOut != nullptr) {
            *reasonOut = "reserved MMIO window exhausted";
        }
        return false;
    }

    const uint64_t slot = s_nextWindowPage++;
    const uint64_t virtualPage = slot_to_virtual_page(slot);

    uint64_t* pt = nullptr;
    if (!walk_to_pt(virtualPage, &pt, reasonOut)) {
        return false;
    }

    const uint64_t ptIndex = (virtualPage >> 12) & 0x1FFULL;
    const uint64_t currentLeaf = pt[ptIndex];
    const uint64_t expectedLeaf = make_mmio_leaf(physicalPage);
    if ((currentLeaf & PTE_PRESENT) != 0u) {
        if ((currentLeaf & PTE_ADDR_MASK) != (expectedLeaf & PTE_ADDR_MASK) ||
            (currentLeaf & (PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT | PTE_NX | PTE_USER)) !=
                (expectedLeaf & (PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT | PTE_NX))) {
            if (reasonOut != nullptr) {
                *reasonOut = "conflicting MMIO mapping already occupies the window slot";
            }
            return false;
        }
    } else {
        pt[ptIndex] = expectedLeaf;
    }

    kernel::arch::invalidate_tlb_entry(virtualPage);

    PageRecord& record = s_windowPages[slot];
    record.active = true;
    record.physicalPage = physicalPage;
    record.virtualPage = virtualPage;
    record.refCount = 1;

    *mappedVirtualPageOut = virtualPage;
    *createdNewOut = true;
    return true;
}

static bool map_page_range(uint64_t alignedBase, uint64_t pageCount,
                           uint64_t* virtualBaseOut, bool* createdNewOut,
                           const char** reasonOut)
{
    if (virtualBaseOut == nullptr || createdNewOut == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO range mapper received a null output pointer";
        }
        return false;
    }

    *virtualBaseOut = 0;
    *createdNewOut = false;

    if (pageCount == 0) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO range length is zero";
        }
        return false;
    }

    uint64_t firstVirtualPage = 0;
    for (uint64_t i = 0; i < pageCount; ++i) {
        uint64_t mappedVirtualPage = 0;
        bool createdNew = false;
        const uint64_t physicalPage = alignedBase + (i * PAGE_SIZE_BYTES);
        if (!map_single_page(physicalPage, &mappedVirtualPage, &createdNew, reasonOut)) {
            return false;
        }

        if (i == 0) {
            firstVirtualPage = mappedVirtualPage;
        }

        *createdNewOut = *createdNewOut || createdNew;
    }

    *virtualBaseOut = firstVirtualPage;
    return true;
}

#endif // ARCH_AMD64 && GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE

} // namespace

bool mapForDevice(uint64_t physicalBase, uint64_t length,
                  uint64_t* mappedVirtualOut,
                  MappingReport* reportOut,
                  uint32_t flags)
{
    MappingReport report = describeRange(physicalBase, length, flags);
    if (mappedVirtualOut != nullptr) {
        *mappedVirtualOut = 0;
    }

    report.success = false;
    report.kernelVirtualBase = 0;
    report.mappedVirtual = 0;
    report.mappedLength = report.alignedLength;

#if defined(ARCH_AMD64) && defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    if ((flags & ~SUPPORTED_FLAG_MASK) != 0u) {
        report.reason = "MMIO mapping received unknown flags";
        report.nextKernelFeature = "flag validation";
        if (reportOut != nullptr) {
            *reportOut = report;
        }
        return false;
    }

    if (length == 0) {
        report.reason = "MMIO range length is zero";
        report.nextKernelFeature = "length validation";
        if (reportOut != nullptr) {
            *reportOut = report;
        }
        return false;
    }

    if (range_overflows(physicalBase, length)) {
        report.reason = "MMIO range overflows address space";
        report.nextKernelFeature = "overflow-safe MMIO range validation";
        if (reportOut != nullptr) {
            *reportOut = report;
        }
        return false;
    }

    if ((flags & (MAP_FLAG_NON_USER | MAP_FLAG_NO_EXEC | MAP_FLAG_UNCACHED)) !=
        (MAP_FLAG_NON_USER | MAP_FLAG_NO_EXEC | MAP_FLAG_UNCACHED)) {
        if ((flags & MAP_FLAG_NON_USER) == 0u || (flags & MAP_FLAG_NO_EXEC) == 0u) {
            report.reason = "MMIO mappings must be kernel-only, no-executable, and uncached";
            report.nextKernelFeature = "kernel-only UC MMIO page-table flags";
        } else if ((flags & MAP_FLAG_UNCACHED) == 0u) {
            report.reason = "MMIO mappings must request the UC cache mode";
            report.nextKernelFeature = "UC PCD/PWT MMIO page-table flags";
        } else {
            report.reason = "MMIO cache flags are invalid";
            report.nextKernelFeature = "UC-only MMIO cache policy";
        }
        if (reportOut != nullptr) {
            *reportOut = report;
        }
        return false;
    }

    if (!report.cacheAttributesSupported) {
        report.reason = "safe UC cache mode is unavailable for this build";
        report.nextKernelFeature = "UC cache support";
        if (reportOut != nullptr) {
            *reportOut = report;
        }
        return false;
    }

    const uint64_t pageCount = report.pageCount;
    const uint64_t offset = report.pageOffset;
    uint64_t virtualBase = 0;
    bool createdNew = false;
    const bool mapped = map_page_range(report.alignedBase, pageCount, &virtualBase, &createdNew, &report.reason);
    if (!mapped) {
        if (report.reason == nullptr) {
            report.reason = "reserved MMIO window mapping failed";
        }
        report.nextKernelFeature = "reserved MMIO window expansion";
        if (reportOut != nullptr) {
            *reportOut = report;
        }
        return false;
    }

    report.success = true;
    report.kernelVirtualBase = virtualBase;
    report.mappedVirtual = virtualBase + offset;
    report.mappedLength = report.alignedLength;
    report.withinSafeDirectMap = true;
    report.requiresNewPageTableEntries = createdNew;
    report.cacheAttributesRequested = true;
    report.cacheAttributesSupported = true;
    report.cacheMode = "uc(pcd+pwt)";
    report.reason = createdNew ? "mapped into reserved kernel MMIO window"
                               : "reused reserved kernel MMIO page";
    report.nextKernelFeature = "controlled feature negotiation";

    if (mappedVirtualOut != nullptr) {
        *mappedVirtualOut = report.mappedVirtual;
    }
    if (reportOut != nullptr) {
        *reportOut = report;
    }
    return true;
#else
    (void)physicalBase;
    (void)length;
    (void)flags;
    report.reason = "runtime MMIO mapping is gated to the x86_64 QEMU probe build";
    report.nextKernelFeature = "GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE";
    if (reportOut != nullptr) {
        *reportOut = report;
    }
    return false;
#endif
}

bool unmap(uint64_t mappedVirtual, uint64_t length, const char** reasonOut)
{
#if defined(ARCH_AMD64) && defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    if (reasonOut != nullptr) {
        *reasonOut = nullptr;
    }

    if (length == 0) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO unmap length is zero";
        }
        return false;
    }

    const uint64_t pageOffset = page_offset(mappedVirtual);
    const uint64_t firstPage = page_base(mappedVirtual);
    const uint64_t pageSpan = align_up(pageOffset + length);
    const uint64_t pageCount = pageSpan / PAGE_SIZE_BYTES;

    if (firstPage < MMIO_WINDOW_BASE || firstPage >= MMIO_WINDOW_LIMIT) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO unmap address is outside the reserved window";
        }
        return false;
    }

    const uint64_t firstSlot = (firstPage - MMIO_WINDOW_BASE) / PAGE_SIZE_BYTES;
    if (firstSlot + pageCount > MMIO_WINDOW_PAGE_COUNT) {
        if (reasonOut != nullptr) {
            *reasonOut = "MMIO unmap extends beyond the reserved window";
        }
        return false;
    }

    for (uint64_t i = 0; i < pageCount; ++i) {
        const uint64_t slot = firstSlot + i;
        PageRecord& record = s_windowPages[slot];
        if (!record.active || record.refCount == 0) {
            if (reasonOut != nullptr) {
                *reasonOut = "MMIO unmap encountered an unmapped window slot";
            }
            return false;
        }

        const uint64_t expectedVirtualPage = slot_to_virtual_page(slot);
        const uint64_t expectedPhysicalPage = record.physicalPage;
        uint64_t* pt = nullptr;
        const char* walkReason = nullptr;
        if (!walk_to_pt(expectedVirtualPage, &pt, &walkReason)) {
            if (reasonOut != nullptr) {
                *reasonOut = walkReason != nullptr ? walkReason : "MMIO unmap could not reach the page table";
            }
            return false;
        }

        const uint64_t ptIndex = (expectedVirtualPage >> 12) & 0x1FFULL;
        uint64_t& leaf = pt[ptIndex];
        const uint64_t expectedLeaf = make_mmio_leaf(expectedPhysicalPage);
        if ((leaf & PTE_PRESENT) == 0u || (leaf & PTE_ADDR_MASK) != (expectedLeaf & PTE_ADDR_MASK)) {
            if (reasonOut != nullptr) {
                *reasonOut = "MMIO unmap encountered a conflicting leaf mapping";
            }
            return false;
        }

        if (record.refCount > 0) {
            --record.refCount;
        }

        if (record.refCount == 0) {
            leaf = 0;
            kernel::arch::invalidate_tlb_entry(expectedVirtualPage);
            record.active = false;
        }
    }

    return true;
#else
    (void)mappedVirtual;
    (void)length;
    if (reasonOut != nullptr) {
        *reasonOut = "runtime MMIO unmap is gated to the x86_64 QEMU probe build";
    }
    return false;
#endif
}

} // namespace mmio
} // namespace kernel
