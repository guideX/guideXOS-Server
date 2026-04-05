// TCP Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/tcp.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/pit.h"

namespace kernel {
namespace tcp {

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

// Simple pseudo-random for ISN generation
static uint32_t s_issClock = 0;

static uint32_t generate_isn()
{
    s_issClock += static_cast<uint32_t>(pit::ticks()) * 64000;
    return s_issClock;
}

// ================================================================
// Internal state
// ================================================================

static Socket s_sockets[MAX_SOCKETS];
static TCB s_tcbs[MAX_TCBS];
static Statistics s_stats;
static int s_nextFd = 100;  // Start at 100 to distinguish from UDP sockets

// ================================================================
// Checksum calculation
// ================================================================

static uint32_t checksum_add(const void* data, uint16_t len)
{
    const uint16_t* ptr = static_cast<const uint16_t*>(data);
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len > 0) {
        sum += *reinterpret_cast<const uint8_t*>(ptr);
    }
    
    return sum;
}

static uint16_t checksum_fold(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

uint16_t calculate_checksum(uint32_t srcIP, uint32_t dstIP,
                            const uint8_t* segment, uint16_t len)
{
    PseudoHeader pseudo;
    pseudo.srcAddr = ethernet::htonl(srcIP);
    pseudo.dstAddr = ethernet::htonl(dstIP);
    pseudo.zero = 0;
    pseudo.protocol = ipv4::PROTO_TCP;
    pseudo.tcpLength = ethernet::htons(len);
    
    uint32_t sum = checksum_add(&pseudo, sizeof(PseudoHeader));
    sum += checksum_add(segment, len);
    
    uint16_t result = checksum_fold(sum);
    if (result == 0) result = 0xFFFF;
    
    return result;
}

bool verify_checksum(uint32_t srcIP, uint32_t dstIP,
                     const uint8_t* segment, uint16_t len)
{
    if (!segment || len < HEADER_LEN_MIN) return false;
    
    const Header* hdr = reinterpret_cast<const Header*>(segment);
    if (hdr->checksum == 0) return false;  // TCP checksum is mandatory
    
    PseudoHeader pseudo;
    pseudo.srcAddr = ethernet::htonl(srcIP);
    pseudo.dstAddr = ethernet::htonl(dstIP);
    pseudo.zero = 0;
    pseudo.protocol = ipv4::PROTO_TCP;
    pseudo.tcpLength = ethernet::htons(len);
    
    uint32_t sum = checksum_add(&pseudo, sizeof(PseudoHeader));
    sum += checksum_add(segment, len);
    
    return checksum_fold(sum) == 0;
}

// ================================================================
// TCB Management
// ================================================================

static TCB* alloc_tcb()
{
    for (uint8_t i = 0; i < MAX_TCBS; ++i) {
        if (!s_tcbs[i].active) {
            memzero(&s_tcbs[i], sizeof(TCB));
            s_tcbs[i].active = true;
            s_tcbs[i].state = STATE_CLOSED;
            s_tcbs[i].rcv_wnd = WINDOW_DEFAULT;
            return &s_tcbs[i];
        }
    }
    return nullptr;
}

static void free_tcb(TCB* tcb)
{
    if (tcb) {
        tcb->active = false;
        tcb->state = STATE_CLOSED;
    }
}

static TCB* find_tcb(const Endpoint* local, const Endpoint* remote)
{
    for (uint8_t i = 0; i < MAX_TCBS; ++i) {
        TCB* tcb = &s_tcbs[i];
        if (!tcb->active) continue;
        
        // Check exact match
        if (tcb->local.port == local->port &&
            (tcb->local.addr == 0 || tcb->local.addr == local->addr) &&
            tcb->remote.port == remote->port &&
            tcb->remote.addr == remote->addr) {
            return tcb;
        }
    }
    return nullptr;
}

static TCB* find_listening_tcb(uint16_t port)
{
    for (uint8_t i = 0; i < MAX_TCBS; ++i) {
        TCB* tcb = &s_tcbs[i];
        if (tcb->active && tcb->state == STATE_LISTEN &&
            tcb->local.port == port) {
            return tcb;
        }
    }
    return nullptr;
}

// ================================================================
// Socket Management
// ================================================================

static Socket* find_socket(int sockfd)
{
    for (uint8_t i = 0; i < MAX_SOCKETS; ++i) {
        if (s_sockets[i].active && s_sockets[i].fd == sockfd) {
            return &s_sockets[i];
        }
    }
    return nullptr;
}

static Socket* find_socket_for_tcb(TCB* tcb)
{
    for (uint8_t i = 0; i < MAX_SOCKETS; ++i) {
        if (s_sockets[i].active && s_sockets[i].tcb == tcb) {
            return &s_sockets[i];
        }
    }
    return nullptr;
}

// ================================================================
// Segment Transmission
// ================================================================

static Status send_segment(TCB* tcb, uint8_t flags,
                           const uint8_t* data, uint16_t dataLen)
{
    if (!ipv4::is_configured()) return TCP_ERR_NETDOWN;
    
    const ipv4::NetworkConfig* cfg = ipv4::get_config();
    if (!cfg) return TCP_ERR_NETDOWN;
    
    // Build TCP segment
    uint8_t segment[HEADER_LEN_MIN + MAX_SEGMENT_DATA];
    Header* hdr = reinterpret_cast<Header*>(segment);
    
    memzero(hdr, HEADER_LEN_MIN);
    
    hdr->srcPort = ethernet::htons(tcb->local.port);
    hdr->dstPort = ethernet::htons(tcb->remote.port);
    hdr->seqNum = ethernet::htonl(tcb->snd_nxt);
    hdr->ackNum = ethernet::htonl(tcb->rcv_nxt);
    set_data_offset(hdr, 5);  // 20 bytes, no options
    hdr->flags = flags;
    hdr->window = ethernet::htons(static_cast<uint16_t>(tcb->rcv_wnd));
    hdr->urgentPtr = 0;
    hdr->checksum = 0;
    
    uint16_t segLen = HEADER_LEN_MIN;
    
    // Copy data if any
    if (data && dataLen > 0) {
        if (dataLen > MAX_SEGMENT_DATA) dataLen = MAX_SEGMENT_DATA;
        memcopy(segment + HEADER_LEN_MIN, data, dataLen);
        segLen += dataLen;
    }
    
    // Calculate checksum
    uint32_t srcIP = cfg->ipAddr;
    if (tcb->local.addr != 0) srcIP = tcb->local.addr;
    
    hdr->checksum = calculate_checksum(srcIP, tcb->remote.addr, segment, segLen);
    
    // Send via IPv4
    ipv4::Status status = ipv4::send_packet(segment, segLen,
                                            tcb->remote.addr, ipv4::PROTO_TCP);
    
    if (status != ipv4::IP_OK) {
        return TCP_ERR_NETDOWN;
    }
    
    // Update sequence number for data + SYN/FIN
    if (dataLen > 0) tcb->snd_nxt += dataLen;
    if (flags & FLAG_SYN) tcb->snd_nxt++;
    if (flags & FLAG_FIN) {
        tcb->snd_nxt++;
        tcb->finSent = true;
    }
    
    tcb->retxTime = static_cast<uint32_t>(pit::ticks());
    s_stats.segmentsSent++;
    
    return TCP_OK;
}

// Send RST segment
static void send_rst(uint32_t srcIP, uint16_t srcPort,
                     uint32_t dstIP, uint16_t dstPort,
                     uint32_t seqNum, uint32_t ackNum)
{
    if (!ipv4::is_configured()) return;
    
    uint8_t segment[HEADER_LEN_MIN];
    Header* hdr = reinterpret_cast<Header*>(segment);
    
    memzero(hdr, HEADER_LEN_MIN);
    
    hdr->srcPort = ethernet::htons(srcPort);
    hdr->dstPort = ethernet::htons(dstPort);
    hdr->seqNum = ethernet::htonl(seqNum);
    hdr->ackNum = ethernet::htonl(ackNum);
    set_data_offset(hdr, 5);
    hdr->flags = FLAG_RST | FLAG_ACK;
    hdr->window = 0;
    hdr->checksum = 0;
    
    hdr->checksum = calculate_checksum(srcIP, dstIP, segment, HEADER_LEN_MIN);
    
    ipv4::send_packet(segment, HEADER_LEN_MIN, dstIP, ipv4::PROTO_TCP);
    s_stats.segmentsSent++;
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    memzero(s_sockets, sizeof(s_sockets));
    memzero(s_tcbs, sizeof(s_tcbs));
    memzero(&s_stats, sizeof(s_stats));
    s_nextFd = 100;
    s_issClock = static_cast<uint32_t>(pit::ticks()) * 1000;
    
    // Register with IPv4 layer
    ipv4::register_handler(ipv4::PROTO_TCP, handle_segment);
    
    serial::puts("[TCP] Stack initialized\n");
}

// ================================================================
// Socket API Implementation
// ================================================================

int tcp_socket()
{
    if (!ipv4::is_configured()) {
        return TCP_ERR_NETDOWN;
    }
    
    // Find free socket
    Socket* sock = nullptr;
    for (uint8_t i = 0; i < MAX_SOCKETS; ++i) {
        if (!s_sockets[i].active) {
            sock = &s_sockets[i];
            break;
        }
    }
    
    if (!sock) return TCP_ERR_NOSOCK;
    
    // Allocate TCB
    TCB* tcb = alloc_tcb();
    if (!tcb) {
        return TCP_ERR_NOBUFS;
    }
    
    memzero(sock, sizeof(Socket));
    sock->fd = s_nextFd++;
    sock->tcb = tcb;
    sock->active = true;
    sock->nonBlocking = true;
    
    serial::puts("[TCP] Created socket fd=");
    serial::put_hex32(sock->fd);
    serial::putc('\n');
    
    return sock->fd;
}

int tcp_bind(int sockfd, uint16_t port)
{
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    
    // Check if port is in use
    for (uint8_t i = 0; i < MAX_TCBS; ++i) {
        if (s_tcbs[i].active && s_tcbs[i].local.port == port) {
            return TCP_ERR_INUSE;
        }
    }
    
    tcb->local.addr = 0;  // Any address
    tcb->local.port = port;
    
    return TCP_OK;
}

int tcp_listen(int sockfd, int backlog)
{
    (void)backlog;  // Simplified: fixed backlog
    
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    
    if (tcb->local.port == 0) {
        return TCP_ERR_NOTBOUND;
    }
    
    tcb->state = STATE_LISTEN;
    sock->listening = true;
    
    serial::puts("[TCP] Listening on port ");
    serial::put_hex32(tcb->local.port);
    serial::putc('\n');
    
    return TCP_OK;
}

int tcp_accept(int sockfd, Endpoint* clientAddr)
{
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->listening) return TCP_ERR_INVALID;
    
    // Check for pending connections
    if (sock->pendingCount == 0) {
        return TCP_ERR_WOULDBLOCK;
    }
    
    // Get first pending connection
    TCB* clientTcb = sock->pendingConnections[0];
    
    // Shift remaining pending connections
    for (uint8_t i = 0; i < sock->pendingCount - 1; ++i) {
        sock->pendingConnections[i] = sock->pendingConnections[i + 1];
    }
    sock->pendingCount--;
    
    // Create new socket for accepted connection
    Socket* newSock = nullptr;
    for (uint8_t i = 0; i < MAX_SOCKETS; ++i) {
        if (!s_sockets[i].active) {
            newSock = &s_sockets[i];
            break;
        }
    }
    
    if (!newSock) {
        // No socket available, reset connection
        send_rst(clientTcb->local.addr, clientTcb->local.port,
                 clientTcb->remote.addr, clientTcb->remote.port,
                 clientTcb->snd_nxt, clientTcb->rcv_nxt);
        free_tcb(clientTcb);
        return TCP_ERR_NOSOCK;
    }
    
    memzero(newSock, sizeof(Socket));
    newSock->fd = s_nextFd++;
    newSock->tcb = clientTcb;
    newSock->active = true;
    newSock->nonBlocking = true;
    
    if (clientAddr) {
        clientAddr->addr = clientTcb->remote.addr;
        clientAddr->port = clientTcb->remote.port;
    }
    
    s_stats.connectionsOpened++;
    s_stats.activeConnections++;
    
    serial::puts("[TCP] Accepted connection on fd=");
    serial::put_hex32(newSock->fd);
    serial::putc('\n');
    
    return newSock->fd;
}

int tcp_connect(int sockfd, uint32_t destIP, uint16_t destPort)
{
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    
    if (tcb->state != STATE_CLOSED) {
        if (tcb->state == STATE_ESTABLISHED) return TCP_ERR_ISCONN;
        return TCP_ERR_ALREADY;
    }
    
    // Assign ephemeral port if not bound
    if (tcb->local.port == 0) {
        static uint16_t ephemeral = 49152;
        tcb->local.port = ephemeral++;
        if (ephemeral > 65535) ephemeral = 49152;
    }
    
    // Get our IP
    const ipv4::NetworkConfig* cfg = ipv4::get_config();
    if (!cfg) return TCP_ERR_NETDOWN;
    
    tcb->local.addr = cfg->ipAddr;
    tcb->remote.addr = destIP;
    tcb->remote.port = destPort;
    
    // Initialize sequence numbers
    tcb->iss = generate_isn();
    tcb->snd_una = tcb->iss;
    tcb->snd_nxt = tcb->iss;
    tcb->snd_wnd = WINDOW_DEFAULT;
    
    // Send SYN
    tcb->state = STATE_SYN_SENT;
    Status status = send_segment(tcb, FLAG_SYN, nullptr, 0);
    
    if (status != TCP_OK) {
        tcb->state = STATE_CLOSED;
        return status;
    }
    
    serial::puts("[TCP] Connecting to ");
    char ipStr[16];
    ipv4::ip_to_string(destIP, ipStr);
    serial::puts(ipStr);
    serial::puts(":");
    serial::put_hex32(destPort);
    serial::putc('\n');
    
    // Non-blocking: return immediately
    // Caller should poll tcp_isconnected()
    return TCP_OK;
}

int tcp_send(int sockfd, const void* buf, uint16_t len)
{
    if (!buf || len == 0) return TCP_ERR_INVALID;
    
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    
    if (tcb->state != STATE_ESTABLISHED &&
        tcb->state != STATE_CLOSE_WAIT) {
        return TCP_ERR_NOTCONN;
    }
    
    // Copy to transmit buffer
    uint16_t space = TX_BUFFER_SIZE - tcb->txLen;
    if (space == 0) return TCP_ERR_WOULDBLOCK;
    
    uint16_t toSend = (len < space) ? len : space;
    const uint8_t* data = static_cast<const uint8_t*>(buf);
    
    for (uint16_t i = 0; i < toSend; ++i) {
        tcb->txBuffer[tcb->txHead] = data[i];
        tcb->txHead = (tcb->txHead + 1) % TX_BUFFER_SIZE;
    }
    tcb->txLen += toSend;
    
    // Try to send immediately
    if (tcb->txLen > 0) {
        // Get data from buffer
        uint16_t segLen = (tcb->txLen < MAX_SEGMENT_DATA) ? tcb->txLen : MAX_SEGMENT_DATA;
        uint8_t segData[MAX_SEGMENT_DATA];
        
        uint16_t pos = tcb->txTail;
        for (uint16_t i = 0; i < segLen; ++i) {
            segData[i] = tcb->txBuffer[pos];
            pos = (pos + 1) % TX_BUFFER_SIZE;
        }
        
        Status status = send_segment(tcb, FLAG_ACK | FLAG_PSH, segData, segLen);
        
        if (status == TCP_OK) {
            tcb->txTail = pos;
            tcb->txLen -= segLen;
        }
    }
    
    return static_cast<int>(toSend);
}

int tcp_recv(int sockfd, void* buf, uint16_t maxLen)
{
    if (!buf || maxLen == 0) return TCP_ERR_INVALID;
    
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    
    // Check if we have data
    if (tcb->rxLen == 0) {
        // Check connection state
        if (tcb->state == STATE_CLOSE_WAIT ||
            tcb->state == STATE_CLOSED ||
            tcb->finReceived) {
            return 0;  // EOF
        }
        return TCP_ERR_WOULDBLOCK;
    }
    
    // Copy from receive buffer
    uint16_t toRecv = (tcb->rxLen < maxLen) ? tcb->rxLen : maxLen;
    uint8_t* data = static_cast<uint8_t*>(buf);
    
    for (uint16_t i = 0; i < toRecv; ++i) {
        data[i] = tcb->rxBuffer[tcb->rxTail];
        tcb->rxTail = (tcb->rxTail + 1) % RX_BUFFER_SIZE;
    }
    tcb->rxLen -= toRecv;
    
    // Update receive window
    tcb->rcv_wnd = RX_BUFFER_SIZE - tcb->rxLen;
    
    return static_cast<int>(toRecv);
}

int tcp_close(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    
    if (tcb) {
        switch (tcb->state) {
            case STATE_CLOSED:
            case STATE_LISTEN:
            case STATE_SYN_SENT:
                // Just close
                free_tcb(tcb);
                break;
                
            case STATE_SYN_RCVD:
            case STATE_ESTABLISHED:
                // Send FIN
                tcb->state = STATE_FIN_WAIT_1;
                send_segment(tcb, FLAG_FIN | FLAG_ACK, nullptr, 0);
                break;
                
            case STATE_CLOSE_WAIT:
                // Send FIN
                tcb->state = STATE_LAST_ACK;
                send_segment(tcb, FLAG_FIN | FLAG_ACK, nullptr, 0);
                break;
                
            default:
                // Already closing
                break;
        }
    }
    
    // Mark socket as inactive (TCB will be freed when connection closes)
    sock->active = false;
    sock->tcb = nullptr;
    
    s_stats.connectionsClosed++;
    if (s_stats.activeConnections > 0) s_stats.activeConnections--;
    
    serial::puts("[TCP] Closed socket fd=");
    serial::put_hex32(sockfd);
    serial::putc('\n');
    
    return TCP_OK;
}

int tcp_abort(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    
    if (tcb && tcb->state != STATE_CLOSED && tcb->state != STATE_LISTEN) {
        // Send RST
        send_rst(tcb->local.addr, tcb->local.port,
                 tcb->remote.addr, tcb->remote.port,
                 tcb->snd_nxt, 0);
        free_tcb(tcb);
        s_stats.connectionsReset++;
    }
    
    sock->active = false;
    sock->tcb = nullptr;
    
    if (s_stats.activeConnections > 0) s_stats.activeConnections--;
    
    return TCP_OK;
}

// ================================================================
// Socket Options
// ================================================================

int tcp_setnonblocking(int sockfd, bool enabled)
{
    Socket* sock = find_socket(sockfd);
    if (!sock) return TCP_ERR_INVALID;
    
    sock->nonBlocking = enabled;
    return TCP_OK;
}

bool tcp_isconnected(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return false;
    
    return sock->tcb->state == STATE_ESTABLISHED;
}

bool tcp_readable(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return false;
    
    return sock->tcb->rxLen > 0;
}

bool tcp_writable(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return false;
    
    TCB* tcb = sock->tcb;
    return (tcb->state == STATE_ESTABLISHED ||
            tcb->state == STATE_CLOSE_WAIT) &&
           tcb->txLen < TX_BUFFER_SIZE;
}

State tcp_getstate(int sockfd)
{
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return STATE_CLOSED;
    
    return sock->tcb->state;
}

int tcp_getpeername(int sockfd, Endpoint* addr)
{
    if (!addr) return TCP_ERR_INVALID;
    
    Socket* sock = find_socket(sockfd);
    if (!sock || !sock->tcb) return TCP_ERR_INVALID;
    
    TCB* tcb = sock->tcb;
    addr->addr = tcb->remote.addr;
    addr->port = tcb->remote.port;
    
    return TCP_OK;
}

// ================================================================
// Segment Processing
// ================================================================

static void process_segment(TCB* tcb, const ParsedSegment* seg)
{
    if (!tcb || !seg) return;
    
    // Handle RST
    if (seg->flags & FLAG_RST) {
        serial::puts("[TCP] RST received\n");
        tcb->state = STATE_CLOSED;
        s_stats.connectionsReset++;
        return;
    }
    
    switch (tcb->state) {
        case STATE_CLOSED:
            // Should not happen - RST should be sent
            break;
            
        case STATE_LISTEN:
            // Should not reach here - handled in handle_segment
            break;
            
        case STATE_SYN_SENT:
            // Expecting SYN+ACK
            if ((seg->flags & (FLAG_SYN | FLAG_ACK)) == (FLAG_SYN | FLAG_ACK)) {
                // Verify ACK
                if (seg->ackNum == tcb->snd_nxt) {
                    tcb->rcv_nxt = seg->seqNum + 1;
                    tcb->irs = seg->seqNum;
                    tcb->snd_una = seg->ackNum;
                    tcb->snd_wnd = seg->window;
                    
                    // Send ACK
                    tcb->state = STATE_ESTABLISHED;
                    send_segment(tcb, FLAG_ACK, nullptr, 0);
                    
                    s_stats.connectionsOpened++;
                    s_stats.activeConnections++;
                    
                    serial::puts("[TCP] Connection established\n");
                }
            }
            break;
            
        case STATE_SYN_RCVD:
            // Expecting ACK
            if (seg->flags & FLAG_ACK) {
                if (seg->ackNum == tcb->snd_nxt) {
                    tcb->state = STATE_ESTABLISHED;
                    tcb->snd_una = seg->ackNum;
                    serial::puts("[TCP] Connection established (server)\n");
                }
            }
            break;
            
        case STATE_ESTABLISHED:
            // Process ACK
            if (seg->flags & FLAG_ACK) {
                if (seg->ackNum > tcb->snd_una && seg->ackNum <= tcb->snd_nxt) {
                    tcb->snd_una = seg->ackNum;
                    tcb->retxCount = 0;
                }
                tcb->snd_wnd = seg->window;
            }
            
            // Process data
            if (seg->dataLen > 0) {
                if (seg->seqNum == tcb->rcv_nxt) {
                    // In-order data
                    uint16_t space = RX_BUFFER_SIZE - tcb->rxLen;
                    uint16_t toRecv = (seg->dataLen < space) ? seg->dataLen : space;
                    
                    for (uint16_t i = 0; i < toRecv; ++i) {
                        tcb->rxBuffer[tcb->rxHead] = seg->data[i];
                        tcb->rxHead = (tcb->rxHead + 1) % RX_BUFFER_SIZE;
                    }
                    tcb->rxLen += toRecv;
                    tcb->rcv_nxt += toRecv;
                    tcb->rcv_wnd = RX_BUFFER_SIZE - tcb->rxLen;
                    tcb->needAck = true;
                }
            }
            
            // Process FIN
            if (seg->flags & FLAG_FIN) {
                tcb->rcv_nxt++;
                tcb->finReceived = true;
                tcb->state = STATE_CLOSE_WAIT;
                send_segment(tcb, FLAG_ACK, nullptr, 0);
                serial::puts("[TCP] FIN received, entering CLOSE_WAIT\n");
            }
            
            // Send ACK if needed
            if (tcb->needAck) {
                send_segment(tcb, FLAG_ACK, nullptr, 0);
                tcb->needAck = false;
            }
            break;
            
        case STATE_FIN_WAIT_1:
            // Process ACK of our FIN
            if (seg->flags & FLAG_ACK) {
                if (seg->ackNum == tcb->snd_nxt) {
                    tcb->state = STATE_FIN_WAIT_2;
                }
            }
            
            // Process FIN
            if (seg->flags & FLAG_FIN) {
                tcb->rcv_nxt++;
                tcb->finReceived = true;
                if (tcb->state == STATE_FIN_WAIT_2) {
                    tcb->state = STATE_TIME_WAIT;
                } else {
                    tcb->state = STATE_CLOSING;
                }
                send_segment(tcb, FLAG_ACK, nullptr, 0);
            }
            break;
            
        case STATE_FIN_WAIT_2:
            // Waiting for FIN
            if (seg->flags & FLAG_FIN) {
                tcb->rcv_nxt++;
                tcb->finReceived = true;
                tcb->state = STATE_TIME_WAIT;
                send_segment(tcb, FLAG_ACK, nullptr, 0);
                serial::puts("[TCP] Entering TIME_WAIT\n");
            }
            break;
            
        case STATE_CLOSE_WAIT:
            // Waiting for application to close
            break;
            
        case STATE_CLOSING:
            // Waiting for ACK of FIN
            if (seg->flags & FLAG_ACK) {
                if (seg->ackNum == tcb->snd_nxt) {
                    tcb->state = STATE_TIME_WAIT;
                }
            }
            break;
            
        case STATE_LAST_ACK:
            // Waiting for ACK of our FIN
            if (seg->flags & FLAG_ACK) {
                if (seg->ackNum == tcb->snd_nxt) {
                    tcb->state = STATE_CLOSED;
                    free_tcb(tcb);
                }
            }
            break;
            
        case STATE_TIME_WAIT:
            // Wait for 2*MSL then close
            // Simplified: just acknowledge and stay
            if (seg->flags & FLAG_FIN) {
                send_segment(tcb, FLAG_ACK, nullptr, 0);
            }
            break;
    }
}

// ================================================================
// Handle incoming segment from IPv4
// ================================================================

void handle_segment(const ipv4::ParsedPacket* ipPacket)
{
    if (!ipPacket || !ipPacket->payload || ipPacket->payloadLen < HEADER_LEN_MIN) {
        s_stats.checksumErrors++;
        return;
    }
    
    // Verify checksum
    if (!verify_checksum(ipPacket->srcAddr, ipPacket->dstAddr,
                         ipPacket->payload, ipPacket->payloadLen)) {
        s_stats.checksumErrors++;
        return;
    }
    
    // Parse segment
    const Header* hdr = reinterpret_cast<const Header*>(ipPacket->payload);
    
    ParsedSegment seg;
    seg.srcPort = ethernet::ntohs(hdr->srcPort);
    seg.dstPort = ethernet::ntohs(hdr->dstPort);
    seg.seqNum = ethernet::ntohl(hdr->seqNum);
    seg.ackNum = ethernet::ntohl(hdr->ackNum);
    seg.headerLen = get_header_len(hdr);
    seg.flags = hdr->flags;
    seg.window = ethernet::ntohs(hdr->window);
    seg.checksum = ethernet::ntohs(hdr->checksum);
    seg.srcIP = ipPacket->srcAddr;
    seg.dstIP = ipPacket->dstAddr;
    
    if (seg.headerLen > ipPacket->payloadLen) {
        return;
    }
    
    seg.data = ipPacket->payload + seg.headerLen;
    seg.dataLen = ipPacket->payloadLen - seg.headerLen;
    seg.isValid = true;
    seg.checksumValid = true;
    
    s_stats.segmentsReceived++;
    
    // Find matching TCB
    Endpoint local = { ipPacket->dstAddr, seg.dstPort };
    Endpoint remote = { seg.srcIP, seg.srcPort };
    
    TCB* tcb = find_tcb(&local, &remote);
    
    if (tcb) {
        process_segment(tcb, &seg);
        return;
    }
    
    // Check for listening socket
    TCB* listenTcb = find_listening_tcb(seg.dstPort);
    
    if (listenTcb && (seg.flags & FLAG_SYN) && !(seg.flags & FLAG_ACK)) {
        // New connection request
        Socket* listenSock = find_socket_for_tcb(listenTcb);
        
        if (listenSock && listenSock->pendingCount < Socket::BACKLOG_MAX) {
            // Create new TCB for this connection
            TCB* newTcb = alloc_tcb();
            
            if (newTcb) {
                const ipv4::NetworkConfig* cfg = ipv4::get_config();
                
                newTcb->local.addr = cfg ? cfg->ipAddr : 0;
                newTcb->local.port = seg.dstPort;
                newTcb->remote.addr = seg.srcIP;
                newTcb->remote.port = seg.srcPort;
                
                newTcb->irs = seg.seqNum;
                newTcb->rcv_nxt = seg.seqNum + 1;
                newTcb->iss = generate_isn();
                newTcb->snd_nxt = newTcb->iss;
                newTcb->snd_una = newTcb->iss;
                newTcb->snd_wnd = seg.window;
                
                newTcb->state = STATE_SYN_RCVD;
                
                // Send SYN+ACK
                send_segment(newTcb, FLAG_SYN | FLAG_ACK, nullptr, 0);
                
                // Add to pending queue
                listenSock->pendingConnections[listenSock->pendingCount++] = newTcb;
                
                serial::puts("[TCP] SYN received, sent SYN+ACK\n");
                return;
            }
        }
    }
    
    // No matching socket - send RST
    if (!(seg.flags & FLAG_RST)) {
        const ipv4::NetworkConfig* cfg = ipv4::get_config();
        if (cfg) {
            uint32_t rstSeq = (seg.flags & FLAG_ACK) ? seg.ackNum : 0;
            uint32_t rstAck = seg.seqNum + seg.dataLen;
            if (seg.flags & FLAG_SYN) rstAck++;
            if (seg.flags & FLAG_FIN) rstAck++;
            
            send_rst(cfg->ipAddr, seg.dstPort,
                     seg.srcIP, seg.srcPort,
                     rstSeq, rstAck);
        }
    }
}

// ================================================================
// Timer Processing
// ================================================================

void process_timers()
{
    uint32_t now = static_cast<uint32_t>(pit::ticks());
    
    for (uint8_t i = 0; i < MAX_TCBS; ++i) {
        TCB* tcb = &s_tcbs[i];
        if (!tcb->active) continue;
        
        // Check retransmission timeout
        if (tcb->state == STATE_SYN_SENT ||
            tcb->state == STATE_SYN_RCVD ||
            tcb->state == STATE_FIN_WAIT_1 ||
            tcb->state == STATE_CLOSING ||
            tcb->state == STATE_LAST_ACK) {
            
            uint32_t elapsed = (now - tcb->retxTime) * 10;  // Approximate ms
            
            if (elapsed > TIMEOUT_RETX) {
                if (tcb->retxCount < MAX_RETRIES) {
                    tcb->retxCount++;
                    s_stats.retransmissions++;
                    
                    // Retransmit
                    switch (tcb->state) {
                        case STATE_SYN_SENT:
                            send_segment(tcb, FLAG_SYN, nullptr, 0);
                            break;
                        case STATE_SYN_RCVD:
                            send_segment(tcb, FLAG_SYN | FLAG_ACK, nullptr, 0);
                            break;
                        case STATE_FIN_WAIT_1:
                        case STATE_CLOSING:
                        case STATE_LAST_ACK:
                            send_segment(tcb, FLAG_FIN | FLAG_ACK, nullptr, 0);
                            break;
                        default:
                            break;
                    }
                } else {
                    // Give up
                    tcb->state = STATE_CLOSED;
                    free_tcb(tcb);
                    s_stats.connectionsReset++;
                }
            }
        }
        
        // TIME_WAIT timeout
        if (tcb->state == STATE_TIME_WAIT) {
            uint32_t elapsed = (now - tcb->retxTime) * 10;
            if (elapsed > TIMEOUT_TIMEWAIT) {
                tcb->state = STATE_CLOSED;
                free_tcb(tcb);
            }
        }
    }
}

// ================================================================
// Statistics
// ================================================================

const Statistics* get_stats()
{
    return &s_stats;
}

// ================================================================
// Utility
// ================================================================

const char* state_name(State state)
{
    switch (state) {
        case STATE_CLOSED:      return "CLOSED";
        case STATE_LISTEN:      return "LISTEN";
        case STATE_SYN_SENT:    return "SYN_SENT";
        case STATE_SYN_RCVD:    return "SYN_RCVD";
        case STATE_ESTABLISHED: return "ESTABLISHED";
        case STATE_FIN_WAIT_1:  return "FIN_WAIT_1";
        case STATE_FIN_WAIT_2:  return "FIN_WAIT_2";
        case STATE_CLOSE_WAIT:  return "CLOSE_WAIT";
        case STATE_CLOSING:     return "CLOSING";
        case STATE_LAST_ACK:    return "LAST_ACK";
        case STATE_TIME_WAIT:   return "TIME_WAIT";
        default:                return "UNKNOWN";
    }
}

} // namespace tcp
} // namespace kernel
