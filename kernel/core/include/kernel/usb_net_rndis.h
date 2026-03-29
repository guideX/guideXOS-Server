// USB RNDIS (Remote NDIS) Network Driver
//
// Implements the Remote NDIS protocol used by many USB-to-Ethernet
// adapters and Android USB tethering.  RNDIS encapsulates NDIS
// messages over USB bulk and control transfers.
//
// Supports:
//   - RNDIS_INITIALIZE / RNDIS_HALT / RNDIS_QUERY / RNDIS_SET
//   - OID_802_3_PERMANENT_ADDRESS (MAC address query)
//   - OID_GEN_CURRENT_PACKET_FILTER
//   - OID_GEN_MAXIMUM_FRAME_SIZE
//   - RNDIS data message encapsulation (sending/receiving Ethernet frames)
//   - RNDIS_INDICATE_STATUS for link-state changes
//   - RNDIS_KEEPALIVE handling
//
// Device identification: class 0xE0 (Wireless Controller), subclass
// 0x01, protocol 0x03  OR  class 0xEF, subclass 0x04, protocol 0x01
// (RNDIS over USB composite).  Many devices also use vendor-specific
// class 0xFF with known VID/PID combinations.
//
// Reference: MS-RNDIS specification (Microsoft), USB CDC 1.2
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_NET_RNDIS_H
#define KERNEL_USB_NET_RNDIS_H

#include "kernel/types.h"
#include "kernel/usb.h"
#include "kernel/usb_net.h"

namespace kernel {
namespace usb_net_rndis {

// ================================================================
// RNDIS message types
// ================================================================

enum RNDISMsgType : uint32_t {
    RNDIS_MSG_PACKET           = 0x00000001,
    RNDIS_MSG_INIT             = 0x00000002,
    RNDIS_MSG_INIT_COMPLETE    = 0x80000002,
    RNDIS_MSG_HALT             = 0x00000003,
    RNDIS_MSG_QUERY            = 0x00000004,
    RNDIS_MSG_QUERY_COMPLETE   = 0x80000004,
    RNDIS_MSG_SET              = 0x00000005,
    RNDIS_MSG_SET_COMPLETE     = 0x80000005,
    RNDIS_MSG_RESET            = 0x00000006,
    RNDIS_MSG_RESET_COMPLETE   = 0x80000006,
    RNDIS_MSG_INDICATE         = 0x00000007,
    RNDIS_MSG_KEEPALIVE        = 0x00000008,
    RNDIS_MSG_KEEPALIVE_CMPLT  = 0x80000008,
};

// ================================================================
// RNDIS status codes
// ================================================================

enum RNDISStatus : uint32_t {
    RNDIS_STATUS_SUCCESS            = 0x00000000,
    RNDIS_STATUS_FAILURE            = 0xC0000001,
    RNDIS_STATUS_INVALID_DATA       = 0xC0010015,
    RNDIS_STATUS_NOT_SUPPORTED      = 0xC00000BB,
    RNDIS_STATUS_MEDIA_CONNECT      = 0x4001000B,
    RNDIS_STATUS_MEDIA_DISCONNECT   = 0x4001000C,
};

// ================================================================
// NDIS OID codes (subset relevant to Ethernet)
// ================================================================

enum NDISOID : uint32_t {
    OID_GEN_SUPPORTED_LIST         = 0x00010101,
    OID_GEN_HARDWARE_STATUS        = 0x00010102,
    OID_GEN_MEDIA_SUPPORTED        = 0x00010103,
    OID_GEN_MEDIA_IN_USE           = 0x00010104,
    OID_GEN_MAXIMUM_FRAME_SIZE     = 0x00010106,
    OID_GEN_LINK_SPEED             = 0x00010107,
    OID_GEN_TRANSMIT_BLOCK_SIZE    = 0x0001010A,
    OID_GEN_RECEIVE_BLOCK_SIZE     = 0x0001010B,
    OID_GEN_VENDOR_ID              = 0x0001010C,
    OID_GEN_VENDOR_DESCRIPTION     = 0x0001010D,
    OID_GEN_CURRENT_PACKET_FILTER  = 0x0001010E,
    OID_GEN_MAXIMUM_TOTAL_SIZE     = 0x00010111,
    OID_GEN_MEDIA_CONNECT_STATUS   = 0x00010114,
    OID_GEN_PHYSICAL_MEDIUM        = 0x00010202,

    OID_802_3_PERMANENT_ADDRESS    = 0x01010101,
    OID_802_3_CURRENT_ADDRESS      = 0x01010102,
    OID_802_3_MULTICAST_LIST       = 0x01010103,
    OID_802_3_MAXIMUM_LIST_SIZE    = 0x01010104,
};

// ================================================================
// RNDIS message structures (packed)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define RNDIS_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define RNDIS_PACKED
#endif

// Generic RNDIS message header
struct RNDISMsgHeader {
    uint32_t msgType;
    uint32_t msgLength;
} RNDIS_PACKED;

// RNDIS_INITIALIZE_MSG (24 bytes)
struct RNDISInitMsg {
    uint32_t msgType;        // RNDIS_MSG_INIT
    uint32_t msgLength;      // 24
    uint32_t requestId;
    uint32_t majorVersion;   // 1
    uint32_t minorVersion;   // 0
    uint32_t maxTransferSize;
} RNDIS_PACKED;

// RNDIS_INITIALIZE_CMPLT (52 bytes)
struct RNDISInitComplete {
    uint32_t msgType;
    uint32_t msgLength;
    uint32_t requestId;
    uint32_t status;
    uint32_t majorVersion;
    uint32_t minorVersion;
    uint32_t deviceFlags;
    uint32_t medium;           // 0 = NdisMedium802_3
    uint32_t maxPacketsPerMsg;
    uint32_t maxTransferSize;
    uint32_t packetAlignFactor;
    uint32_t afListOffset;
    uint32_t afListSize;
} RNDIS_PACKED;

// RNDIS_QUERY_MSG (28 bytes + optional data)
struct RNDISQueryMsg {
    uint32_t msgType;        // RNDIS_MSG_QUERY
    uint32_t msgLength;
    uint32_t requestId;
    uint32_t oid;
    uint32_t infoBufferLength;
    uint32_t infoBufferOffset; // from start of requestId
    uint32_t deviceVcHandle;   // reserved, 0
} RNDIS_PACKED;

// RNDIS_QUERY_CMPLT (variable length)
struct RNDISQueryComplete {
    uint32_t msgType;
    uint32_t msgLength;
    uint32_t requestId;
    uint32_t status;
    uint32_t infoBufferLength;
    uint32_t infoBufferOffset; // from start of requestId
} RNDIS_PACKED;

// RNDIS_SET_MSG (28 bytes + data)
struct RNDISSetMsg {
    uint32_t msgType;        // RNDIS_MSG_SET
    uint32_t msgLength;
    uint32_t requestId;
    uint32_t oid;
    uint32_t infoBufferLength;
    uint32_t infoBufferOffset; // from start of requestId
    uint32_t deviceVcHandle;   // 0
} RNDIS_PACKED;

// RNDIS_SET_CMPLT
struct RNDISSetComplete {
    uint32_t msgType;
    uint32_t msgLength;
    uint32_t requestId;
    uint32_t status;
} RNDIS_PACKED;

// RNDIS_HALT_MSG
struct RNDISHaltMsg {
    uint32_t msgType;        // RNDIS_MSG_HALT
    uint32_t msgLength;      // 12
    uint32_t requestId;
} RNDIS_PACKED;

// RNDIS_INDICATE_STATUS_MSG
struct RNDISIndicateMsg {
    uint32_t msgType;
    uint32_t msgLength;
    uint32_t status;
    uint32_t statusBufLength;
    uint32_t statusBufOffset;
} RNDIS_PACKED;

// RNDIS_KEEPALIVE_MSG
struct RNDISKeepaliveMsg {
    uint32_t msgType;        // RNDIS_MSG_KEEPALIVE
    uint32_t msgLength;      // 12
    uint32_t requestId;
} RNDIS_PACKED;

// RNDIS_KEEPALIVE_CMPLT
struct RNDISKeepaliveCmplt {
    uint32_t msgType;
    uint32_t msgLength;
    uint32_t requestId;
    uint32_t status;
} RNDIS_PACKED;

// RNDIS data packet header (prepended to every Ethernet frame)
struct RNDISPacketMsg {
    uint32_t msgType;        // RNDIS_MSG_PACKET
    uint32_t msgLength;
    uint32_t dataOffset;     // from start of dataOffset field
    uint32_t dataLength;
    uint32_t oobDataOffset;
    uint32_t oobDataLength;
    uint32_t numOOBElements;
    uint32_t perPacketInfoOffset;
    uint32_t perPacketInfoLength;
    uint32_t vcHandle;       // reserved, 0
    uint32_t reserved;       // 0
} RNDIS_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef RNDIS_PACKED

static const uint32_t RNDIS_PACKET_HEADER_SIZE = 44;
static const uint32_t RNDIS_MAX_TRANSFER_SIZE  = 2048;

// ================================================================
// RNDIS device state
// ================================================================

static const uint8_t MAX_RNDIS_DEVICES = 4;

struct RNDISDevice {
    bool     active;
    uint8_t  usbAddress;
    uint8_t  commInterface;
    uint8_t  dataInterface;
    uint8_t  bulkInEP;
    uint8_t  bulkOutEP;
    uint8_t  notifyEP;
    uint16_t bulkInMaxPkt;
    uint16_t bulkOutMaxPkt;
    uint16_t notifyMaxPkt;
    uint8_t  macAddress[6];
    uint16_t mtu;
    uint32_t maxTransferSize;
    uint16_t packetFilter;
    bool     linkUp;
    uint32_t linkSpeed;           // in units of 100 bps
    uint32_t requestId;           // monotonically increasing
};

// ================================================================
// Public API
// ================================================================

void init();

// Probe a USB device for RNDIS interface.
// Returns true and fills parentIndex with the usb_net device index.
bool probe(uint8_t usbAddress, uint8_t* parentIndex);

void release(uint8_t usbAddress);

// Poll for notifications (indicate status, keepalive).
void poll(uint8_t devIndex);

// Frame I/O (handles RNDIS encapsulation internally)
usb::TransferStatus send(uint8_t devIndex,
                          const void* frame, uint16_t len);

usb::TransferStatus receive(uint8_t devIndex,
                             void* frame, uint16_t maxLen,
                             uint16_t* received);

// Set packet filter
usb::TransferStatus set_packet_filter(uint8_t devIndex, uint16_t filter);

// Get device info
const RNDISDevice* get_device(uint8_t devIndex);

uint8_t count();

} // namespace usb_net_rndis
} // namespace kernel

#endif // KERNEL_USB_NET_RNDIS_H
