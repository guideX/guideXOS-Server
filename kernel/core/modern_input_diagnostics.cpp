#include "include/kernel/modern_input_diagnostics.h"

namespace kernel {
namespace modern_input_diagnostics {
namespace {

static Snapshot s_snapshot = {};
static uint8_t s_snapshotLock = 0u;
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
static uint64_t s_usbScanOperationSequence = 0u;
#endif

static void publish_snapshot_change_locked();

static uint64_t next_startup_operation_sequence_locked()
{
    if (s_snapshot.startupOperationSequence != UINT64_MAX) {
        ++s_snapshot.startupOperationSequence;
    }
    return s_snapshot.startupOperationSequence;
}

static uint64_t begin_startup_operation_locked(
    StartupOperation operation, const StartupOperationContext* context)
{
    if (operation == StartupOperation::None) return 0u;

    if (operation == StartupOperation::PciAudioEntry) {
        s_snapshot.pciAudioStatus = StartupOperationStatus::InProgress;
    } else if (operation == StartupOperation::UsbHostScanEnter) {
        s_snapshot.usbHostScanStatus = StartupOperationStatus::InProgress;
    } else if (operation == StartupOperation::XhciInitEnter) {
        s_snapshot.xhciInitStatus = StartupOperationStatus::InProgress;
    }

    StartupOperationRecord record = {};
    record.operation = operation;
    record.status = StartupOperationStatus::InProgress;
    record.enterSequence = next_startup_operation_sequence_locked();
    if (context != nullptr) {
        record.bus = context->bus;
        record.device = context->device;
        record.function = context->function;
        record.vendorId = context->vendorId;
        record.deviceId = context->deviceId;
        record.baseClass = context->baseClass;
        record.subClass = context->subClass;
        record.progIf = context->progIf;
        record.configOffset = context->configOffset;
        record.hasPciAddress = context->hasPciAddress;
        record.hasVendorDevice = context->hasVendorDevice;
        record.hasClass = context->hasClass;
        record.hasConfigOffset = context->hasConfigOffset;
    }

    const uint32_t slot = s_snapshot.startupOperationTraceNext;
    s_snapshot.startupOperationTrace[slot] = record;
    s_snapshot.startupOperationTraceNext =
        (slot + 1u) % MAX_STARTUP_OPERATION_TRACE;
    if (s_snapshot.startupOperationTraceCount < MAX_STARTUP_OPERATION_TRACE) {
        ++s_snapshot.startupOperationTraceCount;
    }
    s_snapshot.lastStartupOperation = record;
    publish_snapshot_change_locked();
    return record.enterSequence;
}

static void publish_snapshot_change_locked()
{
    if (s_snapshot.snapshotSequence != UINT64_MAX) ++s_snapshot.snapshotSequence;
}

static void lock_snapshot()
{
    while (__atomic_exchange_n(&s_snapshotLock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
}

static void unlock_snapshot()
{
    __atomic_store_n(&s_snapshotLock, 0u, __ATOMIC_RELEASE);
}

static bool stage_valid(Stage stage)
{
    return static_cast<uint32_t>(stage) < STAGE_COUNT;
}

static ControllerSnapshot* controller_for_index(int32_t index)
{
    if (index < 0 || static_cast<uint32_t>(index) >= s_snapshot.storedControllerCount) {
        return nullptr;
    }
    ControllerSnapshot& controller = s_snapshot.controllers[static_cast<uint32_t>(index)];
    return controller.occupied && controller.discoveryIndex == static_cast<uint32_t>(index)
        ? &controller : nullptr;
}

static uint32_t append_char(char* out, uint32_t capacity, uint32_t position, char value)
{
    if (out == nullptr || capacity == 0u) return position;
    if (position + 1u < capacity) {
        out[position] = value;
        out[position + 1u] = '\0';
        return position + 1u;
    }
    out[capacity - 1u] = '\0';
    return position;
}

static uint32_t append_text(char* out, uint32_t capacity, uint32_t position,
                            const char* text)
{
    if (text == nullptr) return position;
    while (*text != '\0' && position + 1u < capacity) {
        out[position++] = *text++;
    }
    if (capacity != 0u && out != nullptr) out[position < capacity ? position : capacity - 1u] = '\0';
    return position;
}

static uint32_t append_decimal(char* out, uint32_t capacity, uint32_t position,
                               uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u) position = append_char(out, capacity, position, digits[--count]);
    return position;
}

static uint32_t append_decimal64(char* out, uint32_t capacity, uint32_t position,
                                 uint64_t value)
{
    char digits[20];
    uint32_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u) position = append_char(out, capacity, position, digits[--count]);
    return position;
}

static uint32_t append_hex(char* out, uint32_t capacity, uint32_t position,
                           uint32_t value, uint32_t digits)
{
    static const char kHex[] = "0123456789abcdef";
    for (uint32_t i = 0u; i < digits; ++i) {
        const uint32_t shift = (digits - i - 1u) * 4u;
        position = append_char(out, capacity, position, kHex[(value >> shift) & 0xFu]);
    }
    return position;
}

static uint32_t append_bdf(char* out, uint32_t capacity, uint32_t position,
                           const ControllerSnapshot& controller)
{
    position = append_hex(out, capacity, position, controller.bus, 2u);
    position = append_char(out, capacity, position, ':');
    position = append_hex(out, capacity, position, controller.device, 2u);
    position = append_char(out, capacity, position, '.');
    return append_decimal(out, capacity, position, controller.function);
}

static void initialize_controller(ControllerSnapshot* controller)
{
    if (controller == nullptr) return;
    controller->discoveryIndex = 0u;
    controller->bus = 0u;
    controller->device = 0u;
    controller->function = 0u;
    controller->pciSegment = 0u;
    controller->requesterId = 0u;
    controller->effectiveRequesterId = 0u;
    controller->iommuUnitIndex = -1;
    controller->iommuDeviceId = 0u;
    controller->iommuBase = 0u;
    controller->iommuRouteStatus = amd_ivrs::RouteStatus::Unmatched;
    controller->iommuInspectionStatus = amd_ivrs::InspectionStatus::NotAttempted;
    controller->iommuTranslationState = amd_ivrs::TranslationState::Unknown;
    controller->iommuControlRegisterValid = false;
    controller->iommuStatusRegisterValid = false;
    controller->baseClass = 0u;
    controller->subClass = 0u;
    controller->progIf = 0u;
    controller->vendorId = 0u;
    controller->deviceId = 0u;
    controller->kind = ControllerKind::Other;
    controller->ownership = OwnershipStatus::Unknown;
    controller->occupied = false;
    controller->mmioBase = 0u;
    controller->pciBar0 = 0u;
    controller->pciBar1 = 0u;
    controller->pciCommand = 0u;
    controller->pciCommandInitial = 0u;
    controller->pciCommandInitialCaptured = false;
    controller->pciBarType = 0u;
    controller->pciBarIsIo = false;
    controller->pciBarIs64 = false;
    controller->pciBarPrefetchable = false;
    controller->mmioRequiredSpan = 0u;
    controller->mmioMappedVirtual = 0u;
    controller->mmioMappedLength = 0u;
    controller->mmioMappingSucceeded = false;
    controller->mmioMappingReason = nullptr;
    controller->capLength = 0u;
    controller->hciVersion = 0u;
    controller->maxSlots = 0u;
    controller->maxPorts = 0u;
    controller->contextSize = 0u;
    controller->supportedProtocolCount = 0u;
    controller->scratchpadBuffersRequired = 0u;
    controller->hcsParams2 = 0u;
    controller->scratchpadHigh = 0u;
    controller->scratchpadLow = 0u;
    controller->scratchpadCapacity = 0u;
    controller->scratchpadAllocationStatus = Status::NotAttempted;
    controller->scratchpadAllocationReason = nullptr;
    controller->vtdRouteStatus = intel_dmar::RouteStatus::Unknown;
    controller->vtdUnitIndex = -1;
    controller->vtdRegisterBase = 0u;
    controller->vtdRmrrRequired = false;
    controller->vtdRegisterStatus = intel_dmar::RegisterStatus::NotAttempted;
    controller->vtdTranslationState = intel_dmar::TranslationState::Unknown;
    controller->vtdRootTableState = intel_dmar::RootTableState::Unknown;
    controller->vtdVersion = 0u;
    controller->vtdMaximumGuestAddressWidth = 0u;
    controller->vtdSupportedAdjustedGuestAddressWidths = 0u;
    controller->vtdGlobalCommand = 0u;
    controller->vtdGlobalStatus = 0u;
    controller->vtdFaultStatus = 0u;
    controller->vtdCapability = 0u;
    controller->vtdExtendedCapability = 0u;
    controller->vtdRootTableAddress = 0u;
    controller->vtdCapabilityValid = false;
    controller->vtdExtendedCapabilityValid = false;
    controller->vtdCommandStatusValid = false;
    controller->vtdRootTableValid = false;
    controller->vtdFaultStatusValid = false;
    controller->ac64 = false;
    controller->iommuStatus = IommuStatus::Unknown;
    controller->rootPortCount = 0u;
    controller->connectedPortCount = 0u;
    controller->selectedPort = 0u;
    controller->descriptorTransfersSucceeded = 0u;
    controller->vendorDescriptor = 0u;
    controller->productDescriptor = 0u;
    controller->deviceDescriptorValid = false;
    for (uint32_t i = 0u; i < 5u; ++i) controller->inputCounters[i] = 0u;
    for (uint32_t i = 0u; i < STAGE_COUNT; ++i) {
        controller->stages[i] = {};
        controller->stages[i].status = Status::NotAttempted;
    }
}

} // namespace

void initialize()
{
    lock_snapshot();
    s_snapshot = {};
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    s_usbScanOperationSequence = 0u;
#endif
    s_snapshot.bootStage = BootStage::Unknown;
    s_snapshot.bootSequence = 0u;
    s_snapshot.snapshotSequence = 1u;
    s_snapshot.displayedSequence = 0u;
    s_snapshot.pciEnumeration = Status::NotAttempted;
    s_snapshot.pciScanState = PciScanState::NotInvoked;
    s_snapshot.pciScanFailure = nullptr;
    s_snapshot.selectedControllerIndex = -1;
    s_snapshot.latestControllerIndex = -1;
    s_snapshot.latestStage = Stage::PciDiscovery;
    s_snapshot.latestStatus = {};
    s_snapshot.latestStatus.status = Status::NotAttempted;
    s_snapshot.firstFailure = {};
    s_snapshot.firstFailure.controllerIndex = -1;
    s_snapshot.ivrsStatus = amd_ivrs::ParseStatus::NotPresent;
    s_snapshot.selectedIommuRouteStatus = amd_ivrs::RouteStatus::Unmatched;
    s_snapshot.selectedIommuInspectionStatus = amd_ivrs::InspectionStatus::NotAttempted;
    s_snapshot.selectedIommuTranslationState = amd_ivrs::TranslationState::Unknown;
    s_snapshot.ivrsTableCount = 0u;
    s_snapshot.ivhdUnitCount = 0u;
    s_snapshot.ivmdRangeCount = 0u;
    s_snapshot.ivrsUnsupportedCount = 0u;
    s_snapshot.platform = Platform::Unknown;
    s_snapshot.dmarStatus = intel_dmar::ParseStatus::NotPresent;
    s_snapshot.dmarTableCount = 0u;
    s_snapshot.drhdUnitCount = 0u;
    s_snapshot.rmrrCount = 0u;
    s_snapshot.atsrCount = 0u;
    s_snapshot.rhsaCount = 0u;
    s_snapshot.dmarUnsupportedCount = 0u;
    s_snapshot.dmarScanValid = false;
    s_snapshot.dmarTablePresent = false;
    s_snapshot.acpiRemapScanValid = false;
    s_snapshot.acpiRemappingTablePresent = false;
    s_snapshot.dmaMappingEstablished = false;
    s_snapshot.dmaDecisionReason = "IOMMU_STATE_UNKNOWN";
    s_snapshot.lastStartupOperation = {};
    s_snapshot.lastStartupOperation.status = StartupOperationStatus::NotInvoked;
    s_snapshot.lastCompletedStartupOperation = {};
    s_snapshot.lastCompletedStartupOperation.status = StartupOperationStatus::NotInvoked;
    for (uint32_t i = 0u; i < MAX_STARTUP_OPERATION_TRACE; ++i) {
        s_snapshot.startupOperationTrace[i] = {};
        s_snapshot.startupOperationTrace[i].status = StartupOperationStatus::NotInvoked;
    }
    for (uint32_t i = 0u; i < MAX_CONTROLLERS; ++i) {
        initialize_controller(&s_snapshot.controllers[i]);
    }
    unlock_snapshot();
}

uint64_t begin_startup_operation(StartupOperation operation,
                                 const StartupOperationContext* context)
{
    lock_snapshot();
    const uint64_t sequence = begin_startup_operation_locked(operation, context);
    unlock_snapshot();
    return sequence;
}

void complete_startup_operation(uint64_t enterSequence,
                                StartupOperationStatus status,
                                uint32_t result, bool hasResult,
                                uint32_t errorCode, bool hasErrorCode)
{
    if (enterSequence == 0u || status == StartupOperationStatus::NotInvoked ||
        status == StartupOperationStatus::InProgress) return;

    lock_snapshot();
    StartupOperationRecord* found = nullptr;
    for (uint32_t i = 0u; i < s_snapshot.startupOperationTraceCount; ++i) {
        StartupOperationRecord& candidate = s_snapshot.startupOperationTrace[i];
        if (candidate.enterSequence == enterSequence) {
            found = &candidate;
            break;
        }
    }
    if (found == nullptr && s_snapshot.lastStartupOperation.enterSequence == enterSequence) {
        found = &s_snapshot.lastStartupOperation;
    }
    if (found != nullptr && found->exitSequence == 0u) {
        found->exitSequence = next_startup_operation_sequence_locked();
        found->status = status;
        found->result = result;
        found->hasResult = hasResult;
        found->errorCode = errorCode;
        found->hasErrorCode = hasErrorCode;
        if (found->operation == StartupOperation::PciAudioEntry) {
            s_snapshot.pciAudioStatus = status;
        } else if (found->operation == StartupOperation::UsbHostScanEnter ||
                   found->operation == StartupOperation::UsbHostScanComplete) {
            s_snapshot.usbHostScanStatus = status;
        } else if (found->operation == StartupOperation::XhciInitEnter ||
                   found->operation == StartupOperation::XhciInitComplete) {
            s_snapshot.xhciInitStatus = status;
        }
        if (found->operation == StartupOperation::PciAudioDeviceFound &&
            status == StartupOperationStatus::Completed &&
            s_snapshot.pciAudioDeviceCount != UINT32_MAX) {
            ++s_snapshot.pciAudioDeviceCount;
        }
        if (s_snapshot.lastStartupOperation.enterSequence == enterSequence) {
            s_snapshot.lastStartupOperation = *found;
        }
        s_snapshot.lastCompletedStartupOperation = *found;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_pci_audio_skipped()
{
    lock_snapshot();
    s_snapshot.pciAudioSkipped = true;
    s_snapshot.pciAudioStatus = StartupOperationStatus::Unsupported;
    const uint64_t sequence = begin_startup_operation_locked(
        StartupOperation::PciAudioSkipped, nullptr);
    if (sequence != 0u) {
        // This is an explicit diagnostic choice, not audio initialization.
        StartupOperationRecord* found = nullptr;
        for (uint32_t i = 0u; i < s_snapshot.startupOperationTraceCount; ++i) {
            if (s_snapshot.startupOperationTrace[i].enterSequence == sequence) {
                found = &s_snapshot.startupOperationTrace[i];
                break;
            }
        }
        if (found != nullptr) {
            found->exitSequence = next_startup_operation_sequence_locked();
            found->status = StartupOperationStatus::Unsupported;
            s_snapshot.lastStartupOperation = *found;
            s_snapshot.lastCompletedStartupOperation = *found;
        }
    }
    publish_snapshot_change_locked();
    unlock_snapshot();
}

void record_boot_milestone(BootStage stage)
{
    if (stage <= BootStage::Unknown || stage > BootStage::MainLoopEntered) return;

    lock_snapshot();
    if (s_snapshot.bootSequence != UINT64_MAX) ++s_snapshot.bootSequence;
    s_snapshot.bootStage = stage;
    publish_snapshot_change_locked();
    unlock_snapshot();
}

bool presentation_pending()
{
    lock_snapshot();
    const bool pending = s_snapshot.snapshotSequence > s_snapshot.displayedSequence;
    unlock_snapshot();
    return pending;
}

bool record_panel_presented(uint64_t snapshotSequence)
{
    if (snapshotSequence == 0u) return false;
    bool advanced = false;
    lock_snapshot();
    if (snapshotSequence > s_snapshot.displayedSequence) {
        s_snapshot.displayedSequence = snapshotSequence;
        advanced = true;
    }
    unlock_snapshot();

    return advanced;
}

void begin_pci_enumeration()
{
    lock_snapshot();
    s_snapshot.pciEnumeration = Status::NotAttempted;
    s_snapshot.pciScanState = PciScanState::EntryReached;
    s_snapshot.pciScanFailure = nullptr;
    s_snapshot.usbControllerCount = 0u;
    s_snapshot.xhciControllerCount = 0u;
    s_snapshot.storedControllerCount = 0u;
    s_snapshot.controllersOmitted = 0u;
    s_snapshot.selectedControllerIndex = -1;
    s_snapshot.latestControllerIndex = -1;
    s_snapshot.latestStage = Stage::PciDiscovery;
    s_snapshot.latestStatus = {};
    s_snapshot.latestStatus.status = Status::NotAttempted;
    for (uint32_t i = 0u; i < MAX_CONTROLLERS; ++i) {
        initialize_controller(&s_snapshot.controllers[i]);
    }
    publish_snapshot_change_locked();
    unlock_snapshot();

#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    s_usbScanOperationSequence = begin_startup_operation(
        StartupOperation::UsbHostScanEnter, nullptr);
#endif
}

void complete_pci_enumeration(Status status, const char* failureReason)
{
    const bool failed = status == Status::Fail || status == Status::Timeout;
    const char* resolvedFailure = failureReason;
    if (resolvedFailure == nullptr && status == Status::Unsupported) {
        resolvedFailure = "PCI configuration access is unsupported on this architecture";
    } else if (resolvedFailure == nullptr && status != Status::Pass) {
        resolvedFailure = "PCI discovery did not complete successfully";
    }

#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    const StartupOperationStatus operationStatus = status == Status::Pass
        ? StartupOperationStatus::Completed
        : (status == Status::Timeout ? StartupOperationStatus::TimedOut
           : (status == Status::Unsupported ? StartupOperationStatus::Unsupported
                                            : StartupOperationStatus::ReturnedError));
    complete_startup_operation(s_usbScanOperationSequence, operationStatus);
    s_usbScanOperationSequence = 0u;
    const uint64_t completeSequence = begin_startup_operation(
        StartupOperation::UsbHostScanComplete, nullptr);
    complete_startup_operation(completeSequence, operationStatus);
#endif

    lock_snapshot();
    s_snapshot.pciEnumeration = status;
    if (status == Status::Pass) {
        s_snapshot.pciScanState = s_snapshot.usbControllerCount == 0u
            ? PciScanState::NoControllers : PciScanState::ScanComplete;
        s_snapshot.pciScanFailure = nullptr;
    } else {
        s_snapshot.pciScanState = PciScanState::Failure;
        s_snapshot.pciScanFailure = resolvedFailure;
    }
    s_snapshot.latestControllerIndex = -1;
    s_snapshot.latestStage = Stage::PciDiscovery;
    s_snapshot.latestStatus = {};
    s_snapshot.latestStatus.status = status;
    s_snapshot.latestStatus.detail = resolvedFailure;
    publish_snapshot_change_locked();
    unlock_snapshot();

    if (failed) {
        record_stage(-1, Stage::PciDiscovery, status, 0u, false,
                     0u, false, 0u, 0u, 0u, 0u, false,
                     resolvedFailure);
    }
}

int32_t record_controller(uint8_t bus, uint8_t device, uint8_t function,
                          uint16_t vendorId, uint16_t deviceId,
                          uint8_t baseClass, uint8_t subClass, uint8_t progIf,
                          ControllerKind kind)
{
    lock_snapshot();
    const uint32_t index = s_snapshot.usbControllerCount;
    if (s_snapshot.usbControllerCount != 0xFFFFFFFFu) ++s_snapshot.usbControllerCount;
    if (kind == ControllerKind::XHCI && s_snapshot.xhciControllerCount != 0xFFFFFFFFu) {
        ++s_snapshot.xhciControllerCount;
    }
    if (index >= MAX_CONTROLLERS) {
        if (s_snapshot.controllersOmitted != 0xFFFFFFFFu) ++s_snapshot.controllersOmitted;
        publish_snapshot_change_locked();
        unlock_snapshot();
        return -1;
    }

    ControllerSnapshot* controller = &s_snapshot.controllers[index];
    initialize_controller(controller);
    controller->occupied = true;
    controller->discoveryIndex = index;
    controller->bus = bus;
    controller->device = device;
    controller->function = function;
    controller->vendorId = vendorId;
    controller->deviceId = deviceId;
    controller->baseClass = baseClass;
    controller->subClass = subClass;
    controller->progIf = progIf;
    controller->kind = kind;
    s_snapshot.storedControllerCount = index + 1u;
    publish_snapshot_change_locked();
    unlock_snapshot();
    return static_cast<int32_t>(index);
}

void record_ivrs_discovery(amd_ivrs::ParseStatus status, bool scanValid,
                           bool remappingTablePresent, uint32_t ivrsTableCount,
                           uint32_t ivhdUnits, uint32_t ivmdRanges,
                           uint32_t unsupportedCount)
{
    lock_snapshot();
    s_snapshot.ivrsStatus = status;
    s_snapshot.acpiRemapScanValid = scanValid;
    s_snapshot.acpiRemappingTablePresent = remappingTablePresent;
    s_snapshot.ivrsTableCount = ivrsTableCount;
    s_snapshot.ivhdUnitCount = ivhdUnits;
    s_snapshot.ivmdRangeCount = ivmdRanges;
    s_snapshot.ivrsUnsupportedCount = unsupportedCount;
    publish_snapshot_change_locked();
    unlock_snapshot();
}

void record_platform(Platform platform)
{
    lock_snapshot();
    s_snapshot.platform = platform;
    publish_snapshot_change_locked();
    unlock_snapshot();
}

void record_dmar_discovery(intel_dmar::ParseStatus status, bool scanValid,
                           bool remappingTablePresent, uint32_t dmarTableCount,
                           uint32_t drhdUnits, uint32_t rmrrRanges,
                           uint32_t atsrStructures, uint32_t rhsaStructures,
                           uint32_t unsupportedCount)
{
    lock_snapshot();
    s_snapshot.dmarStatus = status;
    s_snapshot.dmarScanValid = scanValid;
    s_snapshot.dmarTablePresent = remappingTablePresent;
    s_snapshot.dmarTableCount = dmarTableCount;
    s_snapshot.drhdUnitCount = drhdUnits;
    s_snapshot.rmrrCount = rmrrRanges;
    s_snapshot.atsrCount = atsrStructures;
    s_snapshot.rhsaCount = rhsaStructures;
    s_snapshot.dmarUnsupportedCount = unsupportedCount;
    publish_snapshot_change_locked();
    unlock_snapshot();
}

void record_vtd_route(int32_t controllerIndex, uint16_t segment,
                      uint8_t bus, uint8_t device, uint8_t function,
                      intel_dmar::RouteStatus routeStatus, int32_t unitIndex,
                      uint64_t registerBase, bool rmrrRequired)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->pciSegment = segment;
        controller->vtdRouteStatus = routeStatus;
        controller->vtdUnitIndex = unitIndex;
        controller->vtdRegisterBase = registerBase;
        controller->vtdRmrrRequired = rmrrRequired;
        controller->requesterId = static_cast<uint16_t>(
            (static_cast<uint16_t>(bus) << 8u) |
            (static_cast<uint16_t>(device) << 3u) | (function & 7u));
        if (controllerIndex == s_snapshot.selectedControllerIndex) {
            s_snapshot.platform = Platform::Intel;
        }
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_vtd_registers(int32_t controllerIndex,
                          const intel_dmar::RegisterSnapshot& registers)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->vtdRegisterStatus = registers.status;
        controller->vtdTranslationState = registers.translation;
        controller->vtdRootTableState = registers.rootTable;
        controller->vtdVersion = registers.version;
        controller->vtdMaximumGuestAddressWidth = registers.maximumGuestAddressWidth;
        controller->vtdSupportedAdjustedGuestAddressWidths =
            registers.supportedAdjustedGuestAddressWidths;
        controller->vtdGlobalCommand = registers.globalCommand;
        controller->vtdGlobalStatus = registers.globalStatus;
        controller->vtdFaultStatus = registers.faultStatus;
        controller->vtdCapability = registers.capability;
        controller->vtdExtendedCapability = registers.extendedCapability;
        controller->vtdRootTableAddress = registers.rootTableAddress;
        controller->vtdCapabilityValid = registers.capabilityValid;
        controller->vtdExtendedCapabilityValid = registers.extendedCapabilityValid;
        controller->vtdCommandStatusValid = registers.commandStatusValid;
        controller->vtdRootTableValid = registers.rootTableValid;
        controller->vtdFaultStatusValid = registers.faultStatusValid;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_scratchpad_allocation(int32_t controllerIndex, uint32_t capacity,
                                  Status status, const char* reason)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->scratchpadCapacity = capacity;
        controller->scratchpadAllocationStatus = status;
        controller->scratchpadAllocationReason = reason;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_iommu_route(int32_t controllerIndex, uint16_t segment,
                        uint16_t requesterId, uint16_t effectiveRequesterId,
                        amd_ivrs::RouteStatus routeStatus, int32_t unitIndex,
                        uint16_t iommuDeviceId, uint64_t iommuBase)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->pciSegment = segment;
        controller->requesterId = requesterId;
        controller->effectiveRequesterId = effectiveRequesterId;
        controller->iommuRouteStatus = routeStatus;
        controller->iommuUnitIndex = unitIndex;
        controller->iommuDeviceId = iommuDeviceId;
        controller->iommuBase = iommuBase;
        if (controllerIndex == s_snapshot.selectedControllerIndex) {
            s_snapshot.selectedIommuRouteStatus = routeStatus;
        }
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_iommu_registers(int32_t controllerIndex,
                            amd_ivrs::InspectionStatus inspectionStatus,
                            amd_ivrs::TranslationState translationState,
                            bool controlRegisterValid,
                            bool statusRegisterValid)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->iommuInspectionStatus = inspectionStatus;
        controller->iommuTranslationState = translationState;
        controller->iommuControlRegisterValid = controlRegisterValid;
        controller->iommuStatusRegisterValid = statusRegisterValid;
        if (controllerIndex == s_snapshot.selectedControllerIndex) {
            s_snapshot.selectedIommuInspectionStatus = inspectionStatus;
            s_snapshot.selectedIommuTranslationState = translationState;
        }
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_dma_safety(const char* reason, bool mappingEstablished)
{
    lock_snapshot();
    s_snapshot.dmaDecisionReason = reason;
    s_snapshot.dmaMappingEstablished = mappingEstablished;
    publish_snapshot_change_locked();
    unlock_snapshot();
}

int32_t find_controller(uint8_t bus, uint8_t device, uint8_t function)
{
    int32_t found = -1;
    lock_snapshot();
    for (uint32_t i = 0u; i < s_snapshot.storedControllerCount; ++i) {
        const ControllerSnapshot& controller = s_snapshot.controllers[i];
        if (controller.occupied && controller.bus == bus &&
            controller.device == device && controller.function == function) {
            found = static_cast<int32_t>(i);
            break;
        }
    }
    unlock_snapshot();
    return found;
}

void select_controller(int32_t controllerIndex)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        s_snapshot.selectedControllerIndex = controllerIndex;
        s_snapshot.selectedIommuRouteStatus = controller->iommuRouteStatus;
        s_snapshot.selectedIommuInspectionStatus = controller->iommuInspectionStatus;
        s_snapshot.selectedIommuTranslationState = controller->iommuTranslationState;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_mmio_base(int32_t controllerIndex, uint64_t base)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->mmioBase = base;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_mmio_configuration(int32_t controllerIndex, uint32_t bar0,
                                uint32_t bar1, uint16_t pciCommand,
                                uint8_t barType, bool barIsIo, bool barIs64,
                                bool barPrefetchable)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->pciBar0 = bar0;
        controller->pciBar1 = bar1;
        controller->pciCommand = pciCommand;
        if (!controller->pciCommandInitialCaptured) {
            controller->pciCommandInitial = pciCommand;
            controller->pciCommandInitialCaptured = true;
        }
        controller->pciBarType = barType;
        controller->pciBarIsIo = barIsIo;
        controller->pciBarIs64 = barIs64;
        controller->pciBarPrefetchable = barPrefetchable;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_mmio_mapping(int32_t controllerIndex, const char* substage,
                         uint64_t requiredSpan,
                         bool succeeded, uint64_t mappedVirtual,
                         uint64_t mappedLength, const char* reason)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->mmioRequiredSpan = requiredSpan;
        controller->mmioMappingSucceeded = succeeded;
        controller->mmioMappedVirtual = mappedVirtual;
        controller->mmioMappedLength = mappedLength;
        controller->mmioValidationSubstage = substage;
        controller->mmioMappingReason = reason;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_ownership_status(int32_t controllerIndex, OwnershipStatus status)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->ownership = status;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_capabilities(int32_t controllerIndex, uint8_t capLength,
                         uint16_t hciVersion, uint8_t maxSlots,
                         uint8_t maxPorts, uint8_t contextSize, bool ac64,
                         uint32_t supportedProtocolCount,
                         uint32_t scratchpadBuffersRequired,
                         IommuStatus iommuStatus, uint32_t hcsParams2,
                         uint8_t scratchpadHigh, uint8_t scratchpadLow)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->capLength = capLength;
        controller->hciVersion = hciVersion;
        controller->maxSlots = maxSlots;
        controller->maxPorts = maxPorts;
        controller->contextSize = contextSize;
        controller->ac64 = ac64;
        if (supportedProtocolCount != 0u) {
            controller->supportedProtocolCount = supportedProtocolCount;
        }
        controller->scratchpadBuffersRequired = scratchpadBuffersRequired;
        controller->hcsParams2 = hcsParams2;
        controller->scratchpadHigh = scratchpadHigh;
        controller->scratchpadLow = scratchpadLow;
        controller->iommuStatus = iommuStatus;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_root_ports(int32_t controllerIndex, uint32_t rootPortCount,
                       uint32_t connectedPortCount, uint8_t selectedPort)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        controller->rootPortCount = rootPortCount;
        controller->connectedPortCount = connectedPortCount;
        controller->selectedPort = selectedPort;
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_device_descriptor(int32_t controllerIndex, bool transferSucceeded,
                              bool descriptorValid, uint16_t vendorId,
                              uint16_t productId)
{
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) {
        if (transferSucceeded && controller->descriptorTransfersSucceeded != 0xFFFFFFFFu) {
            ++controller->descriptorTransfersSucceeded;
        }
        if (descriptorValid) {
            controller->deviceDescriptorValid = true;
            controller->vendorDescriptor = vendorId;
            controller->productDescriptor = productId;
        }
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void record_stage(int32_t controllerIndex, Stage stage, Status status,
                  uint32_t errorCode, bool hasErrorCode,
                  uint32_t controllerStatus, bool hasControllerStatus,
                  uint8_t slotId, uint8_t portId, uint8_t endpointId,
                  uint8_t completionCode, bool hasCompletionCode,
                  const char* detail)
{
    if (!stage_valid(stage)) return;
    StageResult result = {};
    result.status = status;
    result.errorCode = errorCode;
    result.controllerStatus = controllerStatus;
    result.slotId = slotId;
    result.portId = portId;
    result.endpointId = endpointId;
    result.completionCode = completionCode;
    result.hasErrorCode = hasErrorCode;
    result.hasControllerStatus = hasControllerStatus;
    result.hasCompletionCode = hasCompletionCode;
    result.detail = detail;

    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr) controller->stages[static_cast<uint32_t>(stage)] = result;
    s_snapshot.latestControllerIndex = controllerIndex;
    s_snapshot.latestStage = stage;
    s_snapshot.latestStatus = result;
    // Unsupported HID discovery/report delivery are implementation frontiers,
    // not hardware failures. Preserve an explicit unsupported DMA prerequisite
    // because it is the condition that prevents controller execution.
    const bool unsupportedPrerequisite =
        status == Status::Unsupported && stage == Stage::DmaPrerequisites;
    if (!s_snapshot.firstFailure.present &&
        (status == Status::Fail || status == Status::Timeout ||
         status == Status::Blocked || unsupportedPrerequisite)) {
        s_snapshot.firstFailure.present = true;
        s_snapshot.firstFailure.controllerIndex = controllerIndex;
        if (controller != nullptr) {
            s_snapshot.firstFailure.hasController = true;
            s_snapshot.firstFailure.bus = controller->bus;
            s_snapshot.firstFailure.device = controller->device;
            s_snapshot.firstFailure.function = controller->function;
            s_snapshot.firstFailure.vendorId = controller->vendorId;
            s_snapshot.firstFailure.deviceId = controller->deviceId;
            s_snapshot.firstFailure.kind = controller->kind;
        }
        s_snapshot.firstFailure.stage = stage;
        s_snapshot.firstFailure.result = result;
    }
    publish_snapshot_change_locked();
    unlock_snapshot();
}

void increment_counter(int32_t controllerIndex, Counter counter)
{
    const uint32_t counterIndex = static_cast<uint32_t>(counter);
    if (counterIndex >= 5u) return;
    lock_snapshot();
    ControllerSnapshot* controller = controller_for_index(controllerIndex);
    if (controller != nullptr && controller->inputCounters[counterIndex] != 0xFFFFFFFFu) {
        ++controller->inputCounters[counterIndex];
        publish_snapshot_change_locked();
    }
    unlock_snapshot();
}

void copy_snapshot(Snapshot* outSnapshot)
{
    if (outSnapshot == nullptr) return;
    lock_snapshot();
    *outSnapshot = s_snapshot;
    unlock_snapshot();
}

const char* status_name(Status status)
{
    switch (status) {
        case Status::NotAttempted: return "NOT ATTEMPTED";
        case Status::Unsupported: return "UNSUPPORTED";
        case Status::Pass: return "PASS";
        case Status::Fail: return "FAIL";
        case Status::Timeout: return "TIMEOUT";
        case Status::Blocked: return "BLOCKED";
        default: return "UNKNOWN";
    }
}

const char* boot_stage_name(BootStage stage)
{
    switch (stage) {
        case BootStage::KernelEntry: return "KERNEL_ENTRY";
        case BootStage::MemoryInitialized: return "MEMORY_INITIALIZED";
        case BootStage::FramebufferReady: return "FRAMEBUFFER_READY";
        case BootStage::PciDiscoveryEntered: return "PCI_DISCOVERY_ENTERED";
        case BootStage::PciDiscoveryCompleted: return "PCI_DISCOVERY_COMPLETED";
        case BootStage::UsbDiscoveryEntered: return "USB_DISCOVERY_ENTERED";
        case BootStage::UsbDiscoveryCompleted: return "USB_DISCOVERY_COMPLETED";
        case BootStage::XhciInitEntered: return "XHCI_INIT_ENTERED";
        case BootStage::XhciInitCompleted: return "XHCI_INIT_COMPLETED";
        case BootStage::DesktopInitialized: return "DESKTOP_INITIALIZED";
        case BootStage::MainLoopEntered: return "MAIN_LOOP_ENTERED";
        default: return "UNKNOWN";
    }
}

const char* startup_operation_name(StartupOperation operation)
{
    switch (operation) {
        case StartupOperation::PciAudioEntry: return "PCI_AUDIO_ENTRY";
        case StartupOperation::PciAudioScanEnter: return "PCI_AUDIO_SCAN_ENTER";
        case StartupOperation::PciConfigAddressSelect: return "PCI_CONFIG_ADDRESS_SELECT";
        case StartupOperation::PciConfigDataRead: return "PCI_CONFIG_DATA_READ";
        case StartupOperation::PciConfigDataWrite: return "PCI_CONFIG_DATA_WRITE";
        case StartupOperation::PciAudioDeviceFound: return "PCI_AUDIO_DEVICE_FOUND";
        case StartupOperation::PciDeviceUnsupported: return "PCI_DEVICE_UNSUPPORTED";
        case StartupOperation::PciAudioProbeEnter: return "PCI_AUDIO_PROBE_ENTER";
        case StartupOperation::PciAudioProbeExit: return "PCI_AUDIO_PROBE_EXIT";
        case StartupOperation::HdaResetEnter: return "HDA_RESET_ENTER";
        case StartupOperation::HdaResetWaitCrst: return "HDA_RESET_WAIT_CRST";
        case StartupOperation::HdaCorbRirbInit: return "HDA_CORB_RIRB_INIT";
        case StartupOperation::HdaCodecEnumeration: return "HDA_CODEC_ENUMERATION";
        case StartupOperation::HdaVerbResponseWait: return "HDA_VERB_RESPONSE_WAIT";
        case StartupOperation::PciAudioScanExit: return "PCI_AUDIO_SCAN_EXIT";
        case StartupOperation::PciAudioComplete: return "PCI_AUDIO_COMPLETE";
        case StartupOperation::PciAudioSkipped: return "PCI_AUDIO_DIAGNOSTICALLY_SKIPPED";
        case StartupOperation::UsbHostScanEnter: return "USB_HOST_SCAN_ENTER";
        case StartupOperation::UsbHostScanComplete: return "USB_HOST_SCAN_COMPLETE";
        case StartupOperation::XhciInitEnter: return "XHCI_INIT_ENTER";
        case StartupOperation::XhciInitComplete: return "XHCI_INIT_COMPLETE";
        default: return "UNKNOWN";
    }
}

const char* startup_operation_status_name(StartupOperationStatus status)
{
    switch (status) {
        case StartupOperationStatus::NotInvoked: return "NOT INVOKED";
        case StartupOperationStatus::InProgress: return "IN PROGRESS";
        case StartupOperationStatus::Completed: return "COMPLETED";
        case StartupOperationStatus::ReturnedError: return "RETURNED ERROR";
        case StartupOperationStatus::TimedOut: return "TIMED OUT";
        case StartupOperationStatus::Unsupported: return "UNSUPPORTED";
        default: return "UNABLE TO DETERMINE";
    }
}

uint32_t format_startup_operation(const StartupOperationRecord& operation,
                                  char* out, uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    uint32_t position = append_text(out, capacity, 0u,
        startup_operation_name(operation.operation));
    position = append_text(out, capacity, position, " enter=");
    position = append_decimal64(out, capacity, position, operation.enterSequence);
    position = append_text(out, capacity, position, " exit=");
    if (operation.exitSequence == 0u) {
        position = append_text(out, capacity, position, "PENDING");
    } else {
        position = append_decimal64(out, capacity, position, operation.exitSequence);
    }
    position = append_text(out, capacity, position, " status=");
    position = append_text(out, capacity, position,
        startup_operation_status_name(operation.status));
    if (operation.hasPciAddress) {
        position = append_text(out, capacity, position, " bdf=");
        position = append_hex(out, capacity, position, operation.bus, 2u);
        position = append_char(out, capacity, position, ':');
        position = append_hex(out, capacity, position, operation.device, 2u);
        position = append_char(out, capacity, position, '.');
        position = append_decimal(out, capacity, position, operation.function);
    }
    if (operation.hasVendorDevice) {
        position = append_text(out, capacity, position, " id=");
        position = append_hex(out, capacity, position, operation.vendorId, 4u);
        position = append_char(out, capacity, position, ':');
        position = append_hex(out, capacity, position, operation.deviceId, 4u);
    }
    if (operation.hasClass) {
        position = append_text(out, capacity, position, " class=");
        position = append_hex(out, capacity, position, operation.baseClass, 2u);
        position = append_char(out, capacity, position, '/');
        position = append_hex(out, capacity, position, operation.subClass, 2u);
        position = append_char(out, capacity, position, '/');
        position = append_hex(out, capacity, position, operation.progIf, 2u);
    }
    if (operation.hasConfigOffset) {
        position = append_text(out, capacity, position, " cfg=");
        position = append_hex(out, capacity, position, operation.configOffset, 2u);
    }
    if (operation.hasResult) {
        position = append_text(out, capacity, position, " result=0x");
        position = append_hex(out, capacity, position, operation.result, 8u);
    }
    if (operation.hasErrorCode) {
        position = append_text(out, capacity, position, " error=0x");
        position = append_hex(out, capacity, position, operation.errorCode, 8u);
    }
    return position;
}

const char* pci_scan_state_name(PciScanState state)
{
    switch (state) {
        case PciScanState::NotInvoked: return "NOT INVOKED";
        case PciScanState::EntryReached: return "ENTRY REACHED";
        case PciScanState::ScanComplete: return "SCAN COMPLETE";
        case PciScanState::NoControllers: return "NO CONTROLLERS";
        case PciScanState::Failure: return "FAILURE";
        default: return "UNKNOWN";
    }
}

const char* stage_name(Stage stage)
{
    switch (stage) {
        case Stage::PciDiscovery: return "PCI discovery";
        case Stage::BarMmio: return "BAR/MMIO";
        case Stage::Capabilities: return "capabilities";
        case Stage::BiosOwnership: return "BIOS ownership";
        case Stage::ControllerHalt: return "controller halt";
        case Stage::ControllerReset: return "controller reset";
        case Stage::ControllerNotReady: return "controller ready";
        case Stage::DmaPrerequisites: return "DMA prerequisites";
        case Stage::RingInitialization: return "ring initialization";
        case Stage::ControllerRunning: return "controller running";
        case Stage::RootPorts: return "root ports";
        case Stage::PortReset: return "port reset";
        case Stage::EnableSlot: return "Enable Slot";
        case Stage::AddressDevice: return "Address Device";
        case Stage::ControlTransfer: return "control transfer";
        case Stage::DeviceDescriptor: return "device descriptor";
        case Stage::HidDiscovery: return "HID discovery";
        case Stage::HidReportDelivery: return "HID report delivery";
        case Stage::InputRouting: return "input routing";
        default: return "unknown stage";
    }
}

const char* controller_kind_name(ControllerKind kind)
{
    switch (kind) {
        case ControllerKind::UHCI: return "UHCI";
        case ControllerKind::OHCI: return "OHCI";
        case ControllerKind::EHCI: return "EHCI";
        case ControllerKind::XHCI: return "xHCI";
        default: return "other";
    }
}

const char* iommu_status_name(IommuStatus status)
{
    switch (status) {
        case IommuStatus::NoAcpiRemappingTable: return "NO ACPI REMAP TABLE";
        case IommuStatus::AcpiRemappingTablePresent: return "ACPI REMAP TABLE PRESENT";
        case IommuStatus::Unknown: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

const char* ivrs_status_name(amd_ivrs::ParseStatus status)
{
    switch (status) {
        case amd_ivrs::ParseStatus::NotPresent: return "NOT PRESENT";
        case amd_ivrs::ParseStatus::Valid: return "VALID";
        case amd_ivrs::ParseStatus::ValidWithUnsupported: return "VALID / UNSUPPORTED ENTRY";
        case amd_ivrs::ParseStatus::Malformed: return "MALFORMED";
        case amd_ivrs::ParseStatus::CapacityExceeded: return "CAPACITY EXCEEDED";
        case amd_ivrs::ParseStatus::AmbiguousTables: return "AMBIGUOUS TABLES";
        default: return "UNKNOWN";
    }
}

const char* iommu_route_status_name(amd_ivrs::RouteStatus status)
{
    switch (status) {
        case amd_ivrs::RouteStatus::Unmatched: return "UNMATCHED";
        case amd_ivrs::RouteStatus::Resolved: return "RESOLVED";
        case amd_ivrs::RouteStatus::Ambiguous: return "AMBIGUOUS";
        case amd_ivrs::RouteStatus::Unsupported: return "UNSUPPORTED";
        default: return "UNKNOWN";
    }
}

const char* iommu_inspection_status_name(amd_ivrs::InspectionStatus status)
{
    switch (status) {
        case amd_ivrs::InspectionStatus::NotAttempted: return "NOT ATTEMPTED";
        case amd_ivrs::InspectionStatus::Readable: return "READABLE";
        case amd_ivrs::InspectionStatus::InvalidIdentity: return "INVALID IDENTITY";
        case amd_ivrs::InspectionStatus::InvalidResource: return "INVALID RESOURCE";
        case amd_ivrs::InspectionStatus::UnsupportedRevision: return "UNSUPPORTED REVISION";
        case amd_ivrs::InspectionStatus::MappingUnavailable: return "MAPPING UNAVAILABLE";
        case amd_ivrs::InspectionStatus::ReadUnavailable: return "READ UNAVAILABLE";
        default: return "UNKNOWN";
    }
}

const char* iommu_translation_state_name(amd_ivrs::TranslationState state)
{
    switch (state) {
        case amd_ivrs::TranslationState::EnabledObserved: return "ENABLED OBSERVED";
        case amd_ivrs::TranslationState::DisabledObserved: return "DISABLED OBSERVED";
        case amd_ivrs::TranslationState::Unknown: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

const char* platform_name(Platform platform)
{
    switch (platform) {
        case Platform::Intel: return "INTEL";
        case Platform::Amd: return "AMD";
        case Platform::Other: return "OTHER";
        default: return "UNKNOWN";
    }
}

const char* dmar_status_name(intel_dmar::ParseStatus status)
{
    switch (status) {
        case intel_dmar::ParseStatus::NotPresent: return "NOT PRESENT";
        case intel_dmar::ParseStatus::Valid: return "VALID";
        case intel_dmar::ParseStatus::ValidWithUnsupported: return "UNSUPPORTED";
        case intel_dmar::ParseStatus::Malformed: return "MALFORMED";
        case intel_dmar::ParseStatus::CapacityExceeded: return "CAPACITY EXCEEDED";
        case intel_dmar::ParseStatus::UnsupportedRevision: return "UNSUPPORTED REVISION";
        case intel_dmar::ParseStatus::AmbiguousTables: return "AMBIGUOUS TABLES";
        default: return "UNKNOWN";
    }
}

const char* dmar_route_status_name(intel_dmar::RouteStatus status)
{
    switch (status) {
        case intel_dmar::RouteStatus::Resolved: return "RESOLVED";
        case intel_dmar::RouteStatus::Ambiguous: return "AMBIGUOUS";
        case intel_dmar::RouteStatus::NotCovered: return "NOT COVERED";
        case intel_dmar::RouteStatus::Unsupported: return "UNSUPPORTED";
        default: return "UNKNOWN";
    }
}

const char* vtd_register_status_name(intel_dmar::RegisterStatus status)
{
    switch (status) {
        case intel_dmar::RegisterStatus::NotAttempted: return "NOT ATTEMPTED";
        case intel_dmar::RegisterStatus::Readable: return "READABLE";
        case intel_dmar::RegisterStatus::InvalidResource: return "INVALID RESOURCE";
        case intel_dmar::RegisterStatus::MappingUnavailable: return "MAPPING UNAVAILABLE";
        case intel_dmar::RegisterStatus::UnsupportedVersion: return "UNSUPPORTED VERSION";
        case intel_dmar::RegisterStatus::UnsupportedCapabilities: return "UNSUPPORTED CAPABILITIES";
        case intel_dmar::RegisterStatus::ReadUnavailable: return "READ UNAVAILABLE";
        default: return "UNKNOWN";
    }
}

const char* vtd_translation_state_name(intel_dmar::TranslationState state)
{
    switch (state) {
        case intel_dmar::TranslationState::EnabledObserved: return "ENABLED OBSERVED";
        case intel_dmar::TranslationState::DisabledObserved: return "DISABLED OBSERVED";
        default: return "UNKNOWN";
    }
}

const char* vtd_root_table_state_name(intel_dmar::RootTableState state)
{
    switch (state) {
        case intel_dmar::RootTableState::NotConfigured: return "NOT CONFIGURED";
        case intel_dmar::RootTableState::Configured: return "CONFIGURED";
        case intel_dmar::RootTableState::Invalid: return "INVALID";
        default: return "UNKNOWN";
    }
}

uint32_t format_ivrs_summary(const Snapshot& snapshot, char* out,
                             uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    if (snapshot.platform == Platform::Intel) {
        return append_text(out, capacity, 0u, "ACPI IVRS N/A ON INTEL");
    }
    uint32_t position = append_text(out, capacity, 0u, "ACPI IVRS ");
    position = append_text(out, capacity, position, ivrs_status_name(snapshot.ivrsStatus));
    position = append_text(out, capacity, position, " tables=");
    position = append_decimal(out, capacity, position, snapshot.ivrsTableCount);
    position = append_text(out, capacity, position, " IVHD=");
    position = append_decimal(out, capacity, position, snapshot.ivhdUnitCount);
    position = append_text(out, capacity, position, " IVMD=");
    position = append_decimal(out, capacity, position, snapshot.ivmdRangeCount);
    position = append_text(out, capacity, position, " remap=");
    position = append_text(out, capacity, position,
        !snapshot.acpiRemapScanValid ? "UNKNOWN" :
        (snapshot.acpiRemappingTablePresent ? "YES" : "NO"));
    return position;
}

uint32_t format_dmar_summary(const Snapshot& snapshot, char* out,
                             uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    if (snapshot.platform == Platform::Amd) {
        return append_text(out, capacity, 0u, "ACPI DMAR N/A ON AMD");
    }
    uint32_t position = append_text(out, capacity, 0u, "ACPI DMAR ");
    if (!snapshot.dmarScanValid) {
        position = append_text(out, capacity, position, "SCAN UNKNOWN");
    } else {
        position = append_text(out, capacity, position,
                               dmar_status_name(snapshot.dmarStatus));
    }
    position = append_text(out, capacity, position, " tables=");
    position = append_decimal(out, capacity, position, snapshot.dmarTableCount);
    position = append_text(out, capacity, position, " DRHD=");
    position = append_decimal(out, capacity, position, snapshot.drhdUnitCount);
    position = append_text(out, capacity, position, " RMRR=");
    position = append_decimal(out, capacity, position, snapshot.rmrrCount);
    position = append_text(out, capacity, position, " ATSR=");
    position = append_decimal(out, capacity, position, snapshot.atsrCount);
    position = append_text(out, capacity, position, " RHSA=");
    position = append_decimal(out, capacity, position, snapshot.rhsaCount);
    return position;
}

uint32_t format_controller_iommu_route(const ControllerSnapshot& controller,
                                       char* out, uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    uint32_t position = append_bdf(out, capacity, 0u, controller);
    position = append_text(out, capacity, position, " seg=");
    position = append_hex(out, capacity, position, controller.pciSegment, 4u);
    position = append_text(out, capacity, position, " RID=");
    position = append_hex(out, capacity, position, controller.requesterId, 4u);
    if (controller.effectiveRequesterId != controller.requesterId) {
        position = append_text(out, capacity, position, " alias=");
        position = append_hex(out, capacity, position,
                              controller.effectiveRequesterId, 4u);
    }
    position = append_text(out, capacity, position, " IOMMU=");
    if (controller.iommuUnitIndex < 0) {
        position = append_text(out, capacity, position, "NONE");
    } else {
        const uint16_t id = controller.iommuDeviceId;
        position = append_hex(out, capacity, position,
                              static_cast<uint8_t>(id >> 8u), 2u);
        position = append_text(out, capacity, position, ":");
        position = append_hex(out, capacity, position,
                              static_cast<uint8_t>((id >> 3u) & 0x1Fu), 2u);
        position = append_text(out, capacity, position, ".");
        position = append_decimal(out, capacity, position,
                                  static_cast<uint32_t>(id & 0x07u));
        position = append_text(out, capacity, position, " #");
        position = append_decimal(out, capacity, position,
                                  static_cast<uint32_t>(controller.iommuUnitIndex));
    }
    position = append_text(out, capacity, position, " ");
    return append_text(out, capacity, position,
                       iommu_route_status_name(controller.iommuRouteStatus));
}

uint32_t format_controller_vtd_route(const ControllerSnapshot& controller,
                                     char* out, uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    uint32_t position = append_text(out, capacity, 0u, "xHCI ");
    position = append_bdf(out, capacity, position, controller);
    position = append_text(out, capacity, position, " VT-d=");
    position = append_text(out, capacity, position,
                           dmar_route_status_name(controller.vtdRouteStatus));
    if (controller.vtdUnitIndex >= 0) {
        position = append_text(out, capacity, position, " unit=");
        position = append_decimal(out, capacity, position,
                                  static_cast<uint32_t>(controller.vtdUnitIndex));
        position = append_text(out, capacity, position,
                               controller.vtdRmrrRequired ? " RMRR=yes" : " RMRR=no");
    }
    return position;
}

uint32_t format_controller_vtd_registers(const ControllerSnapshot& controller,
                                         char* out, uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    uint32_t position = append_text(out, capacity, 0u, "VT-d regs=");
    position = append_text(out, capacity, position,
                           vtd_register_status_name(controller.vtdRegisterStatus));
    if (controller.vtdRegisterStatus == intel_dmar::RegisterStatus::Readable ||
        controller.vtdRegisterStatus == intel_dmar::RegisterStatus::UnsupportedCapabilities) {
        position = append_text(out, capacity, position, " v=");
        position = append_hex(out, capacity, position, controller.vtdVersion, 2u);
        position = append_text(out, capacity, position, " TES=");
        position = append_text(out, capacity, position,
            vtd_translation_state_name(controller.vtdTranslationState));
        position = append_text(out, capacity, position, " root=");
        position = append_text(out, capacity, position,
            vtd_root_table_state_name(controller.vtdRootTableState));
        position = append_text(out, capacity, position, " MGAW=");
        position = append_decimal(out, capacity, position,
                                  controller.vtdMaximumGuestAddressWidth);
        position = append_text(out, capacity, position, " SAGAW=0x");
        position = append_hex(out, capacity, position,
            controller.vtdSupportedAdjustedGuestAddressWidths, 2u);
    }
    return position;
}

uint32_t format_stage_result(const StageResult& result, char* out, uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    uint32_t position = append_text(out, capacity, 0u, status_name(result.status));
    const bool includeResult = result.status == Status::Fail ||
                               result.status == Status::Timeout ||
                               result.status == Status::Blocked ||
                               result.status == Status::Unsupported;
    if (result.hasCompletionCode && includeResult) {
        position = append_text(out, capacity, position, " cc=0x");
        position = append_hex(out, capacity, position, result.completionCode, 2u);
    } else if (result.hasErrorCode && includeResult) {
        position = append_text(out, capacity, position, " result=0x");
        position = append_hex(out, capacity, position, result.errorCode, 8u);
    }
    if (result.hasControllerStatus) {
        position = append_text(out, capacity, position, " status=0x");
        position = append_hex(out, capacity, position, result.controllerStatus, 8u);
    }
    if (includeResult && result.detail != nullptr && result.detail[0] != '\0') {
        position = append_text(out, capacity, position, " reason=");
        position = append_text(out, capacity, position, result.detail);
    }
    return position;
}

uint32_t format_first_failure(const Snapshot& snapshot, char* out, uint32_t capacity)
{
    if (out == nullptr || capacity == 0u) return 0u;
    out[0] = '\0';
    uint32_t position = 0u;
    if (!snapshot.firstFailure.present) {
        return append_text(out, capacity, position, "none recorded");
    }
    position = append_text(out, capacity, position, "xHCI ");
    if (snapshot.firstFailure.hasController) {
        ControllerSnapshot controller = {};
        controller.bus = snapshot.firstFailure.bus;
        controller.device = snapshot.firstFailure.device;
        controller.function = snapshot.firstFailure.function;
        position = append_bdf(out, capacity, position, controller);
        position = append_text(out, capacity, position, " ");
    }
    position = append_text(out, capacity, position, stage_name(snapshot.firstFailure.stage));
    position = append_text(out, capacity, position, ": ");
    const StageResult& result = snapshot.firstFailure.result;
    position = append_text(out, capacity, position, status_name(result.status));
    if (result.hasCompletionCode) {
        position = append_text(out, capacity, position, " cc=0x");
        position = append_hex(out, capacity, position, result.completionCode, 2u);
    } else if (result.hasErrorCode) {
        position = append_text(out, capacity, position, " result=0x");
        position = append_hex(out, capacity, position, result.errorCode, 8u);
    }
    if (result.hasControllerStatus) {
        position = append_text(out, capacity, position, " status=0x");
        position = append_hex(out, capacity, position, result.controllerStatus, 8u);
    }
    if (result.detail != nullptr && result.detail[0] != '\0') {
        position = append_text(out, capacity, position, " reason=");
        position = append_text(out, capacity, position, result.detail);
    }
    if (result.slotId != 0u) {
        position = append_text(out, capacity, position, " slot=");
        position = append_decimal(out, capacity, position, result.slotId);
    }
    if (result.portId != 0u) {
        position = append_text(out, capacity, position, " port=");
        position = append_decimal(out, capacity, position, result.portId);
    }
    if (result.endpointId != 0u) {
        position = append_text(out, capacity, position, " ep=");
        position = append_decimal(out, capacity, position, result.endpointId);
    }
    return position;
}

bool calculate_panel_layout(uint32_t framebufferWidth, uint32_t framebufferHeight,
                            PanelLayout* outLayout)
{
    if (outLayout == nullptr || framebufferWidth == 0u || framebufferHeight == 0u) {
        return false;
    }
    const uint32_t smallDimension = framebufferWidth < framebufferHeight
        ? framebufferWidth : framebufferHeight;
    uint32_t margin = smallDimension / 32u;
    if (margin > 12u) margin = 12u;
    if (margin < 3u) margin = 3u;
    if (framebufferWidth <= margin * 2u) margin = 0u;
    if (framebufferHeight <= margin * 2u) margin = 0u;

    const uint32_t availableWidth = framebufferWidth - margin * 2u;
    const uint32_t availableHeight = framebufferHeight - margin * 2u;
    const uint32_t panelWidth = availableWidth < 560u ? availableWidth : 560u;
    const uint32_t preferredHeight = framebufferHeight < 300u ? 260u : 700u;
    const uint32_t panelHeight = availableHeight < preferredHeight ? availableHeight : preferredHeight;
    outLayout->width = panelWidth;
    outLayout->height = panelHeight;
    outLayout->x = framebufferWidth - margin - panelWidth;
    outLayout->y = margin;
    outLayout->rowHeight = framebufferHeight < 700u ? 12u : 14u;
    outLayout->rowCapacity = panelHeight > 22u ? (panelHeight - 22u) / outLayout->rowHeight : 1u;
    if (outLayout->rowCapacity == 0u) outLayout->rowCapacity = 1u;
    outLayout->compact = panelWidth < 420u || panelHeight < 300u;
    return true;
}

} // namespace modern_input_diagnostics
} // namespace kernel
