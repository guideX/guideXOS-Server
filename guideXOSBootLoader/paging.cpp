#include "paging.h"
#include "uefi_shim.h"

namespace guideXOS {
namespace paging {

using namespace amd64_boot_paging;
static_assert(EFI_PAGE_SIZE == kPageBytes, "UEFI and AMD64 boot page sizes must match");

static constexpr UINT64 kPresent = 1ull << 0;
static constexpr UINT64 kWritable = 1ull << 1;
static constexpr UINT64 kLarge = 1ull << 7;

static UINT64* TableAt(EFI_PHYSICAL_ADDRESS physical)
{
    return reinterpret_cast<UINT64*>(static_cast<UINTN>(physical));
}

EFI_STATUS PageTables::Allocate(EFI_BOOT_SERVICES* services,
                                EFI_PHYSICAL_ADDRESS* physical)
{
    if (count_ == kMaxTablePages) return EFI_OUT_OF_RESOURCES;

    *physical = kIdentityLimit - 1u;
    const EFI_STATUS status = services->AllocatePages(
        AllocateMaxAddress, EfiLoaderData, 1, physical);
    if (EFI_ERROR(status)) return status;

    // Record successful firmware allocations before any further validation.
    pages_[count_++] = *physical;
    if (*physical == 0 || (*physical & kPageMask) != 0 ||
        *physical >= kIdentityLimit) return EFI_NO_MAPPING;

    SetMem(TableAt(*physical), EFI_PAGE_SIZE, 0);
    return EFI_SUCCESS;
}

EFI_STATUS PageTables::Release(EFI_BOOT_SERVICES* services)
{
    root_ = 0;
    while (count_ != 0) {
        const EFI_STATUS status = services->FreePages(pages_[count_ - 1], 1);
        if (EFI_ERROR(status)) return status;
        --count_;
    }
    return EFI_SUCCESS;
}

EFI_STATUS PageTables::Map(EFI_BOOT_SERVICES* services, UINT64 virtualBase,
                           EFI_PHYSICAL_ADDRESS physicalBase, UINTN sizeBytes,
                           bool allowKernelAlias)
{
    if (sizeBytes == 0) return EFI_SUCCESS;
    if (physicalBase >= kIdentityLimit ||
        sizeBytes > kIdentityLimit - physicalBase ||
        sizeBytes - 1 > UINT64_MAX - virtualBase ||
        ((virtualBase ^ physicalBase) & kPageMask) != 0) return EFI_INVALID_PARAMETER;

    const UINT64 lastByte = virtualBase + (sizeBytes - 1);
    const UINT64 half = virtualBase >> 47;
    if ((half != 0 && half != (UINT64_MAX >> 47)) ||
        (lastByte >> 47) != half) return EFI_INVALID_PARAMETER;

    // Use a page count so no exclusive end or rounded end can overflow,
    // including for the final page of the upper canonical half.
    UINT64 virtualPage = virtualBase & ~kPageMask;
    UINT64 physicalPage = physicalBase & ~kPageMask;
    UINT64 pagesLeft = (lastByte >> 12) - (virtualPage >> 12) + 1;
    while (pagesLeft != 0) {
        EFI_PHYSICAL_ADDRESS tablePhysical = root_;
        for (UINTN shift = 39; shift > 12; shift -= 9) {
            UINT64& entry = TableAt(tablePhysical)[(virtualPage >> shift) & 0x1FF];
            if ((entry & kPresent) == 0) {
                EFI_STATUS status = Allocate(services, &tablePhysical);
                if (EFI_ERROR(status)) return status;
                entry = tablePhysical | kPresent | kWritable;
            } else {
                if ((entry & kLarge) != 0) return EFI_UNSUPPORTED;
                tablePhysical = entry & kAddressMask;
            }
        }

        UINT64* leaf = TableAt(tablePhysical) + ((virtualPage >> 12) & 0x1FF);
        const UINTN entries = 512 - ((virtualPage >> 12) & 0x1FF);
        const UINTN batch = pagesLeft < entries ? pagesLeft : entries;
        for (UINTN i = 0; i < batch; ++i, physicalPage += kPageBytes) {
            const UINT64 desired = physicalPage | kPresent | kWritable;
            if ((leaf[i] & kPresent) == 0) {
                leaf[i] = desired;
            } else if (leaf[i] != desired && !allowKernelAlias) {
                return EFI_NO_MAPPING;
            }
        }
        pagesLeft -= batch;
        if (pagesLeft != 0) virtualPage += batch * kPageBytes;
    }
    return EFI_SUCCESS;
}

EFI_STATUS PageTables::BuildTree(EFI_BOOT_SERVICES* services, const IdentityRange* ranges,
                                 UINTN rangeCount, UINT64 kernelVirtualBase,
                                 EFI_PHYSICAL_ADDRESS kernelPhysicalBase, UINTN kernelBytes)
{
    EFI_STATUS status = Allocate(services, &root_);
    if (EFI_ERROR(status)) return status;

    status = Map(services, kernelVirtualBase, kernelPhysicalBase, kernelBytes);
    if (EFI_ERROR(status)) return status;

    for (UINTN i = 0; i < rangeCount; ++i) {
        const IdentityRange& range = ranges[i];
        status = Map(services, range.Base, range.Base, range.SizeBytes, range.AllowKernelAlias);
        if (EFI_ERROR(status)) return status;
    }

    // Mapping a table can allocate more tables. Walk the growing list to
    // closure, bounded by Allocate's capacity check before each addition.
    for (UINTN i = 0; i < count_; ++i) {
        status = Map(services, pages_[i], pages_[i], EFI_PAGE_SIZE);
        if (EFI_ERROR(status)) return status;
    }
    return EFI_SUCCESS;
}

EFI_STATUS PageTables::Build(EFI_BOOT_SERVICES* services, const IdentityRange* ranges,
                              UINTN rangeCount, UINT64 kernelVirtualBase,
                              EFI_PHYSICAL_ADDRESS kernelPhysicalBase, UINTN kernelBytes)
{
    if (!services || !services->AllocatePages || !services->FreePages ||
        (!ranges && rangeCount != 0) || kernelBytes == 0) return EFI_INVALID_PARAMETER;
    if (count_ != 0) return EFI_ALREADY_STARTED;

    const EFI_STATUS status = BuildTree(services, ranges, rangeCount,
                                        kernelVirtualBase, kernelPhysicalBase, kernelBytes);
    if (!EFI_ERROR(status)) return status;

    Print((CONST CHAR16*)L"Boot page-table build failed: %r (%u/%u pages allocated)\n",
          status, static_cast<UINT32>(count_), kMaxTablePages);
    const EFI_STATUS releaseStatus = Release(services);
    if (EFI_ERROR(releaseStatus)) {
        Print((CONST CHAR16*)L"Boot page-table cleanup failed: %r\n", releaseStatus);
        return releaseStatus;
    }
    return status;
}

}
}
