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

// Device configuration (read from config space)
struct GpuConfig {
    uint32_t eventsRead;         // Events read by driver
    uint32_t eventsClear;        // Events to clear
    uint32_t numScanouts;        // Number of scanouts
    uint32_t numCapsets;         // Number of capability sets (3D)
} GPU_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef GPU_PACKED

// ================================================================
// Display Information
// ================================================================

struct DisplayInfo {
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
