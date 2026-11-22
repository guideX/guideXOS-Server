#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../guideXOSBootLoader/paging.h"
#include "../kernel/core/include/kernel/amd64_boot_page_tables.h"

EFI_SYSTEM_TABLE* gST = nullptr;

namespace {

using namespace guideXOS::amd64_boot_paging;
using guideXOS::paging::IdentityRange;
using guideXOS::paging::PageTables;
using kernel::memory::BootPageTableStatus;
using kernel::memory::check_boot_page_tables;

constexpr uint64_t kLeafCoverage = 2ull * 1024 * 1024;
constexpr uint64_t kPdCoverage = 512 * kLeafCoverage;
constexpr uint64_t kPresentWritable = 3;
alignas(4096) uint64_t storage[5 * kLeafCoverage / sizeof(uint64_t)];

struct Firmware {
    uint64_t base;
    UINTN active = 0;
    UINTN attempts = 0;
    UINTN failAt = UINT64_MAX;
    bool refuseFree = false;
    EFI_BOOT_SERVICES services{};
};

Firmware firmware{};

EFI_STATUS allocate_pages(EFI_ALLOCATE_TYPE type, EFI_MEMORY_TYPE memoryType,
                          UINTN pages, EFI_PHYSICAL_ADDRESS* address)
{
    assert(type == AllocateMaxAddress && memoryType == EfiLoaderData && pages == 1);
    if (firmware.attempts++ == firmware.failAt) return EFI_OUT_OF_RESOURCES;
    assert(firmware.active < kMaxTablePages);
    const uint64_t base = firmware.base + firmware.active * kPageBytes;
    assert(base + kPageMask <= *address);
    *address = base;
    ++firmware.active;
    return EFI_SUCCESS;
}

EFI_STATUS free_pages(EFI_PHYSICAL_ADDRESS address, UINTN pages)
{
    assert(pages == 1 && firmware.active > 0);
    assert(address == firmware.base + (firmware.active - 1) * kPageBytes);
    if (firmware.refuseFree) return EFI_DEVICE_ERROR;
    --firmware.active;
    return EFI_SUCCESS;
}

void* map_firmware_page(void*, uint64_t address)
{
    if (address < firmware.base ||
        address >= firmware.base + firmware.active * kPageBytes) return nullptr;
    return reinterpret_cast<void*>(address);
}

uint64_t* table_at(uint64_t physical)
{
    auto* table = static_cast<uint64_t*>(map_firmware_page(nullptr, physical));
    assert(table);
    return table;
}

void expect_mapping(const PageTables& tables, uint64_t virtualAddress, uint64_t physicalAddress)
{
    uint64_t table = tables.Root();
    for (unsigned shift : {39u, 30u, 21u, 12u}) {
        const uint64_t entry = table_at(table)[(virtualAddress >> shift) & 0x1FFu];
        // At all levels require exactly present, writable, supervisor-only,
        // executable, with no global, large-page, or cache overrides.
        assert((entry & ~kAddressMask) == kPresentWritable);
        table = entry & kAddressMask;
    }
    assert(table == (physicalAddress & ~kPageMask));
}

void verify_boot_tree(const PageTables& tables)
{
    uint32_t walked = 0;
    assert(check_boot_page_tables(tables.Root(), 0x80000000, 0x81000000,
               &map_firmware_page, nullptr, walked) == BootPageTableStatus::Ok);
    assert(walked == tables.Count() && walked == firmware.active);
    for (UINTN i = 0; i < walked; ++i) {
        const uint64_t physical = firmware.base + i * kPageBytes;
        expect_mapping(tables, physical, physical);
    }
}

void release(PageTables& tables)
{
    assert(tables.Release(&firmware.services) == EFI_SUCCESS);
    assert(tables.Root() == 0 && tables.Count() == 0 && firmware.active == 0);
}

void test_table_budgets(PageTables& tables, uint64_t alias)
{
    // Reserve both self-map leaf windows up front to make exact counts
    // independent of the number of allocations they eventually contain.
    const IdentityRange window{firmware.base, 2 * kLeafCoverage};
    for (const UINTN expected : {511u, 512u, 513u, 1024u}) {
        const UINTN controlPages = expected == 1024 ? 8 : 7;
        const UINTN leafPages = expected - controlPages;
        const UINTN bytes = (leafPages - 1) * kLeafCoverage + kPageBytes;
        assert(tables.Build(&firmware.services, &window, 1, alias, 0x100000, bytes) == EFI_SUCCESS);
        assert(tables.Count() == expected);
        verify_boot_tree(tables);

        if (expected == kMaxTablePages) {
            auto* root = table_at(tables.Root());
            auto* pdpt = table_at(root[(alias >> 39) & 0x1FFu] & kAddressMask);
            auto* pd = table_at(pdpt[1] & kAddressMask);
            const UINTN nextLeaf = leafPages - 512;
            assert(pd[nextLeaf] == 0);
            // This additional present reference would exceed the kernel budget.
            pd[nextLeaf] = pd[nextLeaf - 1];
            uint32_t count = 0;
            assert(check_boot_page_tables(tables.Root(), 0, 0, &map_firmware_page,
                       nullptr, count) == BootPageTableStatus::CapacityExceeded);
            assert(count == kMaxTablePages);
            pd[nextLeaf] = 0;
        }
        assert(tables.Build(&firmware.services, nullptr, 0, alias, 0x100000, 1) ==
               EFI_ALREADY_STARTED);
        release(tables);
        std::printf("PASS exact loader and kernel table budget: %llu pages\n",
                    static_cast<unsigned long long>(expected));
    }

    const UINTN overBudgetBytes = (kMaxTablePages - 8) * kLeafCoverage + kPageBytes;
    firmware.attempts = 0;
    assert(tables.Build(&firmware.services, &window, 1, alias, 0x100000,
                        overBudgetBytes) == EFI_OUT_OF_RESOURCES);
    assert(firmware.attempts == kMaxTablePages && firmware.active == 0 && tables.Root() == 0);
    std::puts("PASS loader capacity rejects page 1025 before firmware allocation");
}

void test_cpu3r1_mapping(PageTables& tables)
{
    constexpr uint64_t virtualBase = 0x100000;
    constexpr uint64_t physicalBase = 0x594CE000;
    constexpr UINTN bytes = 0x22A8EA60;
    const IdentityRange ranges[] = {
        {physicalBase, bytes},
        {0x554CE000, 64u * 1024u * 1024u}, // RAM disk
        {0x7D1B7000, 16u * 1024u * 1024u}, // Task arena
        {0x4000000, 64u * 1024u * 1024u, true}, // Legacy heap window
        {0x100000, 2u * 1024u * 1024u, true}, // Legacy low window
        {0, 0x100000, true},
    };
    const auto build = [&] {
        return tables.Build(&firmware.services, ranges, sizeof(ranges) / sizeof(ranges[0]),
                            virtualBase, physicalBase, bytes);
    };
    assert(build() == EFI_SUCCESS);
    assert(tables.Count() > 556 && tables.Count() <= kMaxTablePages);
    verify_boot_tree(tables);
    expect_mapping(tables, virtualBase, physicalBase);
    expect_mapping(tables, virtualBase + bytes - 1, physicalBase + bytes - 1);
    expect_mapping(tables, physicalBase, physicalBase);
    expect_mapping(tables, physicalBase + bytes - 1, physicalBase + bytes - 1);
    expect_mapping(tables, 0x4000000, physicalBase + 0x4000000 - virtualBase);
    expect_mapping(tables, 0x7D1B7000, 0x7D1B7000);
    const UINTN fullCount = tables.Count();
    uint32_t walked = 0;
    assert(check_boot_page_tables(tables.Root(), firmware.base, firmware.base + kPageBytes,
               &map_firmware_page, nullptr, walked) == BootPageTableStatus::ArenaOverlap);
    release(tables);

    // Inject failures in the root, during both aliases, and while closing
    // the self-map; every partial allocation must be reclaimed.
    for (const UINTN failAt : {UINTN{0}, UINTN{1}, UINTN{260}, UINTN{512}, fullCount - 1}) {
        firmware.attempts = 0;
        firmware.failAt = failAt;
        assert(build() == EFI_OUT_OF_RESOURCES);
        assert(firmware.active == 0 && tables.Root() == 0 && tables.Count() == 0);
    }
    firmware.attempts = 0;
    firmware.failAt = 3;
    firmware.refuseFree = true;
    assert(build() == EFI_DEVICE_ERROR);
    assert(tables.Root() == 0 && tables.Count() == 3);
    assert(build() == EFI_ALREADY_STARTED);
    firmware.refuseFree = false;
    firmware.failAt = UINT64_MAX;
    release(tables);
    assert(build() == EFI_SUCCESS);
    assert(tables.Count() == fullCount);
    release(tables);
    std::printf("PASS CPU3R1 aliases and boot ranges (%llu pages), self-map, and failure cleanup\n",
                static_cast<unsigned long long>(fullCount));
}

void test_range_checks(PageTables& tables, uint64_t alias)
{
    auto build = [&](uint64_t virtualBase, uint64_t physicalBase, UINTN bytes) {
        return tables.Build(&firmware.services, nullptr, 0, virtualBase, physicalBase, bytes);
    };
    assert(build(UINT64_MAX - 8, 0x200FF7, 10) == EFI_INVALID_PARAMETER);
    assert(build(kIdentityLimit - 8, 0x200FF8, 9) == EFI_INVALID_PARAMETER);
    assert(build(alias + 0xFF8, kIdentityLimit - 8, 9) == EFI_INVALID_PARAMETER);
    assert(build(alias + 1, 0x200000, 1) == EFI_INVALID_PARAMETER);
    assert(build(kIdentityLimit, 0x200000, 1) == EFI_INVALID_PARAMETER);
    assert(tables.Root() == 0 && firmware.active == 0);

    assert(build(alias + 0xFFF, 0x200FFF, 2) == EFI_SUCCESS);
    expect_mapping(tables, alias + 0xFFF, 0x200FFF);
    expect_mapping(tables, alias + 0x1000, 0x201000);
    release(tables);
    assert(build(UINT64_MAX, kIdentityLimit - 1, 1) == EFI_SUCCESS);
    expect_mapping(tables, UINT64_MAX, kIdentityLimit - 1);
    release(tables);

    const IdentityRange required{alias, kPageBytes};
    assert(tables.Build(&firmware.services, &required, 1, alias, 0x200000, kPageBytes) ==
           EFI_NO_MAPPING);
    assert(tables.Root() == 0 && firmware.active == 0);
    assert(build(firmware.base, 0x200000, kPageBytes) == EFI_NO_MAPPING);
    assert(tables.Root() == 0 && firmware.active == 0);

    uint32_t count = 0;
    assert(check_boot_page_tables(0, 0, 0, &map_firmware_page, nullptr, count) ==
           BootPageTableStatus::Invalid);
    std::puts("PASS checked canonical ranges, alignment, permissions, and alias conflicts");
}

}

int main()
{
    firmware.base = (reinterpret_cast<uint64_t>(storage) + kLeafCoverage - 1) &
                    ~(kLeafCoverage - 1);
    if ((firmware.base & (kPdCoverage - 1)) == kPdCoverage - kLeafCoverage) {
        firmware.base += kLeafCoverage;
    }
    assert(firmware.base + 2 * kLeafCoverage <=
           reinterpret_cast<uint64_t>(storage) + sizeof(storage));
    const uint64_t alias = firmware.base < (1ull << 46) ? 1ull << 46 : 1ull << 45;
    firmware.services.AllocatePages = allocate_pages;
    firmware.services.FreePages = free_pages;
    PageTables tables;
    test_table_budgets(tables, alias);
    test_cpu3r1_mapping(tables);
    test_range_checks(tables, alias);
}
