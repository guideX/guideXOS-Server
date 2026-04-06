// ICMPv6 Protocol
//
// Implements ICMPv6 (Internet Control Message Protocol for IPv6).
//
// References:
//   - RFC 4443 - ICMPv6
//   - RFC 4861 - Neighbor Discovery for IPv6
//   - RFC 4862 - IPv6 Stateless Address Autoconfiguration
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_ICMPV6_H
#define KERNEL_ICMPV6_H

#include "kernel/types.h"
#include "kernel/ipv6.h"

namespace kernel {
namespace icmpv6 {

// ================================================================
// ICMPv6 Message Types
// ================================================================

// Error Messages (0-127)
static const uint8_t TYPE_DEST_UNREACHABLE     = 1;
static const uint8_t TYPE_PACKET_TOO_BIG       = 2;
static const uint8_t TYPE_TIME_EXCEEDED        = 3;
static const uint8_t TYPE_PARAMETER_PROBLEM    = 4;

// Informational Messages (128-255)
static const uint8_t TYPE_ECHO_REQUEST         = 128;
static const uint8_t TYPE_ECHO_REPLY           = 129;

// Neighbor Discovery Messages
static const uint8_t TYPE_ROUTER_SOLICITATION  = 133;
static const uint8_t TYPE_ROUTER_ADVERTISEMENT = 134;
static const uint8_t TYPE_NEIGHBOR_SOLICITATION = 135;
static const uint8_t TYPE_NEIGHBOR_ADVERTISEMENT = 136;
static const uint8_t TYPE_REDIRECT             = 137;

// Multicast Listener Discovery
static const uint8_t TYPE_MLD_QUERY            = 130;
static const uint8_t TYPE_MLD_REPORT           = 131;
static const uint8_t TYPE_MLD_DONE             = 132;
static const uint8_t TYPE_MLD2_REPORT          = 143;

// ================================================================
// ICMPv6 Code Values
// ================================================================

// Destination Unreachable codes
static const uint8_t CODE_NO_ROUTE            = 0;
static const uint8_t CODE_ADMIN_PROHIBITED    = 1;
static const uint8_t CODE_BEYOND_SCOPE        = 2;
static const uint8_t CODE_ADDR_UNREACHABLE    = 3;
static const uint8_t CODE_PORT_UNREACHABLE    = 4;
static const uint8_t CODE_FAILED_POLICY       = 5;
static const uint8_t CODE_REJECT_ROUTE        = 6;
static const uint8_t CODE_SOURCE_ROUTE_ERROR  = 7;

// Time Exceeded codes
static const uint8_t CODE_HOP_LIMIT_EXCEEDED  = 0;
static const uint8_t CODE_FRAGMENT_REASSEMBLY = 1;

// Parameter Problem codes
static const uint8_t CODE_ERRONEOUS_HEADER    = 0;
static const uint8_t CODE_UNKNOWN_NEXT_HEADER = 1;
static const uint8_t CODE_UNKNOWN_OPTION      = 2;

// ================================================================
// ICMPv6 Header Structure
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define ICMPV6_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define ICMPV6_PACKED
#endif

// Generic ICMPv6 header (4 bytes minimum)
struct Header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
} ICMPV6_PACKED;

// Echo Request/Reply
struct EchoMessage {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t  data[];
} ICMPV6_PACKED;

// Destination Unreachable
struct DestUnreachable {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t unused;
    uint8_t  invoking[];          // As much of invoking packet as fits
} ICMPV6_PACKED;

// Packet Too Big
struct PacketTooBig {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t mtu;
    uint8_t  invoking[];
} ICMPV6_PACKED;

// Time Exceeded
struct TimeExceeded {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t unused;
    uint8_t  invoking[];
} ICMPV6_PACKED;

// Parameter Problem
struct ParameterProblem {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t pointer;             // Offset of problem field
    uint8_t  invoking[];
} ICMPV6_PACKED;

// ================================================================
// Neighbor Discovery Structures
// ================================================================

// Router Solicitation
struct RouterSolicitation {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t reserved;
    uint8_t  options[];
} ICMPV6_PACKED;

// Router Advertisement
struct RouterAdvertisement {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint8_t  curHopLimit;
    uint8_t  flags;               // M, O, and other flags
    uint16_t routerLifetime;
    uint32_t reachableTime;
    uint32_t retransTimer;
    uint8_t  options[];
} ICMPV6_PACKED;

// Router Advertisement flags
static const uint8_t RA_FLAG_MANAGED     = 0x80;  // Managed address config
static const uint8_t RA_FLAG_OTHER       = 0x40;  // Other config
static const uint8_t RA_FLAG_HOME_AGENT  = 0x20;  // Home Agent
static const uint8_t RA_FLAG_DEFAULT_PREF_MASK = 0x18;
static const uint8_t RA_FLAG_PROXY       = 0x04;

// Neighbor Solicitation
struct NeighborSolicitation {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t reserved;
    ipv6::Address targetAddr;
    uint8_t  options[];
} ICMPV6_PACKED;

// Neighbor Advertisement
struct NeighborAdvertisement {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t flags;               // R, S, O flags + reserved
    ipv6::Address targetAddr;
    uint8_t  options[];
} ICMPV6_PACKED;

// Neighbor Advertisement flags
static const uint32_t NA_FLAG_ROUTER     = 0x80000000;
static const uint32_t NA_FLAG_SOLICITED  = 0x40000000;
static const uint32_t NA_FLAG_OVERRIDE   = 0x20000000;

// Redirect
struct Redirect {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t reserved;
    ipv6::Address targetAddr;
    ipv6::Address destAddr;
    uint8_t  options[];
} ICMPV6_PACKED;

// ================================================================
// NDP Option Structures
// ================================================================

// Option header (common to all options)
struct NdpOptionHeader {
    uint8_t type;
    uint8_t length;               // In 8-byte units
} ICMPV6_PACKED;

// NDP Option Types
static const uint8_t OPT_SOURCE_LL_ADDR  = 1;
static const uint8_t OPT_TARGET_LL_ADDR  = 2;
static const uint8_t OPT_PREFIX_INFO     = 3;
static const uint8_t OPT_REDIRECT_HEADER = 4;
static const uint8_t OPT_MTU             = 5;
static const uint8_t OPT_RDNSS           = 25;  // Recursive DNS Server
static const uint8_t OPT_DNSSL           = 31;  // DNS Search List

// Source/Target Link-Layer Address Option
struct LLAddrOption {
    uint8_t type;
    uint8_t length;
    uint8_t llAddr[6];            // Link-layer address (Ethernet)
} ICMPV6_PACKED;

// Prefix Information Option
struct PrefixInfoOption {
    uint8_t  type;
    uint8_t  length;
    uint8_t  prefixLength;
    uint8_t  flags;               // L, A flags
    uint32_t validLifetime;
    uint32_t preferredLifetime;
    uint32_t reserved;
    ipv6::Address prefix;
} ICMPV6_PACKED;

// Prefix Information flags
static const uint8_t PREFIX_FLAG_ON_LINK   = 0x80;
static const uint8_t PREFIX_FLAG_AUTO      = 0x40;
static const uint8_t PREFIX_FLAG_ROUTER    = 0x20;

// MTU Option
struct MtuOption {
    uint8_t  type;
    uint8_t  length;
    uint16_t reserved;
    uint32_t mtu;
} ICMPV6_PACKED;

// RDNSS Option (Recursive DNS Server)
struct RdnssOption {
    uint8_t  type;
    uint8_t  length;
    uint16_t reserved;
    uint32_t lifetime;
    ipv6::Address servers[];      // Variable number of DNS servers
} ICMPV6_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef ICMPV6_PACKED

// ================================================================
// ICMPv6 Functions
// ================================================================

// Process received ICMPv6 message
void process_message(const ipv6::Address* srcAddr, const ipv6::Address* dstAddr,
                     const uint8_t* data, size_t len);

// Send Echo Request (ping)
int send_echo_request(const ipv6::Address* dstAddr, uint16_t identifier,
                      uint16_t sequence, const void* data, size_t dataLen);

// Send Echo Reply
int send_echo_reply(const ipv6::Address* dstAddr, uint16_t identifier,
                    uint16_t sequence, const void* data, size_t dataLen);

// Send Destination Unreachable
int send_dest_unreachable(const ipv6::Address* dstAddr, uint8_t code,
                          const uint8_t* invoking, size_t invokingLen);

// Send Packet Too Big
int send_packet_too_big(const ipv6::Address* dstAddr, uint32_t mtu,
                        const uint8_t* invoking, size_t invokingLen);

// Send Time Exceeded
int send_time_exceeded(const ipv6::Address* dstAddr, uint8_t code,
                       const uint8_t* invoking, size_t invokingLen);

// ================================================================
// Neighbor Discovery Functions
// ================================================================

// Send Router Solicitation
int send_router_solicitation();

// Send Neighbor Solicitation
int send_neighbor_solicitation(const ipv6::Address* targetAddr);

// Send Neighbor Advertisement
int send_neighbor_advertisement(const ipv6::Address* dstAddr,
                                const ipv6::Address* targetAddr,
                                bool router, bool solicited, bool override);

// Process Router Advertisement
void process_router_advertisement(const ipv6::Address* srcAddr,
                                  const RouterAdvertisement* ra, size_t len);

// Process Neighbor Solicitation
void process_neighbor_solicitation(const ipv6::Address* srcAddr,
                                   const NeighborSolicitation* ns, size_t len);

// Process Neighbor Advertisement
void process_neighbor_advertisement(const ipv6::Address* srcAddr,
                                    const NeighborAdvertisement* na, size_t len);

// ================================================================
// Checksum
// ================================================================

// Calculate ICMPv6 checksum
uint16_t calculate_checksum(const ipv6::Address* srcAddr,
                            const ipv6::Address* dstAddr,
                            const uint8_t* data, size_t len);

// Verify ICMPv6 checksum
bool verify_checksum(const ipv6::Address* srcAddr,
                     const ipv6::Address* dstAddr,
                     const uint8_t* data, size_t len);

// ================================================================
// Statistics
// ================================================================

struct Statistics {
    uint64_t echoRequests;
    uint64_t echoReplies;
    uint64_t destUnreachables;
    uint64_t packetTooBig;
    uint64_t timeExceeded;
    uint64_t parameterProblems;
    uint64_t routerSolicitations;
    uint64_t routerAdvertisements;
    uint64_t neighborSolicitations;
    uint64_t neighborAdvertisements;
    uint64_t redirects;
    uint64_t unknownTypes;
    uint64_t checksumErrors;
};

void get_statistics(Statistics* stats);

// ================================================================
// Initialization
// ================================================================

bool init();

} // namespace icmpv6
} // namespace kernel

#endif // KERNEL_ICMPV6_H
