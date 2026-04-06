// VirtIO Network Device Implementation
//
// Implements VirtIO network device (virtio-net) support.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/virtio_net.h"
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
namespace net {

// ================================================================
// Internal state
// ================================================================

static NetDevice* s_devices[MAX_NET_DEVICES];
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
// NetDevice Implementation
// ================================================================

NetDevice::NetDevice()
    : initialized(false)
    , hasCtrlQueue(false)
    , hasMergeableBuffers(false)
    , rxCallback(nullptr)
    , rxCallbackContext(nullptr)
{
    memzero(&config, sizeof(config));
    memzero(&rxQueue, sizeof(rxQueue));
    memzero(&txQueue, sizeof(txQueue));
    memzero(&ctrlQueue, sizeof(ctrlQueue));
    memzero(&stats, sizeof(stats));
    memzero(rxBuffers, sizeof(rxBuffers));
    memzero(txBuffers, sizeof(txBuffers));
}

NetDevice::~NetDevice()
{
    reset();
}

bool NetDevice::init()
{
    if (initialized) return true;
    
    // Read magic value
    uint32_t magic = mmio_read32(baseAddr + mmio::MAGIC_VALUE);
    if (magic != mmio::MAGIC) {
        kernel::serial::puts("VirtIO Net: Invalid magic value\n");
        return false;
    }
    
    // Check version
    uint32_t version = mmio_read32(baseAddr + mmio::VERSION);
    if (version != mmio::VERSION_LEGACY && version != mmio::VERSION_MODERN) {
        kernel::serial::puts("VirtIO Net: Unsupported version\n");
        return false;
    }
    
    // Check device type
    uint32_t deviceId = mmio_read32(baseAddr + mmio::DEVICE_ID);
    if (deviceId != DEVICE_NETWORK) {
        return false;  // Not a network device
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
    if (deviceFeatures & FEATURE_MAC) {
        driverFeatures |= FEATURE_MAC;
    }
    if (deviceFeatures & FEATURE_STATUS) {
        driverFeatures |= FEATURE_STATUS;
    }
    if (deviceFeatures & FEATURE_MTU) {
        driverFeatures |= FEATURE_MTU;
    }
    if (deviceFeatures & FEATURE_MRG_RXBUF) {
        driverFeatures |= FEATURE_MRG_RXBUF;
        hasMergeableBuffers = true;
    }
    if (deviceFeatures & FEATURE_CTRL_VQ) {
        driverFeatures |= FEATURE_CTRL_VQ;
        hasCtrlQueue = true;
    }
    if (deviceFeatures & FEATURE_CTRL_RX) {
        driverFeatures |= FEATURE_CTRL_RX;
    }
    if (deviceFeatures & FEATURE_CSUM) {
        driverFeatures |= FEATURE_CSUM;
    }
    if (deviceFeatures & FEATURE_GUEST_CSUM) {
        driverFeatures |= FEATURE_GUEST_CSUM;
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
        kernel::serial::puts("VirtIO Net: Feature negotiation failed\n");
        setStatus(STATUS_FAILED);
        return false;
    }
    
    // Read device configuration
    for (size_t i = 0; i < sizeof(config); i += 4) {
        uint32_t val = mmio_read32(baseAddr + mmio::CONFIG + i);
        memcopy(reinterpret_cast<uint8_t*>(&config) + i, &val, 4);
    }
    
    // Setup RX queue (queue 0)
    if (!setupQueue(0, &rxQueue)) {
        kernel::serial::puts("VirtIO Net: Failed to setup RX queue\n");
        setStatus(STATUS_FAILED);
        return false;
    }
    
    // Setup TX queue (queue 1)
    if (!setupQueue(1, &txQueue)) {
        kernel::serial::puts("VirtIO Net: Failed to setup TX queue\n");
        setStatus(STATUS_FAILED);
        return false;
    }
    
    // Setup control queue if supported (queue 2)
    if (hasCtrlQueue) {
        if (!setupQueue(2, &ctrlQueue)) {
            kernel::serial::puts("VirtIO Net: Control queue setup failed (continuing without)\n");
            hasCtrlQueue = false;
        }
    }
    
    // Mark device as ready
    setStatus(getStatus() | STATUS_DRIVER_OK);
    
    // Fill RX queue with buffers
    refillRxQueue();
    
    initialized = true;
    
    kernel::serial::puts("VirtIO Net: Initialized, MAC = ");
    for (int i = 0; i < 6; ++i) {
        if (i > 0) kernel::serial::putc(':');
        kernel::serial::put_hex8(config.mac[i]);
    }
    kernel::serial::puts("\n");
    
    return true;
}

uint8_t NetDevice::getStatus() const
{
    return static_cast<uint8_t>(mmio_read32(baseAddr + mmio::STATUS));
}

void NetDevice::setStatus(uint8_t status)
{
    mmio_write32(baseAddr + mmio::STATUS, status);
}

void NetDevice::reset()
{
    setStatus(0);
    initialized = false;
}

uint64_t NetDevice::getFeatures() const
{
    uint64_t features = 0;
    
    mmio_write32(baseAddr + mmio::DEVICE_FEATURES_SEL, 0);
    features = mmio_read32(baseAddr + mmio::DEVICE_FEATURES);
    
    mmio_write32(baseAddr + mmio::DEVICE_FEATURES_SEL, 1);
    features |= static_cast<uint64_t>(mmio_read32(baseAddr + mmio::DEVICE_FEATURES)) << 32;
    
    return features;
}

void NetDevice::setFeatures(uint64_t features)
{
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES_SEL, 0);
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES, static_cast<uint32_t>(features));
    
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES_SEL, 1);
    mmio_write32(baseAddr + mmio::DRIVER_FEATURES, static_cast<uint32_t>(features >> 32));
}

bool NetDevice::setupQueue(uint16_t index, Virtqueue* vq)
{
    if (!vq) return false;
    
    // Select queue
    mmio_write32(baseAddr + mmio::QUEUE_SEL, index);
    
    // Check if queue exists
    uint32_t maxSize = mmio_read32(baseAddr + mmio::QUEUE_NUM_MAX);
    if (maxSize == 0) {
        return false;
    }
    
    // Use max size up to 256
    uint16_t queueSize = (maxSize > 256) ? 256 : static_cast<uint16_t>(maxSize);
    
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
        vq->desc[queueSize - 1].next = 0xFFFF;
    }
    
    mmio_write32(baseAddr + mmio::QUEUE_NUM, queueSize);
    mmio_write64(baseAddr + mmio::QUEUE_DESC_LOW, vq->descPhys);
    mmio_write64(baseAddr + mmio::QUEUE_AVAIL_LOW, vq->availPhys);
    mmio_write64(baseAddr + mmio::QUEUE_USED_LOW, vq->usedPhys);
    mmio_write32(baseAddr + mmio::QUEUE_READY, 1);
    
    return true;
}

void NetDevice::notifyQueue(uint16_t index)
{
    mmio_write32(baseAddr + mmio::QUEUE_NOTIFY, index);
}

uint32_t NetDevice::acknowledgeInterrupt()
{
    uint32_t status = mmio_read32(baseAddr + mmio::INTERRUPT_STATUS);
    mmio_write32(baseAddr + mmio::INTERRUPT_ACK, status);
    return status;
}

void NetDevice::getMacAddress(uint8_t* mac) const
{
    if (mac) {
        memcopy(mac, config.mac, 6);
    }
}

bool NetDevice::setMacAddress(const uint8_t* mac)
{
    if (!mac) return false;
    if (!hasCtrlQueue) return false;
    
    return sendCtrlCommand(CTRL_MAC, CTRL_MAC_ADDR_SET, mac, 6);
}

uint16_t NetDevice::getMtu() const
{
    if (negotiatedFeatures & FEATURE_MTU) {
        return config.mtu;
    }
    return 1500;  // Default MTU
}

bool NetDevice::isLinkUp() const
{
    if (negotiatedFeatures & FEATURE_STATUS) {
        // Re-read status from device
        uint16_t status = static_cast<uint16_t>(mmio_read32(baseAddr + mmio::CONFIG));
        return (status & STATUS_LINK_UP) != 0;
    }
    return true;  // Assume up if status feature not available
}

uint32_t NetDevice::getLinkSpeed() const
{
    if (negotiatedFeatures & FEATURE_SPEED_DUPLEX) {
        return config.speed;
    }
    return 0;
}

int NetDevice::send(const void* data, size_t len)
{
    if (!initialized) return -1;
    if (!data || len == 0) return -2;
    if (len > getMtu() + 14) return -3;  // MTU + ethernet header
    
    // Allocate TX buffer
    int bufIdx = allocTxBuffer();
    if (bufIdx < 0) {
        stats.txDropped++;
        return -4;
    }
    
    TxBuffer* buf = &txBuffers[bufIdx];
    
    // Prepare virtio-net header
    size_t hdrSize = hasMergeableBuffers ? NET_HEADER_SIZE_MRG : NET_HEADER_SIZE;
    NetHeader* hdr = reinterpret_cast<NetHeader*>(buf->data);
    memzero(hdr, hdrSize);
    hdr->flags = 0;
    hdr->gso_type = GSO_NONE;
    
    // Copy packet data after header
    memcopy(buf->data + hdrSize, data, len);
    
    // In real implementation:
    // 1. Allocate descriptor(s)
    // 2. Set up descriptor for header + data
    // 3. Add to available ring
    // 4. Notify device
    
    notifyQueue(1);  // TX queue
    
    stats.txPackets++;
    stats.txBytes += len;
    
    // For now, assume success (polling would wait for completion)
    freeTxBuffer(bufIdx);
    
    return 0;
}

int NetDevice::receive(void* buffer, size_t bufferLen)
{
    if (!initialized) return -1;
    if (!buffer || bufferLen == 0) return -2;
    
    // Check for completed receives
    if (rxQueue.lastUsedIdx == rxQueue.used->idx) {
        return 0;  // No packets available
    }
    
    // Get used element
    uint16_t usedIdx = rxQueue.lastUsedIdx % rxQueue.size;
    VringUsedElem* elem = &rxQueue.used->ring[usedIdx];
    
    // Find corresponding RX buffer
    uint16_t descIdx = static_cast<uint16_t>(elem->id);
    if (descIdx >= RX_BUFFER_COUNT) {
        stats.rxErrors++;
        return -3;
    }
    
    RxBuffer* rxBuf = &rxBuffers[descIdx];
    
    // Parse virtio-net header
    size_t hdrSize = hasMergeableBuffers ? NET_HEADER_SIZE_MRG : NET_HEADER_SIZE;
    size_t dataLen = elem->len - hdrSize;
    
    if (dataLen > bufferLen) {
        stats.rxDropped++;
        dataLen = bufferLen;
    }
    
    // Copy packet data (skip header)
    memcopy(buffer, rxBuf->data + hdrSize, dataLen);
    
    // Update index
    rxQueue.lastUsedIdx++;
    
    // Resubmit buffer to RX queue
    rxBuf->inUse = false;
    refillRxQueue();
    
    stats.rxPackets++;
    stats.rxBytes += dataLen;
    
    return static_cast<int>(dataLen);
}

bool NetDevice::hasPackets() const
{
    if (!initialized) return false;
    return rxQueue.lastUsedIdx != rxQueue.used->idx;
}

void NetDevice::setRxCallback(RxCallback callback, void* context)
{
    rxCallback = callback;
    rxCallbackContext = context;
}

void NetDevice::processRx()
{
    while (hasPackets()) {
        // Get used element
        uint16_t usedIdx = rxQueue.lastUsedIdx % rxQueue.size;
        VringUsedElem* elem = &rxQueue.used->ring[usedIdx];
        
        uint16_t descIdx = static_cast<uint16_t>(elem->id);
        if (descIdx < RX_BUFFER_COUNT) {
            RxBuffer* rxBuf = &rxBuffers[descIdx];
            
            size_t hdrSize = hasMergeableBuffers ? NET_HEADER_SIZE_MRG : NET_HEADER_SIZE;
            size_t dataLen = elem->len - hdrSize;
            
            if (rxCallback) {
                rxCallback(rxBuf->data + hdrSize, dataLen, rxCallbackContext);
            }
            
            stats.rxPackets++;
            stats.rxBytes += dataLen;
            
            rxBuf->inUse = false;
        }
        
        rxQueue.lastUsedIdx++;
    }
    
    refillRxQueue();
}

bool NetDevice::setPromiscuous(bool enable)
{
    if (!hasCtrlQueue) return false;
    
    uint8_t on = enable ? 1 : 0;
    return sendCtrlCommand(CTRL_RX, CTRL_RX_PROMISC, &on, 1);
}

void NetDevice::getStats(NetStats* outStats) const
{
    if (outStats) {
        *outStats = stats;
    }
}

void NetDevice::refillRxQueue()
{
    // Add free buffers to RX queue
    for (int i = 0; i < RX_BUFFER_COUNT; ++i) {
        if (!rxBuffers[i].inUse) {
            // In real implementation:
            // 1. Allocate descriptor
            // 2. Point to buffer (with virtio-net header space)
            // 3. Add to available ring
            rxBuffers[i].inUse = true;
        }
    }
    
    // Notify device
    notifyQueue(0);  // RX queue
}

int NetDevice::allocTxBuffer()
{
    for (int i = 0; i < TX_BUFFER_COUNT; ++i) {
        if (!txBuffers[i].inUse) {
            txBuffers[i].inUse = true;
            return i;
        }
    }
    return -1;
}

void NetDevice::freeTxBuffer(int index)
{
    if (index >= 0 && index < TX_BUFFER_COUNT) {
        txBuffers[index].inUse = false;
    }
}

bool NetDevice::sendCtrlCommand(uint8_t cls, uint8_t cmd, const void* data, size_t len)
{
    if (!hasCtrlQueue) return false;
    
    // Control command format:
    // 1. Header descriptor (device-readable): class + cmd
    // 2. Data descriptor (device-readable): command data
    // 3. Status descriptor (device-writable): 1-byte status
    
    // Simplified implementation - actual would use virtqueue
    (void)cls;
    (void)cmd;
    (void)data;
    (void)len;
    
    return true;
}

// ================================================================
// Device Detection
// ================================================================

int detectMmio(uint64_t baseAddr, uint64_t size)
{
    (void)size;
    
    if (s_deviceCount >= MAX_NET_DEVICES) {
        return 0;
    }
    
    // Check magic value
    uint32_t magic = mmio_read32(baseAddr + mmio::MAGIC_VALUE);
    if (magic != mmio::MAGIC) {
        return 0;
    }
    
    // Check device ID
    uint32_t deviceId = mmio_read32(baseAddr + mmio::DEVICE_ID);
    if (deviceId != DEVICE_NETWORK) {
        return 0;
    }
    
    // Found a network device
    NetDevice* dev = new NetDevice();
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
    //   Device ID: 0x1000 (legacy) or 0x1041 (modern)
    return 0;
}

NetDevice* getDevice(int index)
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
    
    s_initialized = true;
    return true;
}

} // namespace net
} // namespace virtio
} // namespace kernel
