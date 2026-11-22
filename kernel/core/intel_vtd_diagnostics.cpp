#include "include/kernel/intel_vtd.h"

#include "include/kernel/intel_dmar.h"
#include "include/kernel/mmio.h"
#include "include/kernel/modern_input_diagnostics.h"
#include "include/kernel/msi.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace intel_vtd {
namespace {

static const uint32_t VTD_REQUIRED_REGISTER_BYTES = 0x80u;
static const uint32_t PCI_HEADER_TYPE_OFFSET = 0x0Eu;
static const uint32_t PCI_CLASS_REVISION_OFFSET = 0x08u;
static const uint32_t PCI_PRIMARY_SECONDARY_SUBORDINATE_OFFSET = 0x18u;
static const uint8_t PCI_HEADER_TYPE_BRIDGE = 0x01u;
static const uint8_t PCI_CLASS_BRIDGE = 0x06u;
static const uint8_t PCI_SUBCLASS_PCI_BRIDGE = 0x04u;

static intel_dmar::Table s_table = {};
static intel_dmar::PciPath s_paths[modern_input_diagnostics::MAX_CONTROLLERS] = {};
static intel_dmar::Route s_routes[modern_input_diagnostics::MAX_CONTROLLERS] = {};
static intel_dmar::RegisterSnapshot s_registersByUnit[intel_dmar::MAX_DRHD_UNITS] = {};
static bool s_pathValid[modern_input_diagnostics::MAX_CONTROLLERS] = {};
static bool s_unitInspected[intel_dmar::MAX_DRHD_UNITS] = {};

static const char* memory_map_check_name(dma_memory::MemoryMapCheck check)
{
    switch (check) {
        case dma_memory::MemoryMapCheck::Valid: return "VALID";
        case dma_memory::MemoryMapCheck::InvalidView: return "MAP_UNAVAILABLE";
        case dma_memory::MemoryMapCheck::InvalidDescriptor: return "INVALID_DESCRIPTOR";
        case dma_memory::MemoryMapCheck::InvalidAlignment: return "INVALID_ALIGNMENT";
        case dma_memory::MemoryMapCheck::RangeOverflow: return "RANGE_OVERFLOW";
        case dma_memory::MemoryMapCheck::RangeUncovered: return "RANGE_UNCOVERED";
        case dma_memory::MemoryMapCheck::OwnershipTypeMismatch: return "OWNER_MISMATCH";
        case dma_memory::MemoryMapCheck::AmbiguousOwnership: return "OWNER_AMBIGUOUS";
        default: return "UNKNOWN";
    }
}

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

static bool same_pci_path(const intel_dmar::PciPath& left,
                          const intel_dmar::PciPath& right)
{
    if (!left.valid || !right.valid || left.segment != right.segment ||
        left.count == 0u || left.count != right.count ||
        left.count > intel_dmar::MAX_SCOPE_PATH) return false;
    for (uint32_t i = 0u; i < left.count; ++i) {
        if (left.entries[i].bus != right.entries[i].bus ||
            left.entries[i].device != right.entries[i].device ||
            left.entries[i].function != right.entries[i].function) return false;
    }
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

    // Read only architecturally readable state. GCMD fields are write-only;
    // GCMD.TE reads undefined, so GSTS.TES is the translation-state source.
    const uint64_t capability = *reinterpret_cast<const volatile uint64_t*>(bytes + 0x08u);
    const uint64_t extendedCapability =
        *reinterpret_cast<const volatile uint64_t*>(bytes + 0x10u);
    const uint32_t globalStatus =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x1Cu);
    const uint64_t rootTableAddress =
        *reinterpret_cast<const volatile uint64_t*>(bytes + 0x20u);
    const uint32_t faultStatus =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x34u);
    result = intel_dmar::classify_registers(
        true, version, capability, extendedCapability,
        globalStatus, rootTableAddress, faultStatus);
    const uint32_t pmen =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x64u);
    const uint32_t protectedLowBase =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x68u);
    const uint32_t protectedLowLimit =
        *reinterpret_cast<const volatile uint32_t*>(bytes + 0x6Cu);
    const uint64_t protectedHighBase =
        *reinterpret_cast<const volatile uint64_t*>(bytes + 0x70u);
    const uint64_t protectedHighLimit =
        *reinterpret_cast<const volatile uint64_t*>(bytes + 0x78u);
    intel_dmar::classify_protected_memory(
        capability, pmen, protectedLowBase, protectedLowLimit,
        protectedHighBase, protectedHighLimit, &result);
    return result;
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
    for (uint32_t i = 0u; i < modern_input_diagnostics::MAX_CONTROLLERS; ++i) {
        s_paths[i] = {};
        s_routes[i] = {};
        s_routes[i].unitIndex = -1;
        s_pathValid[i] = false;
    }
    for (uint32_t i = 0u; i < intel_dmar::MAX_DRHD_UNITS; ++i) {
        s_registersByUnit[i] = {};
        s_registersByUnit[i].status = intel_dmar::RegisterStatus::NotAttempted;
        s_registersByUnit[i].translation = intel_dmar::TranslationState::Unknown;
        s_registersByUnit[i].rootTable = intel_dmar::RootTableState::Unknown;
        s_unitInspected[i] = false;
    }
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

    for (uint32_t i = 0u; i < snapshot.storedControllerCount; ++i) {
        const modern_input_diagnostics::ControllerSnapshot& controller =
            snapshot.controllers[i];
        if (!controller.occupied ||
            controller.kind != modern_input_diagnostics::ControllerKind::XHCI ||
            !modern_input_diagnostics::intel_dmar_route_diagnostics_applies(
                controller.vendorId, snapshot.dmarTablePresent)) {
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
        if (i < modern_input_diagnostics::MAX_CONTROLLERS) {
            s_paths[i] = path;
            s_pathValid[i] = path.valid;
            s_routes[i] = route;
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
        if (!s_unitInspected[unitIndex]) {
            s_registersByUnit[unitIndex] = inspect_unit(s_table.drhd[unitIndex]);
            s_unitInspected[unitIndex] = true;
            const intel_dmar::RegisterSnapshot& registers = s_registersByUnit[unitIndex];
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
                serial::puts(" GSTS=");
                serial::put_hex32(registers.globalStatus);
                serial::puts(" RTADDR=");
                serial::put_hex64(registers.rootTableAddress);
                serial::puts(" FSTS=");
                serial::put_hex32(registers.faultStatus);
                serial::puts(" PMEN-state=");
                serial::puts(registers.protectedMemoryState ==
                                     intel_dmar::ProtectedMemoryState::Disabled
                                 ? "disabled"
                                 : (registers.protectedMemoryState ==
                                            intel_dmar::ProtectedMemoryState::Enabled
                                        ? "enabled"
                                        : (registers.protectedMemoryState ==
                                                   intel_dmar::ProtectedMemoryState::Invalid
                                               ? "invalid"
                                               : "unknown")));
                serial::puts(" PMEN=0x");
                serial::put_hex32(registers.pmen);
                serial::puts(" PLMB=0x");
                serial::put_hex32(registers.protectedLowBaseRaw);
                serial::puts(" PLML=0x");
                serial::put_hex32(registers.protectedLowLimitRaw);
                serial::puts(" PHMB=0x");
                serial::put_hex64(registers.protectedHighBaseRaw);
                serial::puts(" PHML=0x");
                serial::put_hex64(registers.protectedHighLimitRaw);
                serial::puts("\n");
            }
        }
        modern_input_diagnostics::record_vtd_registers(
            static_cast<int32_t>(i), s_registersByUnit[unitIndex]);

        if (static_cast<int32_t>(i) == snapshot.selectedControllerIndex &&
            snapshot.dmarTablePresent) {
            const intel_dmar::RegisterSnapshot& registers = s_registersByUnit[unitIndex];
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

intel_dmar::DirectDmaDecision qualify_direct_physical_dma(
    int32_t controllerIndex, const dma_memory::MemoryMapView& memoryMap,
    const dma_memory::PhysicalRange* dmaRanges, uint32_t dmaRangeCount,
    bool busMasterDisabled, bool deviceAddressabilityValidated,
    bool workspaceExclusive, intel_dmar::RmrrMemorySummary* rmrrSummary)
{
    intel_dmar::DirectDmaEvidence evidence{};
    evidence.route = intel_dmar::RouteStatus::Unknown;
    evidence.registers = intel_dmar::RegisterStatus::NotAttempted;
    evidence.translation = intel_dmar::TranslationState::Unknown;
    evidence.rootTable = intel_dmar::RootTableState::Unknown;
    evidence.rmrrMemory = intel_dmar::RmrrMemoryStatus::InvalidDmar;
    evidence.busMasterDisabled = busMasterDisabled;
    evidence.workspaceExclusive = workspaceExclusive;
    if (rmrrSummary != nullptr) {
        *rmrrSummary = {};
        rmrrSummary->status = intel_dmar::RmrrMemoryStatus::InvalidDmar;
    }

    if (controllerIndex < 0 ||
        static_cast<uint32_t>(controllerIndex) >=
            modern_input_diagnostics::MAX_CONTROLLERS ||
        !s_pathValid[static_cast<uint32_t>(controllerIndex)]) {
        return intel_dmar::evaluate_direct_physical_dma(evidence);
    }
    const uint32_t index = static_cast<uint32_t>(controllerIndex);
    modern_input_diagnostics::Snapshot snapshot = {};
    modern_input_diagnostics::copy_snapshot(&snapshot);
    if (index >= snapshot.storedControllerCount) {
        return intel_dmar::evaluate_direct_physical_dma(evidence);
    }

    const modern_input_diagnostics::ControllerSnapshot& controller =
        snapshot.controllers[index];
    const intel_dmar::PciPath& path = s_paths[index];
    const intel_dmar::Route& route = s_routes[index];
    const uint32_t liveIdentity = msi::pci_config_read32(
        controller.bus, controller.device, controller.function, 0u);
    const uint32_t liveClassRevision = msi::pci_config_read32(
        controller.bus, controller.device, controller.function,
        PCI_CLASS_REVISION_OFFSET);
    const uint16_t liveVendor = static_cast<uint16_t>(liveIdentity & 0xFFFFu);
    const uint16_t liveDevice = static_cast<uint16_t>(liveIdentity >> 16u);
    const uint8_t liveProgIf = static_cast<uint8_t>(liveClassRevision >> 8u);
    const uint8_t liveSubClass = static_cast<uint8_t>(liveClassRevision >> 16u);
    const uint8_t liveBaseClass = static_cast<uint8_t>(liveClassRevision >> 24u);
    intel_dmar::PciPath livePath = {};
    const bool livePathValidated = build_pci_path(
        controller.pciSegment, controller.bus, controller.device,
        controller.function, &livePath) && same_pci_path(path, livePath);
    evidence.pciIdentityValidated = controller.occupied && path.segment == 0u &&
        controller.kind == modern_input_diagnostics::ControllerKind::XHCI &&
        controller.vendorId == 0x8086u && controller.baseClass == 0x0Cu &&
        controller.subClass == 0x03u && controller.progIf == 0x30u &&
        liveIdentity != UINT32_MAX && liveVendor == controller.vendorId &&
        liveDevice == controller.deviceId && liveBaseClass == 0x0Cu &&
        liveSubClass == 0x03u && liveProgIf == 0x30u && livePathValidated &&
        path.valid && path.count != 0u && path.segment == controller.pciSegment &&
        path.entries[path.count - 1u].bus == controller.bus &&
        path.entries[path.count - 1u].device == controller.device &&
        path.entries[path.count - 1u].function == controller.function;
    evidence.route = route.status;
    if (route.unitIndex >= 0 &&
        static_cast<uint32_t>(route.unitIndex) < s_table.drhdCount) {
        const uint32_t unitIndex = static_cast<uint32_t>(route.unitIndex);
        // Re-read all relevant VT-d state after controller halt and BME-off,
        // immediately before direct-physical DMA admission. The earlier scan
        // is diagnostic context only and may be stale by this point.
        s_registersByUnit[unitIndex] = inspect_unit(s_table.drhd[unitIndex]);
        s_unitInspected[unitIndex] = true;
        const intel_dmar::RegisterSnapshot& registers =
            s_registersByUnit[unitIndex];
        evidence.registers = registers.status;
        evidence.translation = registers.translation;
        evidence.rootTable = registers.rootTable;
        evidence.protectedMemory = registers.protectedMemoryState;
        evidence.protectedMemoryOverlap =
            intel_dmar::protected_memory_overlaps_workspace(
                registers, dmaRanges, dmaRangeCount);
        modern_input_diagnostics::record_vtd_registers(
            controllerIndex, registers);
        serial::puts("[VTD] DMA admission recheck PMEN=0x");
        serial::put_hex32(registers.pmen);
        serial::puts(" PLMB=0x");
        serial::put_hex32(registers.protectedLowBaseRaw);
        serial::puts(" PLML=0x");
        serial::put_hex32(registers.protectedLowLimitRaw);
        serial::puts(" PHMB=0x");
        serial::put_hex64(registers.protectedHighBaseRaw);
        serial::puts(" PHML=0x");
        serial::put_hex64(registers.protectedHighLimitRaw);
        serial::puts(" state=");
        serial::puts(registers.protectedMemoryState ==
                              intel_dmar::ProtectedMemoryState::Disabled
                          ? "disabled"
                          : (registers.protectedMemoryState ==
                                     intel_dmar::ProtectedMemoryState::Enabled
                                 ? "enabled"
                                 : (registers.protectedMemoryState ==
                                            intel_dmar::ProtectedMemoryState::Invalid
                                        ? "invalid"
                                        : "unknown")));
        serial::puts(" overlap=");
        serial::puts(evidence.protectedMemoryOverlap ? "yes\n" : "no\n");
    }

    evidence.physicalBackingValidated = dmaRanges != nullptr &&
        dmaRangeCount != 0u && dma_memory::valid_view(memoryMap);
    for (uint32_t i = 0u; evidence.physicalBackingValidated &&
         i < dmaRangeCount; ++i) {
        evidence.physicalBackingValidated =
            dma_memory::range_has_ownership(memoryMap, dmaRanges[i].base,
                dmaRanges[i].length, false, 64u) == dma_memory::MemoryMapCheck::Valid;
    }
    const uint16_t liveCommand = msi::pci_config_read16(
        controller.bus, controller.device, controller.function, 0x04u);
    evidence.busMasterDisabled = busMasterDisabled &&
        (liveCommand & 0x0004u) == 0u;
    evidence.deviceAddressabilityValidated = deviceAddressabilityValidated;

    const intel_dmar::RmrrMemorySummary summary =
        intel_dmar::validate_rmrr_memory(
            s_table, path, memoryMap, dmaRanges, dmaRangeCount);
    evidence.rmrrMemory = summary.status;
    if (rmrrSummary != nullptr) *rmrrSummary = summary;
    modern_input_diagnostics::record_vtd_rmrr_memory(controllerIndex, summary);
    for (uint32_t i = 0u; i < s_table.rmrrCount; ++i) {
        const intel_dmar::RmrrRegion& region = s_table.rmrr[i];
        bool applies = false;
        if (region.segment == path.segment) {
            for (uint32_t j = 0u; j < region.scopeCount; ++j) {
                if (intel_dmar::scope_matches_path(region.scopes[j], path)) {
                    applies = true;
                    break;
                }
            }
        }
        const dma_memory::MemoryMapCheck ownerCheck =
            dma_memory::range_has_ownership(
                memoryMap, region.base, region.length, true);
        bool workspaceOverlap = false;
        if (dmaRanges != nullptr) {
            const dma_memory::PhysicalRange rmrrRange = {
                region.base, region.length};
            for (uint32_t j = 0u; j < dmaRangeCount; ++j) {
                if (dma_memory::ranges_overlap(rmrrRange, dmaRanges[j])) {
                    workspaceOverlap = true;
                    break;
                }
            }
        }
        serial::puts("[VTD] RMRR index=");
        serial::put_hex32(i);
        serial::puts(" segment=");
        serial::put_hex16(region.segment);
        serial::puts(" base=");
        serial::put_hex64(region.base);
        serial::puts(" limit=");
        serial::put_hex64(region.limit);
        serial::puts(" scope=");
        serial::puts(applies ? "SELECTED_XHCI" : "UNRELATED_TO_SELECTED_XHCI");
        serial::puts(" owner=");
        serial::puts(memory_map_check_name(ownerCheck));
        serial::puts(" workspace-overlap=");
        serial::puts(workspaceOverlap ? "yes" : "no");
        serial::puts("\n");
    }
    return intel_dmar::evaluate_direct_physical_dma(evidence);
}

} // namespace intel_vtd
} // namespace kernel
