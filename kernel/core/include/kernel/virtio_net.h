// VirtIO Network Device Driver
//
// Implements VirtIO network device (virtio-net) support.
// Provides send/receive operations for network packets.
//
// VirtIO Network Device Specification:
//   https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>
#include <kernel/virtio.h>

namespace kernel {
namespace virtio {
namespace net {

// ================================================================
// Network Device Feature Bits
// ================================================================

static const uint64_t FEATURE_CSUM          = (1ULL << 0);   // Checksum offload
static const uint64_t FEATURE_GUEST_CSUM    = (1ULL << 1);   // Guest checksum
static const uint64_t FEATURE_CTRL_GUEST_OFFLOADS = (1ULL << 2); // Control guest offloads
static const uint64_t FEATURE_MTU           = (1ULL << 3);   // MTU configuration
static const uint64_t FEATURE_MAC           = (1ULL << 5);   // MAC address available
static const uint64_t FEATURE_GSO           = (1ULL << 6);   // GSO support (deprecated)
static const uint64_t FEATURE_GUEST_TSO4    = (1ULL << 7);   // Guest TSO4
static const uint64_t FEATURE_GUEST_TSO6    = (1ULL << 8);   // Guest TSO6
static const uint64_t FEATURE_GUEST_ECN     = (1ULL << 9);   // Guest ECN
static const uint64_t FEATURE_GUEST_UFO     = (1ULL << 10);  // Guest UFO
static const uint64_t FEATURE_HOST_TSO4     = (1ULL << 11);  // Host TSO4
static const uint64_t FEATURE_HOST_TSO6     = (1ULL << 12);  // Host TSO6
static const uint64_t FEATURE_HOST_ECN      = (1ULL << 13);  // Host ECN
static const uint64_t FEATURE_HOST_UFO      = (1ULL << 14);  // Host UFO
static const uint64_t FEATURE_MRG_RXBUF     = (1ULL << 15);  // Mergeable RX buffers
static const uint64_t FEATURE_STATUS        = (1ULL << 16);  // Status field available
static const uint64_t FEATURE_CTRL_VQ       = (1ULL << 17);  // Control virtqueue
static const uint64_t FEATURE_CTRL_RX       = (1ULL << 18);  // Control RX mode
static const uint64_t FEATURE_CTRL_VLAN     = (1ULL << 19);  // Control VLAN filtering
static const uint64_t FEATURE_GUEST_ANNOUNCE = (1ULL << 21); // Guest announce
static const uint64_t FEATURE_MQ            = (1ULL << 22);  // Multiqueue
static const uint64_t FEATURE_CTRL_MAC_ADDR = (1ULL << 23);  // Control MAC address
static const uint64_t FEATURE_SPEED_DUPLEX  = (1ULL << 63);  // Speed/duplex available

// ================================================================
// Network Header Flags
// ================================================================

static const uint8_t HDR_F_NEEDS_CSUM = 1;   // Checksum needed
static const uint8_t HDR_F_DATA_VALID = 2;   // Data is valid (Rx)
static const uint8_t HDR_F_RSC_INFO   = 4;   // RSC info available

// GSO types
static const uint8_t GSO_NONE       = 0;
static const uint8_t GSO_TCPV4      = 1;
static const uint8_t GSO_UDP        = 3;
static const uint8_t GSO_TCPV6      = 4;
static const uint8_t GSO_ECN        = 0x80;

// ================================================================
// Network Device Configuration
// ================================================================

struct NetConfig {
    uint8_t  mac[6];         // MAC address
    uint16_t status;         // Device status
    uint16_t max_virtqueue_pairs; // Max queue pairs (if MQ)
    uint16_t mtu;            // MTU (if FEATURE_MTU)
    uint32_t speed;          // Speed in Mbps (if FEATURE_SPEED_DUPLEX)
    uint8_t  duplex;         // Duplex mode (if FEATURE_SPEED_DUPLEX)
} __attribute__((packed));

// Status bits
static const uint16_t STATUS_LINK_UP     = 1;   // Link is up
static const uint16_t STATUS_ANNOUNCE    = 2;   // Announce required

// ================================================================
// Network Packet Header
// ================================================================

struct NetHeader {
    uint8_t  flags;          // HDR_F_* flags
    uint8_t  gso_type;       // GSO_* type
    uint16_t hdr_len;        // Header length (for GSO)
    uint16_t gso_size;       // GSO segment size
    uint16_t csum_start;     // Checksum start offset
    uint16_t csum_offset;    // Checksum offset from csum_start
    uint16_t num_buffers;    // Number of merged buffers (if MRG_RXBUF)
} __attribute__((packed));

// Minimum header size (without num_buffers)
static const size_t NET_HEADER_SIZE = 10;
// Header size with mergeable buffers
static const size_t NET_HEADER_SIZE_MRG = 12;

// ================================================================
// Control Virtqueue Commands
// ================================================================

// Control command classes
static const uint8_t CTRL_RX       = 0;   // RX mode control
static const uint8_t CTRL_MAC      = 1;   // MAC address control
static const uint8_t CTRL_VLAN     = 2;   // VLAN control
static const uint8_t CTRL_ANNOUNCE = 3;   // Announce control
static const uint8_t CTRL_MQ       = 4;   // Multiqueue control

// RX mode commands
static const uint8_t CTRL_RX_PROMISC      = 0;
static const uint8_t CTRL_RX_ALLMULTI     = 1;
static const uint8_t CTRL_RX_ALLUNI       = 2;
static const uint8_t CTRL_RX_NOMULTI      = 3;
static const uint8_t CTRL_RX_NOUNI        = 4;
static const uint8_t CTRL_RX_NOBCAST      = 5;

// MAC address commands
static const uint8_t CTRL_MAC_TABLE_SET   = 0;
static const uint8_t CTRL_MAC_ADDR_SET    = 1;

// Control command status
static const uint8_t CTRL_OK  = 0;
static const uint8_t CTRL_ERR = 1;

// Control command header
struct CtrlHeader {
    uint8_t cls;     // Command class
    uint8_t cmd;     // Command
} __attribute__((packed));

// ================================================================
// Network Statistics
// ================================================================

struct NetStats {
    uint64_t rxPackets;      // Received packets
    uint64_t txPackets;      // Transmitted packets
    uint64_t rxBytes;        // Received bytes
    uint64_t txBytes;        // Transmitted bytes
    uint64_t rxErrors;       // Receive errors
    uint64_t txErrors;       // Transmit errors
    uint64_t rxDropped;      // Dropped RX packets
    uint64_t txDropped;      // Dropped TX packets
};

// ================================================================
// Receive Callback Type
// ================================================================

typedef void (*RxCallback)(const void* data, size_t len, void* context);

// ================================================================
// Network Device Class
// ================================================================

class NetDevice : public VirtioDevice {
public:
    NetDevice();
    virtual ~NetDevice();
    
    // VirtioDevice interface
    bool init() override;
    uint32_t getDeviceType() const override { return DEVICE_NETWORK; }
    uint8_t getStatus() const override;
    void setStatus(uint8_t status) override;
    void reset() override;
    uint64_t getFeatures() const override;
    void setFeatures(uint64_t features) override;
    bool setupQueue(uint16_t index, Virtqueue* vq) override;
    void notifyQueue(uint16_t index) override;
    uint32_t acknowledgeInterrupt() override;
    
    // Network device operations
    
    // Get MAC address
    void getMacAddress(uint8_t* mac) const;
    
    // Set MAC address (if supported)
    bool setMacAddress(const uint8_t* mac);
    
    // Get MTU
    uint16_t getMtu() const;
    
    // Check link status
    bool isLinkUp() const;
    
    // Get link speed in Mbps (0 if unknown)
    uint32_t getLinkSpeed() const;
    
    // Send a packet
    // data: pointer to packet data (ethernet frame)
    // len: packet length in bytes
    // Returns: 0 on success, error code otherwise
    int send(const void* data, size_t len);
    
    // Receive a packet (polling mode)
    // buffer: buffer to receive packet data
    // bufferLen: size of buffer
    // Returns: received packet length, 0 if no packet, negative on error
    int receive(void* buffer, size_t bufferLen);
    
    // Check if packets are available for receive
    bool hasPackets() const;
    
    // Set receive callback (interrupt mode)
    void setRxCallback(RxCallback callback, void* context);
    
    // Process pending receives (call from interrupt handler)
    void processRx();
    
    // Enable promiscuous mode
    bool setPromiscuous(bool enable);
    
    // Get device statistics
    void getStats(NetStats* stats) const;
    
    // Check if device is initialized and ready
    bool isReady() const { return initialized; }
    
private:
    // Device configuration
    NetConfig config;
    
    // Virtqueues
    Virtqueue rxQueue;       // Receive queue (queue 0)
    Virtqueue txQueue;       // Transmit queue (queue 1)
    Virtqueue ctrlQueue;     // Control queue (queue 2, if supported)
    
    // State
    bool initialized;
    bool hasCtrlQueue;
    bool hasMergeableBuffers;
    
    // Statistics
    NetStats stats;
    
    // Receive callback
    RxCallback rxCallback;
    void* rxCallbackContext;
    
    // RX buffer management
    struct RxBuffer {
        uint8_t data[2048];  // Buffer data
        bool inUse;          // Buffer is submitted to device
    };
    static const int RX_BUFFER_COUNT = 16;
    RxBuffer rxBuffers[RX_BUFFER_COUNT];
    
    // TX buffer management  
    struct TxBuffer {
        uint8_t data[2048];  // Buffer data
        bool inUse;          // Buffer is submitted to device
    };
    static const int TX_BUFFER_COUNT = 16;
    TxBuffer txBuffers[TX_BUFFER_COUNT];
    
    // Internal methods
    void refillRxQueue();
    int allocTxBuffer();
    void freeTxBuffer(int index);
    bool sendCtrlCommand(uint8_t cls, uint8_t cmd, const void* data, size_t len);
};

// ================================================================
// Network Device Detection and Management
// ================================================================

// Maximum number of network devices
static const int MAX_NET_DEVICES = 4;

// Detect VirtIO network devices via PCI
int detectPci();

// Detect VirtIO network devices via MMIO
int detectMmio(uint64_t baseAddr, uint64_t size);

// Get network device by index
NetDevice* getDevice(int index);

// Get number of detected network devices
int getDeviceCount();

// Initialize network subsystem
bool init();

} // namespace net
} // namespace virtio
} // namespace kernel
