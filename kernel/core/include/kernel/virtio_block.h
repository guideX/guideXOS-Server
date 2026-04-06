// VirtIO Block Device Driver
//
// Implements VirtIO block device (virtio-blk) support.
// Provides read/write access to virtual block devices.
//
// VirtIO Block Device Specification:
//   https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>
#include <kernel/virtio.h>

namespace kernel {
namespace virtio {
namespace block {

// ================================================================
// Block Device Feature Bits
// ================================================================

static const uint64_t FEATURE_SIZE_MAX    = (1ULL << 1);   // Max segment size
static const uint64_t FEATURE_SEG_MAX     = (1ULL << 2);   // Max segments per request
static const uint64_t FEATURE_GEOMETRY    = (1ULL << 4);   // Disk geometry available
static const uint64_t FEATURE_RO          = (1ULL << 5);   // Read-only device
static const uint64_t FEATURE_BLK_SIZE    = (1ULL << 6);   // Block size available
static const uint64_t FEATURE_FLUSH       = (1ULL << 9);   // Cache flush supported
static const uint64_t FEATURE_TOPOLOGY    = (1ULL << 10);  // Topology info available
static const uint64_t FEATURE_CONFIG_WCE  = (1ULL << 11);  // Writeback mode configurable
static const uint64_t FEATURE_DISCARD     = (1ULL << 13);  // Discard/TRIM supported
static const uint64_t FEATURE_WRITE_ZEROES = (1ULL << 14); // Write zeroes supported

// ================================================================
// Block Device Request Types
// ================================================================

static const uint32_t REQ_TYPE_IN         = 0;   // Read
static const uint32_t REQ_TYPE_OUT        = 1;   // Write
static const uint32_t REQ_TYPE_FLUSH      = 4;   // Flush
static const uint32_t REQ_TYPE_DISCARD    = 11;  // Discard
static const uint32_t REQ_TYPE_WRITE_ZEROES = 13; // Write zeroes

// ================================================================
// Block Device Status Codes
// ================================================================

static const uint8_t STATUS_OK            = 0;
static const uint8_t STATUS_IOERR         = 1;
static const uint8_t STATUS_UNSUPP        = 2;

// ================================================================
// Block Device Configuration (from device config space)
// ================================================================

struct BlockConfig {
    uint64_t capacity;       // Device capacity in 512-byte sectors
    uint32_t size_max;       // Max segment size (if FEATURE_SIZE_MAX)
    uint32_t seg_max;        // Max segments (if FEATURE_SEG_MAX)
    
    // Geometry (if FEATURE_GEOMETRY)
    struct {
        uint16_t cylinders;
        uint8_t  heads;
        uint8_t  sectors;
    } geometry;
    
    uint32_t blk_size;       // Block size (if FEATURE_BLK_SIZE)
    
    // Topology (if FEATURE_TOPOLOGY)
    struct {
        uint8_t  physical_block_exp;  // log2(physical/logical)
        uint8_t  alignment_offset;    // Offset of first aligned block
        uint16_t min_io_size;         // Min I/O size without performance penalty
        uint32_t opt_io_size;         // Optimal I/O size
    } topology;
    
    uint8_t  writeback;      // Writeback mode (if FEATURE_CONFIG_WCE)
    uint8_t  unused0;
    uint16_t num_queues;     // Number of request queues
    
    // Discard (if FEATURE_DISCARD)
    uint32_t max_discard_sectors;
    uint32_t max_discard_seg;
    uint32_t discard_sector_alignment;
    
    // Write zeroes (if FEATURE_WRITE_ZEROES)
    uint32_t max_write_zeroes_sectors;
    uint32_t max_write_zeroes_seg;
    uint8_t  write_zeroes_may_unmap;
    uint8_t  unused1[3];
} __attribute__((packed));

// ================================================================
// Block Device Request Header
// ================================================================

struct BlockReqHeader {
    uint32_t type;           // REQ_TYPE_*
    uint32_t reserved;       // Reserved (must be 0)
    uint64_t sector;         // Starting sector (512-byte units)
} __attribute__((packed));

// ================================================================
// Block Device Statistics
// ================================================================

struct BlockStats {
    uint64_t reads;          // Number of read operations
    uint64_t writes;         // Number of write operations
    uint64_t bytesRead;      // Total bytes read
    uint64_t bytesWritten;   // Total bytes written
    uint64_t errors;         // Number of errors
    uint64_t flushes;        // Number of flush operations
};

// ================================================================
// Block Device Class
// ================================================================

class BlockDevice : public VirtioDevice {
public:
    BlockDevice();
    virtual ~BlockDevice();
    
    // VirtioDevice interface
    bool init() override;
    uint32_t getDeviceType() const override { return DEVICE_BLOCK; }
    uint8_t getStatus() const override;
    void setStatus(uint8_t status) override;
    void reset() override;
    uint64_t getFeatures() const override;
    void setFeatures(uint64_t features) override;
    bool setupQueue(uint16_t index, Virtqueue* vq) override;
    void notifyQueue(uint16_t index) override;
    uint32_t acknowledgeInterrupt() override;
    
    // Block device operations
    
    // Read sectors from device
    // sector: starting sector number (512-byte sectors)
    // count: number of sectors to read
    // buffer: destination buffer (must be count * 512 bytes)
    // Returns: 0 on success, error code otherwise
    int read(uint64_t sector, uint32_t count, void* buffer);
    
    // Write sectors to device
    // sector: starting sector number
    // count: number of sectors to write
    // buffer: source buffer (must be count * 512 bytes)
    // Returns: 0 on success, error code otherwise
    int write(uint64_t sector, uint32_t count, const void* buffer);
    
    // Flush device cache
    int flush();
    
    // Discard sectors (TRIM)
    int discard(uint64_t sector, uint32_t count);
    
    // Write zeroes to sectors
    int writeZeroes(uint64_t sector, uint32_t count);
    
    // Get device capacity in sectors
    uint64_t getCapacity() const;
    
    // Get block size
    uint32_t getBlockSize() const;
    
    // Check if device is read-only
    bool isReadOnly() const;
    
    // Get device statistics
    void getStats(BlockStats* stats) const;
    
    // Check if device is initialized and ready
    bool isReady() const { return initialized; }
    
private:
    // Device configuration
    BlockConfig config;
    
    // Request queue
    Virtqueue requestQueue;
    
    // Statistics
    BlockStats stats;
    
    // State flags
    bool initialized;
    bool readOnly;
    
    // Internal request handling
    int submitRequest(uint32_t type, uint64_t sector, uint32_t count, void* buffer);
    int waitForRequest();
    
    // Buffer management
    uint8_t* requestBuffer;     // DMA-capable buffer for requests
    size_t requestBufferSize;
};

// ================================================================
// Block Device Detection and Management
// ================================================================

// Maximum number of block devices
static const int MAX_BLOCK_DEVICES = 8;

// Detect VirtIO block devices via PCI
int detectPci();

// Detect VirtIO block devices via MMIO
int detectMmio(uint64_t baseAddr, uint64_t size);

// Get block device by index
BlockDevice* getDevice(int index);

// Get number of detected block devices
int getDeviceCount();

// Initialize block subsystem
bool init();

} // namespace block
} // namespace virtio
} // namespace kernel
