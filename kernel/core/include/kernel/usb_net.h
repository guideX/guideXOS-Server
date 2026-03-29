// USB Network Interface Abstraction
//
// Unified network device model that sits above the individual
// transport drivers (CDC-ECM, RNDIS, Wi-Fi).  Provides a common
// Ethernet-frame–level send/receive API for upper-layer protocols.
//
// Architecture-independent — works on x86, amd64, ARM, SPARC v8,
// SPARC v9, and IA-64 through the USB HCI abstraction.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_NET_H
#define KERNEL_USB_NET_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_net {

// ================================================================
// Ethernet constants
// ================================================================

static const uint16_t ETH_ALEN        = 6;      // MAC address length
static const uint16_t ETH_HLEN        = 14;     // Ethernet header length
static const uint16_t ETH_MTU         = 1500;   // standard MTU
static const uint16_t ETH_FRAME_MAX   = 1514;   // header + MTU
static const uint16_t ETH_FRAME_MIN   = 60;     // minimum frame size

// ================================================================
// Ethernet header (14 bytes)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define NET_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define NET_PACKED
#endif

struct EthernetHeader {
    uint8_t  destMAC[ETH_ALEN];
    uint8_t  srcMAC[ETH_ALEN];
    uint16_t etherType;         // big-endian
} NET_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef NET_PACKED

// Common EtherType values (big-endian on wire)
static const uint16_t ETHERTYPE_IPV4 = 0x0800;
static const uint16_t ETHERTYPE_ARP  = 0x0806;
static const uint16_t ETHERTYPE_IPV6 = 0x86DD;

// ================================================================
// Network device type
// ================================================================

enum NetDeviceType : uint8_t {
    NET_TYPE_NONE    = 0,
    NET_TYPE_ECM     = 1,   // CDC Ethernet Control Model
    NET_TYPE_RNDIS   = 2,   // Remote NDIS (Microsoft)
    NET_TYPE_NCM     = 3,   // CDC Network Control Model (future)
    NET_TYPE_WIFI    = 4,   // USB Wi-Fi adapter
};

// ================================================================
// Link state
// ================================================================

enum LinkState : uint8_t {
    LINK_DOWN       = 0,
    LINK_UP         = 1,
    LINK_CONNECTING = 2,    // Wi-Fi associating
};

// ================================================================
// Link speed (reported by device)
// ================================================================

enum LinkSpeed : uint8_t {
    SPEED_UNKNOWN   = 0,
    SPEED_10MBPS    = 1,
    SPEED_100MBPS   = 2,
    SPEED_1000MBPS  = 3,
};

// ================================================================
// Packet filter flags (CDC SET_ETHERNET_PACKET_FILTER / RNDIS OID)
// ================================================================

enum PacketFilter : uint16_t {
    FILTER_DIRECTED       = 0x0001,
    FILTER_MULTICAST      = 0x0002,
    FILTER_ALL_MULTICAST  = 0x0004,
    FILTER_BROADCAST      = 0x0008,
    FILTER_SOURCE_ROUTING = 0x0010,
    FILTER_PROMISCUOUS    = 0x0020,
};

// ================================================================
// Network statistics
// ================================================================

struct NetStats {
    uint32_t txFrames;
    uint32_t rxFrames;
    uint32_t txBytes;
    uint32_t rxBytes;
    uint32_t txErrors;
    uint32_t rxErrors;
    uint32_t txDropped;
    uint32_t rxDropped;
};

// ================================================================
// Unified network device instance
//
// One per attached USB NIC (ECM, RNDIS, or Wi-Fi).  The driver-
// specific state is stored in a union-style overlay to keep the
// struct self-contained.
// ================================================================

static const uint8_t MAX_NET_DEVICES = 4;

struct NetDevice {
    bool           active;
    NetDeviceType  type;
    uint8_t        usbAddress;
    uint8_t        interfaceNum;

    // Endpoints
    uint8_t        bulkInEP;
    uint8_t        bulkOutEP;
    uint8_t        notifyEP;       // interrupt IN (notifications)
    uint16_t       bulkInMaxPkt;
    uint16_t       bulkOutMaxPkt;
    uint16_t       notifyMaxPkt;

    // Link
    uint8_t        macAddress[ETH_ALEN];
    LinkState      link;
    LinkSpeed      speed;
    uint16_t       mtu;
    uint16_t       packetFilter;

    // Statistics
    NetStats       stats;

    // Driver-private index into backend-specific device array
    uint8_t        backendIndex;
};

// ================================================================
// Public API
// ================================================================

// Initialise the USB network subsystem (and all backends).
void init();

// Probe a USB device for network interfaces (ECM, RNDIS, Wi-Fi).
// Returns true if at least one network interface was claimed.
bool probe(uint8_t usbAddress);

// Release network interfaces on a device (on detach).
void release(uint8_t usbAddress);

// Poll all active network devices for link-state changes and
// pending received frames.
void poll();

// Return number of active network devices.
uint8_t device_count();

// Get device info by index (0-based).
const NetDevice* get_device(uint8_t index);

// ----------------------------------------------------------------
// Frame-level I/O
// ----------------------------------------------------------------

// Send a raw Ethernet frame (including 14-byte header).
// Returns XFER_SUCCESS on success.
usb::TransferStatus send_frame(uint8_t devIndex,
                                const void* frame,
                                uint16_t len);

// Receive a raw Ethernet frame (including 14-byte header).
// Returns XFER_SUCCESS if a frame was read, XFER_NAK if none pending.
usb::TransferStatus receive_frame(uint8_t devIndex,
                                   void* frame,
                                   uint16_t maxLen,
                                   uint16_t* received);

// ----------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------

// Set the packet filter (combination of PacketFilter flags).
usb::TransferStatus set_packet_filter(uint8_t devIndex,
                                       uint16_t filter);

// Get the current link state.
LinkState get_link_state(uint8_t devIndex);

// Get the current link speed.
LinkSpeed get_link_speed(uint8_t devIndex);

// Get the MAC address.
const uint8_t* get_mac_address(uint8_t devIndex);

// Get accumulated statistics.
const NetStats* get_stats(uint8_t devIndex);

} // namespace usb_net
} // namespace kernel

#endif // KERNEL_USB_NET_H
