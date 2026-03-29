// USB CDC-ECM (Ethernet Control Model) Network Driver
//
// Implements CDC-ECM as defined in the USB CDC 1.2 specification.
// This driver provides Ethernet-over-USB for wired USB-to-Ethernet
// adapters.  It handles:
//   - ECM functional descriptor parsing (MAC address, statistics caps)
//   - Alternate interface selection (data interface)
//   - SET_ETHERNET_PACKET_FILTER
//   - SET_ETHERNET_MULTICAST_FILTERS
//   - NETWORK_CONNECTION / CONNECTION_SPEED_CHANGE notifications
//   - Raw Ethernet frame send/receive via bulk endpoints
//
// Reference: USB CDC 1.2, CDC ECM Subclass 1.2
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_NET_ECM_H
#define KERNEL_USB_NET_ECM_H

#include "kernel/types.h"
#include "kernel/usb.h"
#include "kernel/usb_net.h"

namespace kernel {
namespace usb_net_ecm {

// ================================================================
// CDC ECM functional descriptor subtype (CS_INTERFACE)
// ================================================================

static const uint8_t CDC_DESC_TYPE_CS_INTERFACE = 0x24;

enum CDCDescSubtype : uint8_t {
    CDC_SUBTYPE_HEADER          = 0x00,
    CDC_SUBTYPE_UNION           = 0x06,
    CDC_SUBTYPE_ETHERNET        = 0x0F,
};

// ================================================================
// ECM Ethernet Networking Functional Descriptor (13 bytes)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define ECM_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define ECM_PACKED
#endif

struct ECMFunctionalDesc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;     // 0x24 (CS_INTERFACE)
    uint8_t  bDescriptorSubtype;  // 0x0F (Ethernet Networking)
    uint8_t  iMACAddress;         // string index for MAC address
    uint32_t bmEthernetStatistics;
    uint16_t wMaxSegmentSize;
    uint16_t wNumberMCFilters;
    uint8_t  bNumberPowerFilters;
} ECM_PACKED;

struct CDCUnionDesc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;     // 0x24
    uint8_t  bDescriptorSubtype;  // 0x06 (Union)
    uint8_t  bControlInterface;
    uint8_t  bSubordinateInterface0;
} ECM_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef ECM_PACKED

// ================================================================
// ECM class-specific requests
// ================================================================

enum ECMRequest : uint8_t {
    ECM_SET_ETHERNET_MULTICAST   = 0x40,
    ECM_SET_ETHERNET_PM_FILTER   = 0x41,
    ECM_GET_ETHERNET_PM_FILTER   = 0x42,
    ECM_SET_ETHERNET_PKT_FILTER  = 0x43,
    ECM_GET_ETHERNET_STATISTIC   = 0x44,
};

// ================================================================
// ECM notification types (interrupt IN)
// ================================================================

enum ECMNotification : uint8_t {
    ECM_NOTIFY_NETWORK_CONNECTION  = 0x00,
    ECM_NOTIFY_RESPONSE_AVAILABLE  = 0x01,
    ECM_NOTIFY_SPEED_CHANGE        = 0x2A,
};

// ================================================================
// ECM speed-change notification payload
// ================================================================

struct ECMSpeedChange {
    uint32_t dlBitRate;   // downstream bits/sec
    uint32_t ulBitRate;   // upstream bits/sec
};

// ================================================================
// ECM device-specific state
// ================================================================

static const uint8_t MAX_ECM_DEVICES = 4;

struct ECMDevice {
    bool     active;
    uint8_t  usbAddress;
    uint8_t  commInterface;       // CDC control interface
    uint8_t  dataInterface;       // CDC data interface
    uint8_t  bulkInEP;
    uint8_t  bulkOutEP;
    uint8_t  notifyEP;
    uint16_t bulkInMaxPkt;
    uint16_t bulkOutMaxPkt;
    uint16_t notifyMaxPkt;
    uint8_t  macAddress[6];
    uint16_t maxSegmentSize;
    uint16_t packetFilter;
    uint16_t numMCFilters;        // max multicast filter entries
    uint32_t statsCaps;           // bmEthernetStatistics
    bool     linkUp;
    uint32_t dlBitRate;           // downstream bit rate
    uint32_t ulBitRate;           // upstream bit rate
};

// ================================================================
// Public API
// ================================================================

void init();

// Probe a USB device for CDC-ECM interfaces.
// Returns true and fills parentIndex with the usb_net device index.
bool probe(uint8_t usbAddress, uint8_t* parentIndex);

void release(uint8_t usbAddress);

// Poll notifications (link state, speed change).
void poll(uint8_t devIndex);

// Frame I/O
usb::TransferStatus send(uint8_t devIndex,
                          const void* frame, uint16_t len);

usb::TransferStatus receive(uint8_t devIndex,
                             void* frame, uint16_t maxLen,
                             uint16_t* received);

// Set packet filter
usb::TransferStatus set_packet_filter(uint8_t devIndex, uint16_t filter);

// Set multicast address list
usb::TransferStatus set_multicast_filters(uint8_t devIndex,
                                           const uint8_t* addrs,
                                           uint8_t count);

// Get device info
const ECMDevice* get_device(uint8_t devIndex);

uint8_t count();

} // namespace usb_net_ecm
} // namespace kernel

#endif // KERNEL_USB_NET_ECM_H
