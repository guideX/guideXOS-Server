#include <stdint.h>

#include "phase2_mmu.h"
#include "../../../aarch64/phase2/phase2_validation.h"

extern "C" uint8_t __text_start[];
extern "C" uint8_t __text_end[];
extern "C" uint8_t __vectors_start[];
extern "C" uint8_t __vectors_end[];
extern "C" uint8_t __rodata_start[];
extern "C" uint8_t __rodata_end[];
extern "C" uint8_t __translation_tables_start[];
extern "C" uint8_t __translation_tables_end[];

namespace {

static const uint64_t kPageMask = UINT64_C(0xfff);
static const uint64_t kMappedPhysicalLimit = UINT64_C(0x80000000);
static const uint32_t kTableEntries = 512;
static const uint32_t kMaxTables = 520;
static const uint64_t kTableDescriptor = UINT64_C(0x3);
static const uint64_t kPageDescriptor = UINT64_C(0x3);
static const uint64_t kAttrDevice = 0;
static const uint64_t kAttrNormal = 1;
static const uint64_t kShareInner = UINT64_C(3) << 8;
static const uint64_t kAccessFlag = UINT64_C(1) << 10;
static const uint64_t kApReadOnlyEl1 = UINT64_C(2) << 6;
static const uint64_t kPxn = UINT64_C(1) << 53;
static const uint64_t kUxn = UINT64_C(1) << 54;
static const uint64_t kMmu = UINT64_C(1) << 0;
static const uint64_t kCache = UINT64_C(1) << 2;
static const uint64_t kInstructionCache = UINT64_C(1) << 12;
static const uint64_t kWriteExecuteNever = UINT64_C(1) << 19;
static const uint64_t kStackAlignment = UINT64_C(0xfff);

// 1 root + 1 L1 + 1 L2 + one L3 table per 2 MiB of the bounded low-2-GiB
// physical window.  The configured QEMU guest uses only 0x40000000-0x60000000
// for RAM, with MMIO below it.
static uint64_t gTables[kMaxTables][kTableEntries]
    __attribute__((aligned(4096), section(".translation_tables")));
static uint32_t gNextTable = 0;
static uint64_t gRoot = 0;

static void clear_table(uint64_t* table)
{
    for (uint32_t i = 0; i < kTableEntries; ++i) table[i] = 0;
}

static uint64_t* allocate_table()
{
    if (gNextTable >= kMaxTables) return nullptr;
    uint64_t* result = gTables[gNextTable++];
    clear_table(result);
    return result;
}

static bool range_end(uint64_t base, uint64_t size, uint64_t* end)
{
    return gxos_aarch64_add_u64(base, size, end) && *end > base;
}

static bool range_intersects(uint64_t page, uint64_t pageEnd, uint64_t base, uint64_t size)
{
    uint64_t end = 0;
    return range_end(base, size, &end) && page < end && base < pageEnd;
}

static uint64_t descriptor_for_page(uint64_t page, uint64_t pageEnd,
                                    uint64_t kernelBase, uint64_t kernelEnd)
{
    const uint64_t textStart = (uint64_t)(uintptr_t)__text_start;
    const uint64_t textEnd = (uint64_t)(uintptr_t)__text_end;
    const uint64_t rodataStart = (uint64_t)(uintptr_t)__rodata_start;
    const uint64_t rodataEnd = (uint64_t)(uintptr_t)__rodata_end;
    const uint64_t vectorsStart = (uint64_t)(uintptr_t)__vectors_start;
    const uint64_t vectorsEnd = (uint64_t)(uintptr_t)__vectors_end;
    const bool isText = (page < textEnd && textStart < pageEnd) ||
                        (page < vectorsEnd && vectorsStart < pageEnd);
    const bool isRodata = page < rodataEnd && rodataStart < pageEnd;
    const bool isKernelReadOnly = isText || isRodata;
    uint64_t descriptor = kPageDescriptor | (kAttrNormal << 2) | kShareInner | kAccessFlag;
    (void)kernelBase;
    (void)kernelEnd;
    if (isKernelReadOnly) descriptor |= kApReadOnlyEl1;
    if (!isText) descriptor |= kPxn | kUxn;
    return descriptor;
}

static bool ensure_l3(uint64_t virtualAddress, uint64_t** l3)
{
    const uint64_t l0Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t l1Index = (virtualAddress >> 30) & 0x1ff;
    const uint64_t l2Index = (virtualAddress >> 21) & 0x1ff;
    if (l0Index != 0) return false;

    uint64_t* l0 = gTables[0];
    uint64_t* l1 = nullptr;
    uint64_t* l2 = nullptr;
    if (l0[l0Index] == 0) {
        l1 = allocate_table();
        if (!l1) return false;
        l0[l0Index] = ((uint64_t)(uintptr_t)l1 & ~kPageMask) | kTableDescriptor;
    } else {
        l1 = (uint64_t*)(uintptr_t)(l0[l0Index] & ~kPageMask);
    }
    if (l1[l1Index] == 0) {
        l2 = allocate_table();
        if (!l2) return false;
        l1[l1Index] = ((uint64_t)(uintptr_t)l2 & ~kPageMask) | kTableDescriptor;
    } else {
        l2 = (uint64_t*)(uintptr_t)(l1[l1Index] & ~kPageMask);
    }
    if (l2[l2Index] == 0) {
        *l3 = allocate_table();
        if (!*l3) return false;
        l2[l2Index] = ((uint64_t)(uintptr_t)*l3 & ~kPageMask) | kTableDescriptor;
    } else {
        *l3 = (uint64_t*)(uintptr_t)(l2[l2Index] & ~kPageMask);
    }
    return true;
}

static bool map_range(uint64_t base, uint64_t size, uint64_t descriptor,
                      uint64_t kernelBase, uint64_t kernelEnd, bool device)
{
    uint64_t end = 0;
    if (!range_end(base, size, &end)) return false;
    const uint64_t start = base & ~kPageMask;
    uint64_t roundedEnd = 0;
    if (!gxos_aarch64_add_u64(end, kPageMask, &roundedEnd)) return false;
    roundedEnd &= ~kPageMask;
    if (roundedEnd <= start || roundedEnd > kMappedPhysicalLimit) return false;

    for (uint64_t page = start; page < roundedEnd; page += GXOS_AARCH64_PHASE2_MMU_GRANULE) {
        uint64_t* l3 = nullptr;
        if (!ensure_l3(page, &l3)) return false;
        const uint32_t l3Index = (uint32_t)((page >> 12) & 0x1ff);
        uint64_t pageDescriptor = descriptor;
        if (!device) pageDescriptor = descriptor_for_page(page, page + 0x1000, kernelBase, kernelEnd);
        pageDescriptor |= page & ~kPageMask;
        if (l3[l3Index] != 0 && l3[l3Index] != pageDescriptor) return false;
        l3[l3Index] = pageDescriptor;
    }
    return true;
}

static uint64_t make_device_descriptor()
{
    return kPageDescriptor | (kAttrDevice << 2) | kShareInner | kAccessFlag | kPxn | kUxn;
}

static uint64_t make_normal_descriptor()
{
    return kPageDescriptor | (kAttrNormal << 2) | kShareInner | kAccessFlag | kUxn;
}

static void clean_tables()
{
    const uint64_t start = (uint64_t)(uintptr_t)__translation_tables_start;
    const uint64_t end = (uint64_t)(uintptr_t)__translation_tables_end;
    for (uint64_t address = start; address < end; address += 64) {
        __asm__ volatile("dc cvac, %0" : : "r"(address) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

} // namespace

uint8_t phase2_mmu_build(const gxos_aarch64_phase2_platform* platform,
                         uint64_t kernel_base, uint64_t kernel_size)
{
    if (!platform || !platform->valid || kernel_size == 0) return 0;
    uint64_t kernelEnd = 0;
    if (!gxos_aarch64_add_u64(kernel_base, kernel_size, &kernelEnd) || kernelEnd > kMappedPhysicalLimit) return 0;
    gNextTable = 0;
    gRoot = (uint64_t)(uintptr_t)allocate_table();
    if (gRoot == 0 || (gRoot & kPageMask) != 0) return 0;

    for (uint32_t i = 0; i < platform->ram_count; ++i) {
        const uint64_t base = platform->ram[i].base;
        uint64_t end = 0;
        if (!range_end(base, platform->ram[i].size, &end) || base >= kMappedPhysicalLimit || end > kMappedPhysicalLimit ||
            (base & kPageMask) != 0 || (platform->ram[i].size & kPageMask) != 0) return 0;
        if (!map_range(base, platform->ram[i].size, make_normal_descriptor(), kernel_base, kernelEnd, false)) return 0;
    }

    if (!map_range(platform->uart_base, platform->uart_size, make_device_descriptor(), kernel_base, kernelEnd, true) ||
        !map_range(platform->gicd_base, platform->gicd_size, make_device_descriptor(), kernel_base, kernelEnd, true) ||
        !map_range(platform->gicc_base, platform->gicc_size, make_device_descriptor(), kernel_base, kernelEnd, true)) return 0;

    clean_tables();
    return 1;
}

void phase2_mmu_enable()
{
    const uint64_t mair = UINT64_C(0x000000000000ff00); // Attr0=device-nGnRnE, Attr1=normal WBWA
    const uint64_t tcr = (UINT64_C(2) << 32) | // IPS=40-bit PA
                         (UINT64_C(1) << 23) | // EPD1: TTBR1 is intentionally unused
                         (UINT64_C(2) << 30) | // TG1=4 KiB
                         (UINT64_C(3) << 28) | // SH1=inner shareable
                         (UINT64_C(1) << 26) | // ORGN1=WBWA
                         (UINT64_C(1) << 24) | // IRGN1=WBWA
                         (UINT64_C(16) << 16) | // T1SZ=48-bit VA policy
                         (UINT64_C(3) << 12) | // SH0=inner shareable
                         (UINT64_C(1) << 10) | // ORGN0=WBWA
                         (UINT64_C(1) << 8) | // IRGN0=WBWA
                         UINT64_C(16); // T0SZ=48-bit VA policy
    const uint64_t root = gRoot;
    uint64_t sctlr = 0;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    // The tables are clean and the transition is ordered below, so establish
    // translation, data caching, instruction caching, and WXN together.
    sctlr |= kMmu | kCache | kInstructionCache | kWriteExecuteNever;

    __asm__ volatile(
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "msr ttbr1_el1, xzr\n"
        "dsb sy\n"
        "isb\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n"
        "msr sctlr_el1, %3\n"
        "dsb sy\n"
        "isb\n"
        :
        : "r"(mair), "r"(tcr), "r"(root), "r"(sctlr)
        : "memory");
}

uint64_t phase2_mmu_tables_start() { return (uint64_t)(uintptr_t)__translation_tables_start; }
uint64_t phase2_mmu_tables_end() { return (uint64_t)(uintptr_t)__translation_tables_end; }
uint64_t phase2_mmu_root() { return gRoot; }

uint64_t phase2_mmu_read_mair() { uint64_t v; __asm__ volatile("mrs %0, mair_el1" : "=r"(v)); return v; }
uint64_t phase2_mmu_read_tcr() { uint64_t v; __asm__ volatile("mrs %0, tcr_el1" : "=r"(v)); return v; }
uint64_t phase2_mmu_read_ttbr0() { uint64_t v; __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v)); return v; }
uint64_t phase2_mmu_read_sctlr() { uint64_t v; __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v)); return v; }

uint64_t phase2_mmu_descriptor_for(uint64_t virtual_address)
{
    if (gRoot == 0 || (virtual_address >> 39) != 0) return 0;
    uint64_t* l0 = (uint64_t*)(uintptr_t)gRoot;
    if ((l0[0] & 3) != 3) return 0;
    uint64_t* l1 = (uint64_t*)(uintptr_t)(l0[0] & ~kPageMask);
    const uint32_t l1Index = (virtual_address >> 30) & 0x1ff;
    if ((l1[l1Index] & 3) != 3) return 0;
    uint64_t* l2 = (uint64_t*)(uintptr_t)(l1[l1Index] & ~kPageMask);
    const uint32_t l2Index = (virtual_address >> 21) & 0x1ff;
    if ((l2[l2Index] & 3) != 3) return 0;
    uint64_t* l3 = (uint64_t*)(uintptr_t)(l2[l2Index] & ~kPageMask);
    return l3[(virtual_address >> 12) & 0x1ff];
}
