#ifndef KERNEL_MODERN_INPUT_DIAGNOSTICS_H
#define KERNEL_MODERN_INPUT_DIAGNOSTICS_H

#include <stdint.h>

namespace kernel {
namespace modern_input_diagnostics {

static const uint32_t MAX_CONTROLLERS = 8u;
static const uint32_t STAGE_COUNT = 19u;

enum class Status : uint8_t {
    Unknown = 0,
    NotAttempted,
    Unsupported,
    Pass,
    Fail,
    Timeout,
    Blocked
};

enum class ControllerKind : uint8_t {
    Other = 0,
    UHCI,
    OHCI,
    EHCI,
    XHCI
};

enum class OwnershipStatus : uint8_t {
    Unknown = 0,
    NotRequired,
    BiosOwned,
    OsOwned,
    HandoffTimeout
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
};

struct ControllerSnapshot {
    uint32_t discoveryIndex;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t baseClass;
    uint8_t subClass;
    uint8_t progIf;
    uint16_t vendorId;
    uint16_t deviceId;
    ControllerKind kind;
    OwnershipStatus ownership;
    bool occupied;
    uint64_t mmioBase;
    uint8_t capLength;
    uint16_t hciVersion;
    uint8_t maxSlots;
    uint8_t maxPorts;
    uint8_t contextSize;
    uint32_t supportedProtocolCount;
    uint32_t scratchpadBuffersRequired;
    bool ac64;
    bool iommuStatusUnknown;
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
    Status pciEnumeration;
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
void begin_pci_enumeration();
void complete_pci_enumeration(Status status);

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
void record_ownership_status(int32_t controllerIndex, OwnershipStatus status);
void record_capabilities(int32_t controllerIndex, uint8_t capLength,
                         uint16_t hciVersion, uint8_t maxSlots,
                         uint8_t maxPorts, uint8_t contextSize, bool ac64,
                         uint32_t supportedProtocolCount,
                         uint32_t scratchpadBuffersRequired);
void record_root_ports(int32_t controllerIndex, uint32_t rootPortCount,
                       uint32_t connectedPortCount, uint8_t selectedPort);
void record_device_descriptor(int32_t controllerIndex, bool transferSucceeded,
                              bool descriptorValid, uint16_t vendorId,
                              uint16_t productId);
void record_stage(int32_t controllerIndex, Stage stage, Status status,
                  uint32_t errorCode, bool hasErrorCode,
                  uint32_t controllerStatus, bool hasControllerStatus,
                  uint8_t slotId, uint8_t portId, uint8_t endpointId,
                  uint8_t completionCode, bool hasCompletionCode);
void increment_counter(int32_t controllerIndex, Counter counter);
void copy_snapshot(Snapshot* outSnapshot);

const char* status_name(Status status);
const char* stage_name(Stage stage);
const char* controller_kind_name(ControllerKind kind);
uint32_t format_stage_result(const StageResult& result, char* out, uint32_t capacity);
uint32_t format_first_failure(const Snapshot& snapshot, char* out, uint32_t capacity);
bool calculate_panel_layout(uint32_t framebufferWidth, uint32_t framebufferHeight,
                            PanelLayout* outLayout);

} // namespace modern_input_diagnostics
} // namespace kernel

#endif
