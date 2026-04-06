// VirtIO GPU Driver Implementation
//
// Provides virtualized graphics support for QEMU, KVM, etc.
//
// Implementation Notes:
// - Supports both MMIO and PCI transport
// - Uses 2D mode for maximum compatibility
// - Framebuffer allocated from kernel memory
// - Polling mode with optional interrupt support
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/virtio_gpu.h"
#include "include/kernel/serial_debug.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#define MEMORY_BARRIER() _ReadWriteBarrier()
#include <intrin.h>
#else
#define GXOS_MSVC_STUB 0
#define MEMORY_BARRIER() asm volatile ("" ::: "memory")
#endif

namespace kernel {
namespace virtio {
namespace gpu {

// ================================================================
// Internal State
// ================================================================

static bool s_initialized = false;
static GpuDevice s_devices[4];
static int s_deviceCount = 0;

// Command/response buffers (static for simplicity)
#if defined(_MSC_VER)
__declspec(align(4096)) static uint8_t s_cmdBuffer[4096];
__declspec(align(4096)) static uint8_t s_respBuffer[4096];
__declspec(align(4096)) static uint8_t s_framebufferMemory[4 * 1024 * 1024];
#else
static uint8_t s_cmdBuffer[4096] __attribute__((aligned(4096)));
static uint8_t s_respBuffer[4096] __attribute__((aligned(4096)));
static uint8_t s_framebufferMemory[4 * 1024 * 1024] __attribute__((aligned(4096)));
#endif

// ================================================================
// Helper Functions
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

static void memcopy(void* dst, const void* src, size_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
}

// ================================================================
// MMIO Helpers
// ================================================================

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr);
    *ptr = value;
    MEMORY_BARRIER();
#endif
}

static inline uint32_t mmio_read32(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return 0;
#else
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr);
    uint32_t value = *ptr;
    MEMORY_BARRIER();
    return value;
#endif
}

// Get physical address (simple identity mapping assumed)
static inline uint64_t virt_to_phys(void* ptr)
{
    return reinterpret_cast<uint64_t>(ptr);
}

// ================================================================
// Virtqueue Operations
// ================================================================

static bool setup_virtqueue(GpuDevice* dev, uint16_t queueIndex, Virtqueue* vq)
{
    /*
     * STUB: Initialize a virtqueue
     * 
     * Full implementation would:
     * 1. Select queue via QUEUE_SEL register
     * 2. Read QUEUE_NUM_MAX for max size
     * 3. Allocate descriptor, avail, and used rings
     * 4. Set QUEUE_DESC, QUEUE_AVAIL, QUEUE_USED addresses
     * 5. Set QUEUE_NUM and QUEUE_READY
     */
    
    (void)dev;
    (void)queueIndex;
    
    memzero(vq, sizeof(Virtqueue));
    vq->index = queueIndex;
    vq->size = 128;  // Default size
    
    // Would allocate memory for rings here
    
    return true;
}

static bool send_command(GpuDevice* dev, const void* cmd, size_t cmdSize,
                         void* resp, size_t respSize)
{
    /*
     * STUB: Send command via controlQ
     * 
     * Full implementation would:
     * 1. Allocate descriptor for command buffer
     * 2. Allocate descriptor for response buffer
     * 3. Add to available ring
     * 4. Notify device
     * 5. Wait for response (poll or interrupt)
     * 6. Process used ring
     */
    
    (void)dev;
    
    // Copy command to buffer
    if (cmdSize > sizeof(s_cmdBuffer)) return false;
    memcopy(s_cmdBuffer, cmd, cmdSize);
    
    // For now, simulate response
    if (resp && respSize >= sizeof(CtrlHeader)) {
        CtrlHeader* respHeader = static_cast<CtrlHeader*>(resp);
        respHeader->type = RESP_OK_NODATA;
        respHeader->flags = 0;
    }
    
    kernel::serial::puts("[VIRTIO-GPU] Command sent (stub)\n");
    return true;
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
    kernel::serial::puts("[VIRTIO-GPU] Initializing VirtIO GPU driver...\n");
    
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;
    s_initialized = true;
    
    // Probe for devices
    probe();
    
    kernel::serial::puts("[VIRTIO-GPU] Driver initialized, ");
    kernel::serial::put_hex32(s_deviceCount);
    kernel::serial::puts(" device(s) found\n");
}

int probe()
{
    /*
     * STUB: Probe for VirtIO GPU devices
     * 
     * Full implementation would:
     * 1. Scan PCI bus for VirtIO vendor (0x1AF4) and GPU device
     * 2. Check MMIO regions for VirtIO magic and GPU device type
     * 3. Initialize found devices
     */
    
    kernel::serial::puts("[VIRTIO-GPU] Probing for devices...\n");
    
    // For now, no devices found (would need PCI enumeration)
    // On QEMU with -device virtio-gpu, device would be found
    
    return s_deviceCount;
}

GpuDevice* get_device(int index)
{
    if (index < 0 || index >= s_deviceCount) {
        return nullptr;
    }
    return &s_devices[index];
}

int device_count()
{
    return s_deviceCount;
}

// ================================================================
// Device Operations
// ================================================================

GpuStatus init_device(GpuDevice* dev)
{
    /*
     * STUB: Initialize a VirtIO GPU device
     * 
     * Full implementation would:
     * 1. Reset device (STATUS = 0)
     * 2. Set ACKNOWLEDGE bit
     * 3. Set DRIVER bit
     * 4. Read device features
     * 5. Negotiate features
     * 6. Set FEATURES_OK
     * 7. Setup virtqueues
     * 8. Set DRIVER_OK
     * 9. Query display info
     */
    
    if (dev == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    kernel::serial::puts("[VIRTIO-GPU] Initializing device...\n");
    
    // Setup virtqueues
    if (!setup_virtqueue(dev, 0, &dev->controlQ)) {
        return GPU_ERR_INIT_FAIL;
    }
    if (!setup_virtqueue(dev, 1, &dev->cursorQ)) {
        return GPU_ERR_INIT_FAIL;
    }
    
    // Initialize state
    dev->nextResourceId = 1;
    dev->fbResourceId = 0;
    dev->framesDisplayed = 0;
    dev->flushCount = 0;
    
    // Get display info
    GpuStatus status = get_display_info(dev);
    if (status != GPU_OK) {
        kernel::serial::puts("[VIRTIO-GPU] Failed to get display info\n");
    }
    
    dev->initialized = true;
    kernel::serial::puts("[VIRTIO-GPU] Device initialized\n");
    
    return GPU_OK;
}

GpuStatus reset_device(GpuDevice* dev)
{
    if (dev == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    // Write 0 to status register
    if (!dev->isPci) {
        mmio_write32(dev->baseAddr + mmio::STATUS, 0);
    }
    
    dev->initialized = false;
    
    return GPU_OK;
}

GpuStatus get_display_info(GpuDevice* dev)
{
    /*
     * STUB: Query display information
     * 
     * Full implementation would send GET_DISPLAY_INFO command
     * and parse the response.
     */
    
    if (dev == nullptr || !dev->initialized) {
        return GPU_ERR_NO_DEVICE;
    }
    
    // Build command
    CtrlHeader cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.type = CMD_GET_DISPLAY_INFO;
    
    // Send command
    RespDisplayInfo resp;
    memzero(&resp, sizeof(resp));
    
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    // Parse response (stub - use defaults)
    dev->numScanouts = 1;
    dev->displays[0].width = 1024;
    dev->displays[0].height = 768;
    dev->displays[0].enabled = true;
    
    kernel::serial::puts("[VIRTIO-GPU] Display 0: ");
    kernel::serial::put_hex32(dev->displays[0].width);
    kernel::serial::putc('x');
    kernel::serial::put_hex32(dev->displays[0].height);
    kernel::serial::putc('\n');
    
    return GPU_OK;
}

// ================================================================
// Framebuffer Operations
// ================================================================

GpuStatus setup_framebuffer(GpuDevice* dev, uint32_t width, uint32_t height,
                            uint32_t scanoutId)
{
    /*
     * Setup framebuffer for a scanout
     * 
     * Steps:
     * 1. Create 2D resource for framebuffer
     * 2. Allocate backing memory
     * 3. Attach backing to resource
     * 4. Set scanout to use resource
     */
    
    if (dev == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    if (scanoutId >= dev->numScanouts) {
        return GPU_ERR_INVALID;
    }
    
    // Use display dimensions if not specified
    if (width == 0) width = dev->displays[scanoutId].width;
    if (height == 0) height = dev->displays[scanoutId].height;
    
    // Calculate framebuffer size (BGRA = 4 bytes per pixel)
    size_t fbSize = width * height * 4;
    if (fbSize > sizeof(s_framebufferMemory)) {
        kernel::serial::puts("[VIRTIO-GPU] Framebuffer too large\n");
        return GPU_ERR_NO_MEMORY;
    }
    
    kernel::serial::puts("[VIRTIO-GPU] Setting up framebuffer ");
    kernel::serial::put_hex32(width);
    kernel::serial::putc('x');
    kernel::serial::put_hex32(height);
    kernel::serial::putc('\n');
    
    // Create resource
    uint32_t resourceId;
    GpuStatus status = create_resource_2d(dev, &resourceId, width, height,
                                          FORMAT_B8G8R8A8_UNORM);
    if (status != GPU_OK) {
        return status;
    }
    
    // Use static framebuffer memory
    dev->fbBuffer = s_framebufferMemory;
    dev->fbBufferPhys = virt_to_phys(s_framebufferMemory);
    dev->fbBufferSize = fbSize;
    
    // Clear framebuffer
    memzero(dev->fbBuffer, fbSize);
    
    // Attach backing
    status = attach_backing(dev, resourceId, dev->fbBufferPhys, fbSize);
    if (status != GPU_OK) {
        destroy_resource(dev, resourceId);
        return status;
    }
    
    // Set scanout
    SetScanout cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_SET_SCANOUT;
    cmd.scanoutId = scanoutId;
    cmd.resourceId = resourceId;
    cmd.rect.x = 0;
    cmd.rect.y = 0;
    cmd.rect.width = width;
    cmd.rect.height = height;
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    // Store framebuffer info
    dev->fbResourceId = resourceId;
    dev->fbWidth = width;
    dev->fbHeight = height;
    dev->fbFormat = FORMAT_B8G8R8A8_UNORM;
    
    kernel::serial::puts("[VIRTIO-GPU] Framebuffer ready\n");
    
    return GPU_OK;
}

uint8_t* get_framebuffer(GpuDevice* dev)
{
    if (dev == nullptr || !dev->initialized) {
        return nullptr;
    }
    return dev->fbBuffer;
}

uint32_t get_framebuffer_width(GpuDevice* dev)
{
    return dev ? dev->fbWidth : 0;
}

uint32_t get_framebuffer_height(GpuDevice* dev)
{
    return dev ? dev->fbHeight : 0;
}

uint32_t get_framebuffer_pitch(GpuDevice* dev)
{
    return dev ? dev->fbWidth * 4 : 0;  // BGRA = 4 bytes
}

GpuStatus flush_framebuffer(GpuDevice* dev, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height)
{
    /*
     * Flush framebuffer region to display
     * 
     * Steps:
     * 1. Transfer updated region to host
     * 2. Flush resource to display
     */
    
    if (dev == nullptr || !dev->initialized || dev->fbResourceId == 0) {
        return GPU_ERR_NO_DEVICE;
    }
    
    // Default to full framebuffer
    if (width == 0) width = dev->fbWidth;
    if (height == 0) height = dev->fbHeight;
    
    // Clamp to framebuffer bounds
    if (x + width > dev->fbWidth) width = dev->fbWidth - x;
    if (y + height > dev->fbHeight) height = dev->fbHeight - y;
    
    // Transfer to host
    GpuStatus status = transfer_to_host(dev, dev->fbResourceId, x, y, width, height);
    if (status != GPU_OK) {
        return status;
    }
    
    // Flush resource
    ResourceFlush cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_RESOURCE_FLUSH;
    cmd.resourceId = dev->fbResourceId;
    cmd.rect.x = x;
    cmd.rect.y = y;
    cmd.rect.width = width;
    cmd.rect.height = height;
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    ++dev->flushCount;
    ++dev->framesDisplayed;
    
    return GPU_OK;
}

GpuStatus flush_all(GpuDevice* dev)
{
    return flush_framebuffer(dev, 0, 0, 0, 0);
}

// ================================================================
// Cursor Operations
// ================================================================

GpuStatus set_cursor(GpuDevice* dev, uint32_t resourceId, 
                     uint32_t hotX, uint32_t hotY)
{
    if (dev == nullptr || !dev->initialized) {
        return GPU_ERR_NO_DEVICE;
    }
    
    UpdateCursor cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_UPDATE_CURSOR;
    cmd.pos.scanoutId = 0;
    cmd.resourceId = resourceId;
    cmd.hotX = hotX;
    cmd.hotY = hotY;
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    return GPU_OK;
}

GpuStatus move_cursor(GpuDevice* dev, uint32_t x, uint32_t y)
{
    if (dev == nullptr || !dev->initialized) {
        return GPU_ERR_NO_DEVICE;
    }
    
    MoveCursor cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_MOVE_CURSOR;
    cmd.pos.scanoutId = 0;
    cmd.pos.x = x;
    cmd.pos.y = y;
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    return GPU_OK;
}

GpuStatus hide_cursor(GpuDevice* dev)
{
    // Set cursor resource to 0 to hide
    return set_cursor(dev, 0, 0, 0);
}

// ================================================================
// Resource Management
// ================================================================

GpuStatus create_resource_2d(GpuDevice* dev, uint32_t* resourceIdOut,
                             uint32_t width, uint32_t height, GpuFormat format)
{
    if (dev == nullptr || resourceIdOut == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    uint32_t resourceId = dev->nextResourceId++;
    
    ResourceCreate2d cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_RESOURCE_CREATE_2D;
    cmd.resourceId = resourceId;
    cmd.format = static_cast<uint32_t>(format);
    cmd.width = width;
    cmd.height = height;
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    *resourceIdOut = resourceId;
    
    kernel::serial::puts("[VIRTIO-GPU] Created resource ");
    kernel::serial::put_hex32(resourceId);
    kernel::serial::putc('\n');
    
    return GPU_OK;
}

GpuStatus attach_backing(GpuDevice* dev, uint32_t resourceId,
                         uint64_t physAddr, size_t size)
{
    if (dev == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    // Build command with inline memory entry
    struct {
        ResourceAttachBacking header;
        MemEntry              entry;
    } cmd;
    
    memzero(&cmd, sizeof(cmd));
    cmd.header.header.type = CMD_RESOURCE_ATTACH_BACKING;
    cmd.header.resourceId = resourceId;
    cmd.header.numEntries = 1;
    cmd.entry.addr = physAddr;
    cmd.entry.length = static_cast<uint32_t>(size);
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    return GPU_OK;
}

GpuStatus detach_backing(GpuDevice* dev, uint32_t resourceId)
{
    if (dev == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    ResourceDetachBacking cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_RESOURCE_DETACH_BACKING;
    cmd.resourceId = resourceId;
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    return GPU_OK;
}

GpuStatus transfer_to_host(GpuDevice* dev, uint32_t resourceId,
                           uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (dev == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    TransferToHost2d cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_TRANSFER_TO_HOST_2D;
    cmd.resourceId = resourceId;
    cmd.rect.x = x;
    cmd.rect.y = y;
    cmd.rect.width = width;
    cmd.rect.height = height;
    cmd.offset = 0;  // Offset in backing
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    return GPU_OK;
}

GpuStatus destroy_resource(GpuDevice* dev, uint32_t resourceId)
{
    if (dev == nullptr) {
        return GPU_ERR_INVALID;
    }
    
    ResourceUnref cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.header.type = CMD_RESOURCE_UNREF;
    cmd.resourceId = resourceId;
    
    CtrlHeader resp;
    if (!send_command(dev, &cmd, sizeof(cmd), &resp, sizeof(resp))) {
        return GPU_ERR_IO;
    }
    
    kernel::serial::puts("[VIRTIO-GPU] Destroyed resource ");
    kernel::serial::put_hex32(resourceId);
    kernel::serial::putc('\n');
    
    return GPU_OK;
}

// ================================================================
// Interrupt Handling
// ================================================================

void irq_handler()
{
    /*
     * STUB: Handle VirtIO GPU interrupt
     * 
     * Full implementation would:
     * 1. Check interrupt status register
     * 2. Process completed commands in used ring
     * 3. Acknowledge interrupt
     */
    
    kernel::serial::puts("[VIRTIO-GPU] IRQ\n");
}

void poll(GpuDevice* dev)
{
    /*
     * STUB: Poll for command completion
     * 
     * Used when not using interrupts.
     */
    
    (void)dev;
}

// ================================================================
// Framebuffer Integration
// ================================================================

GpuStatus register_as_framebuffer(GpuDevice* dev)
{
    /*
     * STUB: Register with kernel framebuffer subsystem
     * 
     * Full implementation would call kernel::framebuffer functions
     * to register this as the system framebuffer.
     */
    
    if (dev == nullptr || !dev->initialized || dev->fbResourceId == 0) {
        return GPU_ERR_NO_DEVICE;
    }
    
    kernel::serial::puts("[VIRTIO-GPU] Registered as system framebuffer\n");
    
    return GPU_OK;
}

// ================================================================
// Debug/Status
// ================================================================

void print_status(GpuDevice* dev)
{
    if (dev == nullptr) {
        kernel::serial::puts("[VIRTIO-GPU] No device\n");
        return;
    }
    
    kernel::serial::puts("[VIRTIO-GPU] Device status:\n");
    kernel::serial::puts("  Initialized: ");
    kernel::serial::puts(dev->initialized ? "yes" : "no");
    kernel::serial::puts("\n  3D support:  ");
    kernel::serial::puts(dev->has3D ? "yes" : "no");
    kernel::serial::puts("\n  Scanouts:    ");
    kernel::serial::put_hex32(dev->numScanouts);
    kernel::serial::putc('\n');
    
    for (uint32_t i = 0; i < dev->numScanouts; ++i) {
        kernel::serial::puts("  Display ");
        kernel::serial::put_hex32(i);
        kernel::serial::puts(": ");
        kernel::serial::put_hex32(dev->displays[i].width);
        kernel::serial::putc('x');
        kernel::serial::put_hex32(dev->displays[i].height);
        kernel::serial::puts(dev->displays[i].enabled ? " (enabled)" : " (disabled)");
        kernel::serial::putc('\n');
    }
    
    if (dev->fbResourceId != 0) {
        kernel::serial::puts("  Framebuffer: ");
        kernel::serial::put_hex32(dev->fbWidth);
        kernel::serial::putc('x');
        kernel::serial::put_hex32(dev->fbHeight);
        kernel::serial::puts(" @ ");
        kernel::serial::put_hex64(reinterpret_cast<uint64_t>(dev->fbBuffer));
        kernel::serial::putc('\n');
    }
    
    kernel::serial::puts("  Frames:      ");
    kernel::serial::put_hex32(dev->framesDisplayed);
    kernel::serial::puts("\n  Flushes:     ");
    kernel::serial::put_hex32(dev->flushCount);
    kernel::serial::putc('\n');
}

void print_all_devices()
{
    kernel::serial::puts("[VIRTIO-GPU] Device summary:\n");
    kernel::serial::puts("  Total devices: ");
    kernel::serial::put_hex32(s_deviceCount);
    kernel::serial::putc('\n');
    
    for (int i = 0; i < s_deviceCount; ++i) {
        kernel::serial::puts("\n  Device ");
        kernel::serial::put_hex32(i);
        kernel::serial::puts(":\n");
        print_status(&s_devices[i]);
    }
}

const char* status_string(GpuStatus status)
{
    switch (status) {
        case GPU_OK:              return "OK";
        case GPU_ERR_NOT_FOUND:   return "Not found";
        case GPU_ERR_NO_DEVICE:   return "No device";
        case GPU_ERR_INIT_FAIL:   return "Initialization failed";
        case GPU_ERR_NO_MEMORY:   return "Out of memory";
        case GPU_ERR_INVALID:     return "Invalid parameter";
        case GPU_ERR_IO:          return "I/O error";
        case GPU_ERR_TIMEOUT:     return "Timeout";
        case GPU_ERR_UNSUPPORTED: return "Unsupported";
        default:                  return "Unknown error";
    }
}

} // namespace gpu
} // namespace virtio
} // namespace kernel
