#include "include/kernel/amd_iommu.h"

#include "include/kernel/amd_ivrs.h"
#include "include/kernel/mmio.h"
#include "include/kernel/modern_input_diagnostics.h"
#include "include/kernel/msi.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace amd_iommu {
namespace {

static amd_ivrs::Table s_table = {};

static uint16_t requester_id(uint8_t bus, uint8_t device, uint8_t function)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bus) << 8u) |
                                  (static_cast<uint16_t>(device) << 3u) |
                                  static_cast<uint16_t>(function & 7u));
}

static void log_route(const modern_input_diagnostics::ControllerSnapshot& controller,
                      const amd_ivrs::Route& route)
{
    serial::puts("[IOMMU] xHCI ");
    serial::put_hex8(controller.bus);
    serial::puts(":");
    serial::put_hex8(controller.device);
    serial::puts(".");
    serial::put_hex8(controller.function);
    serial::puts(" seg=");
    serial::put_hex16(route.segment);
    serial::puts(" rid=");
    serial::put_hex16(route.requesterId);
    if (route.effectiveRequesterId != route.requesterId) {
        serial::puts(" alias=");
        serial::put_hex16(route.effectiveRequesterId);
    }
    serial::puts(" route=");
    serial::puts(modern_input_diagnostics::iommu_route_status_name(route.status));
    if (route.unitIndex >= 0 &&
        static_cast<uint32_t>(route.unitIndex) < s_table.unitCount) {
        const amd_ivrs::IommuUnit& unit =
            s_table.units[static_cast<uint32_t>(route.unitIndex)];
        serial::puts(" unit=");
        serial::put_hex8(static_cast<uint8_t>(route.unitIndex));
        serial::puts(" iommu-rid=");
        serial::put_hex16(unit.deviceId);
        serial::puts(" base=");
        serial::put_hex64(unit.mmioBase);
    }
    serial::puts("\n");
}

static amd_ivrs::RegisterSnapshot inspect_unit(const amd_ivrs::IommuUnit& unit)
{
    amd_ivrs::RegisterSnapshot result = {};
    result.status = amd_ivrs::InspectionStatus::InvalidResource;
    result.translation = amd_ivrs::TranslationState::Unknown;
    if (unit.segment != 0u || unit.capabilityOffset < 0x40u ||
        unit.capabilityOffset > 0xF4u || (unit.capabilityOffset & 3u) != 0u ||
        !amd_ivrs::valid_mmio_resource(unit, amd_ivrs::IVHD_MMIO_MIN_BYTES)) {
        return result;
    }

#if defined(__x86_64__) || defined(_M_X64)
    const uint8_t bus = static_cast<uint8_t>(unit.deviceId >> 8u);
    const uint8_t device = static_cast<uint8_t>((unit.deviceId >> 3u) & 0x1Fu);
    const uint8_t function = static_cast<uint8_t>(unit.deviceId & 0x07u);
    const uint32_t identity = msi::pci_config_read32(bus, device, function, 0x00u);
    const uint32_t classRegister = msi::pci_config_read32(bus, device, function, 0x08u);
    const uint16_t pciStatus = msi::pci_config_read16(bus, device, function, 0x06u);
    const uint16_t vendor = static_cast<uint16_t>(identity & 0xFFFFu);
    const uint16_t product = static_cast<uint16_t>(identity >> 16u);
    if (identity == 0xFFFFFFFFu || vendor == 0u || vendor == 0xFFFFu ||
        product == 0xFFFFu || (classRegister >> 24u) != 0x08u ||
        ((classRegister >> 16u) & 0xFFu) != 0x06u ||
        ((classRegister >> 8u) & 0xFFu) != 0x00u ||
        (pciStatus & 0x0010u) == 0u ||
        msi::find_capability(bus, device, function,
                             amd_ivrs::PCI_IOMMU_CAPABILITY_ID) !=
            static_cast<uint8_t>(unit.capabilityOffset)) {
        result.status = amd_ivrs::InspectionStatus::InvalidIdentity;
        return result;
    }

    const uint32_t capabilityHeader = msi::pci_config_read32(
        bus, device, function, static_cast<uint8_t>(unit.capabilityOffset));
    if ((capabilityHeader & 0xFFu) != amd_ivrs::PCI_IOMMU_CAPABILITY_ID ||
        ((capabilityHeader >> 16u) & 0x07u) != 0x03u) {
        result.status = amd_ivrs::InspectionStatus::InvalidIdentity;
        return result;
    }
    result.capabilityRevision = static_cast<uint8_t>((capabilityHeader >> 19u) & 0x1Fu);

    const uint32_t baseLow = msi::pci_config_read32(
        bus, device, function, static_cast<uint8_t>(unit.capabilityOffset + 4u));
    const uint32_t baseHigh = msi::pci_config_read32(
        bus, device, function, static_cast<uint8_t>(unit.capabilityOffset + 8u));
    const uint64_t configuredBase =
        (static_cast<uint64_t>(baseHigh) << 32u) |
        static_cast<uint64_t>(baseLow & 0xFFFFC000u);
    if ((baseLow & 1u) == 0u || configuredBase != unit.mmioBase ||
        !amd_ivrs::valid_mmio_resource(unit, amd_ivrs::IVHD_MMIO_MIN_BYTES)) {
        result.status = amd_ivrs::InspectionStatus::InvalidResource;
        return result;
    }
    if (result.capabilityRevision != 1u) {
        result.status = amd_ivrs::InspectionStatus::UnsupportedRevision;
        return result;
    }

    uint64_t mappedVirtual = 0u;
    mmio::MappingReport mapping = {};
    const uint32_t mapFlags = mmio::MAP_FLAG_NON_USER |
                              mmio::MAP_FLAG_NO_EXEC |
                              mmio::MAP_FLAG_UNCACHED;
    const bool mapped = mmio::mapForDevice(
        unit.mmioBase, amd_ivrs::IVHD_MMIO_MIN_BYTES,
        &mappedVirtual, &mapping, mapFlags);
    if (!mapped || !mapping.success || mappedVirtual == 0u ||
        mapping.length < 0x2028u ||
        mappedVirtual > UINT64_MAX - 0x2028u) {
        result.status = amd_ivrs::InspectionStatus::MappingUnavailable;
        return result;
    }

    // AMD IOMMU Specification 48882 Rev 3.09: the control register is a
    // 64-bit read at MMIO 0018h; IommuEn is bit 0. The 64-bit status register
    // is at MMIO 2020h. Both are volatile read-only accesses here.
    const volatile uint64_t* control = reinterpret_cast<const volatile uint64_t*>(
        static_cast<uintptr_t>(mappedVirtual + amd_ivrs::IOMMU_CONTROL_OFFSET));
    const volatile uint64_t* status = reinterpret_cast<const volatile uint64_t*>(
        static_cast<uintptr_t>(mappedVirtual + amd_ivrs::IOMMU_STATUS_OFFSET));
    result.control = *control;
    result.iommuStatus = *status;
    result.controlValid = true;
    result.statusValid = true;
    result.translation = amd_ivrs::classify_translation(
        result.controlValid, result.capabilityRevision, result.control);
    result.status = amd_ivrs::InspectionStatus::Readable;
    return result;
#else
    result.status = amd_ivrs::InspectionStatus::InvalidIdentity;
    return result;
#endif
}

} // namespace

void initialize(const uint8_t* ivrsCopy, uint32_t ivrsLength,
                uint32_t ivrsTableCount, bool acpiScanValid,
                bool remappingTablePresent)
{
    amd_ivrs::clear(&s_table);
    amd_ivrs::ParseStatus status = amd_ivrs::ParseStatus::NotPresent;

    if (ivrsTableCount > 1u) {
        status = amd_ivrs::ParseStatus::AmbiguousTables;
        s_table.status = status;
        s_table.hasUnsupportedTopology = true;
        s_table.unsupportedStructureCount = ivrsTableCount;
    } else if (ivrsTableCount == 1u) {
        if (ivrsCopy == nullptr || ivrsLength < amd_ivrs::IVRS_HEADER_BYTES ||
            ivrsLength > amd_ivrs::MAX_TABLE_BYTES) {
            status = amd_ivrs::ParseStatus::Malformed;
            s_table.status = status;
        } else {
            status = amd_ivrs::parse(ivrsCopy, ivrsLength, &s_table);
        }
    }

    modern_input_diagnostics::record_ivrs_discovery(
        status, acpiScanValid, remappingTablePresent, ivrsTableCount,
        s_table.unitCount, s_table.ivmdCount,
        s_table.unsupportedStructureCount);

    const char* reason = "NO_REMAP_TABLE_OBSERVED";
    if (!acpiScanValid) {
        reason = "ACPI_REMAP_SCAN_UNKNOWN";
    } else if (ivrsTableCount > 1u) {
        reason = "MULTIPLE_IVRS_TABLES_AMBIGUOUS";
    } else if (remappingTablePresent && ivrsTableCount == 0u) {
        reason = "NON_IVRS_REMAP_TABLE_PRESENT";
    } else if (ivrsTableCount == 1u &&
               (status == amd_ivrs::ParseStatus::Malformed ||
                status == amd_ivrs::ParseStatus::CapacityExceeded)) {
        reason = "IVRS_PRESENT_UNPARSED";
    } else if (s_table.hasUnsupportedTopology) {
        reason = "IVRS_TOPOLOGY_UNSUPPORTED";
    } else if (ivrsTableCount == 1u && s_table.unitCount == 0u) {
        reason = "IVRS_HAS_NO_IOMMU_UNITS";
    } else if (remappingTablePresent) {
        reason = "DEVICE_DMA_MAPPING_NOT_ESTABLISHED";
    }
    modern_input_diagnostics::record_dma_safety(reason, false);

    serial::puts("[IOMMU] ACPI remap scan=");
    serial::puts(acpiScanValid ? "VALID" : "UNKNOWN");
    serial::puts(" IVRS count=");
    serial::put_hex32(ivrsTableCount);
    serial::puts(" status=");
    serial::puts(modern_input_diagnostics::ivrs_status_name(status));
    serial::puts(" IVHD units=");
    serial::put_hex32(s_table.unitCount);
    serial::puts(" IVMD ranges=");
    serial::put_hex32(s_table.ivmdCount);
    serial::puts(" DMA remains blocked unless existing direct-DMA contract applies\n");
}

void qualify_xhci_controllers()
{
    modern_input_diagnostics::Snapshot snapshot = {};
    modern_input_diagnostics::copy_snapshot(&snapshot);
    amd_ivrs::InspectionStatus inspectionByUnit[amd_ivrs::MAX_IOMMU_UNITS] = {};
    amd_ivrs::TranslationState translationByUnit[amd_ivrs::MAX_IOMMU_UNITS] = {};
    bool controlByUnit[amd_ivrs::MAX_IOMMU_UNITS] = {};
    bool statusByUnit[amd_ivrs::MAX_IOMMU_UNITS] = {};
    bool inspected[amd_ivrs::MAX_IOMMU_UNITS] = {};

    for (uint32_t i = 0u; i < snapshot.storedControllerCount; ++i) {
        const modern_input_diagnostics::ControllerSnapshot& controller =
            snapshot.controllers[i];
        if (!controller.occupied ||
            controller.kind != modern_input_diagnostics::ControllerKind::XHCI) {
            continue;
        }
        const uint16_t rid = requester_id(
            controller.bus, controller.device, controller.function);
        const amd_ivrs::Route route = amd_ivrs::route_device(s_table, 0u, rid);
        const uint64_t iommuBase = route.status == amd_ivrs::RouteStatus::Resolved &&
                                   route.unitIndex >= 0 &&
                                   static_cast<uint32_t>(route.unitIndex) < s_table.unitCount
            ? s_table.units[static_cast<uint32_t>(route.unitIndex)].mmioBase : 0u;
        uint16_t iommuDeviceId = 0u;
        if (route.unitIndex >= 0 &&
            static_cast<uint32_t>(route.unitIndex) < s_table.unitCount) {
            iommuDeviceId = s_table.units[static_cast<uint32_t>(route.unitIndex)].deviceId;
        }
        modern_input_diagnostics::record_iommu_route(
            static_cast<int32_t>(i), 0u, rid, route.effectiveRequesterId,
            route.status, route.unitIndex, iommuDeviceId, iommuBase);
        log_route(controller, route);

        const bool selectedController =
            static_cast<int32_t>(i) == snapshot.selectedControllerIndex;
        if (selectedController && snapshot.acpiRemappingTablePresent) {
            const char* reason = route.status == amd_ivrs::RouteStatus::Ambiguous
                ? "DEVICE_ROUTING_AMBIGUOUS"
                : (route.status == amd_ivrs::RouteStatus::Unsupported
                    ? "DEVICE_ROUTING_UNSUPPORTED"
                    : (route.status == amd_ivrs::RouteStatus::Unmatched
                        ? "DEVICE_ROUTING_UNRESOLVED"
                        : "IOMMU_STATE_UNKNOWN"));
            modern_input_diagnostics::record_dma_safety(reason, false);
        }

        if (route.status != amd_ivrs::RouteStatus::Resolved || route.unitIndex < 0 ||
            static_cast<uint32_t>(route.unitIndex) >= s_table.unitCount) {
            modern_input_diagnostics::record_iommu_registers(
                static_cast<int32_t>(i), amd_ivrs::InspectionStatus::NotAttempted,
                amd_ivrs::TranslationState::Unknown);
            continue;
        }

        const uint32_t unitIndex = static_cast<uint32_t>(route.unitIndex);
        if (!inspected[unitIndex]) {
            const amd_ivrs::RegisterSnapshot registers =
                inspect_unit(s_table.units[unitIndex]);
            inspectionByUnit[unitIndex] = registers.status;
            translationByUnit[unitIndex] = registers.translation;
            controlByUnit[unitIndex] = registers.controlValid;
            statusByUnit[unitIndex] = registers.statusValid;
            inspected[unitIndex] = true;
            serial::puts("[IOMMU] unit=");
            serial::put_hex8(static_cast<uint8_t>(unitIndex));
            serial::puts(" registers=");
            serial::puts(modern_input_diagnostics::iommu_inspection_status_name(registers.status));
            serial::puts(" translation=");
            serial::puts(modern_input_diagnostics::iommu_translation_state_name(registers.translation));
            serial::puts(" (read-only)\n");
        }
        modern_input_diagnostics::record_iommu_registers(
            static_cast<int32_t>(i), inspectionByUnit[unitIndex],
            translationByUnit[unitIndex], controlByUnit[unitIndex],
            statusByUnit[unitIndex]);
        if (selectedController && snapshot.acpiRemappingTablePresent) {
            const bool readable = inspectionByUnit[unitIndex] ==
                                  amd_ivrs::InspectionStatus::Readable;
            modern_input_diagnostics::record_dma_safety(
                readable ? "DEVICE_DMA_MAPPING_NOT_ESTABLISHED"
                         : "IOMMU_STATE_UNKNOWN",
                false);
        }
    }
}

} // namespace amd_iommu
} // namespace kernel
