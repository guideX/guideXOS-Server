// Socket API for guideXOS Server
//
// Provides a lightweight BSD-style socket interface:
//   - udp_socket()    - Create a UDP socket
//   - udp_bind()      - Bind socket to local port
//   - udp_sendto()    - Send datagram to remote address
//   - udp_recvfrom()  - Receive datagram (non-blocking)
//   - udp_close()     - Close socket
//
// Internally uses the UDP and IPv4 layers.
// Supports multiple concurrent UDP endpoints.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_SOCKET_H
#define KERNEL_SOCKET_H

#include "kernel/types.h"
#include "kernel/udp.h"
#include "kernel/ipv4.h"

namespace kernel {
namespace socket {

// ================================================================
// Socket Constants
// ================================================================

static const int INVALID_SOCKET = -1;
static const uint8_t MAX_SOCKETS = 16;
static const uint16_t MAX_RX_QUEUE = 8;      // Max queued datagrams per socket
static const uint16_t MAX_DGRAM_SIZE = 1472; // Max datagram payload (MTU)

// ================================================================
// Socket Types
// ================================================================

enum SocketType : uint8_t {
    SOCK_NONE   = 0,
    SOCK_DGRAM  = 1,    // UDP datagram socket
    // SOCK_STREAM = 2, // TCP stream socket (future)
};

// ================================================================
// Socket Address Structure
// ================================================================

struct SockAddr {
    uint32_t addr;      // IP address (host byte order)
    uint16_t port;      // Port number (host byte order)
};

// Helper to create socket address
inline SockAddr make_sockaddr(uint32_t ip, uint16_t port)
{
    SockAddr sa;
    sa.addr = ip;
    sa.port = port;
    return sa;
}

// Any address (for binding)
static const SockAddr ADDR_ANY = { 0, 0 };

// ================================================================
// Received Datagram (queued for recvfrom)
// ================================================================

struct ReceivedDatagram {
    SockAddr from;                      // Source address
    uint8_t  data[MAX_DGRAM_SIZE];      // Datagram payload
    uint16_t len;                       // Payload length
    bool     valid;                     // Entry is valid
};

// ================================================================
// Socket Structure
// ================================================================

struct Socket {
    int          fd;                    // Socket descriptor
    SocketType   type;                  // Socket type
    SockAddr     localAddr;             // Local bound address
    SockAddr     remoteAddr;            // Connected remote address (optional)
    bool         bound;                 // Socket is bound to port
    bool         connected;             // Socket has default remote
    bool         active;                // Socket is in use
    bool         nonBlocking;           // Non-blocking mode
    
    // Receive queue (circular buffer)
    ReceivedDatagram rxQueue[MAX_RX_QUEUE];
    uint8_t      rxHead;                // Next slot to write
    uint8_t      rxTail;                // Next slot to read
    uint8_t      rxCount;               // Number of queued datagrams
};

// ================================================================
// Error Codes
// ================================================================

enum Error : int {
    SOCK_OK             = 0,
    SOCK_ERR_INVALID    = -1,   // Invalid socket descriptor
    SOCK_ERR_INUSE      = -2,   // Address/port already in use
    SOCK_ERR_NOBUFS     = -3,   // No buffer space available
    SOCK_ERR_NOTCONN    = -4,   // Socket not connected
    SOCK_ERR_DESTADDR   = -5,   // Destination address required
    SOCK_ERR_MSGSIZE    = -6,   // Message too long
    SOCK_ERR_WOULDBLOCK = -7,   // Operation would block
    SOCK_ERR_NOPROTO    = -8,   // Protocol not available
    SOCK_ERR_NOSOCK     = -9,   // No sockets available
    SOCK_ERR_NOTBOUND   = -10,  // Socket not bound
    SOCK_ERR_NETDOWN    = -11,  // Network is down
};

// ================================================================
// Socket API Functions
// ================================================================

// Initialize the socket subsystem
void init();

// ------------------------------------------------------------
// Socket Creation and Destruction
// ------------------------------------------------------------

// Create a UDP socket
// Returns: socket descriptor (>= 0) on success, INVALID_SOCKET on error
int udp_socket();

// Close a socket and release resources
// Returns: SOCK_OK on success, error code on failure
int udp_close(int sockfd);

// ------------------------------------------------------------
// Socket Binding
// ------------------------------------------------------------

// Bind socket to a local port
// port = 0 allocates an ephemeral port
// Returns: SOCK_OK on success, error code on failure
int udp_bind(int sockfd, uint16_t port);

// Bind socket to specific local address and port
int udp_bind_addr(int sockfd, const SockAddr* addr);

// Get the local address of a bound socket
int udp_getsockname(int sockfd, SockAddr* addr);

// ------------------------------------------------------------
// Sending Data
// ------------------------------------------------------------

// Send datagram to specified address
// Returns: number of bytes sent on success, error code on failure
int udp_sendto(int sockfd, const void* buf, uint16_t len,
               const SockAddr* destAddr);

// Send datagram to specified IP and port (convenience)
int udp_sendto_ip(int sockfd, const void* buf, uint16_t len,
                  uint32_t destIP, uint16_t destPort);

// Connect socket to remote address (sets default destination)
int udp_connect(int sockfd, const SockAddr* destAddr);

// Send to connected address
int udp_send(int sockfd, const void* buf, uint16_t len);

// ------------------------------------------------------------
// Receiving Data
// ------------------------------------------------------------

// Receive datagram and get source address
// Returns: number of bytes received on success, error code on failure
// If no data available and non-blocking, returns SOCK_ERR_WOULDBLOCK
int udp_recvfrom(int sockfd, void* buf, uint16_t maxLen,
                 SockAddr* srcAddr);

// Receive from connected socket (discard source address)
int udp_recv(int sockfd, void* buf, uint16_t maxLen);

// Check if data is available for reading
bool udp_readable(int sockfd);

// Get number of queued datagrams
int udp_pending(int sockfd);

// ------------------------------------------------------------
// Socket Options
// ------------------------------------------------------------

// Set socket to non-blocking mode
int udp_setnonblocking(int sockfd, bool enabled);

// Get socket type
SocketType udp_getsocktype(int sockfd);

// Check if socket is valid
bool udp_isvalid(int sockfd);

// ------------------------------------------------------------
// Statistics
// ------------------------------------------------------------

struct SocketStats {
    uint32_t socketsCreated;
    uint32_t socketsClosed;
    uint32_t activeSockets;
    uint32_t datagramsSent;
    uint32_t datagramsReceived;
    uint32_t datagramsDropped;
    uint32_t bytesSent;
    uint32_t bytesReceived;
};

const SocketStats* get_stats();

} // namespace socket
} // namespace kernel

#endif // KERNEL_SOCKET_H
