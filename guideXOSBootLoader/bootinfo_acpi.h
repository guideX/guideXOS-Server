#pragma once

#include "acpi_processor_inventory.h"
#include "guidexOSBootInfo.h"

namespace guideXOS {

constexpr uint32_t kBootInfoAcpiVersion = 1;
constexpr size_t kBootInfoPageBytes = 4096;

// A flagged extension in the already mapped BootInfo page. BootInfo::Size
// and its v3 checksum still cover only the base handoff.
struct BootInfoAcpiExtension {
    uint32_t version;
    uint32_t size;
    uint8_t checksum;
    acpi::ProcessorInventory inventory;
};

struct BootInfoWithAcpi {
    BootInfo base;
    BootInfoAcpiExtension acpi;
};

static_assert(sizeof(BootInfoWithAcpi) <= kBootInfoPageBytes, "ACPI extension must fit in BootInfo page");
static_assert(offsetof(BootInfoWithAcpi, acpi) == 576 &&
              sizeof(BootInfoAcpiExtension) == 2640 &&
              offsetof(BootInfoAcpiExtension, inventory) == 16 &&
              offsetof(acpi::ProcessorInventory, records) == 64 &&
              sizeof(acpi::ProcessorRecord) == 20,
              "ACPI extension v1 must use the same layout in the loader and kernel");

inline uint8_t bootinfo_acpi_sum(const BootInfoAcpiExtension& extension)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&extension);
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(extension); ++i) sum = static_cast<uint8_t>(sum + bytes[i]);
    return sum;
}

inline void seal_bootinfo_acpi(BootInfoWithAcpi& handoff)
{
    auto& extension = handoff.acpi;
    extension.version = kBootInfoAcpiVersion;
    extension.size = sizeof(extension);
    extension.checksum = 0;
    extension.checksum = static_cast<uint8_t>(0 - bootinfo_acpi_sum(extension));
    handoff.base.Flags |= GUIDEXOS_BOOTINFO_FLAG_ACPI_INVENTORY_VALID;
}

// The caller has the mapped UEFI BootInfo page. Firmware addresses in the
// extension are provenance only and are never used to obtain another view.
inline const acpi::ProcessorInventory* bootinfo_acpi_inventory(const BootInfo* boot)
{
    if (!boot || (reinterpret_cast<uintptr_t>(boot) & (kBootInfoPageBytes - 1)) != 0 ||
        boot->Magic != GUIDEXOS_BOOTINFO_MAGIC || boot->Version != GUIDEXOS_BOOTINFO_VERSION ||
        boot->Size != sizeof(BootInfo) || boot->BootMode != BootMode::Uefi ||
        (boot->Flags & GUIDEXOS_BOOTINFO_FLAG_ACPI_INVENTORY_VALID) == 0 ||
        !guidexos_bootinfo_checksum_valid(boot)) return nullptr;

    const auto& extension = reinterpret_cast<const BootInfoWithAcpi*>(boot)->acpi;
    if (extension.version != kBootInfoAcpiVersion || extension.size != sizeof(extension) ||
        bootinfo_acpi_sum(extension) != 0) return nullptr;
    const auto& inventory = extension.inventory;
    if (inventory.rsdp_address != boot->AcpiRsdp ||
        inventory.stored_count > acpi::kProcessorCapacity) return nullptr;
    return &inventory;
}

} // namespace guideXOS
