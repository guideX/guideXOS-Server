// VirtIO Block Device Implementation
//
// Implements VirtIO block device (virtio-blk) support.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/virtio_block.h"
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
namespace block {

// ================================================================
// Internal state
// ================================================================

static BlockDevice* s_devices[MAX_BLOCK_DEVICES];
static int s_deviceCount = 0;
static bool s_initialized = false;

// ================================================================
// Memory helpers
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
// MMIO helpers
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

static inline void mmio_write64(uint64_t addr, uint64_t value)
{
    mmio_write32(addr, static_cast<uint32_t>(value));
    mmio_write32(addr + 4, static_cast<uint32_t>(value >> 32));
}

// ================================================================
// BlockDevice Implementation
// ================================================================

BlockDevice::BlockDevice()
    : initialized(false)
    , readOnly(false)
    , requestBuffer(nullptr)
    , requestBufferSize(0)
{
    memzero(&config, sizeof(config));
    memzero(&requestQueue, sizeof(requestQueue));
    memzero(&stats, sizeof(stats));
}

BlockDevice::~BlockDevice()
{
    reset();
}

bool BlockDevice::init()
{
    if (initialized) return true;
    
    // Read magic value
    uint32_t magic = mmio_read32(baseAddr + mmio::MAGIC_VALUE);
    if (magic != mmio::MAGIC) {
        kernel::serial::puts("VirtIO Block: Invalid magic value\n");
        return false;
    }
    
    // Check version
    uint32_t version = mmio_read32(baseAddr + mmio::VERSION);
    if (version != mmio::VERSION_LEGACY && version != mmio::VERSION_MODERN) {
        kernel::serial::puts("VirtIO Block: Unsupported version\n");
        return false;
    }
    
    // Check device type
    uint32_t deviceId = mmio_read32(baseAddr + mmio::DEVICE_ID);
    if (deviceId != DEVICE_BLOCK) {
        return false;  // Not a block device
    }
    
    // Reset device
    setStatus(0);
    
    // Acknowledge device
    setStatus(STATUS_ACKNOWLEDGE);
    
    // Set driver loaded
    setStatus(getStatus() | STATUS_DRIVER);
    
    // Negotiate features
    uint64_t deviceFeatures = getFeatures();
    uint64_t driverFeatures = 0;
    
    // Accept features we support
    if (deviceFeatures & FEATURE_BLK_SIZE) {
        driverFeatures |= FEATURE_BLK_SIZE;
    }
    if (deviceFeatures & FEATURE_FLUSH) {
        driverFeatures |= FEATURE_FLUSH;
    }
    if (deviceFeatures & FEATURE_RO) {
        driverFeatures |= FEATURE_RO;
        readOnly = true;
    }
    
    // Accept common features
    if (deviceFeatures & FEATURE_VERSION_1) {
        driverFeatures |= FEATURE_VERSION_1;
    }
    
    setFeatures(driverFeatures);
    negotiatedFeatures = driverFeatures;
    
    // Set features OK
    setStatus(getStatus() | STATUS_FEATURES_OK);
    
    // Verify features accepted
    if (!(getStatus() & STATUS_FEATURES_OK)) {
        kernel::serial::puts("VirtIO Block: Feature negotiation failed\n");
        setStatus(STATUS_FAILED);
        return false;
    }
    
    // Read device configuration
    for (size_t i = 0; i < sizeof(config); i += 4) {
        uint32_t val = mmio_read32(baseAddr + mmio::CONFIG + i);
        memcopy(reinterpret_cast<uint8_t*>(&config) + i, &val, 4);
    }
    
    // Setup request queue (queue 0)
    if (!setupQueue(0, &requestQueue)) {
        kernel::serial::puts("VirtIO Block: Failed to setup queue\n");
        setStatus(STATUS_FAILED);
        return false;
    }
    
    // Allocate request buffer
    // We need space for: header + data + status
    requestBufferSize = sizeof(BlockReqHeader) + (128 * 512) + 1;
    // In real implementation, this would use DMA-capable memory allocation
    // For now, we use a static buffer approach
    
    // Mark device as ready
    setStatus(getStatus() | STATUS_DRIVER_OK);
    
    initialized = true;
    
    kernel::serial::puts("VirtIO Block: Initialized, capacity = ");
    // Print capacity (simplified)
    kernel::serial::put_hex32(static_cast<uint32_t>(config.capacity));
    kernel::serial::puts(" sectors\n");
    
    return true;
}

uint8_t BlockDevice::getStatus() const
{
    return static_cast<uint8_t>(mmio_read32(baseAddr + mmio::STATUS));
}

void BlockDevice::setStatus(uint8_t status)
{
    mmio_write32(baseAddr + mmio::STATUS, status);
}

void BlockDevice::reset()
{
    setStatus(0);
    initialized = false;
}

uint64_t BlockDevice::getFeatures() const
{
    uint64_t features = 0;
    
    // Read low 32 bits
    mmio_write32(baseAddr + mmio::DEVICE_FEATURES_SEL, 0);
    features = mmio_read32(baseAddr + mmio::DEVICE_FEATURES);
    
    // Read high 32 bits
    mmio_write32(baseAddr + mmio::DEVICE_FEATURES_SEL, 1);
    features |= static_cast<uint64_t>(mmio_read32(baseAddr + mmio::DEVICE_FEATURES)) << 32;
    
    return features;
}

void BlockDevice::setFeatures(uint64_t features)
{
    // Write low 32 bits
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES_SEL, 0);
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES, static_cast<uint32_t>(features));
    
    // Write high 32 bits
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES_SEL, 1);
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES, static_cast<uint32_t>(features >> 32));
}

bool BlockDevice::setupQueue(uint16_t index, Virtqueue* vq)
{
    if (!vq) return false;
    
    // Select queue
    mmio_write32(baseAddr + mmio::QUEUE_SEL, index);
    
    // Check if queue exists
    uint32_t maxSize = mmio_read32(baseAddr + mmio::QUEUE_NUM_MAX);
    if (maxSize == 0) {
        return false;
    }
    
    // Use max size (typically 128 or 256)
    uint16_t queueSize = (maxSize > 256) ? 256 : static_cast<uint16_t>(maxSize);
    
    // Calculate memory requirements
    size_t descSize = queueSize * sizeof(VringDesc);
    size_t availSize = sizeof(uint16_t) * (2 + queueSize + 1);
    size_t usedSize = sizeof(uint16_t) * 2 + sizeof(VringUsedElem) * queueSize + sizeof(uint16_t);
    
    // In real implementation, allocate DMA-capable, page-aligned memory
    // For now, we assume the queue structures are pre-allocated
    
    vq->size = queueSize;
    vq->index = index;
    vq->lastUsedIdx = 0;
    vq->freeHead = 0;
    vq->numFree = queueSize;
    
    // Initialize descriptor free list
    if (vq->desc) {
        for (uint16_t i = 0; i < queueSize - 1; ++i) {
            vq->desc[i].next = i + 1;
        }
        vq->desc[queueSize - 1].next = 0xFFFF;  // End of list
    }
    
    // Set queue size
    mmio_write32(baseAddr + mmio::QUEUE_NUM, queueSize);
    
    // Set queue addresses (for modern MMIO)
    mmio_write64(baseAddr + mmio::QUEUE_DESC_LOW, vq->descPhys);
    mmio_write64(baseAddr + mmio::QUEUE_AVAIL_LOW, vq->availPhys);
    mmio_write64(baseAddr + mmio::QUEUE_USED_LOW, vq->usedPhys);
    
    // Enable queue
    mmio_write32(baseAddr + mmio::QUEUE_READY, 1);
    
    return true;
}

void BlockDevice::notifyQueue(uint16_t index)
{
    mmio_write32(baseAddr + mmio::QUEUE_NOTIFY, index);
}

uint32_t BlockDevice::acknowledgeInterrupt()
{
    uint32_t status = mmio_read32(baseAddr + mmio::INTERRUPT_STATUS);
    mmio_write32(baseAddr + mmio::INTERRUPT_ACK, status);
    return status;
}

int BlockDevice::submitRequest(uint32_t type, uint64_t sector, uint32_t count, void* buffer)
{
    if (!initialized) return -1;
    if (readOnly && (type == REQ_TYPE_OUT)) return -2;
    
    // For actual implementation:
    // 1. Allocate descriptors from free list
    // 2. Set up header descriptor (device-readable)
    // 3. Set up data descriptor(s) (device-readable for write, device-writable for read)
    // 4. Set up status descriptor (device-writable)
    // 5. Add to available ring
    // 6. Notify device
    // 7. Wait for completion in used ring
    
    // Simplified stub - actual implementation would use virtqueue
    (void)type;
    (void)sector;
    (void)count;
    (void)buffer;
    
    return 0;
}

int BlockDevice::waitForRequest()
{
    // Poll for completion
    while (requestQueue.lastUsedIdx == requestQueue.used->idx) {
        // Could add timeout or yield here
        MEMORY_BARRIER();
    }
    
    // Process completed request
    uint16_t usedIdx = requestQueue.used->idx;
    VringUsedElem* elem = &requestQueue.used->ring[requestQueue.lastUsedIdx % requestQueue.size];
    
    // Get status from last byte of request
    // uint8_t status = ...;
    
    requestQueue.lastUsedIdx = usedIdx;
    
    return 0;  // STATUS_OK
}

int BlockDevice::read(uint64_t sector, uint32_t count, void* buffer)
{
    if (!initialized) return -1;
    if (!buffer) return -2;
    if (sector + count > config.capacity) return -3;
    
    stats.reads++;
    stats.bytesRead += count * 512;
    
    return submitRequest(REQ_TYPE_IN, sector, count, buffer);
}

int BlockDevice::write(uint64_t sector, uint32_t count, const void* buffer)
{
    if (!initialized) return -1;
    if (!buffer) return -2;
    if (readOnly) return -3;
    if (sector + count > config.capacity) return -4;
    
    stats.writes++;
    stats.bytesWritten += count * 512;
    
    return submitRequest(REQ_TYPE_OUT, sector, count, const_cast<void*>(buffer));
}

int BlockDevice::flush()
{
    if (!initialized) return -1;
    if (!(negotiatedFeatures & FEATURE_FLUSH)) return 0;  // No flush needed
    
    stats.flushes++;
    return submitRequest(REQ_TYPE_FLUSH, 0, 0, nullptr);
}

int BlockDevice::discard(uint64_t sector, uint32_t count)
{
    if (!initialized) return -1;
    if (!(negotiatedFeatures & FEATURE_DISCARD)) return -2;
    
    return submitRequest(REQ_TYPE_DISCARD, sector, count, nullptr);
}

int BlockDevice::writeZeroes(uint64_t sector, uint32_t count)
{
    if (!initialized) return -1;
    if (!(negotiatedFeatures & FEATURE_WRITE_ZEROES)) return -2;
    
    return submitRequest(REQ_TYPE_WRITE_ZEROES, sector, count, nullptr);
}

uint64_t BlockDevice::getCapacity() const
{
    return config.capacity;
}

uint32_t BlockDevice::getBlockSize() const
{
    if (negotiatedFeatures & FEATURE_BLK_SIZE) {
        return config.blk_size;
    }
    return 512;  // Default sector size
}

bool BlockDevice::isReadOnly() const
{
    return readOnly;
}

void BlockDevice::getStats(BlockStats* outStats) const
{
    if (outStats) {
        *outStats = stats;
    }
}

// ================================================================
// Device Detection (MMIO)
// ================================================================

int detectMmio(uint64_t baseAddr, uint64_t size)
{
    if (s_deviceCount >= MAX_BLOCK_DEVICES) {
        return 0;
    }
    
    // Check magic value
    uint32_t magic = mmio_read32(baseAddr + mmio::MAGIC_VALUE);
    if (magic != mmio::MAGIC) {
        return 0;
    }
    
    // Check device ID
    uint32_t deviceId = mmio_read32(baseAddr + mmio::DEVICE_ID);
    if (deviceId != DEVICE_BLOCK) {
        return 0;
    }
    
    // Found a block device
    BlockDevice* dev = new BlockDevice();
    dev->setBaseAddress(baseAddr);
    
    if (dev->init()) {
        s_devices[s_deviceCount++] = dev;
        return 1;
    }
    
    delete dev;
    return 0;
}

int detectPci()
{
    // PCI detection requires PCI enumeration
    // Look for devices with:
    //   Vendor ID: 0x1AF4
    //   Device ID: 0x1001 (legacy) or 0x1042 (modern)
    
    // This is a stub - actual implementation would scan PCI bus
    return 0;
}

BlockDevice* getDevice(int index)
{
    if (index < 0 || index >= s_deviceCount) {
        return nullptr;
    }
    return s_devices[index];
}

int getDeviceCount()
{
    return s_deviceCount;
}

bool init()
{
    if (s_initialized) return true;
    
    s_deviceCount = 0;
    memzero(s_devices, sizeof(s_devices));
    
    // Try to detect MMIO devices at known addresses
    // QEMU virt machine often places virtio-blk at 0x0A003000+
    // This would be read from device tree in a full implementation
    
    s_initialized = true;
    return true;
}

} // namespace block
} // namespace virtio
} // namespace kernel
