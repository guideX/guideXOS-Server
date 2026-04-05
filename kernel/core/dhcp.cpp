// DHCP Client Implementation for guideXOS
//
// Implements RFC 2131 DHCP state machine:
//   INIT -> SELECTING -> REQUESTING -> BOUND
// with lease renewal, release, and NAK retry support.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/dhcp.h"
#include "include/kernel/udp.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/nic.h"
#include "include/kernel/socket.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace dhcp {

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
// Byte order helpers (reuse ethernet layer)
// ================================================================

static inline uint16_t dhcp_htons(uint16_t val)
{
    return ethernet::htons(val);
}

static inline uint16_t dhcp_ntohs(uint16_t val)
{
    return ethernet::ntohs(val);
}

static inline uint32_t dhcp_htonl(uint32_t val)
{
    return ethernet::htonl(val);
}

static inline uint32_t dhcp_ntohl(uint32_t val)
{
    return ethernet::ntohl(val);
}

// ================================================================
// Simple pseudo-random XID generator
// ================================================================

static uint32_t s_randState = 0x12345678;

static uint32_t generate_xid()
{
    // xorshift32
    s_randState ^= s_randState << 13;
    s_randState ^= s_randState >> 17;
    s_randState ^= s_randState << 5;
    return s_randState;
}

// ================================================================
// Internal state
// ================================================================

static LeaseInfo   s_lease;
static ClientState s_state = STATE_INIT;
static Statistics  s_stats;
static uint32_t    s_tickCounter = 0;  // Simple tick counter for lease timing

// Static buffers to avoid large stack allocations (prevents ___chkstk_ms)
static uint8_t s_txBuffer[sizeof(Packet)];
static uint8_t s_rxBuffer[sizeof(Packet)];

// ================================================================
// Initialization
// ================================================================

void init()
{
    memzero(&s_lease, sizeof(s_lease));
    memzero(&s_stats, sizeof(s_stats));
    s_state = STATE_INIT;
    s_tickCounter = 0;

    // Seed the RNG with something device-specific if available
    const uint8_t* mac = nic::get_mac_address();
    if (mac) {
        s_randState = (static_cast<uint32_t>(mac[2]) << 24) |
                      (static_cast<uint32_t>(mac[3]) << 16) |
                      (static_cast<uint32_t>(mac[4]) << 8)  |
                      static_cast<uint32_t>(mac[5]);
        if (s_randState == 0) s_randState = 0xDEADBEEF;
    }

    serial::puts("[DHCP] Client initialized\n");
}

// ================================================================
// Options writer helpers
// ================================================================

// Write magic cookie at the start of the options field
static uint16_t write_magic_cookie(uint8_t* opts)
{
    opts[0] = 0x63;  // DHCP_MAGIC_COOKIE bytes (network order)
    opts[1] = 0x82;
    opts[2] = 0x53;
    opts[3] = 0x63;
    return 4;
}

// Write a single-byte option (tag, len=1, value)
static uint16_t write_option_byte(uint8_t* opts, uint8_t tag, uint8_t value)
{
    opts[0] = tag;
    opts[1] = 1;
    opts[2] = value;
    return 3;
}

// Write a 4-byte IP address option (tag, len=4, ip in network order)
static uint16_t write_option_ip(uint8_t* opts, uint8_t tag, uint32_t ip_host)
{
    uint32_t ip_net = dhcp_htonl(ip_host);
    opts[0] = tag;
    opts[1] = 4;
    opts[2] = static_cast<uint8_t>((ip_net >> 0)  & 0xFF);
    opts[3] = static_cast<uint8_t>((ip_net >> 8)  & 0xFF);
    opts[4] = static_cast<uint8_t>((ip_net >> 16) & 0xFF);
    opts[5] = static_cast<uint8_t>((ip_net >> 24) & 0xFF);
    return 6;
}

// Write parameter request list option
static uint16_t write_param_request_list(uint8_t* opts)
{
    opts[0] = OPT_PARAM_REQUEST;
    opts[1] = 4;                    // Request 4 parameters
    opts[2] = OPT_SUBNET_MASK;
    opts[3] = OPT_ROUTER;
    opts[4] = OPT_DNS_SERVER;
    opts[5] = OPT_LEASE_TIME;
    return 6;
}

// Write END option
static uint16_t write_option_end(uint8_t* opts)
{
    opts[0] = OPT_END;
    return 1;
}

// ================================================================
// Packet Construction
// ================================================================

// Fill common DHCP packet header fields
static void fill_packet_header(Packet* pkt, const uint8_t* mac, uint32_t xid)
{
    memzero(pkt, sizeof(Packet));
    pkt->op    = BOOTREQUEST;
    pkt->htype = HTYPE_ETHERNET;
    pkt->hlen  = HLEN_ETHERNET;
    pkt->hops  = 0;
    pkt->xid   = dhcp_htonl(xid);
    pkt->secs  = 0;
    pkt->flags = dhcp_htons(FLAG_BROADCAST);

    // Copy MAC address into chaddr (first 6 bytes, rest zeroed by memzero)
    if (mac) {
        memcopy(pkt->chaddr, mac, 6);
    }
}

Status build_discover(uint8_t* buffer, uint16_t bufferSize,
                      const uint8_t* mac, uint32_t xid,
                      uint16_t* packetLen)
{
    if (!buffer || !mac || !packetLen) return DHCP_ERR_INVALID;
    if (bufferSize < sizeof(Packet)) return DHCP_ERR_INVALID;

    Packet* pkt = reinterpret_cast<Packet*>(buffer);
    fill_packet_header(pkt, mac, xid);

    // Build options
    uint16_t optOff = 0;
    optOff += write_magic_cookie(pkt->options + optOff);
    optOff += write_option_byte(pkt->options + optOff, OPT_MSG_TYPE, DHCPDISCOVER);
    optOff += write_param_request_list(pkt->options + optOff);
    optOff += write_option_end(pkt->options + optOff);

    *packetLen = sizeof(Packet) - DHCP_OPTIONS_MAX + optOff;
    // Ensure minimum size
    if (*packetLen < DHCP_PACKET_MIN_SIZE) *packetLen = DHCP_PACKET_MIN_SIZE;

    return DHCP_OK;
}

Status build_request(uint8_t* buffer, uint16_t bufferSize,
                     const uint8_t* mac, uint32_t xid,
                     uint32_t offeredIP, uint32_t serverIP,
                     uint16_t* packetLen)
{
    if (!buffer || !mac || !packetLen) return DHCP_ERR_INVALID;
    if (bufferSize < sizeof(Packet)) return DHCP_ERR_INVALID;

    Packet* pkt = reinterpret_cast<Packet*>(buffer);
    fill_packet_header(pkt, mac, xid);

    // Build options
    uint16_t optOff = 0;
    optOff += write_magic_cookie(pkt->options + optOff);
    optOff += write_option_byte(pkt->options + optOff, OPT_MSG_TYPE, DHCPREQUEST);
    optOff += write_option_ip(pkt->options + optOff, OPT_REQUESTED_IP, offeredIP);
    optOff += write_option_ip(pkt->options + optOff, OPT_SERVER_ID, serverIP);
    optOff += write_param_request_list(pkt->options + optOff);
    optOff += write_option_end(pkt->options + optOff);

    *packetLen = sizeof(Packet) - DHCP_OPTIONS_MAX + optOff;
    if (*packetLen < DHCP_PACKET_MIN_SIZE) *packetLen = DHCP_PACKET_MIN_SIZE;

    return DHCP_OK;
}

Status build_release(uint8_t* buffer, uint16_t bufferSize,
                     const uint8_t* mac, uint32_t xid,
                     uint32_t clientIP, uint32_t serverIP,
                     uint16_t* packetLen)
{
    if (!buffer || !mac || !packetLen) return DHCP_ERR_INVALID;
    if (bufferSize < sizeof(Packet)) return DHCP_ERR_INVALID;

    Packet* pkt = reinterpret_cast<Packet*>(buffer);
    fill_packet_header(pkt, mac, xid);

    // RELEASE: set ciaddr to our assigned IP, clear broadcast flag
    pkt->ciaddr = dhcp_htonl(clientIP);
    pkt->flags  = 0;

    // Build options
    uint16_t optOff = 0;
    optOff += write_magic_cookie(pkt->options + optOff);
    optOff += write_option_byte(pkt->options + optOff, OPT_MSG_TYPE, DHCPRELEASE);
    optOff += write_option_ip(pkt->options + optOff, OPT_SERVER_ID, serverIP);
    optOff += write_option_end(pkt->options + optOff);

    *packetLen = sizeof(Packet) - DHCP_OPTIONS_MAX + optOff;
    if (*packetLen < DHCP_PACKET_MIN_SIZE) *packetLen = DHCP_PACKET_MIN_SIZE;

    return DHCP_OK;
}

// ================================================================
// Options Parsing
// ================================================================

Status parse_options(const Packet* pkt, ParsedOptions* opts)
{
    if (!pkt || !opts) return DHCP_ERR_INVALID;

    memzero(opts, sizeof(ParsedOptions));

    const uint8_t* data = pkt->options;

    // Verify magic cookie
    if (data[0] != 0x63 || data[1] != 0x82 ||
        data[2] != 0x53 || data[3] != 0x63) {
        return DHCP_ERR_PARSE;
    }

    uint16_t pos = 4;  // Skip magic cookie

    while (pos < DHCP_OPTIONS_MAX) {
        uint8_t tag = data[pos++];

        if (tag == OPT_END) break;
        if (tag == OPT_PAD) continue;

        // Read option length
        if (pos >= DHCP_OPTIONS_MAX) break;
        uint8_t len = data[pos++];
        if (pos + len > DHCP_OPTIONS_MAX) break;

        switch (tag) {
        case OPT_MSG_TYPE:
            if (len >= 1) opts->messageType = data[pos];
            break;

        case OPT_SUBNET_MASK:
            if (len >= 4) {
                opts->subnetMask = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
                opts->hasSubnetMask = true;
            }
            break;

        case OPT_ROUTER:
            if (len >= 4) {
                opts->router = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
                opts->hasRouter = true;
            }
            break;

        case OPT_DNS_SERVER:
            if (len >= 4) {
                opts->dnsServer = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
                opts->hasDNS = true;
            }
            break;

        case OPT_SERVER_ID:
            if (len >= 4) {
                opts->serverID = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
                opts->hasServerID = true;
            }
            break;

        case OPT_LEASE_TIME:
            if (len >= 4) {
                opts->leaseTime = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
                opts->hasLeaseTime = true;
            }
            break;

        case OPT_RENEWAL_TIME:
            if (len >= 4) {
                opts->renewalTime = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
                opts->hasRenewalTime = true;
            }
            break;

        case OPT_REBINDING_TIME:
            if (len >= 4) {
                opts->rebindingTime = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
                opts->hasRebindingTime = true;
            }
            break;

        case OPT_BROADCAST_ADDR:
            if (len >= 4) {
                opts->broadcastAddr = dhcp_ntohl(
                    static_cast<uint32_t>(data[pos])         |
                    (static_cast<uint32_t>(data[pos+1]) << 8)  |
                    (static_cast<uint32_t>(data[pos+2]) << 16) |
                    (static_cast<uint32_t>(data[pos+3]) << 24));
            }
            break;

        default:
            // Skip unknown options
            break;
        }

        pos += len;
    }

    return DHCP_OK;
}

// ================================================================
// DHCP Send / Receive using existing UDP stack
// ================================================================

Status dhcp_send(uint8_t* packet, size_t length, uint32_t server_ip)
{
    if (!packet || length == 0) return DHCP_ERR_INVALID;

    // Use the UDP layer to send from DHCP client port (68) to server port (67)
    udp::Status udpSt = udp::send(DHCP_CLIENT_PORT, server_ip,
                                   DHCP_SERVER_PORT,
                                   packet,
                                   static_cast<uint16_t>(length));
    if (udpSt != udp::UDP_OK) {
        serial::puts("[DHCP] UDP send failed\n");
        s_stats.errors++;
        return DHCP_ERR_SEND_FAIL;
    }

    return DHCP_OK;
}

Status dhcp_receive(uint8_t* buffer, size_t max_len, uint32_t* server_ip)
{
    if (!buffer || max_len == 0) return DHCP_ERR_INVALID;

    // Create a temporary socket bound to the DHCP client port
    int sock = socket::udp_socket();
    if (sock == socket::INVALID_SOCKET) {
        serial::puts("[DHCP] Failed to create socket\n");
        return DHCP_ERR_NETWORK;
    }

    int bindResult = socket::udp_bind(sock, DHCP_CLIENT_PORT);
    if (bindResult != socket::SOCK_OK && bindResult != socket::SOCK_ERR_INUSE) {
        socket::udp_close(sock);
        serial::puts("[DHCP] Failed to bind to port 68\n");
        return DHCP_ERR_NETWORK;
    }

    socket::udp_setnonblocking(sock, true);

    // Poll for a response with timeout
    // Simple busy-wait with iteration count as timeout approximation
    static const uint32_t POLL_ITERATIONS = 500000;
    Status result = DHCP_ERR_TIMEOUT;

    socket::SockAddr srcAddr;
    for (uint32_t i = 0; i < POLL_ITERATIONS; ++i) {
        int received = socket::udp_recvfrom(sock, buffer,
                                            static_cast<uint16_t>(max_len),
                                            &srcAddr);
        if (received > 0) {
            if (server_ip) *server_ip = srcAddr.addr;
            result = DHCP_OK;
            break;
        }

        // Brief delay (architecture-dependent, simple spin)
        for (volatile uint32_t d = 0; d < 100; ++d) { }
    }

    socket::udp_close(sock);
    return result;
}

// ================================================================
// DHCP Discovery State Machine
// ================================================================

// Internal: Send DISCOVER and wait for OFFER
static Status do_discover(const uint8_t* mac, uint32_t xid,
                          Packet* offerPkt, ParsedOptions* offerOpts)
{
    uint16_t pktLen = 0;

    Status st = build_discover(s_txBuffer, sizeof(s_txBuffer), mac, xid, &pktLen);
    if (st != DHCP_OK) return st;

    serial::puts("[DHCP] Sending DISCOVER (xid=0x");
    serial::put_hex32(xid);
    serial::puts(")\n");

    s_state = STATE_SELECTING;

    for (uint8_t attempt = 0; attempt < MAX_DISCOVER_RETRIES; ++attempt) {
        st = dhcp_send(s_txBuffer, pktLen, ipv4::ADDR_BROADCAST);
        if (st != DHCP_OK) {
            s_stats.errors++;
            continue;
        }
        s_stats.discoversSent++;

        // Wait for OFFER
        uint32_t fromIP = 0;
        st = dhcp_receive(s_rxBuffer, sizeof(s_rxBuffer), &fromIP);
        if (st == DHCP_OK) {
            Packet* resp = reinterpret_cast<Packet*>(s_rxBuffer);

            // Validate: must be BOOTREPLY with our xid
            if (resp->op != BOOTREPLY) continue;
            if (dhcp_ntohl(resp->xid) != xid) continue;

            // Parse options
            ParsedOptions opts;
            if (parse_options(resp, &opts) != DHCP_OK) continue;
            if (opts.messageType != DHCPOFFER) continue;

            // Valid OFFER received
            memcopy(offerPkt, resp, sizeof(Packet));
            memcopy(offerOpts, &opts, sizeof(ParsedOptions));
            s_stats.offersReceived++;

            serial::puts("[DHCP] OFFER received, offered IP: ");
            char ipStr[16];
            ipv4::ip_to_string(dhcp_ntohl(resp->yiaddr), ipStr);
            serial::puts(ipStr);
            serial::putc('\n');

            return DHCP_OK;
        }

        s_stats.timeouts++;
        serial::puts("[DHCP] DISCOVER timeout, retrying...\n");
    }

    return DHCP_ERR_NO_OFFER;
}

// Internal: Send REQUEST and wait for ACK
static Status do_request(const uint8_t* mac, uint32_t xid,
                         uint32_t offeredIP, uint32_t serverIP,
                         Packet* ackPkt, ParsedOptions* ackOpts)
{
    uint16_t pktLen = 0;

    Status st = build_request(s_txBuffer, sizeof(s_txBuffer), mac, xid,
                              offeredIP, serverIP, &pktLen);
    if (st != DHCP_OK) return st;

    serial::puts("[DHCP] Sending REQUEST for IP: ");
    char ipStr[16];
    ipv4::ip_to_string(offeredIP, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    s_state = STATE_REQUESTING;

    for (uint8_t attempt = 0; attempt < MAX_REQUEST_RETRIES; ++attempt) {
        st = dhcp_send(s_txBuffer, pktLen, ipv4::ADDR_BROADCAST);
        if (st != DHCP_OK) {
            s_stats.errors++;
            continue;
        }
        s_stats.requestsSent++;

        // Wait for ACK or NAK
        uint32_t fromIP = 0;
        st = dhcp_receive(s_rxBuffer, sizeof(s_rxBuffer), &fromIP);
        if (st == DHCP_OK) {
            Packet* resp = reinterpret_cast<Packet*>(s_rxBuffer);

            // Validate
            if (resp->op != BOOTREPLY) continue;
            if (dhcp_ntohl(resp->xid) != xid) continue;

            ParsedOptions opts;
            if (parse_options(resp, &opts) != DHCP_OK) continue;

            if (opts.messageType == DHCPNAK) {
                s_stats.naksReceived++;
                serial::puts("[DHCP] NAK received, restarting discovery\n");
                return DHCP_ERR_NAK;
            }

            if (opts.messageType == DHCPACK) {
                memcopy(ackPkt, resp, sizeof(Packet));
                memcopy(ackOpts, &opts, sizeof(ParsedOptions));
                s_stats.acksReceived++;

                serial::puts("[DHCP] ACK received\n");
                return DHCP_OK;
            }
        }

        s_stats.timeouts++;
        serial::puts("[DHCP] REQUEST timeout, retrying...\n");
    }

    return DHCP_ERR_NO_ACK;
}

// Internal: Apply lease and configure the kernel networking state
static void apply_lease(const Packet* ackPkt, const ParsedOptions* opts,
                        uint32_t xid)
{
    uint32_t assignedIP = dhcp_ntohl(ackPkt->yiaddr);

    s_lease.assignedIP     = assignedIP;
    s_lease.subnetMask     = opts->hasSubnetMask ? opts->subnetMask : ipv4::MASK_24;
    s_lease.gateway        = opts->hasRouter     ? opts->router     : 0;
    s_lease.dnsServer      = opts->hasDNS        ? opts->dnsServer  : 0;
    s_lease.serverIP       = opts->hasServerID   ? opts->serverID   : 0;
    s_lease.leaseTime      = opts->hasLeaseTime  ? opts->leaseTime  : DEFAULT_LEASE_SECS;
    s_lease.renewalTime    = opts->hasRenewalTime
                             ? opts->renewalTime
                             : s_lease.leaseTime / 2;
    s_lease.rebindingTime  = opts->hasRebindingTime
                             ? opts->rebindingTime
                             : (s_lease.leaseTime * 7) / 8;
    s_lease.leaseStartTick = s_tickCounter;
    s_lease.xid            = xid;
    s_lease.valid          = true;

    // Configure IPv4 layer with the obtained parameters
    ipv4::configure(s_lease.assignedIP,
                    s_lease.subnetMask,
                    s_lease.gateway,
                    s_lease.dnsServer);

    s_state = STATE_BOUND;
}

// ================================================================
// Public: Full DHCP Discovery
// ================================================================

Status discover()
{
    if (!nic::is_active()) {
        serial::puts("[DHCP] No NIC available\n");
        return DHCP_ERR_NO_NIC;
    }

    const uint8_t* mac = nic::get_mac_address();
    if (!mac) return DHCP_ERR_NO_NIC;

    // Bind DHCP client port in UDP layer before starting
    udp::bind(DHCP_CLIENT_PORT, ipv4::ADDR_ANY, nullptr);

    uint32_t xid = generate_xid();

    // Phase 1: DISCOVER -> OFFER
    Packet offerPkt;
    ParsedOptions offerOpts;
    Status st = do_discover(mac, xid, &offerPkt, &offerOpts);
    if (st != DHCP_OK) {
        s_state = STATE_ERROR;
        serial::puts("[DHCP] Discovery failed\n");
        udp::unbind(DHCP_CLIENT_PORT);
        return st;
    }

    // Phase 2: REQUEST -> ACK (with NAK retry)
    uint32_t offeredIP = dhcp_ntohl(offerPkt.yiaddr);
    uint32_t serverIP  = offerOpts.hasServerID
                         ? offerOpts.serverID
                         : dhcp_ntohl(offerPkt.siaddr);

    for (uint8_t retry = 0; retry < MAX_DISCOVER_RETRIES; ++retry) {
        Packet ackPkt;
        ParsedOptions ackOpts;
        st = do_request(mac, xid, offeredIP, serverIP, &ackPkt, &ackOpts);

        if (st == DHCP_OK) {
            // Apply configuration
            apply_lease(&ackPkt, &ackOpts, xid);
            dhcp_print_info();
            udp::unbind(DHCP_CLIENT_PORT);
            return DHCP_OK;
        }

        if (st == DHCP_ERR_NAK) {
            // NAK received — restart from DISCOVER
            xid = generate_xid();
            st = do_discover(mac, xid, &offerPkt, &offerOpts);
            if (st != DHCP_OK) break;
            offeredIP = dhcp_ntohl(offerPkt.yiaddr);
            serverIP  = offerOpts.hasServerID
                        ? offerOpts.serverID
                        : dhcp_ntohl(offerPkt.siaddr);
            continue;
        }

        // Timeout or other error
        break;
    }

    s_state = STATE_ERROR;
    serial::puts("[DHCP] Configuration failed\n");
    udp::unbind(DHCP_CLIENT_PORT);
    return st;
}

// ================================================================
// Lease Management
// ================================================================

const LeaseInfo* get_lease()
{
    return &s_lease;
}

ClientState get_state()
{
    return s_state;
}

void check_renewal()
{
    if (s_state != STATE_BOUND && s_state != STATE_RENEWING) return;
    if (!s_lease.valid) return;

    uint32_t elapsed = s_tickCounter - s_lease.leaseStartTick;

    // Check if lease has expired
    if (elapsed >= s_lease.leaseTime) {
        serial::puts("[DHCP] Lease expired, restarting discovery\n");
        s_lease.valid = false;
        s_state = STATE_INIT;
        discover();
        return;
    }

    // Check T2 (rebinding)
    if (elapsed >= s_lease.rebindingTime && s_state != STATE_REBINDING) {
        serial::puts("[DHCP] T2 expired, entering REBINDING state\n");
        s_state = STATE_REBINDING;
    }

    // Check T1 (renewal)
    if (elapsed >= s_lease.renewalTime &&
        (s_state == STATE_BOUND || s_state == STATE_RENEWING)) {

        serial::puts("[DHCP] T1 expired, attempting renewal\n");
        s_state = STATE_RENEWING;

        const uint8_t* mac = nic::get_mac_address();
        if (!mac) return;

        uint32_t xid = generate_xid();

        // Build a unicast REQUEST to renew
        uint16_t pktLen = 0;
        Status st = build_request(s_txBuffer, sizeof(s_txBuffer), mac, xid,
                                  s_lease.assignedIP, s_lease.serverIP,
                                  &pktLen);
        if (st != DHCP_OK) return;

        // Send to server directly
        udp::bind(DHCP_CLIENT_PORT, ipv4::ADDR_ANY, nullptr);
        st = dhcp_send(s_txBuffer, pktLen, s_lease.serverIP);
        if (st != DHCP_OK) {
            udp::unbind(DHCP_CLIENT_PORT);
            return;
        }
        s_stats.requestsSent++;

        // Wait for ACK
        uint32_t fromIP = 0;
        st = dhcp_receive(s_rxBuffer, sizeof(s_rxBuffer), &fromIP);
        udp::unbind(DHCP_CLIENT_PORT);

        if (st == DHCP_OK) {
            Packet* resp = reinterpret_cast<Packet*>(s_rxBuffer);
            if (resp->op == BOOTREPLY && dhcp_ntohl(resp->xid) == xid) {
                ParsedOptions opts;
                if (parse_options(resp, &opts) == DHCP_OK) {
                    if (opts.messageType == DHCPACK) {
                        apply_lease(resp, &opts, xid);
                        s_stats.renewals++;
                        serial::puts("[DHCP] Lease renewed\n");
                        return;
                    }
                    if (opts.messageType == DHCPNAK) {
                        s_stats.naksReceived++;
                        s_lease.valid = false;
                        s_state = STATE_INIT;
                        serial::puts("[DHCP] NAK on renewal, restarting\n");
                        discover();
                        return;
                    }
                }
            }
        }

        // Renewal failed — will retry on next check
        serial::puts("[DHCP] Renewal failed, will retry\n");
    }
}

// ================================================================
// Helper Functions
// ================================================================

Status dhcp_release()
{
    if (s_state != STATE_BOUND && s_state != STATE_RENEWING &&
        s_state != STATE_REBINDING) {
        return DHCP_ERR_NOT_BOUND;
    }

    if (!s_lease.valid) return DHCP_ERR_NOT_BOUND;

    const uint8_t* mac = nic::get_mac_address();
    if (!mac) return DHCP_ERR_NO_NIC;

    uint32_t xid = generate_xid();

    uint16_t pktLen = 0;
    Status st = build_release(s_txBuffer, sizeof(s_txBuffer), mac, xid,
                              s_lease.assignedIP, s_lease.serverIP,
                              &pktLen);
    if (st != DHCP_OK) return st;

    serial::puts("[DHCP] Sending RELEASE\n");

    udp::bind(DHCP_CLIENT_PORT, ipv4::ADDR_ANY, nullptr);
    st = dhcp_send(s_txBuffer, pktLen, s_lease.serverIP);
    udp::unbind(DHCP_CLIENT_PORT);

    if (st == DHCP_OK) {
        s_stats.releasesSent++;
    }

    // Invalidate lease regardless of send result
    s_lease.valid = false;
    s_state = STATE_RELEASED;

    serial::puts("[DHCP] Lease released\n");
    return DHCP_OK;
}

void dhcp_print_info()
{
    char ipStr[16];

    serial::puts("[DHCP] === DHCP Lease Information ===\n");

    if (!s_lease.valid) {
        serial::puts("[DHCP] No active lease\n");
        return;
    }

    serial::puts("[DHCP] IP Address  : ");
    ipv4::ip_to_string(s_lease.assignedIP, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    serial::puts("[DHCP] Subnet Mask : ");
    ipv4::ip_to_string(s_lease.subnetMask, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    serial::puts("[DHCP] Gateway     : ");
    ipv4::ip_to_string(s_lease.gateway, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    serial::puts("[DHCP] DNS Server  : ");
    ipv4::ip_to_string(s_lease.dnsServer, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    serial::puts("[DHCP] DHCP Server : ");
    ipv4::ip_to_string(s_lease.serverIP, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    serial::puts("[DHCP] Lease Time  : ");
    serial::put_hex32(s_lease.leaseTime);
    serial::puts(" sec\n");

    serial::puts("[DHCP] State       : ");
    serial::puts(state_to_string(s_state));
    serial::putc('\n');

    serial::puts("[DHCP] ================================\n");
}

void dhcp_debug()
{
    char ipStr[16];

    serial::puts("[DHCP-DBG] === Debug Info ===\n");

    serial::puts("[DHCP-DBG] State: ");
    serial::puts(state_to_string(s_state));
    serial::putc('\n');

    serial::puts("[DHCP-DBG] XID: 0x");
    serial::put_hex32(s_lease.xid);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Server IP: ");
    ipv4::ip_to_string(s_lease.serverIP, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Lease valid: ");
    serial::puts(s_lease.valid ? "yes" : "no");
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Lease start tick: 0x");
    serial::put_hex32(s_lease.leaseStartTick);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Renewal time: 0x");
    serial::put_hex32(s_lease.renewalTime);
    serial::puts(" sec\n");

    serial::puts("[DHCP-DBG] Rebinding time: 0x");
    serial::put_hex32(s_lease.rebindingTime);
    serial::puts(" sec\n");

    serial::puts("[DHCP-DBG] --- Statistics ---\n");

    serial::puts("[DHCP-DBG] Discovers sent: 0x");
    serial::put_hex32(s_stats.discoversSent);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Offers received: 0x");
    serial::put_hex32(s_stats.offersReceived);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Requests sent: 0x");
    serial::put_hex32(s_stats.requestsSent);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] ACKs received: 0x");
    serial::put_hex32(s_stats.acksReceived);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] NAKs received: 0x");
    serial::put_hex32(s_stats.naksReceived);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Releases sent: 0x");
    serial::put_hex32(s_stats.releasesSent);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Timeouts: 0x");
    serial::put_hex32(s_stats.timeouts);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Errors: 0x");
    serial::put_hex32(s_stats.errors);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] Renewals: 0x");
    serial::put_hex32(s_stats.renewals);
    serial::putc('\n');

    serial::puts("[DHCP-DBG] ========================\n");
}

// ================================================================
// String Helpers
// ================================================================

const char* msgtype_to_string(uint8_t type)
{
    switch (type) {
    case DHCPDISCOVER: return "DISCOVER";
    case DHCPOFFER:    return "OFFER";
    case DHCPREQUEST:  return "REQUEST";
    case DHCPDECLINE:  return "DECLINE";
    case DHCPACK:      return "ACK";
    case DHCPNAK:      return "NAK";
    case DHCPRELEASE:  return "RELEASE";
    case DHCPINFORM:   return "INFORM";
    default:           return "UNKNOWN";
    }
}

const char* state_to_string(ClientState state)
{
    switch (state) {
    case STATE_INIT:       return "INIT";
    case STATE_SELECTING:  return "SELECTING";
    case STATE_REQUESTING: return "REQUESTING";
    case STATE_BOUND:      return "BOUND";
    case STATE_RENEWING:   return "RENEWING";
    case STATE_REBINDING:  return "REBINDING";
    case STATE_RELEASED:   return "RELEASED";
    case STATE_ERROR:      return "ERROR";
    default:               return "UNKNOWN";
    }
}

// ================================================================
// Statistics
// ================================================================

const Statistics* get_stats()
{
    return &s_stats;
}

void reset_stats()
{
    memzero(&s_stats, sizeof(s_stats));
}

} // namespace dhcp
} // namespace kernel
