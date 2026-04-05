// UDP (User Datagram Protocol) Implementation
//
// Provides:
//   - UDP header structure and parsing
//   - Datagram send/receive functionality
//   - Port binding for multiple endpoints
//   - Checksum calculation (optional per RFC 768)
//   - Integration with IPv4 layer
//
// Reference: RFC 768 - User Datagram Protocol
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_UDP_H
#define KERNEL_UDP_H

#include "kernel/types.h"
#include "kernel/ipv4.h"

namespace kernel {
namespace udp {

// ================================================================
// UDP Constants
// ================================================================

static const uint16_t HEADER_LEN     = 8;       // UDP header size
static const uint16_t MAX_DATAGRAM   = 65535;   // Maximum UDP datagram
static const uint16_t MAX_PAYLOAD    = 65527;   // Max payload (65535 - 8)
static const uint16_t MTU_PAYLOAD    = 1472;    // Max payload for Ethernet MTU

// ================================================================
// Well-known ports
// ================================================================

static const uint16_t PORT_ECHO      = 7;
static const uint16_t PORT_DISCARD   = 9;
static const uint16_t PORT_DAYTIME   = 13;
static const uint16_t PORT_FTP_DATA  = 20;
static const uint16_t PORT_FTP       = 21;
static const uint16_t PORT_SSH       = 22;
static const uint16_t PORT_TELNET    = 23;
static const uint16_t PORT_SMTP      = 25;
static const uint16_t PORT_DNS       = 53;
static const uint16_t PORT_BOOTPS    = 67;      // DHCP server
static const uint16_t PORT_BOOTPC    = 68;      // DHCP client
static const uint16_t PORT_TFTP      = 69;
static const uint16_t PORT_HTTP      = 80;
static const uint16_t PORT_NTP       = 123;
static const uint16_t PORT_SNMP      = 161;
static const uint16_t PORT_HTTPS     = 443;
static const uint16_t PORT_SYSLOG    = 514;

// Ephemeral port range
static const uint16_t EPHEMERAL_START = 49152;
static const uint16_t EPHEMERAL_END   = 65535;

// ================================================================
// UDP Header Structure (8 bytes, packed)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define UDP_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define UDP_PACKED
#endif

struct Header {
    uint16_t srcPort;     // Source port (network byte order)
    uint16_t dstPort;     // Destination port (network byte order)
    uint16_t length;      // Length of UDP header + data (network byte order)
    uint16_t checksum;    // Checksum (0 = disabled)
} UDP_PACKED;

// UDP pseudo-header for checksum calculation
struct PseudoHeader {
    uint32_t srcAddr;     // Source IP (network byte order)
    uint32_t dstAddr;     // Destination IP (network byte order)
    uint8_t  zero;        // Zero padding
    uint8_t  protocol;    // Protocol (17 for UDP)
    uint16_t udpLength;   // UDP length (network byte order)
} UDP_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef UDP_PACKED

// ================================================================
// Parsed datagram information
// ================================================================

struct ParsedDatagram {
    // Header fields (host byte order)
    uint16_t srcPort;
    uint16_t dstPort;
    uint16_t length;
    uint16_t checksum;
    
    // IP layer info
    uint32_t srcIP;
    uint32_t dstIP;
    
    // Payload
    const uint8_t* data;
    uint16_t dataLen;
    
    // Validation
    bool isValid;
    bool checksumValid;
};

// ================================================================
// Socket/Endpoint binding
// ================================================================

// Callback for received datagrams on a bound port
typedef void (*DatagramHandler)(const ParsedDatagram* datagram);

struct Binding {
    uint16_t port;              // Bound local port
    uint32_t localAddr;         // Bound local address (0 = any)
    DatagramHandler handler;    // Callback for received data
    bool active;
};

static const uint8_t MAX_BINDINGS = 16;

// ================================================================
// Status codes
// ================================================================

enum Status : uint8_t {
    UDP_OK              = 0,
    UDP_ERR_NULL_PTR    = 1,
    UDP_ERR_TOO_SHORT   = 2,
    UDP_ERR_TOO_LONG    = 3,
    UDP_ERR_BAD_CHECKSUM = 4,
    UDP_ERR_NO_BINDING  = 5,    // Port not bound
    UDP_ERR_PORT_IN_USE = 6,    // Port already bound
    UDP_ERR_NO_PORT     = 7,    // No available ephemeral port
    UDP_ERR_TX_FAILED   = 8,
    UDP_ERR_NOT_CONFIGURED = 9,
    UDP_ERR_BUFFER_SMALL = 10,
};

// ================================================================
// Statistics
// ================================================================

struct Statistics {
    uint32_t rxDatagrams;
    uint32_t txDatagrams;
    uint32_t rxBytes;
    uint32_t txBytes;
    uint32_t rxErrors;
    uint32_t txErrors;
    uint32_t checksumErrors;
    uint32_t noPortErrors;      // Datagrams to unbound ports
    uint32_t droppedDatagrams;
};

// ================================================================
// Checksum Functions
// ================================================================

// Calculate UDP checksum with pseudo-header
// Returns checksum in network byte order (ready to put in header)
uint16_t calculate_checksum(uint32_t srcIP, uint32_t dstIP,
                            const uint8_t* udpPacket, uint16_t udpLen);

// Verify UDP checksum (0 = disabled, 0xFFFF = valid)
bool verify_checksum(uint32_t srcIP, uint32_t dstIP,
                     const uint8_t* udpPacket, uint16_t udpLen);

// ================================================================
// Initialization
// ================================================================

// Initialize UDP layer and register with IPv4
void init();

// ================================================================
// Port Binding
// ================================================================

// Bind a port to receive datagrams
// localAddr = 0 means bind to all interfaces (INADDR_ANY)
Status bind(uint16_t port, uint32_t localAddr, DatagramHandler handler);

// Unbind a port
Status unbind(uint16_t port);

// Check if a port is bound
bool is_bound(uint16_t port);

// Allocate an ephemeral port (for client sockets)
// Returns allocated port number, or 0 on failure
uint16_t alloc_ephemeral_port();

// Free an ephemeral port
void free_ephemeral_port(uint16_t port);

// ================================================================
// Datagram Parsing
// ================================================================

// Parse a UDP datagram (from IPv4 payload)
Status parse_datagram(const uint8_t* data, uint16_t len,
                      uint32_t srcIP, uint32_t dstIP,
                      ParsedDatagram* parsed);

// ================================================================
// Datagram Transmission
// ================================================================

// Send a UDP datagram
//
// Parameters:
//   srcPort  - Source port (local)
//   dstIP    - Destination IP address (host byte order)
//   dstPort  - Destination port
//   data     - Payload data
//   len      - Payload length
//
// Returns UDP_OK on success
Status send(uint16_t srcPort, uint32_t dstIP, uint16_t dstPort,
            const uint8_t* data, uint16_t len);

// Convenience function: send UDP datagram (as specified in requirements)
inline Status udp_send(uint16_t port, uint32_t dst_ip, uint16_t dst_port,
                       const uint8_t* data, uint16_t length)
{
    return send(port, dst_ip, dst_port, data, length);
}

// Send datagram with automatic ephemeral source port
// Returns the source port used in *srcPort (0 on error)
Status send_auto(uint32_t dstIP, uint16_t dstPort,
                 const uint8_t* data, uint16_t len,
                 uint16_t* srcPort);

// ================================================================
// Datagram Reception (called from IPv4 layer)
// ================================================================

// Handle incoming UDP datagram
void handle_packet(const ipv4::ParsedPacket* ipPacket);

// ================================================================
// Statistics
// ================================================================

const Statistics* get_stats();
void reset_stats();

// ================================================================
// Utility
// ================================================================

// Get service name for well-known port
const char* port_name(uint16_t port);

} // namespace udp
} // namespace kernel

#endif // KERNEL_UDP_H
