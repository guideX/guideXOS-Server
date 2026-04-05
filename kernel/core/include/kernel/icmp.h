// ICMP (Internet Control Message Protocol) Implementation
//
// Provides:
//   - ICMP packet structure and parsing
//   - Echo Request/Reply (ping) functionality
//   - ICMP checksum calculation
//   - Integration with IPv4 layer
//
// Reference: RFC 792 - Internet Control Message Protocol
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_ICMP_H
#define KERNEL_ICMP_H

#include "kernel/types.h"
#include "kernel/ipv4.h"

namespace kernel {
namespace icmp {

// ================================================================
// ICMP Message Types
// ================================================================

static const uint8_t TYPE_ECHO_REPLY        = 0;
static const uint8_t TYPE_DEST_UNREACHABLE  = 3;
static const uint8_t TYPE_SOURCE_QUENCH     = 4;
static const uint8_t TYPE_REDIRECT          = 5;
static const uint8_t TYPE_ECHO_REQUEST      = 8;
static const uint8_t TYPE_TIME_EXCEEDED     = 11;
static const uint8_t TYPE_PARAMETER_PROBLEM = 12;
static const uint8_t TYPE_TIMESTAMP         = 13;
static const uint8_t TYPE_TIMESTAMP_REPLY   = 14;
static const uint8_t TYPE_INFO_REQUEST      = 15;
static const uint8_t TYPE_INFO_REPLY        = 16;

// ================================================================
// ICMP Codes for Destination Unreachable (Type 3)
// ================================================================

static const uint8_t CODE_NET_UNREACHABLE   = 0;
static const uint8_t CODE_HOST_UNREACHABLE  = 1;
static const uint8_t CODE_PROTO_UNREACHABLE = 2;
static const uint8_t CODE_PORT_UNREACHABLE  = 3;
static const uint8_t CODE_FRAG_NEEDED       = 4;
static const uint8_t CODE_SRC_ROUTE_FAILED  = 5;

// ================================================================
// ICMP Codes for Time Exceeded (Type 11)
// ================================================================

static const uint8_t CODE_TTL_EXCEEDED      = 0;
static const uint8_t CODE_FRAG_REASSEMBLY   = 1;

// ================================================================
// ICMP Header Structure (8 bytes minimum)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define ICMP_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define ICMP_PACKED
#endif

struct Header {
    uint8_t  type;        // ICMP message type
    uint8_t  code;        // Type-specific code
    uint16_t checksum;    // ICMP checksum (header + data)
    // Rest of header depends on type
    union {
        struct {
            uint16_t id;        // Identifier (for echo)
            uint16_t sequence;  // Sequence number (for echo)
        } echo;
        uint32_t gateway;       // Gateway address (for redirect)
        struct {
            uint16_t unused;
            uint16_t nextHopMTU; // For fragmentation needed
        } frag;
        uint32_t unused;        // For other types
    } rest;
} ICMP_PACKED;

// Echo data follows the header
struct EchoPacket {
    Header  header;
    uint8_t data[56];    // Typical ping payload (64 bytes total ICMP)
} ICMP_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef ICMP_PACKED

// ================================================================
// Parsed ICMP packet
// ================================================================

struct ParsedPacket {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;          // For echo messages
    uint16_t sequence;    // For echo messages
    
    const uint8_t* data;
    uint16_t dataLen;
    
    uint32_t srcIP;       // From IP header
    uint32_t dstIP;       // From IP header
    
    bool isValid;
    bool checksumValid;
};

// ================================================================
// Ping result
// ================================================================

enum PingResult : uint8_t {
    PING_SUCCESS         = 0,
    PING_TIMEOUT         = 1,
    PING_NET_UNREACHABLE = 2,
    PING_HOST_UNREACHABLE = 3,
    PING_DEST_UNREACHABLE = 4,
    PING_TTL_EXCEEDED    = 5,
    PING_NO_ROUTE        = 6,
    PING_TX_FAILED       = 7,
    PING_NOT_CONFIGURED  = 8,
};

struct PingReply {
    PingResult result;
    uint32_t   srcIP;         // IP that responded
    uint16_t   sequence;      // Sequence number
    uint16_t   ttl;           // TTL from reply
    uint16_t   rtt;           // Round-trip time in ms (approximate)
    uint16_t   dataLen;       // Reply data length
};

// ================================================================
// Ping session (for tracking requests)
// ================================================================

struct PingSession {
    uint32_t targetIP;
    uint16_t id;              // Session ID
    uint16_t sequence;        // Current sequence number
    uint16_t sent;            // Packets sent
    uint16_t received;        // Replies received
    uint16_t timeoutMs;       // Timeout in milliseconds
    bool     active;
    bool     replyReceived;   // Flag set when reply arrives
    PingReply lastReply;      // Most recent reply
};

// ================================================================
// Status codes
// ================================================================

enum Status : uint8_t {
    ICMP_OK             = 0,
    ICMP_ERR_NULL_PTR   = 1,
    ICMP_ERR_TOO_SHORT  = 2,
    ICMP_ERR_BAD_CHECKSUM = 3,
    ICMP_ERR_TX_FAILED  = 4,
    ICMP_ERR_NOT_CONFIGURED = 5,
    ICMP_ERR_NO_SESSION = 6,
};

// ================================================================
// Checksum
// ================================================================

// Calculate ICMP checksum over header and data
uint16_t calculate_checksum(const void* data, uint16_t len);

// Verify ICMP checksum
bool verify_checksum(const uint8_t* data, uint16_t len);

// ================================================================
// Initialization
// ================================================================

// Initialize ICMP module and register with IPv4 layer
void init();

// ================================================================
// Packet Parsing
// ================================================================

// Parse an ICMP packet (from IPv4 payload)
Status parse_packet(const uint8_t* data, uint16_t len,
                    uint32_t srcIP, uint32_t dstIP,
                    ParsedPacket* parsed);

// ================================================================
// Echo (Ping) Functions
// ================================================================

// Send a single ICMP echo request (ping)
// Returns sequence number used, or 0 on error
Status send_echo_request(uint32_t targetIP, uint16_t id,
                         uint16_t sequence, uint16_t dataLen);

// Send echo reply (response to request)
Status send_echo_reply(uint32_t destIP, uint16_t id,
                       uint16_t sequence, const uint8_t* data,
                       uint16_t dataLen);

// High-level ping function - sends ping and waits for reply
// timeout in milliseconds (approximate, uses polling)
PingResult ping(uint32_t targetIP, uint16_t timeoutMs, PingReply* reply);

// Start a ping session (for multiple pings)
Status start_ping_session(uint32_t targetIP, uint16_t timeoutMs);

// Send next ping in session
Status ping_session_send();

// Check for ping reply (non-blocking)
bool ping_session_check_reply(PingReply* reply);

// Get ping session statistics
void ping_session_stats(uint16_t* sent, uint16_t* received);

// End ping session
void end_ping_session();

// ================================================================
// Send ICMP Error Messages
// ================================================================

// Send Destination Unreachable
Status send_dest_unreachable(uint32_t destIP, uint8_t code,
                             const uint8_t* originalIP, uint16_t originalLen);

// Send Time Exceeded
Status send_time_exceeded(uint32_t destIP, uint8_t code,
                          const uint8_t* originalIP, uint16_t originalLen);

// ================================================================
// Handler (called from IPv4 layer)
// ================================================================

// Handle incoming ICMP packet
void handle_packet(const ipv4::ParsedPacket* ipPacket);

// ================================================================
// Statistics
// ================================================================

struct Statistics {
    uint32_t echoRequestsSent;
    uint32_t echoRequestsReceived;
    uint32_t echoRepliesSent;
    uint32_t echoRepliesReceived;
    uint32_t destUnreachableSent;
    uint32_t destUnreachableReceived;
    uint32_t timeExceededReceived;
    uint32_t checksumErrors;
    uint32_t unknownTypes;
};

const Statistics* get_stats();

// ================================================================
// Utility - get human-readable name for type
// ================================================================

const char* type_name(uint8_t type);

} // namespace icmp
} // namespace kernel

#endif // KERNEL_ICMP_H
