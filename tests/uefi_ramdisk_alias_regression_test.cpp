#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../guideXOSBootLoader/guidexOSBootInfo.h"
#include "../guideXOSBootLoader/paging.h"

EFI_SYSTEM_TABLE* gST = nullptr;

namespace {

static const uint64_t kPageSize = 4096u;
static const uint64_t kPageMask = kPageSize - 1u;
static const uint64_t kPresent = 1ull << 0;
static const uint64_t kWritable = 1ull << 1;
static const uint64_t kUser = 1ull << 2;
static const uint64_t kLargePage = 1ull << 7;
static const uint64_t kNoExecute = 1ull << 63;
static const uint64_t kPhysicalMask = 0x000FFFFFFFFFF000ull;
static const uint64_t kKernelVirtualBase = 0x00100000ull;
static const uint64_t kKernelPhysicalBase = 0x190D9000ull;
static const uint64_t kKernelSpanBytes = 0x22A92A60ull;
static const uint64_t kRamdiskPhysicalBase = 0x150D9000ull;
static const uint64_t kRamdiskLength = 0x04000000ull + 123u;
static const uint64_t kRamdiskAlias = guideXOS::GUIDEXOS_RAMDISK_ALIAS_BASE;
static const uint64_t kIdentityBytes = 0x3C000000ull;
static const size_t kPageTableArenaBytes = 8u * 1024u * 1024u;

alignas(4096) static uint8_t g_pageTableArena[kPageTableArenaBytes];
static size_t g_pageTableArenaUsed = 0;
static size_t g_pageTableAllocationCount = 0;

static EFI_STATUS EFIAPI fake_allocate_pages(EFI_ALLOCATE_TYPE type,
                                              EFI_MEMORY_TYPE memoryType,
                                              UINTN pages,
                                              EFI_PHYSICAL_ADDRESS* memory)
{
    if (type != AllocateAnyPages || memoryType != EfiLoaderData || pages != 1 ||
        !memory || g_pageTableArenaUsed > kPageTableArenaBytes - kPageSize) {
        return EFI_OUT_OF_RESOURCES;
    }

    uint8_t* page = g_pageTableArena + g_pageTableArenaUsed;
    g_pageTableArenaUsed += kPageSize;
    ++g_pageTableAllocationCount;
    std::memset(page, 0, kPageSize);
    *memory = static_cast<EFI_PHYSICAL_ADDRESS>(
        reinterpret_cast<uintptr_t>(page));
    return EFI_SUCCESS;
}

static bool walk_translation(uint64_t rootPhysical,
                             uint64_t virtualAddress,
                             uint64_t* outPhysical,
                             uint64_t* outLeaf,
                             bool* outEffectiveUser)
{
    if (!outPhysical || !outLeaf || !outEffectiveUser || rootPhysical == 0 ||
        (rootPhysical & kPageMask) != 0) return false;

    const uint64_t indices[4] = {
        (virtualAddress >> 39) & 0x1FFu,
        (virtualAddress >> 30) & 0x1FFu,
        (virtualAddress >> 21) & 0x1FFu,
        (virtualAddress >> 12) & 0x1FFu
    };
    const volatile uint64_t* table = reinterpret_cast<const volatile uint64_t*>(
        static_cast<uintptr_t>(rootPhysical));
    bool effectiveUser = false;

    for (uint32_t level = 0; level < 4; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & kPresent) == 0) return false;
        effectiveUser = effectiveUser || ((entry & kUser) != 0);
        if (level < 3) {
            if ((entry & kLargePage) != 0) return false;
            table = reinterpret_cast<const volatile uint64_t*>(
                static_cast<uintptr_t>(entry & kPhysicalMask));
        } else {
            *outPhysical = (entry & kPhysicalMask) |
                           (virtualAddress & kPageMask);
            *outLeaf = entry;
            *outEffectiveUser = effectiveUser;
        }
    }
    return true;
}

static bool verify_translation(uint64_t root,
                               uint64_t virtualAddress,
                               uint64_t expectedPhysical,
                               bool expectWritable,
                               bool expectExecutable,
                               const char* description)
{
    uint64_t physical = 0;
    uint64_t leaf = 0;
    bool user = false;
    const bool valid = walk_translation(root, virtualAddress, &physical, &leaf, &user) &&
        physical == expectedPhysical && !user &&
        (((leaf & kWritable) != 0) == expectWritable) &&
        (((leaf & kNoExecute) == 0) == expectExecutable);
    if (!valid) {
        std::printf("FAIL %s: VA=0x%llx PA=0x%llx leaf=0x%llx user=%u\n",
                    description,
                    static_cast<unsigned long long>(virtualAddress),
                    static_cast<unsigned long long>(physical),
                    static_cast<unsigned long long>(leaf), user ? 1u : 0u);
    }
    return valid;
}

} // namespace

int main()
{
    const uint64_t arenaBase = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(g_pageTableArena));
    if ((arenaBase & kPageMask) != 0 || arenaBase >= (1ull << 47) ||
        kPageTableArenaBytes > (1ull << 47) - arenaBase) {
        std::fprintf(stderr, "FAIL hosted page-table arena is outside the identity-map range\n");
        return 2;
    }

    EFI_BOOT_SERVICES bootServices{};
    bootServices.AllocatePages = &fake_allocate_pages;
    EFI_SYSTEM_TABLE systemTable{};
    systemTable.BootServices = &bootServices;

    const EFI_PHYSICAL_ADDRESS ranges[] = {
        0,
        static_cast<EFI_PHYSICAL_ADDRESS>(arenaBase)
    };
    const UINTN sizes[] = {
        static_cast<UINTN>(kIdentityBytes),
        static_cast<UINTN>(kPageTableArenaBytes)
    };
    guideXOS::paging::PageTables pageTables{};
    EFI_STATUS status = guideXOS::paging::BuildIdentityPageTables(
        &systemTable, ranges, ranges + 2, sizes, &pageTables);
    if (EFI_ERROR(status)) {
        std::printf("FAIL identity map status=0x%llx tablePages=%zu\n",
                    static_cast<unsigned long long>(status),
                    g_pageTableAllocationCount);
        return 1;
    }

    status = guideXOS::paging::MapRange(
        &systemTable, pageTables.Pml4Phys, kKernelVirtualBase,
        kKernelPhysicalBase, static_cast<UINTN>(kKernelSpanBytes), true);
    if (EFI_ERROR(status)) {
        std::printf("FAIL kernel image map status=0x%llx tablePages=%zu\n",
                    static_cast<unsigned long long>(status),
                    g_pageTableAllocationCount);
        return 1;
    }

    uint64_t physical = 0;
    uint64_t leaf = 0;
    bool user = false;
    if (!walk_translation(pageTables.Pml4Phys, kRamdiskPhysicalBase,
                          &physical, &leaf, &user) ||
        physical == kRamdiskPhysicalBase) {
        std::fprintf(stderr, "FAIL setup did not create the low-VA RAMDISK collision\n");
        return 1;
    }

    status = guideXOS::paging::MapRange(
        &systemTable, pageTables.Pml4Phys, kRamdiskAlias,
        kRamdiskPhysicalBase, static_cast<UINTN>(kRamdiskLength));
    if (EFI_ERROR(status)) {
        std::printf("FAIL RAMDISK alias map status=0x%llx tablePages=%zu\n",
                    static_cast<unsigned long long>(status),
                    g_pageTableAllocationCount);
        return 1;
    }

    const uint64_t pageCount = (kRamdiskLength + kPageMask) / kPageSize;
    for (uint64_t index = 0; index < pageCount; ++index) {
        if (!verify_translation(pageTables.Pml4Phys,
                               kRamdiskAlias + index * kPageSize,
                               kRamdiskPhysicalBase + index * kPageSize,
                               true, true, "RAMDISK page alias")) return 1;
    }
    if (!verify_translation(pageTables.Pml4Phys,
                            kRamdiskAlias + kRamdiskLength - 1,
                            kRamdiskPhysicalBase + kRamdiskLength - 1,
                            true, true, "non-aligned RAMDISK tail")) return 1;
    if (!verify_translation(pageTables.Pml4Phys, kKernelVirtualBase,
                            kKernelPhysicalBase, true, true,
                            "kernel image first byte") ||
        !verify_translation(pageTables.Pml4Phys,
                            kKernelVirtualBase + kKernelSpanBytes - 1,
                            kKernelPhysicalBase + kKernelSpanBytes - 1,
                            true, true, "kernel image last byte")) return 1;

    status = guideXOS::paging::IdentityMapPageTablePages(
        &systemTable, pageTables.Pml4Phys);
    if (EFI_ERROR(status)) {
        std::printf("FAIL page-table identity map status=0x%llx tablePages=%zu\n",
                    static_cast<unsigned long long>(status),
                    g_pageTableAllocationCount);
        return 1;
    }
    for (size_t index = 0; index < g_pageTableAllocationCount; ++index) {
        const uint64_t tableAddress = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(g_pageTableArena + index * kPageSize));
        if (!verify_translation(pageTables.Pml4Phys, tableAddress, tableAddress,
                                true, true, "loader page-table self-map")) return 1;
    }

    if (g_pageTableAllocationCount >
        guideXOS::GUIDEXOS_MAX_BOOT_PAGE_TABLE_PAGES) {
        std::printf("FAIL page-table tree uses %zu pages beyond shared boot bound %u\n",
                    g_pageTableAllocationCount,
                    guideXOS::GUIDEXOS_MAX_BOOT_PAGE_TABLE_PAGES);
        return 1;
    }

    std::printf("PASS RAMDISK alias maps %llu pages with supervisor identity-mapped page tables (tablePages=%zu)\n",
                static_cast<unsigned long long>(pageCount),
                g_pageTableAllocationCount);
    return 0;
}
