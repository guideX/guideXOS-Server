#ifndef KERNEL_MODERN_INPUT_DIAGNOSTICS_H
#define KERNEL_MODERN_INPUT_DIAGNOSTICS_H

#include <stdint.h>
#include "kernel/amd_ivrs.h"
#include "kernel/intel_dmar.h"

namespace kernel {
namespace modern_input_diagnostics {

static const uint32_t MAX_CONTROLLERS = 8u;
static const uint32_t STAGE_COUNT = 19u;
static const uint32_t MAX_STARTUP_OPERATION_TRACE = 16u;

enum class Status : uint8_t {
    Unknown = 0,
    NotAttempted,
    Unsupported,
    Pass,
    Fail,
    Timeout,
    Blocked
};

enum class PciScanState : uint8_t {
    NotInvoked = 0,
    EntryReached,
    ScanComplete,
    NoControllers,
    Failure
};

enum class BootStage : uint8_t {
    Unknown = 0,
    KernelEntry,
    MemoryInitialized,
    FramebufferReady,
    PciDiscoveryEntered,
    PciDiscoveryCompleted,
    UsbDiscoveryEntered,
    UsbDiscoveryCompleted,
    XhciInitEntered,
    XhciInitCompleted,
    DesktopInitialized,
    MainLoopEntered
};

enum class StartupOperation : uint8_t {
    None = 0,
    PciAudioEntry,
    PciAudioScanEnter,
    PciConfigAddressSelect,
    PciConfigDataRead,
    PciConfigDataWrite,
    PciAudioDeviceFound,
    PciDeviceUnsupported,
    PciAudioProbeEnter,
    PciAudioProbeExit,
    HdaResetEnter,
    HdaResetWaitCrst,
    HdaCorbRirbInit,
    HdaCodecEnumeration,
    HdaVerbResponseWait,
    PciAudioScanExit,
    PciAudioComplete,
    PciAudioSkipped,
    UsbHostScanEnter,
    UsbHostScanComplete,
    XhciInitEnter,
    XhciInitComplete
};

enum class StartupOperationStatus : uint8_t {
    NotInvoked = 0,
    InProgress,
    Completed,
    ReturnedError,
    TimedOut,
    Unsupported,
    Unknown
};

struct StartupOperationRecord {
    StartupOperation operation;
    StartupOperationStatus status;
    uint64_t enterSequence;
    uint64_t exitSequence;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t baseClass;
    uint8_t subClass;
    uint8_t progIf;
    uint8_t configOffset;
    uint16_t vendorId;
    uint16_t deviceId;
    uint32_t result;
    uint32_t errorCode;
    bool hasPciAddress;
    bool hasVendorDevice;
    bool hasClass;
    bool hasConfigOffset;
    bool hasResult;
    bool hasErrorCode;
};

struct StartupOperationContext {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t baseClass;
    uint8_t subClass;
    uint8_t progIf;
    uint8_t configOffset;
    uint16_t vendorId;
    uint16_t deviceId;
    bool hasPciAddress;
    bool hasVendorDevice;
    bool hasClass;
    bool hasConfigOffset;
};

enum class ControllerKind : uint8_t {
    Other = 0,
    UHCI,
    OHCI,
    EHCI,
    XHCI
};

enum class Platform : uint8_t {
    Unknown = 0,
    Intel,
    Amd,
    Other
};

enum class OwnershipStatus : uint8_t {
    Unknown = 0,
    NotRequired,
    BiosOwned,
    OsOwned,
    HandoffTimeout
};

enum class IommuStatus : uint8_t {
    Unknown = 0,
    NoAcpiRemappingTable,
    AcpiRemappingTablePresent
};

enum class Stage : uint8_t {
    PciDiscovery = 0,
    BarMmio,
    Capabilities,
    BiosOwnership,
    ControllerHalt,
    ControllerReset,
    ControllerNotReady,
    DmaPrerequisites,
    RingInitialization,
    ControllerRunning,
    RootPorts,
    PortReset,
    EnableSlot,
    AddressDevice,
    ControlTransfer,
    DeviceDescriptor,
    HidDiscovery,
    HidReportDelivery,
    InputRouting
};

enum class Counter : uint8_t {
    KeyboardReports = 0,
    MouseReports,
    DecodedKeyEvents,
    DecodedPointerEvents,
    DeliveredInputEvents
};

struct StageResult {
    Status status;
    uint32_t errorCode;
    uint32_t controllerStatus;
    uint8_t slotId;
    uint8_t portId;
    uint8_t endpointId;
    uint8_t completionCode;
    bool hasErrorCode;
    bool hasControllerStatus;
    bool hasCompletionCode;
    const char* detail;
};

struct ControllerSnapshot {
    uint32_t discoveryIndex;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t pciSegment;
    uint16_t requesterId;
    uint16_t effectiveRequesterId;
    int32_t iommuUnitIndex;
    uint16_t iommuDeviceId;
    uint64_t iommuBase;
    amd_ivrs::RouteStatus iommuRouteStatus;
    amd_ivrs::InspectionStatus iommuInspectionStatus;
    amd_ivrs::TranslationState iommuTranslationState;
    bool iommuControlRegisterValid;
    bool iommuStatusRegisterValid;
    uint8_t baseClass;
    uint8_t subClass;
    uint8_t progIf;
    uint16_t vendorId;
    uint16_t deviceId;
    ControllerKind kind;
    OwnershipStatus ownership;
    bool occupied;
    uint64_t mmioBase;
    uint32_t pciBar0;
    uint32_t pciBar1;
    uint16_t pciCommand;
    uint16_t pciCommandInitial;
    bool pciCommandInitialCaptured;
    uint8_t pciBarType;
    bool pciBarIsIo;
    bool pciBarIs64;
    bool pciBarPrefetchable;
    uint64_t mmioRequiredSpan;
    uint64_t mmioMappedVirtual;
    uint64_t mmioMappedLength;
    bool mmioMappingSucceeded;
    const char* mmioValidationSubstage;
    const char* mmioMappingReason;
    uint8_t capLength;
    uint16_t hciVersion;
    uint8_t maxSlots;
    uint8_t maxPorts;
    uint8_t contextSize;
    uint32_t supportedProtocolCount;
    uint32_t scratchpadBuffersRequired;
    uint32_t hcsParams2;
    uint8_t scratchpadHigh;
    uint8_t scratchpadLow;
    uint32_t scratchpadCapacity;
    Status scratchpadAllocationStatus;
    const char* scratchpadAllocationReason;
    intel_dmar::RouteStatus vtdRouteStatus;
    int32_t vtdUnitIndex;
    uint64_t vtdRegisterBase;
    bool vtdRmrrRequired;
    intel_dmar::RegisterStatus vtdRegisterStatus;
    intel_dmar::TranslationState vtdTranslationState;
    intel_dmar::RootTableState vtdRootTableState;
    uint8_t vtdVersion;
    uint8_t vtdMaximumGuestAddressWidth;
    uint8_t vtdSupportedAdjustedGuestAddressWidths;
    uint32_t vtdGlobalCommand;
    uint32_t vtdGlobalStatus;
    uint32_t vtdFaultStatus;
    uint64_t vtdCapability;
    uint64_t vtdExtendedCapability;
    uint64_t vtdRootTableAddress;
    bool vtdCapabilityValid;
    bool vtdExtendedCapabilityValid;
    bool vtdCommandStatusValid;
    bool vtdRootTableValid;
    bool vtdFaultStatusValid;
    bool ac64;
    IommuStatus iommuStatus;
    uint32_t rootPortCount;
    uint32_t connectedPortCount;
    uint8_t selectedPort;
    uint32_t descriptorTransfersSucceeded;
    uint16_t vendorDescriptor;
    uint16_t productDescriptor;
    bool deviceDescriptorValid;
    uint32_t inputCounters[5];
    StageResult stages[STAGE_COUNT];
};

struct FirstFailure {
    bool present;
    bool hasController;
    int32_t controllerIndex;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendorId;
    uint16_t deviceId;
    ControllerKind kind;
    Stage stage;
    StageResult result;
};

struct Snapshot {
    BootStage bootStage;
    uint64_t bootSequence;
    uint64_t snapshotSequence;
    uint64_t displayedSequence;
    Status pciEnumeration;
    PciScanState pciScanState;
    const char* pciScanFailure;
    uint32_t usbControllerCount;
    uint32_t xhciControllerCount;
    uint32_t storedControllerCount;
    uint32_t controllersOmitted;
    int32_t selectedControllerIndex;
    ControllerSnapshot controllers[MAX_CONTROLLERS];
    FirstFailure firstFailure;
    int32_t latestControllerIndex;
    Stage latestStage;
    StageResult latestStatus;
    uint64_t startupOperationSequence;
    StartupOperationRecord lastStartupOperation;
    StartupOperationRecord lastCompletedStartupOperation;
    StartupOperationRecord startupOperationTrace[MAX_STARTUP_OPERATION_TRACE];
    uint32_t startupOperationTraceCount;
    uint32_t startupOperationTraceNext;
    StartupOperationStatus pciAudioStatus;
    StartupOperationStatus usbHostScanStatus;
    StartupOperationStatus xhciInitStatus;
    uint32_t pciAudioDeviceCount;
    bool pciAudioSkipped;
    amd_ivrs::ParseStatus ivrsStatus;
    amd_ivrs::RouteStatus selectedIommuRouteStatus;
    amd_ivrs::InspectionStatus selectedIommuInspectionStatus;
    amd_ivrs::TranslationState selectedIommuTranslationState;
    uint32_t ivrsTableCount;
    uint32_t ivhdUnitCount;
    uint32_t ivmdRangeCount;
    uint32_t ivrsUnsupportedCount;
    Platform platform;
    intel_dmar::ParseStatus dmarStatus;
    uint32_t dmarTableCount;
    uint32_t drhdUnitCount;
    uint32_t rmrrCount;
    uint32_t atsrCount;
    uint32_t rhsaCount;
    uint32_t dmarUnsupportedCount;
    bool dmarScanValid;
    bool dmarTablePresent;
    bool acpiRemapScanValid;
    bool acpiRemappingTablePresent;
    bool dmaMappingEstablished;
    const char* dmaDecisionReason;
};

struct PanelLayout {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t rowHeight;
    uint32_t rowCapacity;
    bool compact;
};

// Called once at kernel entry. All diagnostic updates happen at boot or from
// the kernel main loop; interrupt handlers never publish diagnostic state.
void initialize();
void record_boot_milestone(BootStage stage);
uint64_t begin_startup_operation(StartupOperation operation,
                                 const StartupOperationContext* context = nullptr);
void complete_startup_operation(uint64_t enterSequence,
                                StartupOperationStatus status,
                                uint32_t result = 0u, bool hasResult = false,
                                uint32_t errorCode = 0u, bool hasErrorCode = false);
void record_pci_audio_skipped();
bool presentation_pending();
bool record_panel_presented(uint64_t snapshotSequence);
void begin_pci_enumeration();
void complete_pci_enumeration(Status status, const char* failureReason = nullptr);
void record_ivrs_discovery(amd_ivrs::ParseStatus status, bool scanValid,
                           bool remappingTablePresent, uint32_t ivrsTableCount,
                           uint32_t ivhdUnits, uint32_t ivmdRanges,
                           uint32_t unsupportedCount);
void record_platform(Platform platform);
void record_dmar_discovery(intel_dmar::ParseStatus status, bool scanValid,
                           bool remappingTablePresent, uint32_t dmarTableCount,
                           uint32_t drhdUnits, uint32_t rmrrRanges,
                           uint32_t atsrStructures, uint32_t rhsaStructures,
                           uint32_t unsupportedCount);
void record_vtd_route(int32_t controllerIndex, uint16_t segment,
                      uint8_t bus, uint8_t device, uint8_t function,
                      intel_dmar::RouteStatus routeStatus, int32_t unitIndex,
                      uint64_t registerBase, bool rmrrRequired);
void record_vtd_registers(int32_t controllerIndex,
                          const intel_dmar::RegisterSnapshot& registers);
void record_scratchpad_allocation(int32_t controllerIndex, uint32_t capacity,
                                  Status status, const char* reason);
void record_iommu_route(int32_t controllerIndex, uint16_t segment,
                        uint16_t requesterId, uint16_t effectiveRequesterId,
                        amd_ivrs::RouteStatus routeStatus, int32_t unitIndex,
                        uint16_t iommuDeviceId, uint64_t iommuBase);
void record_iommu_registers(int32_t controllerIndex,
                            amd_ivrs::InspectionStatus inspectionStatus,
                            amd_ivrs::TranslationState translationState,
                            bool controlRegisterValid = false,
                            bool statusRegisterValid = false);
void record_dma_safety(const char* reason, bool mappingEstablished = false);

// Returns the deterministic PCI discovery index, or -1 when the bounded
// per-controller snapshot is full. Aggregate counts still include omitted
// devices.
int32_t record_controller(uint8_t bus, uint8_t device, uint8_t function,
                          uint16_t vendorId, uint16_t deviceId,
                          uint8_t baseClass, uint8_t subClass, uint8_t progIf,
                          ControllerKind kind);
int32_t find_controller(uint8_t bus, uint8_t device, uint8_t function);
void select_controller(int32_t controllerIndex);
void record_mmio_base(int32_t controllerIndex, uint64_t base);
void record_mmio_configuration(int32_t controllerIndex, uint32_t bar0,
                                uint32_t bar1, uint16_t pciCommand,
                                uint8_t barType, bool barIsIo, bool barIs64,
                                bool barPrefetchable);
void record_mmio_mapping(int32_t controllerIndex, const char* substage,
                         uint64_t requiredSpan,
                         bool succeeded, uint64_t mappedVirtual,
                         uint64_t mappedLength, const char* reason);
void record_ownership_status(int32_t controllerIndex, OwnershipStatus status);
void record_capabilities(int32_t controllerIndex, uint8_t capLength,
                         uint16_t hciVersion, uint8_t maxSlots,
                         uint8_t maxPorts, uint8_t contextSize, bool ac64,
                         uint32_t supportedProtocolCount,
                         uint32_t scratchpadBuffersRequired,
                         IommuStatus iommuStatus = IommuStatus::Unknown,
                         uint32_t hcsParams2 = 0u,
                         uint8_t scratchpadHigh = 0u,
                         uint8_t scratchpadLow = 0u);
void record_root_ports(int32_t controllerIndex, uint32_t rootPortCount,
                       uint32_t connectedPortCount, uint8_t selectedPort);
void record_device_descriptor(int32_t controllerIndex, bool transferSucceeded,
                              bool descriptorValid, uint16_t vendorId,
                              uint16_t productId);
void record_stage(int32_t controllerIndex, Stage stage, Status status,
                  uint32_t errorCode, bool hasErrorCode,
                  uint32_t controllerStatus, bool hasControllerStatus,
                  uint8_t slotId, uint8_t portId, uint8_t endpointId,
                  uint8_t completionCode, bool hasCompletionCode,
                  const char* detail = nullptr);
void increment_counter(int32_t controllerIndex, Counter counter);
void copy_snapshot(Snapshot* outSnapshot);

const char* status_name(Status status);
const char* boot_stage_name(BootStage stage);
const char* startup_operation_name(StartupOperation operation);
const char* startup_operation_status_name(StartupOperationStatus status);
uint32_t format_startup_operation(const StartupOperationRecord& operation,
                                 char* out, uint32_t capacity);
const char* pci_scan_state_name(PciScanState state);
const char* stage_name(Stage stage);
const char* controller_kind_name(ControllerKind kind);
const char* iommu_status_name(IommuStatus status);
const char* ivrs_status_name(amd_ivrs::ParseStatus status);
const char* iommu_route_status_name(amd_ivrs::RouteStatus status);
const char* iommu_inspection_status_name(amd_ivrs::InspectionStatus status);
const char* iommu_translation_state_name(amd_ivrs::TranslationState state);
const char* platform_name(Platform platform);
const char* dmar_status_name(intel_dmar::ParseStatus status);
const char* dmar_route_status_name(intel_dmar::RouteStatus status);
const char* vtd_register_status_name(intel_dmar::RegisterStatus status);
const char* vtd_translation_state_name(intel_dmar::TranslationState state);
const char* vtd_root_table_state_name(intel_dmar::RootTableState state);
uint32_t format_ivrs_summary(const Snapshot& snapshot, char* out,
                             uint32_t capacity);
uint32_t format_dmar_summary(const Snapshot& snapshot, char* out,
                             uint32_t capacity);
uint32_t format_controller_iommu_route(const ControllerSnapshot& controller,
                                       char* out, uint32_t capacity);
uint32_t format_controller_vtd_route(const ControllerSnapshot& controller,
                                     char* out, uint32_t capacity);
uint32_t format_controller_vtd_registers(const ControllerSnapshot& controller,
                                         char* out, uint32_t capacity);
uint32_t format_stage_result(const StageResult& result, char* out, uint32_t capacity);
uint32_t format_first_failure(const Snapshot& snapshot, char* out, uint32_t capacity);
bool calculate_panel_layout(uint32_t framebufferWidth, uint32_t framebufferHeight,
                            PanelLayout* outLayout);

} // namespace modern_input_diagnostics
} // namespace kernel

#endif
