// IPv6 Network Layer
//
// Provides:
//   - IPv6 header structure and parsing
//   - IPv6 extension headers
//   - ICMPv6 protocol
//   - Neighbor Discovery Protocol (NDP)
//   - IPv6 address utilities
//
// References:
//   - RFC 8200 - Internet Protocol, Version 6 (IPv6)
//   - RFC 4443 - ICMPv6
//   - RFC 4861 - Neighbor Discovery for IPv6
//   - RFC 4862 - IPv6 Stateless Address Autoconfiguration
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_IPV6_H
#define KERNEL_IPV6_H

#include "kernel/types.h"
#include "kernel/ethernet.h"

namespace kernel {
namespace ipv6 {

// ================================================================
// IPv6 Constants
// ================================================================

static const uint8_t  IP_VERSION        = 6;
static const uint8_t  HEADER_LEN        = 40;     // Fixed header length
static const uint16_t DEFAULT_HOP_LIMIT = 64;
static const uint16_t MTU_MIN           = 1280;   // Minimum IPv6 MTU
static const uint16_t MTU_DEFAULT       = 1500;   // Standard Ethernet MTU

// ================================================================
// IPv6 Address Structure (128 bits)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define IPV6_ADDR_PACKED __attribute__((packed))
#else
#define IPV6_ADDR_PACKED
#endif

struct Address {
    union {
        uint8_t  bytes[16];
        uint16_t words[8];
        uint32_t dwords[4];
        uint64_t qwords[2];
    };
} IPV6_ADDR_PACKED;

#undef IPV6_ADDR_PACKED

// ================================================================
// Well-Known IPv6 Addresses
// ================================================================

// Unspecified address (::)
static const Address ADDR_UNSPECIFIED = {{{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}};

// Loopback address (::1)
static const Address ADDR_LOOPBACK = {{{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}}};

// All-nodes multicast (ff02::1)
static const Address ADDR_ALL_NODES_LINK = {{{0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,1}}};

// All-routers multicast (ff02::2)
static const Address ADDR_ALL_ROUTERS_LINK = {{{0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,2}}};

// ================================================================
// IPv6 Next Header Values (Protocol Numbers)
// ================================================================

static const uint8_t PROTO_HOP_BY_HOP   = 0;    // Hop-by-Hop Options
static const uint8_t PROTO_ICMPV6       = 58;   // ICMPv6
static const uint8_t PROTO_TCP          = 6;    // TCP
static const uint8_t PROTO_UDP          = 17;   // UDP
static const uint8_t PROTO_ROUTING      = 43;   // Routing Header
static const uint8_t PROTO_FRAGMENT     = 44;   // Fragment Header
static const uint8_t PROTO_ESP          = 50;   // Encapsulating Security Payload
static const uint8_t PROTO_AH           = 51;   // Authentication Header
static const uint8_t PROTO_NONE         = 59;   // No Next Header
static const uint8_t PROTO_DEST_OPTIONS = 60;   // Destination Options
static const uint8_t PROTO_MOBILITY     = 135;  // Mobility
static const uint8_t PROTO_HIP          = 139;  // Host Identity Protocol
static const uint8_t PROTO_SHIM6        = 140;  // Shim6

// ================================================================
// IPv6 Header Structure (40 bytes, fixed)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define IPV6_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define IPV6_PACKED
#endif

struct Header {
    uint32_t versionClassFlow;    // Version (4) + Traffic Class (8) + Flow Label (20)
    uint16_t payloadLength;       // Payload length (network order)
    uint8_t  nextHeader;          // Next header type (protocol)
    uint8_t  hopLimit;            // Hop limit (TTL equivalent)
    Address  srcAddr;             // Source address (128 bits)
    Address  dstAddr;             // Destination address (128 bits)
} IPV6_PACKED;

// ================================================================
// Extension Header Structures
// ================================================================

// Generic extension header (for hop-by-hop, destination options)
struct ExtensionHeader {
    uint8_t  nextHeader;          // Next header type
    uint8_t  headerLen;           // Header length in 8-byte units (excluding first 8)
    uint8_t  data[];              // Variable length data
} IPV6_PACKED;

// Routing Header
struct RoutingHeader {
    uint8_t  nextHeader;
    uint8_t  headerLen;
    uint8_t  routingType;
    uint8_t  segmentsLeft;
    uint8_t  data[];              // Type-specific data
} IPV6_PACKED;

// Fragment Header (8 bytes, fixed)
struct FragmentHeader {
    uint8_t  nextHeader;
    uint8_t  reserved;
    uint16_t fragOffsetFlags;     // Fragment Offset (13) + Reserved (2) + M flag (1)
    uint32_t identification;       // Fragment identification
} IPV6_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef IPV6_PACKED

// ================================================================
// IPv6 Header Field Accessors
// ================================================================

// Get IP version from header (should be 6)
inline uint8_t get_version(const Header* hdr)
{
    return (ethernet::ntohl(hdr->versionClassFlow) >> 28) & 0x0F;
}

// Get Traffic Class
inline uint8_t get_traffic_class(const Header* hdr)
{
    return (ethernet::ntohl(hdr->versionClassFlow) >> 20) & 0xFF;
}

// Get Flow Label
inline uint32_t get_flow_label(const Header* hdr)
{
    return ethernet::ntohl(hdr->versionClassFlow) & 0x000FFFFF;
}

// Get payload length
inline uint16_t get_payload_length(const Header* hdr)
{
    return ethernet::ntohs(hdr->payloadLength);
}

// Set version, traffic class, and flow label
inline void set_version_class_flow(Header* hdr, uint8_t trafficClass, uint32_t flowLabel)
{
    uint32_t value = (6U << 28) | 
                     ((static_cast<uint32_t>(trafficClass) & 0xFF) << 20) |
                     (flowLabel & 0x000FFFFF);
    hdr->versionClassFlow = ethernet::htonl(value);
}

// Fragment header accessors
inline uint16_t get_fragment_offset(const FragmentHeader* frag)
{
    return (ethernet::ntohs(frag->fragOffsetFlags) >> 3) * 8;
}

inline bool get_more_fragments(const FragmentHeader* frag)
{
    return (ethernet::ntohs(frag->fragOffsetFlags) & 1) != 0;
}

// ================================================================
// IPv6 Address Utilities
// ================================================================

// Compare two IPv6 addresses
bool addr_equals(const Address* a, const Address* b);

// Check if address is unspecified (::)
bool is_unspecified(const Address* addr);

// Check if address is loopback (::1)
bool is_loopback(const Address* addr);

// Check if address is multicast (ff00::/8)
bool is_multicast(const Address* addr);

// Check if address is link-local (fe80::/10)
bool is_link_local(const Address* addr);

// Check if address is site-local (fec0::/10, deprecated)
bool is_site_local(const Address* addr);

// Check if address is global unicast
bool is_global_unicast(const Address* addr);

// Check if address is solicited-node multicast
bool is_solicited_node_multicast(const Address* addr);

// Create solicited-node multicast address from unicast
void make_solicited_node_multicast(const Address* unicast, Address* multicast);

// Create link-local address from EUI-64 (MAC address)
void make_link_local_from_mac(const uint8_t* mac, Address* addr);

// Convert address to string (returns static buffer)
const char* addr_to_string(const Address* addr);

// Parse address from string
bool string_to_addr(const char* str, Address* addr);

// ================================================================
// Pseudo Header for Checksum (ICMPv6, TCP, UDP)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define IPV6_PSEUDO_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define IPV6_PSEUDO_PACKED
#endif

struct PseudoHeader {
    Address  srcAddr;
    Address  dstAddr;
    uint32_t upperLayerLength;    // Upper-layer packet length
    uint8_t  zeros[3];
    uint8_t  nextHeader;          // Protocol
} IPV6_PSEUDO_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef IPV6_PSEUDO_PACKED

// Calculate checksum for upper-layer protocols
uint16_t calculate_checksum(const Address* srcAddr, const Address* dstAddr,
                            uint8_t protocol, const uint8_t* data, uint16_t len);

// ================================================================
// IPv6 Packet Handling
// ================================================================

// Validate IPv6 header
bool validate_header(const Header* hdr, size_t totalLen);

// Process received IPv6 packet
void process_packet(const uint8_t* packet, size_t len);

// Send IPv6 packet
int send_packet(const Address* dstAddr, uint8_t protocol,
                const uint8_t* payload, uint16_t payloadLen);

// ================================================================
// Extension Header Processing
// ================================================================

// Find next header after processing extension headers
// Returns pointer to upper-layer data, or nullptr on error
// Updates nextProto with final next header value
const uint8_t* skip_extension_headers(const uint8_t* data, size_t len,
                                       uint8_t firstNextHeader, uint8_t* nextProto);

// ================================================================
// IPv6 Statistics
// ================================================================

struct Statistics {
    uint64_t rxPackets;           // Received packets
    uint64_t txPackets;           // Transmitted packets
    uint64_t rxBytes;             // Received bytes
    uint64_t txBytes;             // Transmitted bytes
    uint64_t rxErrors;            // Receive errors
    uint64_t txErrors;            // Transmit errors
    uint64_t badVersion;          // Wrong IP version
    uint64_t badLength;           // Invalid length
    uint64_t unknownNextHeader;   // Unknown next header
    uint64_t hopLimitExceeded;    // Hop limit exceeded
    uint64_t fragments;           // Fragments received
    uint64_t reassembled;         // Packets reassembled
    uint64_t reassemblyFailed;    // Reassembly failures
};

void get_statistics(Statistics* stats);
void reset_statistics();

// ================================================================
// Initialization
// ================================================================

bool init();

} // namespace ipv6
} // namespace kernel

#endif // KERNEL_IPV6_H
