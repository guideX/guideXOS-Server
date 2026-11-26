#include "../kernel/core/include/kernel/ramdisk_integrity_diagnostic.h"

#include <stdint.h>
#include <iostream>

namespace {

using guideXOS::ramdisk_diagnostic::Mapping;
using guideXOS::ramdisk_diagnostic::MappingStatus;
using guideXOS::ramdisk_diagnostic::SectorZero;

struct TestTables {
    uint64_t pml4[512];
    uint64_t pdpt[512];
    uint64_t pd[512];
    uint64_t pt[512];
};

static bool expect(bool condition, const char* label)
{
    std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
    return condition;
}

static const volatile uint64_t* read_test_table(void* context, uint64_t physical)
{
    TestTables* tables = static_cast<TestTables*>(context);
    switch (physical) {
    case 0x1000u: return tables->pml4;
    case 0x2000u: return tables->pdpt;
    case 0x3000u: return tables->pd;
    case 0x4000u: return tables->pt;
    default: return nullptr;
    }
}

} // namespace

int main()
{
    using namespace guideXOS::ramdisk_diagnostic;
    bool ok = true;

    uint8_t sector[kSectorBytes] = {};
    sector[11] = 0x00;
    sector[12] = 0x02;
    sector[13] = 16;
    sector[14] = 32;
    sector[16] = 2;
    sector[510] = 0x55;
    sector[511] = 0xAA;
    SectorZero observed{};
    ok &= expect(observe_sector_zero(sector, &observed),
                 "sector-zero observation accepts bounded 512-byte input");
    ok &= expect(observed.fnv1a64 == 0x0E1330C7AD21B1E6ull,
                 "fingerprint matches the FNV-1a64 512-byte test vector");
    ok &= expect(observed.signature == 0xAA55u &&
                 observed.bytesPerSector == 512u &&
                 observed.sectorsPerCluster == 16u &&
                 observed.reservedSectors == 32u && observed.fatCount == 2u,
                 "bounded BPB fields decode little-endian sector-zero offsets");
    ok &= expect(!observe_sector_zero(nullptr, &observed) &&
                 !observe_sector_zero(sector, nullptr),
                 "sector-zero observation rejects null inputs");

    TestTables tables{};
    const uint64_t virtualAddress = 0x150DB000ull;
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1FFu;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1FFu;
    const uint64_t pdIndex = (virtualAddress >> 21) & 0x1FFu;
    const uint64_t ptIndex = (virtualAddress >> 12) & 0x1FFu;
    tables.pml4[pml4Index] = 0x2003u;
    tables.pdpt[pdptIndex] = 0x3003u;
    tables.pd[pdIndex] = 0x4003u;
    tables.pt[ptIndex] = virtualAddress | 3u;

    Mapping mapping{};
    MappingStatus status = translate(0x1000u, virtualAddress,
                                    &read_test_table, &tables, &mapping);
    ok &= expect(status == MappingStatus::Mapped4K &&
                 mapping.physicalAddress == virtualAddress &&
                 mapping.writable && !mapping.user && mapping.executable,
                 "four-level walk resolves an identity 4 KiB supervisor mapping");

    tables.pt[ptIndex] = 0x250DB000ull | 3u;
    status = translate(0x1000u, virtualAddress,
                       &read_test_table, &tables, &mapping);
    ok &= expect(status == MappingStatus::Mapped4K &&
                 mapping.physicalAddress == 0x250DB000ull,
                 "walk reports a different physical backing without dereferencing it");

    const uint64_t ramdiskPhysicalBase = 0x150DB000ull;
    const uint64_t kernelVirtualBase = 0x00100000ull;
    const uint64_t kernelPhysicalBase = 0x190DB000ull;
    const uint64_t kernelImageBytes = 0x22A90A60ull;
    const uint64_t legacyKernelBacking = kernelPhysicalBase +
        (ramdiskPhysicalBase - kernelVirtualBase);
    TestTables legacyTables{};
    const uint64_t legacyPml4Index = (ramdiskPhysicalBase >> 39) & 0x1FFu;
    const uint64_t legacyPdptIndex = (ramdiskPhysicalBase >> 30) & 0x1FFu;
    const uint64_t legacyPdIndex = (ramdiskPhysicalBase >> 21) & 0x1FFu;
    const uint64_t legacyPtIndex = (ramdiskPhysicalBase >> 12) & 0x1FFu;
    legacyTables.pml4[legacyPml4Index] = 0x2003u;
    legacyTables.pdpt[legacyPdptIndex] = 0x3003u;
    legacyTables.pd[legacyPdIndex] = 0x4003u;
    legacyTables.pt[legacyPtIndex] = legacyKernelBacking | 3u;
    status = translate(0x1000u, ramdiskPhysicalBase,
                       &read_test_table, &legacyTables, &mapping);
    uint8_t zeroFilledKernelBssSector[kSectorBytes] = {};
    SectorZero legacyObserved{};
    ok &= expect(ramdiskPhysicalBase >= kernelVirtualBase &&
                 ramdiskPhysicalBase - kernelVirtualBase < kernelImageBytes &&
                 legacyKernelBacking == 0x2E0B6000ull &&
                 status == MappingStatus::Mapped4K &&
                 mapping.physicalAddress == legacyKernelBacking &&
                 legacyKernelBacking != ramdiskPhysicalBase &&
                 observe_sector_zero(zeroFilledKernelBssSector, &legacyObserved) &&
                 legacyObserved.fnv1a64 == 0x7DA144B97D054B25ull,
                 "R1 alias collision resolves RAMDISK VA into zeroed kernel BSS and reproduces guest FNV");

    const uint64_t r2RamdiskPhysicalBase = 0x150D9000ull;
    const uint64_t r2KernelPhysicalBase = 0x190D9000ull;
    const uint64_t r2KernelImageBytes = 0x22A92A60ull;
    const uint64_t r2LegacyKernelBacking = r2KernelPhysicalBase +
        (r2RamdiskPhysicalBase - kernelVirtualBase);
    TestTables r2LegacyTables{};
    const uint64_t r2Pml4Index = (r2RamdiskPhysicalBase >> 39) & 0x1FFu;
    const uint64_t r2PdptIndex = (r2RamdiskPhysicalBase >> 30) & 0x1FFu;
    const uint64_t r2PdIndex = (r2RamdiskPhysicalBase >> 21) & 0x1FFu;
    const uint64_t r2PtIndex = (r2RamdiskPhysicalBase >> 12) & 0x1FFu;
    r2LegacyTables.pml4[r2Pml4Index] = 0x2003u;
    r2LegacyTables.pdpt[r2PdptIndex] = 0x3003u;
    r2LegacyTables.pd[r2PdIndex] = 0x4003u;
    r2LegacyTables.pt[r2PtIndex] = r2LegacyKernelBacking | 3u;
    status = translate(0x1000u, r2RamdiskPhysicalBase,
                       &read_test_table, &r2LegacyTables, &mapping);
    ok &= expect(r2RamdiskPhysicalBase >= kernelVirtualBase &&
                 r2RamdiskPhysicalBase - kernelVirtualBase < r2KernelImageBytes &&
                 r2LegacyKernelBacking == 0x2E0B2000ull &&
                 status == MappingStatus::Mapped4K &&
                 mapping.physicalAddress == r2LegacyKernelBacking &&
                 observe_sector_zero(zeroFilledKernelBssSector, &legacyObserved) &&
                 legacyObserved.fnv1a64 == 0x7DA144B97D054B25ull,
                 "R2 guest layout repeats the shadow mapping into zeroed kernel BSS");

    TestTables aliasTables{};
    const uint64_t ramdiskAlias = kKernelRamdiskVirtualBase;
    const uint64_t aliasPml4Index = (ramdiskAlias >> 39) & 0x1FFu;
    const uint64_t aliasPdptIndex = (ramdiskAlias >> 30) & 0x1FFu;
    const uint64_t aliasPdIndex = (ramdiskAlias >> 21) & 0x1FFu;
    const uint64_t aliasPtIndex = (ramdiskAlias >> 12) & 0x1FFu;
    aliasTables.pml4[aliasPml4Index] = 0x2003u;
    aliasTables.pdpt[aliasPdptIndex] = 0x3003u;
    aliasTables.pd[aliasPdIndex] = 0x4003u;
    aliasTables.pt[aliasPtIndex] = ramdiskPhysicalBase | 3u;
    aliasTables.pt[aliasPtIndex + 1u] =
        (ramdiskPhysicalBase + kPageBytes) | 3u;
    uint64_t aliasPagesVerified = 0u;
    uint64_t aliasFirstMismatch = 0u;
    ok &= expect(range_maps_to_physical(
                     0x1000u, ramdiskAlias, ramdiskPhysicalBase,
                     2u * kPageBytes, &read_test_table, &aliasTables,
                     &aliasPagesVerified, &aliasFirstMismatch) &&
                 aliasPagesVerified == 2u && aliasFirstMismatch == 0u,
                 "high virtual RAMDISK alias verifies both pages against intended physical frames");
    aliasTables.pt[aliasPtIndex + 1u] =
        (ramdiskPhysicalBase + 3u * kPageBytes) | 3u;
    aliasPagesVerified = 0u;
    aliasFirstMismatch = 0u;
    ok &= expect(!range_maps_to_physical(
                     0x1000u, ramdiskAlias, ramdiskPhysicalBase,
                     2u * kPageBytes, &read_test_table, &aliasTables,
                     &aliasPagesVerified, &aliasFirstMismatch) &&
                 aliasPagesVerified == 2u &&
                 aliasFirstMismatch == ramdiskAlias + kPageBytes,
                 "virtual-alias range check reports the first frame with wrong physical backing");

    tables.pd[pdIndex] = (virtualAddress & ~((1ull << 21) - 1u)) | 0x83u;
    status = translate(0x1000u, virtualAddress,
                       &read_test_table, &tables, &mapping);
    ok &= expect(status == MappingStatus::Mapped2M &&
                 mapping.physicalAddress == virtualAddress &&
                 mapping.pageShift == 21u,
                 "walk resolves a 2 MiB leaf and preserves the offset");

    tables.pd[pdIndex] = 0u;
    status = translate(0x1000u, virtualAddress,
                       &read_test_table, &tables, &mapping);
    ok &= expect(status == MappingStatus::NotPresent,
                 "walk reports an absent mapping without following it");

    tables.pd[pdIndex] = 0x7003u;
    status = translate(0x1000u, virtualAddress,
                       &read_test_table, &tables, &mapping);
    ok &= expect(status == MappingStatus::PageTableUnavailable,
                 "walk reports an unreadable page-table page");

    status = translate(0x1001u, virtualAddress,
                       &read_test_table, &tables, &mapping);
    ok &= expect(status == MappingStatus::Invalid,
                 "walk rejects an unaligned CR3 root");

    tables.pd[pdIndex] = 0x4003u;
    tables.pt[ptIndex] = virtualAddress | 3u;
    tables.pt[(ptIndex + 1u) & 0x1FFu] =
        ((virtualAddress + kPageBytes) & kAddressMask) | 3u;
    uint64_t pagesVerified = 0u;
    uint64_t firstMismatch = 0u;
    ok &= expect(identity_range_is_mapped(0x1000u, virtualAddress,
                                         2u * kPageBytes, &read_test_table,
                                         &tables, &pagesVerified, &firstMismatch) &&
                 pagesVerified == 2u && firstMismatch == 0u,
                 "range walk verifies every page of an identity RAMDISK mapping");

    tables.pt[(ptIndex + 1u) & 0x1FFu] =
        ((virtualAddress + 2u * kPageBytes) & kAddressMask) | 3u;
    pagesVerified = 0u;
    firstMismatch = 0u;
    ok &= expect(!identity_range_is_mapped(0x1000u, virtualAddress,
                                          2u * kPageBytes, &read_test_table,
                                          &tables, &pagesVerified, &firstMismatch) &&
                 pagesVerified == 2u &&
                 firstMismatch == virtualAddress + kPageBytes,
                 "range walk identifies the first page with different backing");

    pagesVerified = 0u;
    firstMismatch = 0u;
    ok &= expect(!identity_range_is_mapped(0x1000u, virtualAddress,
                                          UINT64_MAX, &read_test_table,
                                          &tables, &pagesVerified, &firstMismatch) &&
                 pagesVerified == 0u && firstMismatch == virtualAddress,
                 "range walk rejects overflowing RAMDISK extents safely");

    std::cout << (ok ? "RAMDISK integrity diagnostic helper tests PASS\n"
                     : "RAMDISK integrity diagnostic helper tests FAIL\n");
    return ok ? 0 : 1;
}
