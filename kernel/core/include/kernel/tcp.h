// TCP (Transmission Control Protocol) Implementation
//
// Provides:
//   - TCP header structure and parsing
//   - Connection state machine (RFC 793)
//   - Reliable data transfer with retransmissions
//   - Socket-like API for applications
//
// Reference: RFC 793 - Transmission Control Protocol
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_TCP_H
#define KERNEL_TCP_H

#include "kernel/types.h"
#include "kernel/ipv4.h"
#include "kernel/ethernet.h"

namespace kernel {
namespace tcp {

// ================================================================
// TCP Constants
// ================================================================

static const uint8_t  HEADER_LEN_MIN   = 20;      // Minimum header (no options)
static const uint8_t  HEADER_LEN_MAX   = 60;      // Maximum header
static const uint16_t MSS_DEFAULT      = 1460;    // Default MSS (MTU - IP - TCP)
static const uint16_t WINDOW_DEFAULT   = 8192;    // Default window size
static const uint16_t MAX_SEGMENT_DATA = 1460;    // Max segment payload

// Timeouts (in milliseconds, approximate using PIT ticks)
static const uint32_t TIMEOUT_CONNECT  = 5000;    // Connection timeout
static const uint32_t TIMEOUT_RETX     = 1000;    // Retransmission timeout
static const uint32_t TIMEOUT_TIMEWAIT = 30000;   // TIME_WAIT timeout
static const uint8_t  MAX_RETRIES      = 5;       // Max retransmission attempts

// Buffer sizes
static const uint16_t TX_BUFFER_SIZE   = 4096;
static const uint16_t RX_BUFFER_SIZE   = 4096;

// ================================================================
// TCP Flags
// ================================================================

static const uint8_t FLAG_FIN = 0x01;
static const uint8_t FLAG_SYN = 0x02;
static const uint8_t FLAG_RST = 0x04;
static const uint8_t FLAG_PSH = 0x08;
static const uint8_t FLAG_ACK = 0x10;
static const uint8_t FLAG_URG = 0x20;

// ================================================================
// TCP Connection States (RFC 793)
// ================================================================

enum State : uint8_t {
    STATE_CLOSED      = 0,
    STATE_LISTEN      = 1,
    STATE_SYN_SENT    = 2,
    STATE_SYN_RCVD    = 3,
    STATE_ESTABLISHED = 4,
    STATE_FIN_WAIT_1  = 5,
    STATE_FIN_WAIT_2  = 6,
    STATE_CLOSE_WAIT  = 7,
    STATE_CLOSING     = 8,
    STATE_LAST_ACK    = 9,
    STATE_TIME_WAIT   = 10,
};

// ================================================================
// TCP Header Structure (20-60 bytes, packed)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define TCP_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define TCP_PACKED
#endif

struct Header {
    uint16_t srcPort;       // Source port
    uint16_t dstPort;       // Destination port
    uint32_t seqNum;        // Sequence number
    uint32_t ackNum;        // Acknowledgment number
    uint8_t  dataOffset;    // Data offset (4 bits) + reserved (4 bits)
    uint8_t  flags;         // Control flags
    uint16_t window;        // Window size
    uint16_t checksum;      // Checksum
    uint16_t urgentPtr;     // Urgent pointer
    // Options follow if data offset > 5
} TCP_PACKED;

// Pseudo-header for checksum (same as UDP)
struct PseudoHeader {
    uint32_t srcAddr;
    uint32_t dstAddr;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcpLength;
} TCP_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef TCP_PACKED

// ================================================================
// Header field accessors
// ================================================================

inline uint8_t get_data_offset(const Header* hdr)
{
    return (hdr->dataOffset >> 4) & 0x0F;
}

inline uint8_t get_header_len(const Header* hdr)
{
    return get_data_offset(hdr) * 4;
}

inline void set_data_offset(Header* hdr, uint8_t words)
{
    hdr->dataOffset = (words << 4) | (hdr->dataOffset & 0x0F);
}

// ================================================================
// TCP Endpoint (address + port)
// ================================================================

struct Endpoint {
    uint32_t addr;
    uint16_t port;
};

inline bool endpoint_equals(const Endpoint* a, const Endpoint* b)
{
    return a->addr == b->addr && a->port == b->port;
}

// ================================================================
// Transmission Control Block (TCB)
// ================================================================

struct TCB {
    // Connection identification
    Endpoint local;
    Endpoint remote;
    
    // Connection state
    State state;
    
    // Send sequence variables
    uint32_t snd_una;       // Send unacknowledged
    uint32_t snd_nxt;       // Send next
    uint32_t snd_wnd;       // Send window
    uint32_t iss;           // Initial send sequence number
    
    // Receive sequence variables
    uint32_t rcv_nxt;       // Receive next
    uint32_t rcv_wnd;       // Receive window
    uint32_t irs;           // Initial receive sequence number
    
    // Buffers
    uint8_t  txBuffer[TX_BUFFER_SIZE];
    uint16_t txLen;
    uint16_t txHead;
    uint16_t txTail;
    
    uint8_t  rxBuffer[RX_BUFFER_SIZE];
    uint16_t rxLen;
    uint16_t rxHead;
    uint16_t rxTail;
    
    // Retransmission
    uint32_t retxTime;      // Time of last transmission
    uint8_t  retxCount;     // Retry counter
    
    // Flags
    bool     active;
    bool     needAck;       // Need to send ACK
    bool     finSent;       // FIN has been sent
    bool     finReceived;   // FIN has been received
};

// ================================================================
// TCP Socket
// ================================================================

struct Socket {
    int      fd;            // Socket descriptor
    TCB*     tcb;           // Transmission control block
    bool     active;
    bool     listening;     // In LISTEN state
    bool     nonBlocking;
    
    // For listening sockets: pending connections
    static const uint8_t BACKLOG_MAX = 4;
    TCB*     pendingConnections[4];
    uint8_t  pendingCount;
};

static const uint8_t MAX_SOCKETS = 8;
static const uint8_t MAX_TCBS = 16;

// ================================================================
// Status codes
// ================================================================

enum Status : int {
    TCP_OK              = 0,
    TCP_ERR_INVALID     = -1,
    TCP_ERR_INUSE       = -2,
    TCP_ERR_NOBUFS      = -3,
    TCP_ERR_NOTCONN     = -4,
    TCP_ERR_CONNREFUSED = -5,
    TCP_ERR_TIMEOUT     = -6,
    TCP_ERR_CONNRESET   = -7,
    TCP_ERR_WOULDBLOCK  = -8,
    TCP_ERR_ALREADY     = -9,
    TCP_ERR_ISCONN      = -10,
    TCP_ERR_NOTBOUND    = -11,
    TCP_ERR_NOSOCK      = -12,
    TCP_ERR_NETDOWN     = -13,
};

// ================================================================
// Parsed segment information
// ================================================================

struct ParsedSegment {
    // Header fields (host byte order)
    uint16_t srcPort;
    uint16_t dstPort;
    uint32_t seqNum;
    uint32_t ackNum;
    uint8_t  headerLen;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    
    // IP layer
    uint32_t srcIP;
    uint32_t dstIP;
    
    // Payload
    const uint8_t* data;
    uint16_t dataLen;
    
    bool isValid;
    bool checksumValid;
};

// ================================================================
// Statistics
// ================================================================

struct Statistics {
    uint32_t segmentsSent;
    uint32_t segmentsReceived;
    uint32_t retransmissions;
    uint32_t connectionsOpened;
    uint32_t connectionsClosed;
    uint32_t connectionsReset;
    uint32_t checksumErrors;
    uint32_t activeConnections;
};

// ================================================================
// Initialization
// ================================================================

void init();

// ================================================================
// Checksum
// ================================================================

uint16_t calculate_checksum(uint32_t srcIP, uint32_t dstIP,
                            const uint8_t* segment, uint16_t len);

bool verify_checksum(uint32_t srcIP, uint32_t dstIP,
                     const uint8_t* segment, uint16_t len);

// ================================================================
// Socket API
// ================================================================

// Create a TCP socket
// Returns: socket descriptor (>= 0) or error code
int tcp_socket();

// Bind socket to local port
int tcp_bind(int sockfd, uint16_t port);

// Start listening for connections
int tcp_listen(int sockfd, int backlog);

// Accept incoming connection (returns new socket)
int tcp_accept(int sockfd, Endpoint* clientAddr);

// Connect to remote host
int tcp_connect(int sockfd, uint32_t destIP, uint16_t destPort);

// Send data
// Returns: bytes sent or error code
int tcp_send(int sockfd, const void* buf, uint16_t len);

// Receive data
// Returns: bytes received or error code  
int tcp_recv(int sockfd, void* buf, uint16_t maxLen);

// Close connection gracefully
int tcp_close(int sockfd);

// Abort connection (send RST)
int tcp_abort(int sockfd);

// ================================================================
// Socket Options
// ================================================================

// Set non-blocking mode
int tcp_setnonblocking(int sockfd, bool enabled);

// Check if socket is connected
bool tcp_isconnected(int sockfd);

// Check if data is available
bool tcp_readable(int sockfd);

// Check if socket can send
bool tcp_writable(int sockfd);

// Get connection state
State tcp_getstate(int sockfd);

// Get peer address
int tcp_getpeername(int sockfd, Endpoint* addr);

// ================================================================
// Internal: Packet handling
// ================================================================

// Handle incoming TCP segment (called from IPv4)
void handle_segment(const ipv4::ParsedPacket* ipPacket);

// Process timer events (call periodically)
void process_timers();

// ================================================================
// Statistics
// ================================================================

const Statistics* get_stats();

// ================================================================
// Utility
// ================================================================

// Get state name as string
const char* state_name(State state);

} // namespace tcp
} // namespace kernel

#endif // KERNEL_TCP_H
