#include "include/kernel/intel_vtd.h"

#include "include/kernel/intel_dmar.h"
#include "include/kernel/mmio.h"
#include "include/kernel/modern_input_diagnostics.h"
#include "include/kernel/msi.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace intel_vtd {
namespace {

static const uint32_t VTD_REQUIRED_REGISTER_BYTES = 0x38u;
static const uint32_t PCI_HEADER_TYPE_OFFSET = 0x0Eu;
static const uint32_t PCI_CLASS_REVISION_OFFSET = 0x08u;
static const uint32_t PCI_PRIMARY_SECONDARY_SUBORDINATE_OFFSET = 0x18u;
static const uint8_t PCI_HEADER_TYPE_BRIDGE = 0x01u;
static const uint8_t PCI_CLASS_BRIDGE = 0x06u;
static const uint8_t PCI_SUBCLASS_PCI_BRIDGE = 0x04u;

static intel_dmar::Table s_table = {};

struct BridgePathElement {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t secondaryBus;
};

static bool pci_function_present(uint8_t bus, uint8_t device, uint8_t function)
{
    const uint32_t identity = msi::pci_config_read32(bus, device, function, 0u);
    const uint16_t vendor = static_cast<uint16_t>(identity & 0xFFFFu);
    return identity != UINT32_MAX && vendor != 0u && vendor != 0xFFFFu;
}

// The current PCI config front door can address segment 0 only. Reconstruct
// the bridge chain using bounded standard config reads, requiring exactly one
// parent bridge at each bus level. A guessed or ambiguous chain never routes.
static bool build_pci_path(uint16_t segment, uint8_t bus, uint8_t device,
                           uint8_t function, intel_dmar::PciPath* out)
{
    if (out == nullptr) return false;
    *out = {};
    out->segment = segment;
    if (segment != 0u || device > 31u || function > 7u) return false;

    static const uint32_t MAX_PCI_BRIDGE_CANDIDATES = 256u;
    BridgePathElement candidates[MAX_PCI_BRIDGE_CANDIDATES] = {};
    uint32_t candidateCount = 0u;
    for (uint32_t scanBus = 0u; scanBus < bus; ++scanBus) {
        for (uint32_t scanDevice = 0u; scanDevice < 32u; ++scanDevice) {
            if (!pci_function_present(static_cast<uint8_t>(scanBus),
                                      static_cast<uint8_t>(scanDevice), 0u)) {
                continue;
            }
            const uint8_t header = msi::pci_config_read8(
                static_cast<uint8_t>(scanBus), static_cast<uint8_t>(scanDevice),
                0u, PCI_HEADER_TYPE_OFFSET);
            const uint32_t functionLimit = (header & 0x80u) != 0u ? 8u : 1u;
            for (uint32_t scanFunction = 0u; scanFunction < functionLimit;
                 ++scanFunction) {
                const uint8_t candidateFunction = static_cast<uint8_t>(scanFunction);
                if (!pci_function_present(static_cast<uint8_t>(scanBus),
                                          static_cast<uint8_t>(scanDevice),
                                          candidateFunction)) {
                    continue;
                }
                const uint32_t classRevision = msi::pci_config_read32(
                    static_cast<uint8_t>(scanBus), static_cast<uint8_t>(scanDevice),
                    candidateFunction, PCI_CLASS_REVISION_OFFSET);
                const uint8_t baseClass = static_cast<uint8_t>(classRevision >> 24u);
                const uint8_t subClass = static_cast<uint8_t>(classRevision >> 16u);
                const uint8_t candidateHeader = msi::pci_config_read8(
                    static_cast<uint8_t>(scanBus), static_cast<uint8_t>(scanDevice),
                    candidateFunction, PCI_HEADER_TYPE_OFFSET) & 0x7Fu;
                if (baseClass != PCI_CLASS_BRIDGE ||
                    subClass != PCI_SUBCLASS_PCI_BRIDGE ||
                    candidateHeader != PCI_HEADER_TYPE_BRIDGE) {
                    continue;
                }
                const uint32_t buses = msi::pci_config_read32(
                    static_cast<uint8_t>(scanBus), static_cast<uint8_t>(scanDevice),
                    candidateFunction, PCI_PRIMARY_SECONDARY_SUBORDINATE_OFFSET);
                const uint8_t primary = static_cast<uint8_t>(buses & 0xFFu);
                const uint8_t secondary = static_cast<uint8_t>((buses >> 8u) & 0xFFu);
                const uint8_t subordinate = static_cast<uint8_t>((buses >> 16u) & 0xFFu);
                if (primary != scanBus || primary >= secondary ||
                    secondary > bus || subordinate < secondary) {
                    continue;
                }
                if (candidateCount >= MAX_PCI_BRIDGE_CANDIDATES) return false;
                BridgePathElement& candidate = candidates[candidateCount++];
                candidate.bus = primary;
                candidate.device = static_cast<uint8_t>(scanDevice);
                candidate.function = candidateFunction;
                candidate.secondaryBus = secondary;
            }
        }
    }

    BridgePathElement reverse[intel_dmar::MAX_SCOPE_PATH] = {};
    uint32_t bridgeCount = 0u;
    uint8_t currentBus = bus;
    while (currentBus != 0u) {
        if (bridgeCount >= intel_dmar::MAX_SCOPE_PATH - 1u) return false;
        int32_t parentIndex = -1;
        for (uint32_t i = 0u; i < candidateCount; ++i) {
            if (candidates[i].secondaryBus != currentBus) continue;
            if (parentIndex >= 0) return false;
            parentIndex = static_cast<int32_t>(i);
        }
        if (parentIndex < 0) return false;
        const BridgePathElement& parent = candidates[static_cast<uint32_t>(parentIndex)];
        reverse[bridgeCount++] = parent;
        currentBus = parent.bus;
    }

    if (bridgeCount + 1u > intel_dmar::MAX_SCOPE_PATH) return false;
    for (uint32_t i = 0u; i < bridgeCount; ++i) {
        const BridgePathElement& element = reverse[bridgeCount - i - 1u];
        out->entries[i].bus = element.bus;
        out->entries[i].device = element.device;
        out->entries[i].function = element.function;
    }
    out->entries[bridgeCount].bus = bus;
    out->entries[bridgeCount].device = device;
    out->entries[bridgeCount].function = function;
    out->count = static_cast<uint8_t>(bridgeCount + 1u);
    out->valid = true;
    return true;
}

static intel_dmar::RegisterSnapshot inspect_unit(const intel_dmar::DrhdUnit& unit)
{
    intel_dmar::RegisterSnapshot result = {};
    result.status = intel_dmar::RegisterStatus::InvalidResource;
    result.translation = intel_dmar::TranslationState::Unknown;
    result.rootTable = intel_dmar::RootTableState::Unknown;
    if (!intel_dmar::valid_register_resource(
            unit.registerBase, unit.registerBytes, VTD_REQUIRED_REGISTER_BYTES)) {
        return result;
    }

#if defined(__x86_64__) || defined(_M_X64)
    uint64_t mappedVirtual = 0u;
    mmio::MappingReport mapping = {};
    const uint32_t mapFlags = mmio::MAP_FLAG_NON_USER |
                              mmio::MAP_FLAG_NO_EXEC |
                              mmio::MAP_FLAG_UNCACHED;
    if (!mmio::mapForDevice(unit.registerBase, VTD_REQUIRED_REGISTER_BYTES,
                            &mappedVirtual, &mapping, mapFlags) ||
        !mapping.success || mappedVirtual == 0u ||
        mapping.mappedLength < VTD_REQUIRED_REGISTER_BYTES ||
        mappedVirtual > UINT64_MAX - VTD_REQUIRED_REGISTER_BYTES) {
        result.status = intel_dmar::RegisterStatus::MappingUnavailable;
        return result;
    }

    const volatile uint8_t* bytes = reinterpret_cast<const volatile uint8_t*>(
        static_cast<uintptr_t>(mappedVirtual));
    const uint32_t version = *reinterpret_cast<const volatile uint32_t*>(bytes + 0x00u);
    result.version = static_cast<uint8_t>(version & 0xFFu);
    if (version == 0u || version == UINT32_MAX || (result.version >> 4u) != 1u) {
        result.status = version == 0u || version == UINT32_MAX
            ? intel_dmar::RegisterStatus::ReadUnavailable
            : intel_dmar::RegisterStatus::UnsupportedVersion;
        return result;
    }

    // All accesses below are volatile reads. R7 does not write GCMD, GSTS,
    // RTADDR, FSTS, or any other VT-d register.
    const uint64_t capability = *reinterpret_cast<const volatile uint64_t*>(bytes + 0x08u);
    const uint64_t extendedCapability =
        *reinterpret_cast<const volatile uint64_t*>(bytes + 0x10u);
    const uint32_t globalCommand =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x18u);
    const uint32_t globalStatus =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x1Cu);
    const uint64_t rootTableAddress =
        *reinterpret_cast<const volatile uint64_t*>(bytes + 0x20u);
    const uint32_t faultStatus =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x34u);
    return intel_dmar::classify_registers(
        true, version, capability, extendedCapability, globalCommand,
        globalStatus, rootTableAddress, faultStatus);
#else
    result.status = intel_dmar::RegisterStatus::MappingUnavailable;
    return result;
#endif
}

static void log_route(const modern_input_diagnostics::ControllerSnapshot& controller,
                      const intel_dmar::Route& route)
{
    serial::puts("[VTD] xHCI ");
    serial::put_hex8(controller.bus);
    serial::puts(":");
    serial::put_hex8(controller.device);
    serial::puts(".");
    serial::put_hex8(controller.function);
    serial::puts(" segment=");
    serial::put_hex16(route.segment);
    serial::puts(" route=");
    serial::puts(modern_input_diagnostics::dmar_route_status_name(route.status));
    if (route.unitIndex >= 0 &&
        static_cast<uint32_t>(route.unitIndex) < s_table.drhdCount) {
        serial::puts(" unit=");
        serial::put_hex8(static_cast<uint8_t>(route.unitIndex));
        serial::puts(" base=");
        serial::put_hex64(route.registerBase);
        serial::puts(route.rmrrRequired ? " RMRR=yes" : " RMRR=no");
    }
    serial::puts("\n");
}

static const char* dma_route_reason(const intel_dmar::Route& route)
{
    if (route.status == intel_dmar::RouteStatus::Ambiguous) {
        return "DEVICE_ROUTING_AMBIGUOUS";
    }
    if (route.status == intel_dmar::RouteStatus::NotCovered) {
        return "DEVICE_ROUTING_NOT_COVERED";
    }
    if (route.status != intel_dmar::RouteStatus::Resolved) {
        return "DEVICE_ROUTING_UNSUPPORTED";
    }
    return "DEVICE_DMA_MAPPING_NOT_ESTABLISHED";
}

} // namespace

void initialize(const uint8_t* dmarCopy, uint32_t dmarLength,
                uint32_t dmarTableCount, bool acpiScanValid,
                bool remappingTablePresent)
{
    intel_dmar::clear(&s_table);
    intel_dmar::ParseStatus status = intel_dmar::ParseStatus::NotPresent;
    if (dmarTableCount > 1u) {
        status = intel_dmar::ParseStatus::AmbiguousTables;
        s_table.status = status;
        s_table.hasUnsupportedTopology = true;
        s_table.unsupportedStructureCount = dmarTableCount;
    } else if (dmarTableCount == 1u) {
        if (dmarCopy == nullptr || dmarLength < intel_dmar::DMAR_HEADER_BYTES ||
            dmarLength > intel_dmar::MAX_TABLE_BYTES) {
            status = intel_dmar::ParseStatus::Malformed;
            s_table.status = status;
        } else {
            status = intel_dmar::parse(dmarCopy, dmarLength, &s_table);
        }
    }

    modern_input_diagnostics::record_dmar_discovery(
        status, acpiScanValid, remappingTablePresent, dmarTableCount,
        s_table.drhdCount, s_table.rmrrCount, s_table.atsrCount,
        s_table.rhsaCount, s_table.unsupportedStructureCount +
            s_table.unsupportedScopeCount);

    const char* reason = "NO_REMAP_TABLE_OBSERVED";
    if (!acpiScanValid) {
        reason = "ACPI_REMAP_SCAN_UNKNOWN";
    } else if (dmarTableCount > 1u) {
        reason = "MULTIPLE_DMAR_TABLES_AMBIGUOUS";
    } else if (remappingTablePresent && dmarTableCount == 0u) {
        reason = "NON_DMAR_REMAP_TABLE_PRESENT";
    } else if (dmarTableCount == 1u &&
               (status == intel_dmar::ParseStatus::Malformed ||
                status == intel_dmar::ParseStatus::CapacityExceeded ||
                status == intel_dmar::ParseStatus::UnsupportedRevision)) {
        reason = "DMAR_PRESENT_UNPARSED";
    } else if (s_table.hasUnsupportedTopology) {
        reason = "DMAR_TOPOLOGY_UNSUPPORTED";
    } else if (dmarTableCount == 1u && s_table.drhdCount == 0u) {
        reason = "DMAR_HAS_NO_DRHD_UNITS";
    } else if (remappingTablePresent) {
        reason = "DEVICE_DMA_MAPPING_NOT_ESTABLISHED";
    }
    modern_input_diagnostics::record_dma_safety(reason, false);

    serial::puts("[VTD] ACPI remap scan=");
    serial::puts(acpiScanValid ? "VALID" : "UNKNOWN");
    serial::puts(" DMAR count=");
    serial::put_hex32(dmarTableCount);
    serial::puts(" status=");
    serial::puts(modern_input_diagnostics::dmar_status_name(status));
    serial::puts(" DRHD=");
    serial::put_hex32(s_table.drhdCount);
    serial::puts(" RMRR=");
    serial::put_hex32(s_table.rmrrCount);
    serial::puts("; register inspection read-only, DMA mapping not established\n");
}

void qualify_xhci_controllers()
{
    modern_input_diagnostics::Snapshot snapshot = {};
    modern_input_diagnostics::copy_snapshot(&snapshot);
    intel_dmar::RegisterSnapshot registersByUnit[intel_dmar::MAX_DRHD_UNITS] = {};
    bool inspected[intel_dmar::MAX_DRHD_UNITS] = {};

    for (uint32_t i = 0u; i < snapshot.storedControllerCount; ++i) {
        const modern_input_diagnostics::ControllerSnapshot& controller =
            snapshot.controllers[i];
        if (!controller.occupied ||
            controller.kind != modern_input_diagnostics::ControllerKind::XHCI ||
            (controller.vendorId != 0x8086u &&
             !modern_input_diagnostics::intel_dmar_applies_to_platform(
                 snapshot.platform, snapshot.dmarTablePresent))) {
            continue;
        }

        intel_dmar::PciPath path = {};
        intel_dmar::Route route = {};
        route.status = intel_dmar::RouteStatus::Unsupported;
        route.segment = controller.pciSegment;
        route.bus = controller.bus;
        route.device = controller.device;
        route.function = controller.function;
        route.unitIndex = -1;
        if (s_table.status == intel_dmar::ParseStatus::Valid) {
            const bool pathValid = build_pci_path(
                controller.pciSegment, controller.bus, controller.device,
                controller.function, &path);
            if (pathValid) route = intel_dmar::route_device(s_table, path);
        }

        modern_input_diagnostics::record_vtd_route(
            static_cast<int32_t>(i), controller.pciSegment, controller.bus,
            controller.device, controller.function, route.status,
            route.unitIndex, route.registerBase, route.rmrrRequired);
        log_route(controller, route);

        if (route.unitIndex < 0 ||
            static_cast<uint32_t>(route.unitIndex) >= s_table.drhdCount) {
            if (static_cast<int32_t>(i) == snapshot.selectedControllerIndex &&
                snapshot.dmarTablePresent) {
                modern_input_diagnostics::record_dma_safety(
                    dma_route_reason(route), false);
            }
            continue;
        }

        const uint32_t unitIndex = static_cast<uint32_t>(route.unitIndex);
        if (!inspected[unitIndex]) {
            registersByUnit[unitIndex] = inspect_unit(s_table.drhd[unitIndex]);
            inspected[unitIndex] = true;
            const intel_dmar::RegisterSnapshot& registers = registersByUnit[unitIndex];
            serial::puts("[VTD] unit=");
            serial::put_hex8(static_cast<uint8_t>(unitIndex));
            serial::puts(" registers=");
            serial::puts(modern_input_diagnostics::vtd_register_status_name(registers.status));
            serial::puts(" translation=");
            serial::puts(modern_input_diagnostics::vtd_translation_state_name(registers.translation));
            serial::puts(" root=");
            serial::puts(modern_input_diagnostics::vtd_root_table_state_name(registers.rootTable));
            serial::puts(" (read-only)\n");
            if (registers.capabilityValid) {
                serial::puts("[VTD] CAP=");
                serial::put_hex64(registers.capability);
                serial::puts(" ECAP=");
                serial::put_hex64(registers.extendedCapability);
                serial::puts(" GCMD=");
                serial::put_hex32(registers.globalCommand);
                serial::puts(" GSTS=");
                serial::put_hex32(registers.globalStatus);
                serial::puts(" RTADDR=");
                serial::put_hex64(registers.rootTableAddress);
                serial::puts(" FSTS=");
                serial::put_hex32(registers.faultStatus);
                serial::puts("\n");
            }
        }
        modern_input_diagnostics::record_vtd_registers(
            static_cast<int32_t>(i), registersByUnit[unitIndex]);

        if (static_cast<int32_t>(i) == snapshot.selectedControllerIndex &&
            snapshot.dmarTablePresent) {
            const intel_dmar::RegisterSnapshot& registers = registersByUnit[unitIndex];
            const char* reason = "DEVICE_DMA_MAPPING_NOT_ESTABLISHED";
            if (route.rmrrRequired) {
                reason = "RMRR_MAPPING_REQUIREMENTS_UNSATISFIED";
            } else if (registers.status == intel_dmar::RegisterStatus::MappingUnavailable) {
                reason = "VTD_REGISTERS_MAPPING_UNAVAILABLE";
            } else if (registers.status != intel_dmar::RegisterStatus::Readable) {
                reason = registers.status == intel_dmar::RegisterStatus::UnsupportedVersion
                    ? "VTD_VERSION_UNSUPPORTED"
                    : (registers.status == intel_dmar::RegisterStatus::UnsupportedCapabilities
                        ? "VTD_CAPABILITIES_UNSUPPORTED"
                        : "VTD_REGISTERS_NOT_READABLE");
            } else if (registers.translation ==
                       intel_dmar::TranslationState::DisabledObserved) {
                reason = "VTD_TRANSLATION_DISABLED_MAPPING_UNAVAILABLE";
            } else if (registers.translation !=
                       intel_dmar::TranslationState::EnabledObserved) {
                reason = "VTD_TRANSLATION_STATE_UNKNOWN";
            } else if (registers.rootTable != intel_dmar::RootTableState::Configured) {
                reason = "VTD_ROOT_TABLE_NOT_VERIFIED";
            }
            modern_input_diagnostics::record_dma_safety(reason, false);
        }
    }
}

} // namespace intel_vtd
} // namespace kernel
