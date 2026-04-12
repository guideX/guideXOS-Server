// IPv4 Network Layer
//
// Provides:
//   - IPv4 header structure and parsing
//   - IP packet construction and transmission
//   - Header checksum calculation and verification
//   - Simple routing (local network vs gateway)
//   - IP address utilities
//
// Sits above the Ethernet layer and below transport protocols (TCP/UDP).
//
// Reference: RFC 791 - Internet Protocol
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_IPV4_H
#define KERNEL_IPV4_H

#include "kernel/types.h"
#include "kernel/ethernet.h"

namespace kernel {
namespace ipv4 {

// ================================================================
// IPv4 Constants
// ================================================================

static const uint8_t  IP_VERSION        = 4;
static const uint8_t  MIN_HEADER_LEN    = 20;     // Minimum header (no options)
static const uint8_t  MAX_HEADER_LEN    = 60;     // Maximum header (with options)
static const uint16_t MAX_PACKET_LEN    = 65535;  // Maximum total length
static const uint16_t DEFAULT_TTL       = 64;     // Default Time To Live
static const uint16_t MTU               = 1500;   // Standard Ethernet MTU
static const uint16_t MAX_PAYLOAD       = 1480;   // MTU - min header

// ================================================================
// IP Protocol Numbers (next header / transport layer)
// ================================================================

static const uint8_t PROTO_ICMP     = 1;
static const uint8_t PROTO_IGMP     = 2;
static const uint8_t PROTO_TCP      = 6;
static const uint8_t PROTO_UDP      = 17;
static const uint8_t PROTO_ENCAP    = 41;   // IPv6 encapsulation
static const uint8_t PROTO_OSPF     = 89;
static const uint8_t PROTO_SCTP     = 132;

// ================================================================
// Special IP Addresses
// ================================================================

static const uint32_t ADDR_ANY          = 0x00000000;  // 0.0.0.0
static const uint32_t ADDR_BROADCAST    = 0xFFFFFFFF;  // 255.255.255.255
static const uint32_t ADDR_LOOPBACK     = 0x7F000001;  // 127.0.0.1

// Subnet masks
static const uint32_t MASK_8            = 0xFF000000;  // /8  (255.0.0.0)
static const uint32_t MASK_16           = 0xFFFF0000;  // /16 (255.255.0.0)
static const uint32_t MASK_24           = 0xFFFFFF00;  // /24 (255.255.255.0)

// ================================================================
// IPv4 Header Structure (20-60 bytes, packed)
//
// Network byte order for multi-byte fields.
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define IP_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define IP_PACKED
#endif

struct Header {
    uint8_t  versionIHL;      // Version (4 bits) + IHL (4 bits)
    uint8_t  tos;             // Type of Service / DSCP + ECN
    uint16_t totalLength;     // Total packet length (network order)
    uint16_t identification;  // Fragment identification
    uint16_t flagsFragment;   // Flags (3 bits) + Fragment Offset (13 bits)
    uint8_t  ttl;             // Time To Live
    uint8_t  protocol;        // Next protocol (TCP=6, UDP=17, ICMP=1)
    uint16_t checksum;        // Header checksum
    uint32_t srcAddr;         // Source IP address (network order)
    uint32_t dstAddr;         // Destination IP address (network order)
    // Options follow if IHL > 5 (up to 40 bytes)
} IP_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef IP_PACKED

// ================================================================
// Header field accessors
// ================================================================

// Get IP version from header (should be 4)
inline uint8_t get_version(const Header* hdr)
{
    return (hdr->versionIHL >> 4) & 0x0F;
}

// Get Internet Header Length in 32-bit words (5-15)
inline uint8_t get_ihl(const Header* hdr)
{
    return hdr->versionIHL & 0x0F;
}

// Get header length in bytes (20-60)
inline uint8_t get_header_len(const Header* hdr)
{
    return get_ihl(hdr) * 4;
}

// Get total packet length (header + payload)
inline uint16_t get_total_len(const Header* hdr)
{
    return ethernet::ntohs(hdr->totalLength);
}

// Get payload length
inline uint16_t get_payload_len(const Header* hdr)
{
    return get_total_len(hdr) - get_header_len(hdr);
}

// Fragment flags
static const uint16_t FLAG_RESERVED = 0x8000;  // Reserved (must be 0)
static const uint16_t FLAG_DF       = 0x4000;  // Don't Fragment
static const uint16_t FLAG_MF       = 0x2000;  // More Fragments
static const uint16_t FRAG_OFFSET_MASK = 0x1FFF;

// Get flags from flagsFragment field
inline uint16_t get_flags(const Header* hdr)
{
    return ethernet::ntohs(hdr->flagsFragment) & 0xE000;
}

// Get fragment offset (in 8-byte units)
inline uint16_t get_frag_offset(const Header* hdr)
{
    return ethernet::ntohs(hdr->flagsFragment) & FRAG_OFFSET_MASK;
}

// Check if packet is fragmented
inline bool is_fragmented(const Header* hdr)
{
    uint16_t flags = ethernet::ntohs(hdr->flagsFragment);
    return (flags & FLAG_MF) || (flags & FRAG_OFFSET_MASK);
}

// ================================================================
// Parsed packet information
// ================================================================

struct ParsedPacket {
    // Header fields (host byte order where applicable)
    uint8_t   version;
    uint8_t   headerLen;         // Header length in bytes
    uint8_t   tos;
    uint16_t  totalLen;
    uint16_t  identification;
    uint16_t  flags;
    uint16_t  fragOffset;
    uint8_t   ttl;
    uint8_t   protocol;
    uint16_t  checksum;
    uint32_t  srcAddr;           // Host byte order
    uint32_t  dstAddr;           // Host byte order
    
    // Payload
    const uint8_t* payload;
    uint16_t       payloadLen;
    
    // Validation
    bool      isValid;
    bool      checksumValid;
    bool      isFragmented;
    bool      isBroadcast;
    bool      isMulticast;
    bool      isLoopback;
};

// ================================================================
// Network configuration
// ================================================================

struct NetworkConfig {
    uint32_t ipAddr;             // Our IP address (host byte order)
    uint32_t subnetMask;         // Subnet mask (host byte order)
    uint32_t gateway;            // Default gateway (host byte order)
    uint32_t dns;                // DNS server (host byte order)
    uint8_t  macAddr[6];         // Our MAC address
    bool     configured;         // Network is configured
};

// ================================================================
// Routing table entry
// ================================================================

struct RouteEntry {
    uint32_t network;            // Network address
    uint32_t mask;               // Network mask
    uint32_t gateway;            // Gateway (0 = direct)
    uint8_t  metric;             // Route metric/priority
    bool     active;
};

static const uint8_t MAX_ROUTES = 8;

// ================================================================
// Status codes
// ================================================================

enum Status : uint8_t {
    IP_OK                = 0,
    IP_ERR_NULL_PTR      = 1,
    IP_ERR_TOO_SHORT     = 2,    // Packet too short
    IP_ERR_TOO_LONG      = 3,    // Packet too long for MTU
    IP_ERR_BAD_VERSION   = 4,    // Not IPv4
    IP_ERR_BAD_HEADER    = 5,    // Invalid header length
    IP_ERR_BAD_CHECKSUM  = 6,    // Checksum mismatch
    IP_ERR_NO_ROUTE      = 7,    // No route to destination
    IP_ERR_NOT_CONFIGURED = 8,   // Network not configured
    IP_ERR_BUFFER_SMALL  = 9,    // Output buffer too small
    IP_ERR_TX_FAILED     = 10,   // Transmission failed
    IP_ERR_FRAGMENTED    = 11,   // Fragmentation not supported
};

// ================================================================
// IP Address Utilities
// ================================================================

// Create IPv4 address from octets (e.g., make_ip(192, 168, 1, 1))
inline uint32_t make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8)  |
           static_cast<uint32_t>(d);
}

// Extract octets from IP address
inline uint8_t ip_octet(uint32_t ip, uint8_t index)
{
    return static_cast<uint8_t>((ip >> (24 - index * 8)) & 0xFF);
}

// Convert IP address to string "A.B.C.D" (requires 16-byte buffer)
void ip_to_string(uint32_t ip, char* str);

// Parse IP address from string "A.B.C.D"
bool ip_from_string(const char* str, uint32_t* ip);

// Check if address is in the same subnet
inline bool is_same_subnet(uint32_t ip1, uint32_t ip2, uint32_t mask)
{
    return (ip1 & mask) == (ip2 & mask);
}

// Check if address is a broadcast address for the subnet
inline bool is_subnet_broadcast(uint32_t ip, uint32_t network, uint32_t mask)
{
    return ip == (network | ~mask);
}

// Check if address is a multicast address (224.0.0.0 - 239.255.255.255)
inline bool is_multicast(uint32_t ip)
{
    return (ip & 0xF0000000) == 0xE0000000;
}

// Check if address is loopback (127.x.x.x)
inline bool is_loopback(uint32_t ip)
{
    return (ip & 0xFF000000) == 0x7F000000;
}

// Check if address is link-local (169.254.x.x)
inline bool is_link_local(uint32_t ip)
{
    return (ip & 0xFFFF0000) == 0xA9FE0000;
}

// Check if address is private (10.x.x.x, 172.16-31.x.x, 192.168.x.x)
bool is_private(uint32_t ip);

// ================================================================
// Checksum Functions
// ================================================================

// Calculate IP header checksum
// Returns checksum in host byte order
uint16_t calculate_checksum(const void* data, uint16_t len);

// Verify IP header checksum
// Returns true if checksum is valid
bool verify_checksum(const Header* hdr);

// ================================================================
// Configuration
// ================================================================

// Initialize the IPv4 layer
void init();

// Configure network settings
void configure(uint32_t ip, uint32_t mask, uint32_t gateway, uint32_t dns);

// Set our MAC address (for Ethernet frame building)
void set_mac_address(const uint8_t* mac);

// Get current network configuration
const NetworkConfig* get_config();

// Check if network is configured
bool is_configured();

// ================================================================
// Routing
// ================================================================

// Add a route to the routing table
Status add_route(uint32_t network, uint32_t mask, uint32_t gateway, uint8_t metric);

// Remove a route
Status remove_route(uint32_t network, uint32_t mask);

// Find the best route for a destination
// Returns gateway address (0 = direct connection, or specific gateway)
uint32_t lookup_route(uint32_t dstAddr);

// Check if destination is on local network
bool is_local(uint32_t dstAddr);

// ================================================================
// Packet Parsing
// ================================================================

// Parse an IP packet from raw data
// Returns IP_OK on success, populates 'parsed' structure
Status parse_packet(const uint8_t* data, uint16_t len, ParsedPacket* parsed);

// Validate IP header (version, length, checksum)
Status validate_header(const uint8_t* data, uint16_t len);

// Extract just the header from packet data
Status extract_header(const uint8_t* data, uint16_t len, Header* header);

// Get payload pointer and length from packet
Status get_payload(const uint8_t* data, uint16_t len,
                   const uint8_t** payload, uint16_t* payloadLen);

// ================================================================
// Packet Building
// ================================================================

// Build an IP packet
//
// Parameters:
//   buffer     - Output buffer (must be at least headerLen + payloadLen)
//   bufferSize - Size of output buffer
//   dstAddr    - Destination IP (host byte order)
//   protocol   - Protocol number (PROTO_TCP, PROTO_UDP, PROTO_ICMP)
//   payload    - Payload data
//   payloadLen - Length of payload
//   packetLen  - Output: total packet length
//
// Returns IP_OK on success
Status build_packet(uint8_t* buffer,
                    uint16_t bufferSize,
                    uint32_t dstAddr,
                    uint8_t protocol,
                    const uint8_t* payload,
                    uint16_t payloadLen,
                    uint16_t* packetLen);

// Build IP header only (caller fills payload)
Status build_header(Header* hdr,
                    uint32_t srcAddr,
                    uint32_t dstAddr,
                    uint8_t protocol,
                    uint16_t payloadLen);

// ================================================================
// Packet Transmission
// ================================================================

// Send an IP packet to a destination
// Handles routing and Ethernet frame building internally
//
// Parameters:
//   payload    - Payload data (transport layer data)
//   len        - Payload length
//   dstAddr    - Destination IP address (host byte order)
//   protocol   - Protocol number
//
// Returns IP_OK on success
Status send_packet(const uint8_t* payload,
                   uint16_t len,
                   uint32_t dstAddr,
                   uint8_t protocol);

// Convenience function: send IP packet (as specified in requirements)
inline Status send_ip_packet(uint8_t* payload, uint16_t len, uint32_t dst_ip)
{
    return send_packet(payload, len, dst_ip, PROTO_UDP);
}

// Send raw IP packet (already built)
Status send_raw_packet(const uint8_t* packet, uint16_t len, uint32_t dstAddr);

// ================================================================
// Packet Reception (callback-based)
// ================================================================

// Protocol handler callback type
// Called when a packet is received for the specified protocol
typedef void (*ProtocolHandler)(const ParsedPacket* packet);

// Register a protocol handler
void register_handler(uint8_t protocol, ProtocolHandler handler);

// Unregister a protocol handler
void unregister_handler(uint8_t protocol);

// Process a received IP packet (called from Ethernet layer)
// Validates packet and dispatches to registered protocol handler
void handle_packet(const uint8_t* data, uint16_t len);

// ================================================================
// Statistics
// ================================================================

struct Statistics {
    uint32_t rxPackets;
    uint32_t txPackets;
    uint32_t rxBytes;
    uint32_t txBytes;
    uint32_t rxErrors;
    uint32_t txErrors;
    uint32_t checksumErrors;
    uint32_t noRouteErrors;
    uint32_t ttlExpired;
    uint32_t fragmentsDropped;
};

// Get IPv4 statistics
const Statistics* get_stats();

// Reset statistics
void reset_stats();

// ================================================================
// Network Polling
// ================================================================

// Poll for received network frames and dispatch through protocol stack
// Should be called periodically from main loop and during active waits
void poll_network();

} // namespace ipv4
} // namespace kernel

#endif // KERNEL_IPV4_H
