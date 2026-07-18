// VirtIO GPU Driver
//
// Implements VirtIO GPU device support for virtualized graphics.
// Provides framebuffer access in QEMU, KVM, and other hypervisors.
//
// VirtIO GPU Features:
//   - 2D framebuffer with scanout support
//   - Resource creation and management
//   - Cursor support
//   - Display info queries
//   - Optional 3D acceleration (Virgl)
//
// Reference: VirtIO GPU Specification
//   https://docs.oasis-open.org/virtio/virtio/
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_VIRTIO_GPU_H
#define KERNEL_VIRTIO_GPU_H

#include "kernel/types.h"
#include "kernel/virtio.h"
#include "kernel/display_input_mapper.h"
#include "display_configuration_command.h"

// Keep backend activation, manual mode, and proof activation conceptually
// separate.  The compatibility aliases preserve the existing internal probe
// implementation while allowing launchers to select the explicit QEMU-only
// backend/manual gates without affecting the normal product path.
#if defined(GXOS_QEMU_VIRTIO_GPU_BACKEND_ACTIVE) && !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
#define GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE
#endif
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_MODE) && !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
#define GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE
#endif

namespace kernel {
namespace virtio {
namespace gpu {

// ================================================================
// VirtIO GPU Feature Bits
// ================================================================

static const uint64_t FEATURE_VIRGL       = (1ULL << 0);  // Virgl 3D support
static const uint64_t FEATURE_EDID        = (1ULL << 1);  // EDID support
static const uint64_t FEATURE_RESOURCE_UUID = (1ULL << 2); // Resource UUID
static const uint64_t FEATURE_RESOURCE_BLOB = (1ULL << 3); // Blob resources
static const uint64_t FEATURE_CONTEXT_INIT = (1ULL << 4); // Context init

// ================================================================
// Command Types
// ================================================================

enum GpuCtrlType : uint32_t {
    // 2D commands
    CMD_GET_DISPLAY_INFO          = 0x0100,
    CMD_RESOURCE_CREATE_2D        = 0x0101,
    CMD_RESOURCE_UNREF            = 0x0102,
    CMD_SET_SCANOUT               = 0x0103,
    CMD_RESOURCE_FLUSH            = 0x0104,
    CMD_TRANSFER_TO_HOST_2D       = 0x0105,
    CMD_RESOURCE_ATTACH_BACKING   = 0x0106,
    CMD_RESOURCE_DETACH_BACKING   = 0x0107,
    CMD_GET_CAPSET_INFO           = 0x0108,
    CMD_GET_CAPSET                = 0x0109,
    CMD_GET_EDID                  = 0x010A,
    
    // Cursor commands
    CMD_UPDATE_CURSOR             = 0x0300,
    CMD_MOVE_CURSOR               = 0x0301,
    
    // Responses
    RESP_OK_NODATA                = 0x1100,
    RESP_OK_DISPLAY_INFO          = 0x1101,
    RESP_OK_CAPSET_INFO           = 0x1102,
    RESP_OK_CAPSET                = 0x1103,
    RESP_OK_EDID                  = 0x1104,
    
    // Error responses
    RESP_ERR_UNSPEC               = 0x1200,
    RESP_ERR_OUT_OF_MEMORY        = 0x1201,
    RESP_ERR_INVALID_SCANOUT_ID   = 0x1202,
    RESP_ERR_INVALID_RESOURCE_ID  = 0x1203,
    RESP_ERR_INVALID_CONTEXT_ID   = 0x1204,
    RESP_ERR_INVALID_PARAMETER    = 0x1205,
};

// ================================================================
// Pixel Formats
// ================================================================

enum GpuFormat : uint32_t {
    FORMAT_B8G8R8A8_UNORM  = 1,   // BGRA 32-bit (common)
    FORMAT_B8G8R8X8_UNORM  = 2,   // BGRX 32-bit (no alpha)
    FORMAT_A8R8G8B8_UNORM  = 3,   // ARGB 32-bit
    FORMAT_X8R8G8B8_UNORM  = 4,   // XRGB 32-bit
    FORMAT_R8G8B8A8_UNORM  = 67,  // RGBA 32-bit
    FORMAT_X8B8G8R8_UNORM  = 68,  // XBGR 32-bit
    FORMAT_A8B8G8R8_UNORM  = 121, // ABGR 32-bit
    FORMAT_R8G8B8X8_UNORM  = 134, // RGBX 32-bit
};

// ================================================================
// Status Codes
// ================================================================

enum GpuStatus : int8_t {
    GPU_OK              =  0,
    GPU_ERR_NOT_FOUND   = -1,
    GPU_ERR_NO_DEVICE   = -2,
    GPU_ERR_INIT_FAIL   = -3,
    GPU_ERR_NO_MEMORY   = -4,
    GPU_ERR_INVALID     = -5,
    GPU_ERR_IO          = -6,
    GPU_ERR_TIMEOUT     = -7,
    GPU_ERR_UNSUPPORTED = -8,
};

// ================================================================
// On-Disk Structures
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define GPU_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define GPU_PACKED
#endif

// Common control header for all commands
struct CtrlHeader {
    uint32_t type;               // GpuCtrlType
    uint32_t flags;              // Command flags
    uint64_t fenceId;            // Fence ID for synchronization
    uint32_t ctxId;              // 3D context ID (0 for 2D)
    uint32_t padding;
} GPU_PACKED;

// Rectangle structure
struct Rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} GPU_PACKED;

// Display information for one scanout
struct DisplayOne {
    Rect     rect;               // Display dimensions
    uint32_t enabled;            // Is this display enabled
    uint32_t flags;              // Display flags
} GPU_PACKED;

// GET_DISPLAY_INFO response
static const int MAX_SCANOUTS = 16;

struct RespDisplayInfo {
    CtrlHeader  header;
    DisplayOne  pmodes[MAX_SCANOUTS];
} GPU_PACKED;

// RESOURCE_CREATE_2D command
struct ResourceCreate2d {
    CtrlHeader header;
    uint32_t   resourceId;       // Resource ID to create
    uint32_t   format;           // GpuFormat
    uint32_t   width;
    uint32_t   height;
} GPU_PACKED;

// RESOURCE_UNREF command
struct ResourceUnref {
    CtrlHeader header;
    uint32_t   resourceId;
    uint32_t   padding;
} GPU_PACKED;

// SET_SCANOUT command
struct SetScanout {
    CtrlHeader header;
    Rect       rect;             // Scanout rectangle
    uint32_t   scanoutId;        // Scanout index
    uint32_t   resourceId;       // Resource to display
} GPU_PACKED;

// RESOURCE_FLUSH command
struct ResourceFlush {
    CtrlHeader header;
    Rect       rect;             // Region to flush
    uint32_t   resourceId;
    uint32_t   padding;
} GPU_PACKED;

// TRANSFER_TO_HOST_2D command
struct TransferToHost2d {
    CtrlHeader header;
    Rect       rect;             // Transfer region
    uint64_t   offset;           // Offset in backing store
    uint32_t   resourceId;
    uint32_t   padding;
} GPU_PACKED;

// Memory entry for backing store
struct MemEntry {
    uint64_t addr;               // Physical address
    uint32_t length;             // Length in bytes
    uint32_t padding;
} GPU_PACKED;

// RESOURCE_ATTACH_BACKING command
struct ResourceAttachBacking {
    CtrlHeader header;
    uint32_t   resourceId;
    uint32_t   numEntries;       // Number of memory entries
    // Followed by MemEntry[numEntries]
} GPU_PACKED;

// RESOURCE_DETACH_BACKING command
struct ResourceDetachBacking {
    CtrlHeader header;
    uint32_t   resourceId;
    uint32_t   padding;
} GPU_PACKED;

// Cursor position update
struct CursorPos {
    uint32_t scanoutId;
    uint32_t x;
    uint32_t y;
    uint32_t padding;
} GPU_PACKED;

// UPDATE_CURSOR command
struct UpdateCursor {
    CtrlHeader header;
    CursorPos  pos;
    uint32_t   resourceId;       // Resource for cursor image
    uint32_t   hotX;
    uint32_t   hotY;
    uint32_t   padding;
} GPU_PACKED;

// MOVE_CURSOR command
struct MoveCursor {
    CtrlHeader header;
    CursorPos  pos;
} GPU_PACKED;

// Device configuration (read from config space).  These fields are packed
// little-endian device data.  eventsRead is device-owned/read-only and
// eventsClear is write-to-clear; MMIO access uses the explicit offsets below.
enum GpuDeviceConfigOffset : uint32_t {
    DEVICE_CONFIG_EVENTS_READ = 0x00u,
    DEVICE_CONFIG_EVENTS_CLEAR = 0x04u,
    DEVICE_CONFIG_NUM_SCANOUTS = 0x08u,
    DEVICE_CONFIG_NUM_CAPSETS = 0x0Cu
};

static const uint32_t VIRTIO_GPU_EVENT_DISPLAY = 1u << 0;

struct GpuConfig {
    uint32_t eventsReadLe;
    uint32_t eventsClearLe;
    uint32_t numScanoutsLe;
    uint32_t numCapsetsLe;
} GPU_PACKED;

static_assert(sizeof(GpuConfig) == 16u, "VirtIO-GPU device config layout must remain packed");

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef GPU_PACKED

// ================================================================
// Display Information
// ================================================================

struct DisplayInfo {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    bool     enabled;
};

// ================================================================
// GPU Device Context
// ================================================================

struct GpuDevice {
    // Base VirtIO info
    uint64_t baseAddr;           // MMIO or PCI BAR base
    bool     isPci;              // PCI (true) or MMIO (false)
    uint8_t  pciBus;
    uint8_t  pciDevice;
    uint8_t  pciFunction;
    uint8_t  irqLine;
    
    // Device state
    bool     initialized;
    bool     has3D;              // Virgl support
    uint64_t features;
    
    // Virtqueues
    Virtqueue controlQ;          // Control queue
    Virtqueue cursorQ;           // Cursor queue
    
    // Display info
    uint32_t    numScanouts;
    DisplayInfo displays[MAX_SCANOUTS];
    
    // Resources
    uint32_t nextResourceId;
    
    // Framebuffer
    uint32_t fbResourceId;       // Current framebuffer resource
    uint32_t fbWidth;
    uint32_t fbHeight;
    uint32_t fbFormat;
    uint8_t* fbBuffer;           // Framebuffer backing memory
    uint64_t fbBufferPhys;       // Physical address
    size_t   fbBufferSize;
    
    // Statistics
    uint32_t framesDisplayed;
    uint32_t flushCount;
};

// ================================================================
// Public API
// ================================================================

// Initialize VirtIO GPU subsystem
// Call once at kernel boot
void init();

// Provide the loaded physical base of the kernel image so the diagnostic
// probe can translate static command/queue buffers into DMA-visible
// physical addresses.
void set_kernel_physical_base(uint64_t physicalBase);

// Probe for VirtIO GPU devices
// Returns number of devices found
int probe();

// Get device by index
GpuDevice* get_device(int index);

// Get number of detected devices
int device_count();

// ================================================================
// Device Operations
// ================================================================

// Initialize a specific GPU device
GpuStatus init_device(GpuDevice* dev);

// Reset device
GpuStatus reset_device(GpuDevice* dev);

// Get display information
GpuStatus get_display_info(GpuDevice* dev);

// ================================================================
// Framebuffer Operations
// ================================================================

// Create and configure framebuffer for a scanout
// width/height: desired resolution (0 = use display's preferred)
// scanoutId: which display to use (usually 0)
GpuStatus setup_framebuffer(GpuDevice* dev, uint32_t width, uint32_t height,
                            uint32_t scanoutId);

// Get framebuffer pointer for direct pixel access
// Returns pointer to BGRA pixel buffer
uint8_t* get_framebuffer(GpuDevice* dev);

// Get framebuffer dimensions
uint32_t get_framebuffer_width(GpuDevice* dev);
uint32_t get_framebuffer_height(GpuDevice* dev);
uint32_t get_framebuffer_pitch(GpuDevice* dev);

// Flush framebuffer region to display
// x, y, width, height: region to update (0,0,0,0 = entire buffer)
GpuStatus flush_framebuffer(GpuDevice* dev, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height);

// Flush entire framebuffer
GpuStatus flush_all(GpuDevice* dev);

// ================================================================
// Cursor Operations
// ================================================================

// Set cursor image
// resourceId: pre-created cursor resource
// hotX, hotY: cursor hotspot
GpuStatus set_cursor(GpuDevice* dev, uint32_t resourceId, 
                     uint32_t hotX, uint32_t hotY);

// Move cursor to position
GpuStatus move_cursor(GpuDevice* dev, uint32_t x, uint32_t y);

// Hide cursor
GpuStatus hide_cursor(GpuDevice* dev);

// ================================================================
// Resource Management
// ================================================================

// Create a 2D resource
GpuStatus create_resource_2d(GpuDevice* dev, uint32_t* resourceIdOut,
                             uint32_t width, uint32_t height, GpuFormat format);

// Attach backing memory to resource
GpuStatus attach_backing(GpuDevice* dev, uint32_t resourceId,
                         uint64_t physAddr, size_t size);

// Detach backing memory
GpuStatus detach_backing(GpuDevice* dev, uint32_t resourceId);

// Transfer data to host
GpuStatus transfer_to_host(GpuDevice* dev, uint32_t resourceId,
                           uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Destroy resource
GpuStatus destroy_resource(GpuDevice* dev, uint32_t resourceId);

// ================================================================
// Interrupt Handling
// ================================================================

// IRQ handler for VirtIO GPU
void irq_handler();

// Poll for completion (if not using interrupts)
void poll(GpuDevice* dev);

// Scheduler-owned QEMU-only live presenter step.  It is a no-op unless both
// GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE and the explicit live build gate are set.
void presentation_tick();

// True once the bounded live proof has stopped, or in builds where the live
// presenter is not compiled.  This is only used by the QEMU text-mode probe
// pump and is not a product presentation-control API.
bool presentation_finished();

// Read-only QEMU manual-readiness snapshot.  Counts come from the live
// backend inventory and presenter state; this does not activate any proof
// coordinator or mutate display configuration.
struct VirtioGpuDisplayReadiness {
    uint32_t displayMonitorCount{0u};
    uint32_t displayRenderTargetCount{0u};
    uint32_t operationalOutputCount{0u};
    uint32_t virtualDesktopWidth{0u};
    uint32_t virtualDesktopHeight{0u};
    uint8_t backendInitialized{0u};
    uint8_t presenterActive{0u};
    uint8_t topologyControlsAvailable{0u};
    uint8_t reserved{0u};
};

bool get_display_readiness(VirtioGpuDisplayReadiness* readiness);

// Copy the QEMU-only operational monitor geometry into the backend-neutral
// input mapper representation. No hardware path calls this API.
bool get_display_input_layout(
    int32_t* left, int32_t* top, int32_t* right, int32_t* bottom,
    display_input::DisplayInputMonitor* monitors, uint8_t capacity,
    uint8_t* monitorCount);

// Typed display-configuration backend adapter. The public service owns the
// transaction and calls these QEMU-only hooks for resource-preserving target
// layout changes; no raw backend state crosses the command contract.
struct DisplayConfigurationBackendResult {
    uint8_t targetRebuilt;
    uint8_t validationFrame;
    uint8_t success;
    uint8_t reserved;
    char diagnostic[gxos::display::kDisplayConfigurationDiagnosticBytes];
};

struct VirtioGpuConfigSnapshot {
    uint8_t firstGeneration{0u};
    uint8_t finalGeneration{0u};
    uint8_t retryCount{0u};
    uint8_t coherent{0u};
    uint32_t eventsRead{0u};
    uint32_t numScanouts{0u};
    uint32_t numCapsets{0u};
    char failureReason[128]{};
};

struct VirtioGpuDisplayEventObserverStatus {
    uint8_t initialized{0u};
    uint8_t enabled{0u};
    uint8_t rescanInProgress{0u};
    uint8_t pendingTopologyChange{0u};
    uint64_t polls{0u};
    uint64_t coherentReads{0u};
    uint64_t incoherentReads{0u};
    uint64_t eventsObserved{0u};
    uint64_t displayEventsObserved{0u};
    uint64_t unknownEventBitsObserved{0u};
    uint64_t displayEventsProcessed{0u};
    uint64_t eventClearWrites{0u};
    uint64_t rescansSubmitted{0u};
    uint64_t rescansCoalesced{0u};
    uint64_t rescansSuccessful{0u};
    uint64_t rescansFailed{0u};
    uint64_t reassertions{0u};
    uint32_t lastEventsRead{0u};
    uint32_t lastEventsCleared{0u};
    uint32_t lastConfigGeneration{0u};
    uint32_t lastTopologyGeneration{0u};
    uint64_t lastPollTick{0u};
    uint32_t pollInterval{0u};
    uint32_t pendingRescanRetries{0u};
    uint8_t lastReasserted{0u};
    uint8_t reserved[3]{};
    char lastError[128]{};
    char disabledReason[128]{};
    gxos::display::DisplayTopologyChangeQuery pendingQuery{};
};

bool get_display_configuration_backend_snapshots(
    gxos::display::DisplayConfigurationSnapshot* detected,
    gxos::display::DisplayConfigurationSnapshot* active);
void set_display_configuration_backend_presentation_paused(bool paused);
bool display_configuration_backend_presentation_paused();
bool apply_display_configuration_backend_layout(
    const gxos::display::DisplayConfigurationRequest& requested,
    uint32_t failureInjectionFlags,
    DisplayConfigurationBackendResult* result);

// QEMU-only display-event observation and read-only publication. These APIs
// never apply a detected topology, destroy a resource, rebind a scanout, or
// mutate persisted configuration.
bool read_virtio_gpu_config_snapshot(GpuDevice* dev, VirtioGpuConfigSnapshot* snapshot);
bool query_detected_topology_change(gxos::display::DisplayTopologyChangeQuery* query);
bool refresh_detected_topology_for_service();
bool inject_display_topology_change_for_test(uint32_t kind);
bool dismiss_detected_topology_for_service(uint32_t topologyGeneration);
bool apply_detected_topology_for_service(uint32_t topologyGeneration);
void get_display_event_observer_status(VirtioGpuDisplayEventObserverStatus* status);

// ================================================================
// Integration with Kernel Framebuffer
// ================================================================

// Register VirtIO GPU as the system framebuffer
// This integrates with kernel/framebuffer.h
GpuStatus register_as_framebuffer(GpuDevice* dev);

// ================================================================
// Debug/Status
// ================================================================

// Print device status to serial
void print_status(GpuDevice* dev);

// Print all devices
void print_all_devices();

// Get status string
const char* status_string(GpuStatus status);

} // namespace gpu
} // namespace virtio
} // namespace kernel

#endif // KERNEL_VIRTIO_GPU_H
