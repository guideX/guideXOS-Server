// Socket API Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/socket.h"
#include "include/kernel/udp.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace socket {

// ================================================================
// Internal helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

// ================================================================
// Internal state
// ================================================================

static Socket s_sockets[MAX_SOCKETS];
static SocketStats s_stats;
static int s_nextFd = 0;

// ================================================================
// Internal: UDP datagram handler
// ================================================================

// Forward received datagrams to the appropriate socket
static void socket_datagram_handler(const udp::ParsedDatagram* datagram)
{
    if (!datagram || !datagram->isValid) return;
    
    // Find the socket bound to this port
    for (uint8_t i = 0; i < MAX_SOCKETS; ++i) {
        Socket* sock = &s_sockets[i];
        
        if (!sock->active || !sock->bound) continue;
        if (sock->localAddr.port != datagram->dstPort) continue;
        
        // Check local address filter (0 = any)
        if (sock->localAddr.addr != 0 &&
            sock->localAddr.addr != datagram->dstIP) continue;
        
        // Found matching socket - queue the datagram
        if (sock->rxCount >= MAX_RX_QUEUE) {
            // Queue full, drop datagram
            s_stats.datagramsDropped++;
            return;
        }
        
        // Copy to receive queue
        ReceivedDatagram* rx = &sock->rxQueue[sock->rxHead];
        rx->from.addr = datagram->srcIP;
        rx->from.port = datagram->srcPort;
        rx->len = (datagram->dataLen > MAX_DGRAM_SIZE) ? 
                  MAX_DGRAM_SIZE : datagram->dataLen;
        
        if (datagram->data && rx->len > 0) {
            memcopy(rx->data, datagram->data, rx->len);
        }
        rx->valid = true;
        
        // Advance head pointer
        sock->rxHead = (sock->rxHead + 1) % MAX_RX_QUEUE;
        sock->rxCount++;
        
        s_stats.datagramsReceived++;
        s_stats.bytesReceived += rx->len;
        
        return;
    }
    
    // No socket found for this port (handled by UDP layer)
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    memzero(s_sockets, sizeof(s_sockets));
    memzero(&s_stats, sizeof(s_stats));
    s_nextFd = 0;
    
    serial::puts("[SOCKET] API initialized\n");
}

// ================================================================
// Internal: Find socket by fd
// ================================================================

static Socket* find_socket(int sockfd)
{
    if (sockfd < 0) return nullptr;
    
    for (uint8_t i = 0; i < MAX_SOCKETS; ++i) {
        if (s_sockets[i].active && s_sockets[i].fd == sockfd) {
            return &s_sockets[i];
        }
    }
    return nullptr;
}

// ================================================================
// Socket Creation and Destruction
// ================================================================

int udp_socket()
{
    // Check network availability
    if (!ipv4::is_configured()) {
        return SOCK_ERR_NETDOWN;
    }
    
    // Find free socket slot
    Socket* sock = nullptr;
    for (uint8_t i = 0; i < MAX_SOCKETS; ++i) {
        if (!s_sockets[i].active) {
            sock = &s_sockets[i];
            break;
        }
    }
    
    if (!sock) {
        return SOCK_ERR_NOSOCK;
    }
    
    // Initialize socket
    memzero(sock, sizeof(Socket));
    sock->fd = s_nextFd++;
    sock->type = SOCK_DGRAM;
    sock->active = true;
    sock->nonBlocking = true;  // Default to non-blocking
    
    s_stats.socketsCreated++;
    s_stats.activeSockets++;
    
    serial::puts("[SOCKET] Created UDP socket fd=");
    serial::put_hex32(sock->fd);
    serial::putc('\n');
    
    return sock->fd;
}

int udp_close(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    // Unbind from UDP layer if bound
    if (sock->bound) {
        udp::unbind(sock->localAddr.port);
    }
    
    // Clear socket
    sock->active = false;
    
    s_stats.socketsClosed++;
    s_stats.activeSockets--;
    
    serial::puts("[SOCKET] Closed socket fd=");
    serial::put_hex32(sockfd);
    serial::putc('\n');
    
    return SOCK_OK;
}

// ================================================================
// Socket Binding
// ================================================================

int udp_bind(int sockfd, uint16_t port)
{
    SockAddr addr;
    addr.addr = 0;  // Any address
    addr.port = port;
    return udp_bind_addr(sockfd, &addr);
}

int udp_bind_addr(int sockfd, const SockAddr* addr)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    if (sock->bound) {
        // Already bound - unbind first
        udp::unbind(sock->localAddr.port);
    }
    
    uint16_t port = addr ? addr->port : 0;
    
    // Allocate ephemeral port if needed
    if (port == 0) {
        port = udp::alloc_ephemeral_port();
        if (port == 0) {
            return SOCK_ERR_NOBUFS;
        }
    }
    
    // Bind to UDP layer
    udp::Status status = udp::bind(port, addr ? addr->addr : 0, 
                                   socket_datagram_handler);
    
    if (status == udp::UDP_ERR_PORT_IN_USE) {
        return SOCK_ERR_INUSE;
    }
    if (status != udp::UDP_OK) {
        return SOCK_ERR_NOBUFS;
    }
    
    sock->localAddr.addr = addr ? addr->addr : 0;
    sock->localAddr.port = port;
    sock->bound = true;
    
    serial::puts("[SOCKET] Bound socket fd=");
    serial::put_hex32(sockfd);
    serial::puts(" to port ");
    serial::put_hex32(port);
    serial::putc('\n');
    
    return SOCK_OK;
}

int udp_getsockname(int sockfd, SockAddr* addr)
{
    if (!addr) return SOCK_ERR_INVALID;
    
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    if (!sock->bound) {
        return SOCK_ERR_NOTBOUND;
    }
    
    *addr = sock->localAddr;
    
    // If bound to any address, return our actual IP
    if (addr->addr == 0) {
        const ipv4::NetworkConfig* cfg = ipv4::get_config();
        if (cfg) {
            addr->addr = cfg->ipAddr;
        }
    }
    
    return SOCK_OK;
}

// ================================================================
// Sending Data
// ================================================================

int udp_sendto(int sockfd, const void* buf, uint16_t len,
               const SockAddr* destAddr)
{
    if (!buf || !destAddr) {
        return SOCK_ERR_INVALID;
    }
    
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    if (len > MAX_DGRAM_SIZE) {
        return SOCK_ERR_MSGSIZE;
    }
    
    // Auto-bind if not bound
    if (!sock->bound) {
        int err = udp_bind(sockfd, 0);
        if (err < 0) return err;
    }
    
    // Send via UDP layer
    udp::Status status = udp::send(
        sock->localAddr.port,
        destAddr->addr,
        destAddr->port,
        static_cast<const uint8_t*>(buf),
        len);
    
    if (status != udp::UDP_OK) {
        return SOCK_ERR_NETDOWN;
    }
    
    s_stats.datagramsSent++;
    s_stats.bytesSent += len;
    
    return static_cast<int>(len);
}

int udp_sendto_ip(int sockfd, const void* buf, uint16_t len,
                  uint32_t destIP, uint16_t destPort)
{
    SockAddr dest;
    dest.addr = destIP;
    dest.port = destPort;
    return udp_sendto(sockfd, buf, len, &dest);
}

int udp_connect(int sockfd, const SockAddr* destAddr)
{
    if (!destAddr) {
        return SOCK_ERR_INVALID;
    }
    
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    sock->remoteAddr = *destAddr;
    sock->connected = true;
    
    return SOCK_OK;
}

int udp_send(int sockfd, const void* buf, uint16_t len)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    if (!sock->connected) {
        return SOCK_ERR_DESTADDR;
    }
    
    return udp_sendto(sockfd, buf, len, &sock->remoteAddr);
}

// ================================================================
// Receiving Data
// ================================================================

int udp_recvfrom(int sockfd, void* buf, uint16_t maxLen,
                 SockAddr* srcAddr)
{
    if (!buf) {
        return SOCK_ERR_INVALID;
    }
    
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    if (!sock->bound) {
        return SOCK_ERR_NOTBOUND;
    }
    
    // Check receive queue
    if (sock->rxCount == 0) {
        return SOCK_ERR_WOULDBLOCK;
    }
    
    // Get datagram from queue
    ReceivedDatagram* rx = &sock->rxQueue[sock->rxTail];
    
    if (!rx->valid) {
        return SOCK_ERR_WOULDBLOCK;
    }
    
    // Copy data
    uint16_t copyLen = (rx->len < maxLen) ? rx->len : maxLen;
    memcopy(buf, rx->data, copyLen);
    
    // Return source address if requested
    if (srcAddr) {
        *srcAddr = rx->from;
    }
    
    // Clear entry and advance tail
    rx->valid = false;
    sock->rxTail = (sock->rxTail + 1) % MAX_RX_QUEUE;
    sock->rxCount--;
    
    return static_cast<int>(copyLen);
}

int udp_recv(int sockfd, void* buf, uint16_t maxLen)
{
    return udp_recvfrom(sockfd, buf, maxLen, nullptr);
}

bool udp_readable(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) return false;
    
    return sock->rxCount > 0;
}

int udp_pending(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) return SOCK_ERR_INVALID;
    
    return static_cast<int>(sock->rxCount);
}

// ================================================================
// Socket Options
// ================================================================

int udp_setnonblocking(int sockfd, bool enabled)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) {
        return SOCK_ERR_INVALID;
    }
    
    sock->nonBlocking = enabled;
    return SOCK_OK;
}

SocketType udp_getsocktype(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) return SOCK_NONE;
    
    return sock->type;
}

bool udp_isvalid(int sockfd)
{
    return find_socket(sockfd) != nullptr;
}

// ================================================================
// Statistics
// ================================================================

const SocketStats* get_stats()
{
    return &s_stats;
}

} // namespace socket
} // namespace kernel
