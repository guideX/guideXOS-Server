// VirtIO Common Definitions
//
// VirtIO is a standardized interface for virtual I/O devices.
// This header provides common structures and constants used by
// all VirtIO device drivers.
//
// VirtIO Specification: https://docs.oasis-open.org/virtio/virtio/
//
// Supports both legacy (0.9.x) and modern (1.0+) VirtIO devices.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace virtio {

// ================================================================
// VirtIO Device IDs
// ================================================================

static const uint32_t DEVICE_NETWORK     = 1;
static const uint32_t DEVICE_BLOCK       = 2;
static const uint32_t DEVICE_CONSOLE     = 3;
static const uint32_t DEVICE_ENTROPY     = 4;
static const uint32_t DEVICE_BALLOON     = 5;
static const uint32_t DEVICE_IOMEM       = 6;
static const uint32_t DEVICE_RPMSG       = 7;
static const uint32_t DEVICE_SCSI        = 8;
static const uint32_t DEVICE_9P          = 9;
static const uint32_t DEVICE_RPROC_SERIAL = 11;
static const uint32_t DEVICE_CAIF        = 12;
static const uint32_t DEVICE_GPU         = 16;
static const uint32_t DEVICE_INPUT       = 18;
static const uint32_t DEVICE_VSOCK       = 19;
static const uint32_t DEVICE_CRYPTO      = 20;
static const uint32_t DEVICE_IOMMU       = 23;
static const uint32_t DEVICE_MEM         = 24;
static const uint32_t DEVICE_FS          = 26;
static const uint32_t DEVICE_PMEM        = 27;
static const uint32_t DEVICE_SOUND       = 25;

// ================================================================
// VirtIO PCI Vendor/Device IDs
// ================================================================

static const uint16_t PCI_VENDOR_ID = 0x1AF4;

// Transitional device IDs (legacy)
static const uint16_t PCI_DEVICE_NETWORK_LEGACY = 0x1000;
static const uint16_t PCI_DEVICE_BLOCK_LEGACY   = 0x1001;
static const uint16_t PCI_DEVICE_BALLOON_LEGACY = 0x1002;
static const uint16_t PCI_DEVICE_CONSOLE_LEGACY = 0x1003;
static const uint16_t PCI_DEVICE_SCSI_LEGACY    = 0x1004;
static const uint16_t PCI_DEVICE_ENTROPY_LEGACY = 0x1005;
static const uint16_t PCI_DEVICE_9P_LEGACY      = 0x1009;

// Modern device IDs (1.0+)
static const uint16_t PCI_DEVICE_BASE_MODERN = 0x1040;
// Actual device ID = 0x1040 + device_id

// ================================================================
// VirtIO Status Bits
// ================================================================

static const uint8_t STATUS_ACKNOWLEDGE = 1;    // Guest OS has found device
static const uint8_t STATUS_DRIVER      = 2;    // Guest OS knows how to drive device
static const uint8_t STATUS_DRIVER_OK   = 4;    // Driver setup complete
static const uint8_t STATUS_FEATURES_OK = 8;    // Feature negotiation complete
static const uint8_t STATUS_DEVICE_NEEDS_RESET = 64;  // Device error
static const uint8_t STATUS_FAILED      = 128;  // Something went wrong

// ================================================================
// Common Feature Bits
// ================================================================

// Feature bits 0-23 are device-specific
// Feature bits 24-37 are reserved for extensions
// Feature bit 32+ are for VirtIO 1.0+ features

static const uint64_t FEATURE_NOTIFY_ON_EMPTY   = (1ULL << 24);
static const uint64_t FEATURE_ANY_LAYOUT        = (1ULL << 27);
static const uint64_t FEATURE_RING_INDIRECT_DESC = (1ULL << 28);
static const uint64_t FEATURE_RING_EVENT_IDX    = (1ULL << 29);
static const uint64_t FEATURE_UNUSED            = (1ULL << 30);
static const uint64_t FEATURE_VERSION_1         = (1ULL << 32);  // VirtIO 1.0
static const uint64_t FEATURE_ACCESS_PLATFORM   = (1ULL << 33);
static const uint64_t FEATURE_RING_PACKED       = (1ULL << 34);
static const uint64_t FEATURE_IN_ORDER          = (1ULL << 35);
static const uint64_t FEATURE_ORDER_PLATFORM    = (1ULL << 36);
static const uint64_t FEATURE_SR_IOV            = (1ULL << 37);
static const uint64_t FEATURE_NOTIFICATION_DATA = (1ULL << 38);

// ================================================================
// Virtqueue Descriptor Flags
// ================================================================

static const uint16_t VRING_DESC_F_NEXT     = 1;   // Buffer continues via 'next'
static const uint16_t VRING_DESC_F_WRITE    = 2;   // Buffer is write-only (device)
static const uint16_t VRING_DESC_F_INDIRECT = 4;   // Buffer contains descriptor list

// Available ring flags
static const uint16_t VRING_AVAIL_F_NO_INTERRUPT = 1;

// Used ring flags
static const uint16_t VRING_USED_F_NO_NOTIFY = 1;

// ================================================================
// Virtqueue Structures (Split Ring format)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define VIRTIO_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define VIRTIO_PACKED
#endif

// Descriptor table entry
struct VringDesc {
    uint64_t addr;      // Buffer physical address
    uint32_t len;       // Buffer length
    uint16_t flags;     // VRING_DESC_F_* flags
    uint16_t next;      // Next descriptor (if flags & NEXT)
} VIRTIO_PACKED;

// Available ring
struct VringAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[1];   // Flexible array (use [1] for MSVC)
    // uint16_t used_event; // Only if FEATURE_EVENT_IDX
} VIRTIO_PACKED;

// Used ring element
struct VringUsedElem {
    uint32_t id;        // Descriptor index
    uint32_t len;       // Bytes written (for read buffers)
} VIRTIO_PACKED;

// Used ring
struct VringUsed {
    uint16_t flags;
    uint16_t idx;
    VringUsedElem ring[1];  // Flexible array (use [1] for MSVC)
    // uint16_t avail_event; // Only if FEATURE_EVENT_IDX
} VIRTIO_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef VIRTIO_PACKED

// Complete virtqueue structure
struct Virtqueue {
    // Queue size (number of descriptors)
    uint16_t size;
    
    // Queue index
    uint16_t index;
    
    // Descriptor table
    VringDesc* desc;
    
    // Available ring
    VringAvail* avail;
    
    // Used ring
    VringUsed* used;
    
    // Last seen used index (for polling)
    uint16_t lastUsedIdx;
    
    // Free descriptor head
    uint16_t freeHead;
    
    // Number of free descriptors
    uint16_t numFree;
    
    // Notify offset (for MMIO)
    uint32_t notifyOffset;
    
    // Physical addresses (for device)
    uint64_t descPhys;
    uint64_t availPhys;
    uint64_t usedPhys;
};

// ================================================================
// MMIO Register Offsets (VirtIO over MMIO)
// ================================================================

namespace mmio {
    static const uint32_t MAGIC_VALUE       = 0x000;  // Magic value "virt"
    static const uint32_t VERSION           = 0x004;  // Device version
    static const uint32_t DEVICE_ID         = 0x008;  // VirtIO device ID
    static const uint32_t VENDOR_ID         = 0x00C;  // VirtIO vendor ID
    static const uint32_t DEVICE_FEATURES   = 0x010;  // Device features
    static const uint32_t DEVICE_FEATURES_SEL = 0x014; // Feature selection
    static const uint32_t DRIVER_FEATURES   = 0x020;  // Driver features
    static const uint32_t DRIVER_FEATURES_SEL = 0x024; // Feature selection
    static const uint32_t QUEUE_SEL         = 0x030;  // Queue selection
    static const uint32_t QUEUE_NUM_MAX     = 0x034;  // Max queue size
    static const uint32_t QUEUE_NUM         = 0x038;  // Current queue size
    static const uint32_t QUEUE_READY       = 0x044;  // Queue ready
    static const uint32_t QUEUE_NOTIFY      = 0x050;  // Queue notification
    static const uint32_t INTERRUPT_STATUS  = 0x060;  // Interrupt status
    static const uint32_t INTERRUPT_ACK     = 0x064;  // Interrupt acknowledge
    static const uint32_t STATUS            = 0x070;  // Device status
    static const uint32_t QUEUE_DESC_LOW    = 0x080;  // Descriptor area (low)
    static const uint32_t QUEUE_DESC_HIGH   = 0x084;  // Descriptor area (high)
    static const uint32_t QUEUE_AVAIL_LOW   = 0x090;  // Available area (low)
    static const uint32_t QUEUE_AVAIL_HIGH  = 0x094;  // Available area (high)
    static const uint32_t QUEUE_USED_LOW    = 0x0A0;  // Used area (low)
    static const uint32_t QUEUE_USED_HIGH   = 0x0A4;  // Used area (high)
    static const uint32_t CONFIG_GENERATION = 0x0FC;  // Config generation
    static const uint32_t CONFIG            = 0x100;  // Device config space
    
    // Magic value
    static const uint32_t MAGIC = 0x74726976;  // "virt"
    
    // Version numbers
    static const uint32_t VERSION_LEGACY = 1;
    static const uint32_t VERSION_MODERN = 2;
}

// ================================================================
// PCI Configuration (VirtIO over PCI)
// ================================================================

namespace pci {
    // PCI capability types
    static const uint8_t CAP_COMMON_CFG = 1;
    static const uint8_t CAP_NOTIFY_CFG = 2;
    static const uint8_t CAP_ISR_CFG    = 3;
    static const uint8_t CAP_DEVICE_CFG = 4;
    static const uint8_t CAP_PCI_CFG    = 5;
    
    // Common configuration offsets
    static const uint32_t COMMON_DFSELECT    = 0x00;  // Device feature select
    static const uint32_t COMMON_DF          = 0x04;  // Device feature bits
    static const uint32_t COMMON_GFSELECT    = 0x08;  // Guest feature select
    static const uint32_t COMMON_GF          = 0x0C;  // Guest feature bits
    static const uint32_t COMMON_MSIX_CONFIG = 0x10;  // MSI-X config vector
    static const uint32_t COMMON_NUM_QUEUES  = 0x12;  // Number of queues
    static const uint32_t COMMON_STATUS      = 0x14;  // Device status
    static const uint32_t COMMON_CFG_GEN     = 0x15;  // Config generation
    static const uint32_t COMMON_Q_SELECT    = 0x16;  // Queue select
    static const uint32_t COMMON_Q_SIZE      = 0x18;  // Queue size
    static const uint32_t COMMON_Q_MSIX_VEC  = 0x1A;  // Queue MSI-X vector
    static const uint32_t COMMON_Q_ENABLE    = 0x1C;  // Queue enable
    static const uint32_t COMMON_Q_NOTIFY_OFF = 0x1E; // Queue notify offset
    static const uint32_t COMMON_Q_DESC      = 0x20;  // Queue descriptor area
    static const uint32_t COMMON_Q_AVAIL     = 0x28;  // Queue available area
    static const uint32_t COMMON_Q_USED      = 0x30;  // Queue used area
}

// ================================================================
// VirtIO Device Base Class
// ================================================================

class VirtioDevice {
public:
    virtual ~VirtioDevice() {}
    
    // Initialize the device
    virtual bool init() = 0;
    
    // Get device type
    virtual uint32_t getDeviceType() const = 0;
    
    // Get device status
    virtual uint8_t getStatus() const = 0;
    
    // Set device status
    virtual void setStatus(uint8_t status) = 0;
    
    // Reset device
    virtual void reset() = 0;
    
    // Get device features
    virtual uint64_t getFeatures() const = 0;
    
    // Set driver features
    virtual void setFeatures(uint64_t features) = 0;
    
    // Setup a virtqueue
    virtual bool setupQueue(uint16_t index, Virtqueue* vq) = 0;
    
    // Notify device about available buffers
    virtual void notifyQueue(uint16_t index) = 0;
    
    // Check for and acknowledge interrupts
    virtual uint32_t acknowledgeInterrupt() = 0;

protected:
    uint64_t baseAddr;
    uint64_t negotiatedFeatures;
    Virtqueue* queues;
    uint16_t numQueues;
};

// ================================================================
// Helper Functions
// ================================================================

// Calculate virtqueue memory requirements
// Returns total bytes needed for a virtqueue of given size
static inline size_t vring_size(uint16_t num)
{
    size_t size = 0;
    
    // Descriptor table
    size += num * sizeof(VringDesc);
    
    // Align to 2
    size = (size + 1) & ~1;
    
    // Available ring (header + entries + optional used_event)
    size += sizeof(uint16_t) * 2;  // flags + idx
    size += sizeof(uint16_t) * num; // ring
    size += sizeof(uint16_t);       // used_event (may be unused)
    
    // Align to page boundary for used ring
    size = (size + 4095) & ~4095;
    
    // Used ring (header + entries + optional avail_event)
    size += sizeof(uint16_t) * 2;  // flags + idx
    size += sizeof(VringUsedElem) * num; // ring
    size += sizeof(uint16_t);       // avail_event (may be unused)
    
    return size;
}

// Check if virtqueue has pending work
static inline bool vring_needs_event(uint16_t event_idx, uint16_t new_idx, uint16_t old_idx)
{
    return static_cast<uint16_t>(new_idx - event_idx - 1) <
           static_cast<uint16_t>(new_idx - old_idx);
}

} // namespace virtio
} // namespace kernel
