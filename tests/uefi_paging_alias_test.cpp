#include <cstdio>
#include <cstdint>
#include <cstring>

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
static const uint64_t kRamdiskAlias = 0x0000400000000000ull;
static const uint64_t kIdentityBytes = 0x3C000000ull; // 960 MiB low identity map
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
        !memory ||
        g_pageTableArenaUsed > kPageTableArenaBytes - kPageSize) {
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
    if (!outPhysical || !outLeaf || !outEffectiveUser ||
        rootPhysical == 0 || (rootPhysical & kPageMask) != 0) {
        return false;
    }

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

static void reset_arena()
{
    std::memset(g_pageTableArena, 0, kPageTableArenaBytes);
    g_pageTableArenaUsed = 0;
    g_pageTableAllocationCount = 0;
}

static bool run_alias_case(bool expectLegacyExhaustion)
{
    reset_arena();

    EFI_BOOT_SERVICES bootServices{};
    bootServices.AllocatePages = &fake_allocate_pages;
    EFI_SYSTEM_TABLE systemTable{};
    systemTable.BootServices = &bootServices;

    const EFI_PHYSICAL_ADDRESS ranges[] = {
        0,
        static_cast<EFI_PHYSICAL_ADDRESS>(
            reinterpret_cast<uintptr_t>(g_pageTableArena))
    };
    const UINTN sizes[] = {
        static_cast<UINTN>(kIdentityBytes),
        static_cast<UINTN>(kPageTableArenaBytes)
    };
    guideXOS::paging::PageTables pageTables{};
    EFI_STATUS status = guideXOS::paging::BuildIdentityPageTables(
        &systemTable, ranges, ranges + 2, sizes, &pageTables);
    if (EFI_ERROR(status)) {
        std::printf("FAIL identity-map build status=0x%llx pages=%zu\n",
                    static_cast<unsigned long long>(status),
                    g_pageTableAllocationCount);
        return false;
    }

    status = guideXOS::paging::MapRange(
        &systemTable, pageTables.Pml4Phys, kKernelVirtualBase,
        kKernelPhysicalBase, static_cast<UINTN>(kKernelSpanBytes));
    if (EFI_ERROR(status)) {
        std::printf("FAIL kernel image map status=0x%llx pages=%zu\n",
                    static_cast<unsigned long long>(status),
                    g_pageTableAllocationCount);
        return false;
    }

    uint64_t physical = 0;
    uint64_t leaf = 0;
    bool user = false;
    if (!walk_translation(pageTables.Pml4Phys, kRamdiskPhysicalBase,
                          &physical, &leaf, &user) ||
        physical == kRamdiskPhysicalBase) {
        std::printf("FAIL regression setup did not reproduce the low-VA collision\n");
        return false;
    }

    status = guideXOS::paging::MapRange(
        &systemTable, pageTables.Pml4Phys, kRamdiskAlias,
        kRamdiskPhysicalBase, static_cast<UINTN>(kRamdiskLength));
    if (expectLegacyExhaustion) {
        if (status != EFI_OUT_OF_RESOURCES || g_pageTableAllocationCount != 512u) {
            std::printf("FAIL expected legacy page-table cap exhaustion; status=0x%llx pages=%zu\n",
                        static_cast<unsigned long long>(status),
                        g_pageTableAllocationCount);
            return false;
        }
        std::printf("PASS legacy candidate reproduces alias failure at the 512 tracked-page limit\n");
        return true;
    }

    if (EFI_ERROR(status)) {
        std::printf("FAIL corrected RAMDISK alias map status=0x%llx pages=%zu\n",
                    static_cast<unsigned long long>(status),
                    g_pageTableAllocationCount);
        return false;
    }

    const uint64_t pageCount =
        (kRamdiskLength + kPageMask) / kPageSize;
    for (uint64_t index = 0; index < pageCount; ++index) {
        const uint64_t address = kRamdiskAlias + index * kPageSize;
        if (!walk_translation(pageTables.Pml4Phys, address,
                              &physical, &leaf, &user) ||
            physical != kRamdiskPhysicalBase + index * kPageSize ||
            user || (leaf & kWritable) == 0 || (leaf & kNoExecute) != 0) {
            std::printf("FAIL alias page %llu translation or supervisor permissions\n",
                        static_cast<unsigned long long>(index));
            return false;
        }
    }

    const uint64_t lastByteVirtual = kRamdiskAlias + kRamdiskLength - 1u;
    if (!walk_translation(pageTables.Pml4Phys, lastByteVirtual,
                          &physical, &leaf, &user) ||
        physical != kRamdiskPhysicalBase + kRamdiskLength - 1u || user) {
        std::printf("FAIL non-page-aligned final RAMDISK byte is not mapped to backing\n");
        return false;
    }

    if (!walk_translation(pageTables.Pml4Phys, kKernelVirtualBase,
                          &physical, &leaf, &user) ||
        physical != kKernelPhysicalBase || user ||
        !walk_translation(pageTables.Pml4Phys,
                          kKernelVirtualBase + kKernelSpanBytes - 1u,
                          &physical, &leaf, &user) ||
        physical != kKernelPhysicalBase + kKernelSpanBytes - 1u || user) {
        std::printf("FAIL original low kernel mappings changed while adding alias\n");
        return false;
    }

    status = guideXOS::paging::IdentityMapPageTablePages(
        &systemTable, pageTables.Pml4Phys);
    if (EFI_ERROR(status)) {
        std::printf("FAIL page-table self-mapping status=0x%llx\n",
                    static_cast<unsigned long long>(status));
        return false;
    }
    for (size_t index = 0; index < g_pageTableAllocationCount; ++index) {
        const uint64_t tableAddress = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(g_pageTableArena + index * kPageSize));
        if (!walk_translation(pageTables.Pml4Phys, tableAddress,
                              &physical, &leaf, &user) ||
            physical != tableAddress || user) {
            std::printf("FAIL loader page-table allocation %zu is not identity mapped\n",
                        index);
            return false;
        }
    }

    std::printf("PASS corrected alias maps %llu pages including non-page-aligned tail; "
                "kernel and tracked supervisor page-table mappings remain valid (tables=%zu)\n",
                static_cast<unsigned long long>(pageCount),
                g_pageTableAllocationCount);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    const bool expectLegacyExhaustion =
        argc == 2 && std::strcmp(argv[1], "--expect-legacy-exhaustion") == 0;
    if (argc > 2 || (argc == 2 && !expectLegacyExhaustion)) {
        std::fprintf(stderr, "usage: uefi_paging_alias_test [--expect-legacy-exhaustion]\n");
        return 64;
    }

    const uint64_t arenaBase = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(g_pageTableArena));
    if ((arenaBase & kPageMask) != 0 ||
        arenaBase >= (1ull << 47) ||
        kPageTableArenaBytes > (1ull << 47) - arenaBase) {
        std::fprintf(stderr, "FAIL hosted page-table arena is not a valid low physical range\n");
        return 2;
    }

    const bool passed = run_alias_case(expectLegacyExhaustion);
    return passed ? 0 : 1;
}
