// DHCP Client for guideXOS
//
// Provides:
//   - DHCP packet format structures (RFC 2131)
//   - DHCP DISCOVER / OFFER / REQUEST / ACK state machine
//   - Automatic IP configuration via DHCP
//   - Lease management with renewal
//   - Integration with existing UDP and IPv4 layers
//
// Reference: RFC 2131 - Dynamic Host Configuration Protocol
//            RFC 2132 - DHCP Options and BOOTP Vendor Extensions
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_DHCP_H
#define KERNEL_DHCP_H

#include "kernel/types.h"

namespace kernel {
namespace dhcp {

// ================================================================
// DHCP Constants
// ================================================================

static const uint16_t DHCP_SERVER_PORT = 67;
static const uint16_t DHCP_CLIENT_PORT = 68;

static const uint16_t DHCP_PACKET_MIN_SIZE = 300;  // Minimum DHCP packet size
static const uint16_t DHCP_PACKET_MAX_SIZE = 576;  // Standard max (without options overflow)
static const uint16_t DHCP_OPTIONS_MAX     = 312;  // Maximum options field length

static const uint32_t DHCP_MAGIC_COOKIE = 0x63825363;  // Options magic cookie

// Hardware types (htype)
static const uint8_t HTYPE_ETHERNET = 1;
static const uint8_t HLEN_ETHERNET  = 6;    // Ethernet MAC address length

// Op codes
static const uint8_t BOOTREQUEST = 1;
static const uint8_t BOOTREPLY   = 2;

// Flags
static const uint16_t FLAG_BROADCAST = 0x8000;

// ================================================================
// DHCP Message Types (option 53)
// ================================================================

static const uint8_t DHCPDISCOVER = 1;
static const uint8_t DHCPOFFER   = 2;
static const uint8_t DHCPREQUEST = 3;
static const uint8_t DHCPDECLINE = 4;
static const uint8_t DHCPACK     = 5;
static const uint8_t DHCPNAK     = 6;
static const uint8_t DHCPRELEASE = 7;
static const uint8_t DHCPINFORM  = 8;

// ================================================================
// DHCP Option Tags (RFC 2132)
// ================================================================

static const uint8_t OPT_PAD             = 0;
static const uint8_t OPT_SUBNET_MASK     = 1;
static const uint8_t OPT_ROUTER          = 3;
static const uint8_t OPT_DNS_SERVER      = 6;
static const uint8_t OPT_HOSTNAME        = 12;
static const uint8_t OPT_DOMAIN_NAME     = 15;
static const uint8_t OPT_BROADCAST_ADDR  = 28;
static const uint8_t OPT_REQUESTED_IP    = 50;
static const uint8_t OPT_LEASE_TIME      = 51;
static const uint8_t OPT_MSG_TYPE        = 53;
static const uint8_t OPT_SERVER_ID       = 54;
static const uint8_t OPT_PARAM_REQUEST   = 55;
static const uint8_t OPT_RENEWAL_TIME    = 58;   // T1
static const uint8_t OPT_REBINDING_TIME  = 59;   // T2
static const uint8_t OPT_CLIENT_ID       = 61;
static const uint8_t OPT_END             = 255;

// ================================================================
// Timeouts and retries
// ================================================================

static const uint32_t DISCOVER_TIMEOUT_MS = 4000;   // Timeout for OFFER response
static const uint32_t REQUEST_TIMEOUT_MS  = 4000;   // Timeout for ACK response
static const uint8_t  MAX_DISCOVER_RETRIES = 4;
static const uint8_t  MAX_REQUEST_RETRIES  = 3;
static const uint32_t DEFAULT_LEASE_SECS   = 86400;  // 24 hours fallback

// ================================================================
// DHCP Packet Structure (548 bytes fixed, packed)
//
// All multi-byte fields in network byte order.
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define DHCP_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define DHCP_PACKED
#endif

struct Packet {
    uint8_t  op;           // Message op code: 1 = BOOTREQUEST, 2 = BOOTREPLY
    uint8_t  htype;        // Hardware address type (1 = Ethernet)
    uint8_t  hlen;         // Hardware address length (6 for Ethernet)
    uint8_t  hops;         // Relay agent hops
    uint32_t xid;          // Transaction ID (random)
    uint16_t secs;         // Seconds since client began process
    uint16_t flags;        // Flags (bit 15 = broadcast)
    uint32_t ciaddr;       // Client IP address (if already bound)
    uint32_t yiaddr;       // 'Your' (client) IP address (from server)
    uint32_t siaddr;       // Next server IP address
    uint32_t giaddr;       // Relay agent IP address
    uint8_t  chaddr[16];   // Client hardware address (MAC + padding)
    uint8_t  sname[64];    // Server host name (optional)
    uint8_t  file[128];    // Boot file name (optional)
    uint8_t  options[DHCP_OPTIONS_MAX]; // Options (starts with magic cookie)
} DHCP_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef DHCP_PACKED

// ================================================================
// DHCP Lease Information
// ================================================================

struct LeaseInfo {
    uint32_t assignedIP;       // IP address assigned by server
    uint32_t subnetMask;       // Subnet mask
    uint32_t gateway;          // Default gateway / router
    uint32_t dnsServer;        // Primary DNS server
    uint32_t serverIP;         // DHCP server identifier
    uint32_t leaseTime;        // Lease duration in seconds
    uint32_t renewalTime;      // T1: renewal time in seconds
    uint32_t rebindingTime;    // T2: rebinding time in seconds
    uint32_t leaseStartTick;   // System tick when lease was obtained
    uint32_t xid;              // Transaction ID used
    bool     valid;            // Lease is currently valid
};

// ================================================================
// DHCP Client State
// ================================================================

enum ClientState : uint8_t {
    STATE_INIT       = 0,      // Not started
    STATE_SELECTING  = 1,      // DISCOVER sent, waiting for OFFER
    STATE_REQUESTING = 2,      // REQUEST sent, waiting for ACK
    STATE_BOUND      = 3,      // Lease obtained, configured
    STATE_RENEWING   = 4,      // Renewing lease (T1 expired)
    STATE_REBINDING  = 5,      // Rebinding (T2 expired)
    STATE_RELEASED   = 6,      // Lease released
    STATE_ERROR      = 7,      // Error state
};

// ================================================================
// Parsed DHCP Options
// ================================================================

struct ParsedOptions {
    uint8_t  messageType;      // DHCP message type (option 53)
    uint32_t subnetMask;       // Subnet mask (option 1)
    uint32_t router;           // Router / gateway (option 3)
    uint32_t dnsServer;        // DNS server (option 6)
    uint32_t serverID;         // DHCP server identifier (option 54)
    uint32_t leaseTime;        // Lease time in seconds (option 51)
    uint32_t renewalTime;      // T1 renewal time (option 58)
    uint32_t rebindingTime;    // T2 rebinding time (option 59)
    uint32_t requestedIP;      // Requested IP (option 50)
    uint32_t broadcastAddr;    // Broadcast address (option 28)
    bool     hasSubnetMask;
    bool     hasRouter;
    bool     hasDNS;
    bool     hasServerID;
    bool     hasLeaseTime;
    bool     hasRenewalTime;
    bool     hasRebindingTime;
};

// ================================================================
// Status Codes
// ================================================================

enum Status : uint8_t {
    DHCP_OK              = 0,
    DHCP_ERR_NO_NIC      = 1,   // No NIC available
    DHCP_ERR_TIMEOUT     = 2,   // Response timed out
    DHCP_ERR_NAK         = 3,   // Server sent NAK
    DHCP_ERR_INVALID     = 4,   // Invalid packet or parameter
    DHCP_ERR_NETWORK     = 5,   // Network send/receive error
    DHCP_ERR_NO_OFFER    = 6,   // No OFFER received
    DHCP_ERR_NO_ACK      = 7,   // No ACK received
    DHCP_ERR_NOT_BOUND   = 8,   // Not in BOUND state
    DHCP_ERR_SEND_FAIL   = 9,   // UDP send failed
    DHCP_ERR_PARSE       = 10,  // Failed to parse response
};

// ================================================================
// Statistics
// ================================================================

struct Statistics {
    uint32_t discoversSent;
    uint32_t offersReceived;
    uint32_t requestsSent;
    uint32_t acksReceived;
    uint32_t naksReceived;
    uint32_t releasesSent;
    uint32_t timeouts;
    uint32_t errors;
    uint32_t renewals;
};

// ================================================================
// Initialization
// ================================================================

// Initialize DHCP client
void init();

// ================================================================
// DHCP Discovery and Configuration
// ================================================================

// Run full DHCP discovery (DISCOVER -> OFFER -> REQUEST -> ACK)
// This is a synchronous (blocking) operation.
// On success, configures the kernel networking state.
//
// Returns DHCP_OK on success
Status discover();

// ================================================================
// Packet Construction
// ================================================================

// Build a DHCP DISCOVER packet
// Sets random xid, includes client MAC, adds options (msg type, param request list)
//
// Parameters:
//   buffer     - Output buffer (must be >= sizeof(Packet))
//   bufferSize - Size of output buffer
//   mac        - Client MAC address (6 bytes)
//   xid        - Transaction ID
//   packetLen  - Output: length of built packet
//
// Returns DHCP_OK on success
Status build_discover(uint8_t* buffer, uint16_t bufferSize,
                      const uint8_t* mac, uint32_t xid,
                      uint16_t* packetLen);

// Build a DHCP REQUEST packet
// Includes offered IP and server identifier
//
// Parameters:
//   buffer     - Output buffer
//   bufferSize - Size of output buffer
//   mac        - Client MAC address (6 bytes)
//   xid        - Transaction ID
//   offeredIP  - IP offered by server (host byte order)
//   serverIP   - DHCP server IP (host byte order)
//   packetLen  - Output: length of built packet
//
// Returns DHCP_OK on success
Status build_request(uint8_t* buffer, uint16_t bufferSize,
                     const uint8_t* mac, uint32_t xid,
                     uint32_t offeredIP, uint32_t serverIP,
                     uint16_t* packetLen);

// Build a DHCP RELEASE packet
//
// Parameters:
//   buffer     - Output buffer
//   bufferSize - Size of output buffer
//   mac        - Client MAC address (6 bytes)
//   xid        - Transaction ID
//   clientIP   - Our assigned IP (host byte order)
//   serverIP   - DHCP server IP (host byte order)
//   packetLen  - Output: length of built packet
//
// Returns DHCP_OK on success
Status build_release(uint8_t* buffer, uint16_t bufferSize,
                     const uint8_t* mac, uint32_t xid,
                     uint32_t clientIP, uint32_t serverIP,
                     uint16_t* packetLen);

// ================================================================
// Packet Send / Receive (using existing UDP stack)
// ================================================================

// Send a DHCP packet via UDP broadcast
//
// Parameters:
//   packet    - DHCP packet buffer
//   length    - Packet length
//   serverIP  - Destination IP (host byte order, typically 0xFFFFFFFF)
//
// Returns DHCP_OK on success
Status dhcp_send(uint8_t* packet, size_t length, uint32_t server_ip);

// Receive a DHCP packet via UDP
//
// Parameters:
//   buffer    - Output buffer
//   maxLen    - Maximum buffer size
//   serverIP  - Output: source IP of the received packet (host byte order)
//
// Returns DHCP_OK on success, DHCP_ERR_TIMEOUT on timeout
Status dhcp_receive(uint8_t* buffer, size_t max_len, uint32_t* server_ip);

// ================================================================
// Options Parsing
// ================================================================

// Parse DHCP options from a packet
Status parse_options(const Packet* pkt, ParsedOptions* opts);

// ================================================================
// Lease Management
// ================================================================

// Get current lease information
const LeaseInfo* get_lease();

// Get current client state
ClientState get_state();

// Check if lease renewal is needed (call periodically)
// If renewal is needed, attempts to renew
void check_renewal();

// ================================================================
// Helper Functions
// ================================================================

// Release the current DHCP lease
// Sends DHCP RELEASE to the server
Status dhcp_release();

// Print assigned IP, subnet mask, gateway, and DNS
void dhcp_print_info();

// Log transaction IDs, server IPs, and message types for testing
void dhcp_debug();

// Get DHCP message type name as string
const char* msgtype_to_string(uint8_t type);

// Get client state name as string
const char* state_to_string(ClientState state);

// ================================================================
// Statistics
// ================================================================

const Statistics* get_stats();
void reset_stats();

} // namespace dhcp
} // namespace kernel

#endif // KERNEL_DHCP_H
