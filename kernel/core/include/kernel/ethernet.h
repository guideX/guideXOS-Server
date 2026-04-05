// Ethernet Frame Parser and Builder
//
// Provides functions to:
//   - Parse raw Ethernet frames (extract MAC addresses, EtherType, payload)
//   - Build Ethernet frames for transmission
//   - Convert between host and network byte order
//   - Format MAC addresses as strings
//
// Works with the NIC driver's raw frame send/receive API.
//
// Reference: IEEE 802.3 Ethernet Frame Format
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_ETHERNET_H
#define KERNEL_ETHERNET_H

#include "kernel/types.h"

namespace kernel {
namespace ethernet {

// ================================================================
// Ethernet constants
// ================================================================

static const uint16_t MAC_ADDR_LEN   = 6;       // MAC address length (bytes)
static const uint16_t HEADER_LEN     = 14;      // Ethernet header size
static const uint16_t MTU            = 1500;    // Maximum Transmission Unit
static const uint16_t MIN_FRAME_LEN  = 60;      // Minimum frame (no FCS)
static const uint16_t MAX_FRAME_LEN  = 1514;    // Maximum frame (no FCS)
static const uint16_t MAX_PAYLOAD    = 1500;    // Maximum payload size
static const uint16_t MIN_PAYLOAD    = 46;      // Minimum payload (with padding)

// ================================================================
// Common EtherType values (host byte order for comparison)
// ================================================================

static const uint16_t ETHERTYPE_IPV4      = 0x0800;
static const uint16_t ETHERTYPE_ARP       = 0x0806;
static const uint16_t ETHERTYPE_RARP      = 0x8035;
static const uint16_t ETHERTYPE_VLAN      = 0x8100;  // 802.1Q VLAN tag
static const uint16_t ETHERTYPE_IPV6      = 0x86DD;
static const uint16_t ETHERTYPE_LLDP      = 0x88CC;  // Link Layer Discovery
static const uint16_t ETHERTYPE_LOOPBACK  = 0x9000;  // Ethernet loopback

// ================================================================
// Special MAC addresses
// ================================================================

// Broadcast address: FF:FF:FF:FF:FF:FF
static const uint8_t BROADCAST_MAC[MAC_ADDR_LEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// Zero/null address: 00:00:00:00:00:00
static const uint8_t ZERO_MAC[MAC_ADDR_LEN] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ================================================================
// Ethernet header structure (14 bytes, packed)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define ETH_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define ETH_PACKED
#endif

struct Header {
    uint8_t  destMAC[MAC_ADDR_LEN];   // Destination MAC address
    uint8_t  srcMAC[MAC_ADDR_LEN];    // Source MAC address
    uint16_t etherType;                // EtherType (network byte order)
} ETH_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef ETH_PACKED

// ================================================================
// Parsed frame information
// ================================================================

struct ParsedFrame {
    // Header fields
    uint8_t  destMAC[MAC_ADDR_LEN];   // Destination MAC
    uint8_t  srcMAC[MAC_ADDR_LEN];    // Source MAC
    uint16_t etherType;                // EtherType (host byte order)
    
    // Payload pointer and length
    const uint8_t* payload;            // Pointer to payload data
    uint16_t       payloadLen;         // Payload length in bytes
    
    // Frame metadata
    bool     isBroadcast;              // Destination is broadcast
    bool     isMulticast;              // Destination is multicast
    bool     isValid;                  // Frame passed validation
};

// ================================================================
// Frame builder context
// ================================================================

struct FrameBuilder {
    uint8_t* buffer;                   // Output buffer
    uint16_t bufferSize;               // Total buffer capacity
    uint16_t frameLen;                 // Current frame length
    bool     headerSet;                // Header has been written
};

// ================================================================
// Status codes
// ================================================================

enum Status : uint8_t {
    ETH_OK               = 0,
    ETH_ERR_NULL_PTR     = 1,   // Null pointer argument
    ETH_ERR_TOO_SHORT    = 2,   // Frame too short
    ETH_ERR_TOO_LONG     = 3,   // Frame/payload too long
    ETH_ERR_BUFFER_SMALL = 4,   // Output buffer too small
    ETH_ERR_NO_HEADER    = 5,   // Header not set (builder)
    ETH_ERR_INVALID      = 6,   // Invalid parameter
};

// ================================================================
// Byte Order Conversion (network = big-endian)
// ================================================================

// Convert 16-bit value from host to network byte order
inline uint16_t htons(uint16_t val)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return val;
#else
    return static_cast<uint16_t>((val >> 8) | (val << 8));
#endif
}

// Convert 16-bit value from network to host byte order
inline uint16_t ntohs(uint16_t val)
{
    return htons(val);  // Same operation for 16-bit swap
}

// Convert 32-bit value from host to network byte order
inline uint32_t htonl(uint32_t val)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return val;
#else
    return ((val >> 24) & 0x000000FF) |
           ((val >>  8) & 0x0000FF00) |
           ((val <<  8) & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
#endif
}

// Convert 32-bit value from network to host byte order
inline uint32_t ntohl(uint32_t val)
{
    return htonl(val);  // Same operation for 32-bit swap
}

// ================================================================
// MAC Address Utilities
// ================================================================

// Copy a MAC address
void mac_copy(uint8_t* dest, const uint8_t* src);

// Compare two MAC addresses (returns true if equal)
bool mac_equals(const uint8_t* mac1, const uint8_t* mac2);

// Check if MAC is the broadcast address
bool mac_is_broadcast(const uint8_t* mac);

// Check if MAC is a multicast address (bit 0 of first byte set)
bool mac_is_multicast(const uint8_t* mac);

// Check if MAC is the zero/null address
bool mac_is_zero(const uint8_t* mac);

// Format MAC address as string "XX:XX:XX:XX:XX:XX" (requires 18-byte buffer)
void mac_to_string(const uint8_t* mac, char* str);

// Parse MAC address from string "XX:XX:XX:XX:XX:XX"
bool mac_from_string(const char* str, uint8_t* mac);

// ================================================================
// Frame Parsing
// ================================================================

// Parse a raw Ethernet frame and extract header/payload information.
// The parsed frame contains pointers into the original frame buffer.
//
// Parameters:
//   frame    - Raw frame data (must remain valid while ParsedFrame is used)
//   len      - Length of frame data
//   parsed   - Output structure to receive parsed information
//
// Returns:
//   ETH_OK on success, error code otherwise
Status parse_frame(const uint8_t* frame, uint16_t len, ParsedFrame* parsed);

// Extract just the Ethernet header from a frame.
//
// Parameters:
//   frame    - Raw frame data (at least HEADER_LEN bytes)
//   len      - Length of frame data
//   header   - Output structure to receive header
//
// Returns:
//   ETH_OK on success, error code otherwise
Status extract_header(const uint8_t* frame, uint16_t len, Header* header);

// Get pointer to payload data within a frame.
//
// Parameters:
//   frame    - Raw frame data
//   len      - Length of frame data
//   payload  - Output: pointer to payload start
//   payloadLen - Output: payload length
//
// Returns:
//   ETH_OK on success, error code otherwise
Status get_payload(const uint8_t* frame, uint16_t len,
                   const uint8_t** payload, uint16_t* payloadLen);

// Get the EtherType from a frame (in host byte order).
//
// Parameters:
//   frame    - Raw frame data
//   len      - Length of frame data
//
// Returns:
//   EtherType value, or 0 if frame is too short
uint16_t get_ethertype(const uint8_t* frame, uint16_t len);

// ================================================================
// Frame Building
// ================================================================

// Initialize a frame builder with an output buffer.
//
// Parameters:
//   builder    - Builder context to initialize
//   buffer     - Output buffer for frame data
//   bufferSize - Size of output buffer (should be >= MAX_FRAME_LEN)
//
// Returns:
//   ETH_OK on success, error code otherwise
Status builder_init(FrameBuilder* builder, uint8_t* buffer, uint16_t bufferSize);

// Set the Ethernet header in the frame being built.
//
// Parameters:
//   builder   - Builder context
//   destMAC   - Destination MAC address
//   srcMAC    - Source MAC address
//   etherType - EtherType value (host byte order, will be converted)
//
// Returns:
//   ETH_OK on success, error code otherwise
Status builder_set_header(FrameBuilder* builder,
                          const uint8_t* destMAC,
                          const uint8_t* srcMAC,
                          uint16_t etherType);

// Append payload data to the frame being built.
//
// Parameters:
//   builder - Builder context
//   data    - Payload data to append
//   len     - Length of data to append
//
// Returns:
//   ETH_OK on success, error code otherwise
Status builder_append_payload(FrameBuilder* builder,
                              const uint8_t* data,
                              uint16_t len);

// Finalize the frame (apply padding if needed).
// After this call, the frame is ready for transmission.
//
// Parameters:
//   builder  - Builder context
//   frameLen - Output: final frame length
//
// Returns:
//   ETH_OK on success, error code otherwise
Status builder_finalize(FrameBuilder* builder, uint16_t* frameLen);

// Reset a builder to start a new frame (keeps same buffer).
void builder_reset(FrameBuilder* builder);

// ================================================================
// Convenience Functions
// ================================================================

// Build a complete Ethernet frame in one call.
//
// Parameters:
//   buffer     - Output buffer (must be at least MAX_FRAME_LEN)
//   bufferSize - Size of output buffer
//   destMAC    - Destination MAC address
//   srcMAC     - Source MAC address
//   etherType  - EtherType value (host byte order)
//   payload    - Payload data
//   payloadLen - Length of payload
//   frameLen   - Output: total frame length
//
// Returns:
//   ETH_OK on success, error code otherwise
Status build_frame(uint8_t* buffer,
                   uint16_t bufferSize,
                   const uint8_t* destMAC,
                   const uint8_t* srcMAC,
                   uint16_t etherType,
                   const uint8_t* payload,
                   uint16_t payloadLen,
                   uint16_t* frameLen);

// Build a broadcast frame (destination = FF:FF:FF:FF:FF:FF).
Status build_broadcast_frame(uint8_t* buffer,
                             uint16_t bufferSize,
                             const uint8_t* srcMAC,
                             uint16_t etherType,
                             const uint8_t* payload,
                             uint16_t payloadLen,
                             uint16_t* frameLen);

// ================================================================
// EtherType Utilities
// ================================================================

// Get human-readable name for common EtherTypes.
const char* ethertype_name(uint16_t etherType);

// Check if EtherType indicates an IPv4 frame
inline bool is_ipv4(uint16_t etherType) { return etherType == ETHERTYPE_IPV4; }

// Check if EtherType indicates an ARP frame
inline bool is_arp(uint16_t etherType) { return etherType == ETHERTYPE_ARP; }

// Check if EtherType indicates an IPv6 frame
inline bool is_ipv6(uint16_t etherType) { return etherType == ETHERTYPE_IPV6; }

// Check if EtherType indicates a VLAN-tagged frame
inline bool is_vlan(uint16_t etherType) { return etherType == ETHERTYPE_VLAN; }

} // namespace ethernet
} // namespace kernel

#endif // KERNEL_ETHERNET_H
