#pragma once

#include "Uefi.h"
#include "amd64_boot_paging.h"

namespace guideXOS {
namespace paging {

struct IdentityRange
{
    EFI_PHYSICAL_ADDRESS Base;
    UINTN SizeBytes;
    bool AllowKernelAlias = false;
};

// Owns a single boot tree while UEFI Boot Services are available. Keep this
// workspace in loader storage so its tracking array does not use UEFI stack.
class PageTables
{
public:
    constexpr PageTables() = default;
    PageTables(const PageTables&) = delete;
    PageTables& operator=(const PageTables&) = delete;

    // Installs 4 KiB writable, executable, supervisor-only mappings.
    // A successful build also identity maps every table page.
    EFI_STATUS Build(EFI_BOOT_SERVICES* services, const IdentityRange* ranges,
                     UINTN rangeCount, UINT64 kernelVirtualBase,
                     EFI_PHYSICAL_ADDRESS kernelPhysicalBase, UINTN kernelBytes);

    // Release on failure before handoff. After ExitBootServices succeeds the
    // active tree belongs to the kernel and must remain reserved.
    EFI_STATUS Release(EFI_BOOT_SERVICES* services);

    EFI_PHYSICAL_ADDRESS Root() const { return root_; }
    UINTN Count() const { return count_; }

private:
    EFI_STATUS Allocate(EFI_BOOT_SERVICES* services, EFI_PHYSICAL_ADDRESS* physical);
    EFI_STATUS Map(EFI_BOOT_SERVICES* services, UINT64 virtualBase,
                   EFI_PHYSICAL_ADDRESS physicalBase, UINTN sizeBytes,
                   bool allowKernelAlias = false);
    EFI_STATUS BuildTree(EFI_BOOT_SERVICES* services, const IdentityRange* ranges,
                         UINTN rangeCount, UINT64 kernelVirtualBase,
                         EFI_PHYSICAL_ADDRESS kernelPhysicalBase, UINTN kernelBytes);

    EFI_PHYSICAL_ADDRESS root_ = 0;
    UINTN count_ = 0;
    EFI_PHYSICAL_ADDRESS pages_[amd64_boot_paging::kMaxTablePages]{};
};

}
}
