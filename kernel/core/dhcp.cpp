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
#include "include/kernel/dns.h"
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
static Diagnostics s_diagnostics;
static uint32_t    s_tickCounter = 0;  // Simple tick counter for lease timing

// Static buffers to avoid large stack allocations (prevents ___chkstk_ms)
static uint8_t s_txBuffer[sizeof(Packet)];
static uint8_t s_rxBuffer[sizeof(Packet)];
static uint8_t s_udpWireBuffer[1500];
static uint8_t s_ipWireBuffer[1500];
static uint8_t s_frameWireBuffer[ethernet::MAX_FRAME_LEN];
static uint8_t s_receiveFrame[ethernet::MAX_FRAME_LEN];

static void copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (!destination || capacity == 0) return;
    uint32_t i = 0;
    if (source) {
        while (source[i] != '\0' && i + 1u < capacity) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

static void set_failure(FailureStage stage, const char* reason)
{
    s_diagnostics.failureStage = stage;
    copy_text(s_diagnostics.failureReason,
              sizeof(s_diagnostics.failureReason), reason);
}

static void reset_invocation_diagnostics()
{
    const uint32_t commandInvocations = s_diagnostics.commandInvocations;
    memzero(&s_diagnostics, sizeof(s_diagnostics));
    s_diagnostics.commandInvocations = commandInvocations;
    s_diagnostics.linkRefreshResult = nic::LinkRefreshResult::NotAttempted;
    s_diagnostics.linkRefreshState = nic::NIC_LINK_UNKNOWN;
}

static uint8_t packet_message_type(const uint8_t* packet, size_t length)
{
    const uint16_t optionsOffset = sizeof(Packet) - DHCP_OPTIONS_MAX;
    if (!packet || length < static_cast<size_t>(optionsOffset + 7u)) return 0;
    const uint8_t* options = packet + optionsOffset;
    if (options[0] != 0x63 || options[1] != 0x82 ||
        options[2] != 0x53 || options[3] != 0x63 ||
        options[4] != OPT_MSG_TYPE || options[5] != 1u) {
        return 0;
    }
    return options[6];
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    memzero(&s_lease, sizeof(s_lease));
    memzero(&s_stats, sizeof(s_stats));
    memzero(&s_diagnostics, sizeof(s_diagnostics));
    s_diagnostics.linkRefreshResult = nic::LinkRefreshResult::NotAttempted;
    s_diagnostics.linkRefreshState = nic::NIC_LINK_UNKNOWN;
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

void note_command_invocation()
{
    s_diagnostics.commandInvocations++;
}

void clear_lease_for_manual_configuration()
{
    memzero(&s_lease, sizeof(s_lease));
    s_state = STATE_INIT;
}

void set_automatic_mode()
{
    clear_lease_for_manual_configuration();
    ipv4::select_dhcp_mode();
    dns::set_server(0);
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
    if (!packet || length == 0 || length > 0xFFFFu) return DHCP_ERR_INVALID;

    const uint8_t messageType = packet_message_type(packet, length);
    s_diagnostics.lastMessageType = messageType;
    s_diagnostics.sourcePort = DHCP_CLIENT_PORT;
    s_diagnostics.destinationPort = DHCP_SERVER_PORT;

    const nic::NICDevice* device = nic::get_device();
    if (!device || !nic::is_active()) {
        set_failure(FAILURE_INTERFACE, "no suitable active interface");
        return DHCP_ERR_NO_NIC;
    }

    const uint8_t* sourceMAC = nic::get_mac_address();
    if (!sourceMAC) {
        set_failure(FAILURE_INTERFACE, "interface has no station MAC");
        return DHCP_ERR_NO_NIC;
    }

    const ipv4::NetworkConfig* config = ipv4::get_config();
    const uint32_t sourceIP = config && config->configured
        ? config->ipAddr : ipv4::ADDR_ANY;

    // Initial DHCP DISCOVER/REQUEST traffic is a bootstrap packet. It must
    // not go through the configured-only UDP/IPv4 route, ARP, or gateway
    // lookup path. The same raw path is used for the broadcast RELEASE-style
    // calls that this client issues today; unicast renewal keeps the existing
    // configured UDP path below.
    if (server_ip != ipv4::ADDR_BROADCAST) {
        s_diagnostics.txSubmissionAttempted = true;
        const nic::TxDiagnostics before = device->tx;
        const udp::Status udpStatus = udp::send(
            DHCP_CLIENT_PORT, server_ip, DHCP_SERVER_PORT, packet,
            static_cast<uint16_t>(length));
        const nic::NICDevice* afterDevice = nic::get_device();
        const nic::TxEvidence evidence = afterDevice
            ? nic::observe_tx(before, afterDevice->tx,
                              udpStatus == udp::UDP_OK ? nic::NIC_OK
                                                       : nic::NIC_ERR_INIT_FAIL)
            : nic::TxEvidence{};
        s_diagnostics.txDescriptorPublished = evidence.descriptorPublished;
        s_diagnostics.txDescriptorAccepted = evidence.descriptorAccepted;
        s_diagnostics.txDoorbellObserved = evidence.doorbellObserved;
        s_diagnostics.txTailAdvanced = evidence.tailAdvanced;
        s_diagnostics.txCompletionObserved = evidence.completionObserved;
        s_diagnostics.txCompletionTimeout = evidence.completionTimedOut;
        s_diagnostics.txDriverError = evidence.driverError ||
                                      udpStatus != udp::UDP_OK;
        if (afterDevice) {
            s_diagnostics.txDescriptor = afterDevice->tx.lastDescriptor;
            s_diagnostics.txTailBefore = afterDevice->tx.tailBefore;
            s_diagnostics.txTailAfter = afterDevice->tx.tailAfter;
            s_diagnostics.txDescriptorSubmissions =
                afterDevice->tx.descriptorSubmissions;
            s_diagnostics.txDescriptorCompletions =
                afterDevice->tx.descriptorCompletions;
            s_diagnostics.txHardwareTimeouts = afterDevice->tx.hardwareTimeouts;
            s_diagnostics.txFailureReason = afterDevice->tx.failureReason;
            s_diagnostics.txDriverStatus = afterDevice->tx.lastStatus;
        }
        if (udpStatus != udp::UDP_OK) {
            set_failure(evidence.completionTimedOut
                            ? FAILURE_TX_COMPLETION : FAILURE_TX_SUBMIT,
                        "configured UDP send failed");
            s_stats.errors++;
            return DHCP_ERR_SEND_FAIL;
        }
        return DHCP_OK;
    }

    uint16_t udpLength = 0;
    uint16_t ipLength = 0;
    uint16_t frameLength = 0;

    udp::Status udpStatus = udp::build_datagram(
        s_udpWireBuffer, sizeof(s_udpWireBuffer), DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
        sourceIP, ipv4::ADDR_BROADCAST, packet,
        static_cast<uint16_t>(length), &udpLength);
    if (udpStatus != udp::UDP_OK) {
        set_failure(FAILURE_UDP_BUILD, "UDP 68->67 datagram build failed");
        s_stats.errors++;
        return DHCP_ERR_BUILD;
    }

    const ipv4::Status ipStatus = ipv4::build_packet_from_source(
        s_ipWireBuffer, sizeof(s_ipWireBuffer), sourceIP, ipv4::ADDR_BROADCAST,
        ipv4::PROTO_UDP, s_udpWireBuffer, udpLength, &ipLength);
    if (ipStatus != ipv4::IP_OK) {
        set_failure(FAILURE_IPV4_BUILD, "IPv4 bootstrap broadcast build failed");
        s_stats.errors++;
        return DHCP_ERR_BUILD;
    }

    const ethernet::Status ethernetStatus = ethernet::build_broadcast_frame(
        s_frameWireBuffer, sizeof(s_frameWireBuffer), sourceMAC,
        ethernet::ETHERTYPE_IPV4, s_ipWireBuffer, ipLength, &frameLength);
    if (ethernetStatus != ethernet::ETH_OK) {
        set_failure(FAILURE_ETHERNET_BUILD,
                    "Ethernet broadcast frame build failed");
        s_stats.errors++;
        return DHCP_ERR_BUILD;
    }

    s_diagnostics.dhcpPacketLength = static_cast<uint16_t>(length);
    s_diagnostics.frameLength = frameLength;
    ethernet::mac_copy(s_diagnostics.sourceMAC, sourceMAC);
    ethernet::mac_copy(s_diagnostics.destinationMAC, ethernet::BROADCAST_MAC);
    s_diagnostics.txSubmissionAttempted = true;

    const nic::TxDiagnostics before = device->tx;
    const nic::Status nicStatus = nic::send_frame(s_frameWireBuffer, frameLength);
    const nic::NICDevice* afterDevice = nic::get_device();
    const nic::TxEvidence evidence = afterDevice
        ? nic::observe_tx(before, afterDevice->tx, nicStatus)
        : nic::TxEvidence{};
    s_diagnostics.txDescriptorPublished = evidence.descriptorPublished;
    s_diagnostics.txDescriptorAccepted = evidence.descriptorAccepted;
    s_diagnostics.txDoorbellObserved = evidence.doorbellObserved;
    s_diagnostics.txTailAdvanced = evidence.tailAdvanced;
    s_diagnostics.txCompletionObserved = evidence.completionObserved;
    s_diagnostics.txCompletionTimeout = evidence.completionTimedOut;
    s_diagnostics.txDriverError = evidence.driverError;
    if (afterDevice) {
        s_diagnostics.txDescriptor = afterDevice->tx.lastDescriptor;
        s_diagnostics.txTailBefore = afterDevice->tx.tailBefore;
        s_diagnostics.txTailAfter = afterDevice->tx.tailAfter;
        s_diagnostics.txDescriptorSubmissions =
            afterDevice->tx.descriptorSubmissions;
        s_diagnostics.txDescriptorCompletions =
            afterDevice->tx.descriptorCompletions;
        s_diagnostics.txHardwareTimeouts = afterDevice->tx.hardwareTimeouts;
        s_diagnostics.txFailureReason = afterDevice->tx.failureReason;
        s_diagnostics.txDriverStatus = afterDevice->tx.lastStatus;
    }

    if (nicStatus != nic::NIC_OK) {
        set_failure(evidence.completionTimedOut
                        ? FAILURE_TX_COMPLETION : FAILURE_TX_SUBMIT,
                    evidence.completionTimedOut
                        ? "NIC TX descriptor completion timed out"
                        : "NIC TX submission failed");
        s_stats.errors++;
        return DHCP_ERR_SEND_FAIL;
    }

    return DHCP_OK;
}

Status dhcp_receive(uint8_t* buffer, size_t max_len, uint32_t* server_ip)
{
    if (!buffer || max_len == 0) return DHCP_ERR_INVALID;

    // Poll completed RX descriptors directly. DHCP is deliberately allowed
    // to receive before IPv4 configuration exists, so this path does not use
    // the configured-only socket factory or main-loop IPv4 polling.
    static const uint32_t POLL_ITERATIONS = 500000;
    static const uint16_t RX_FRAME_MAX = ethernet::MAX_FRAME_LEN;
    for (uint32_t i = 0; i < POLL_ITERATIONS; ++i) {
        uint16_t frameLen = 0;
        const nic::Status nicStatus = nic::receive_frame(
            s_receiveFrame, RX_FRAME_MAX, &frameLen);
        if (nicStatus == nic::NIC_OK) {
            ethernet::ParsedFrame ethernetFrame;
            if (ethernet::parse_frame(s_receiveFrame, frameLen, &ethernetFrame) !=
                ethernet::ETH_OK) {
                continue;
            }
            if (ethernetFrame.isBroadcast) {
                s_diagnostics.rxEthernetBroadcast++;
            }
            if (ethernetFrame.etherType != ethernet::ETHERTYPE_IPV4) continue;
            s_diagnostics.rxIPv4++;

            ipv4::ParsedPacket ipPacket;
            if (ipv4::parse_packet(ethernetFrame.payload,
                                   ethernetFrame.payloadLen, &ipPacket) !=
                ipv4::IP_OK) {
                continue;
            }
            if (ipPacket.protocol != ipv4::PROTO_UDP) continue;
            s_diagnostics.rxUDP++;

            udp::ParsedDatagram datagram;
            if (udp::parse_datagram(ipPacket.payload, ipPacket.payloadLen,
                                    ipPacket.srcAddr, ipPacket.dstAddr,
                                    &datagram) != udp::UDP_OK ||
                !datagram.checksumValid) {
                continue;
            }
            if (datagram.dstPort != DHCP_CLIENT_PORT) continue;
            s_diagnostics.rxDHCP++;
            if (datagram.dataLen < 240u || datagram.dataLen > max_len) {
                s_diagnostics.rxDHCPMalformed++;
                continue;
            }
            memzero(buffer, static_cast<uint32_t>(max_len));
            memcopy(buffer, datagram.data, datagram.dataLen);
            if (server_ip) *server_ip = ipPacket.srcAddr;
            return DHCP_OK;
        }

        if (nicStatus != nic::NIC_ERR_RX_EMPTY) return DHCP_ERR_NETWORK;

        // Brief bounded delay (architecture-dependent, simple spin).
        for (volatile uint32_t d = 0; d < 100; ++d) { }
    }

    return DHCP_ERR_TIMEOUT;
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
    if (st != DHCP_OK) {
        set_failure(FAILURE_DHCP_BUILD, "DHCP DISCOVER packet build failed");
        return DHCP_ERR_BUILD;
    }

    s_stats.discoverBuilt++;
    s_diagnostics.discoverPacketBuilt = true;
    s_diagnostics.transactionId = xid;
    s_diagnostics.dhcpPacketLength = pktLen;
    s_diagnostics.lastMessageType = DHCPDISCOVER;

    serial::puts("[DHCP] Sending DISCOVER (xid=0x");
    serial::put_hex32(xid);
    serial::puts(")\n");

    s_state = STATE_SELECTING;

    bool submitted = false;
    for (uint8_t attempt = 0; attempt < MAX_DISCOVER_RETRIES; ++attempt) {
        s_stats.discoverAttempts++;
        s_diagnostics.lastMessageType = DHCPDISCOVER;
        st = dhcp_send(s_txBuffer, pktLen, ipv4::ADDR_BROADCAST);
        // Descriptor publication and TDT readback are submission evidence;
        // DD completion is tracked separately. This preserves a real
        // submission even when the hardware never writes DD.
        if (s_diagnostics.txDescriptorAccepted) {
            s_stats.discoverSubmissions++;
        }
        if (s_diagnostics.txCompletionObserved) {
            s_stats.discoverCompletions++;
        }
        if (st != DHCP_OK) {
            s_stats.discoverSendFailures++;
            if (s_diagnostics.txCompletionTimeout) {
                s_stats.discoverTxTimeouts++;
            }
            if (st == DHCP_ERR_BUILD || st == DHCP_ERR_NO_NIC ||
                st == DHCP_ERR_NOT_READY || st == DHCP_ERR_LINK_DOWN) {
                return st;
            }
            // A completion timeout poisons the synchronous TX ring because
            // the shared buffer may still be owned by hardware. Do not issue
            // another descriptor or overwrite that buffer without a full
            // device/ring reinitialisation boundary.
            if (s_diagnostics.txCompletionTimeout ||
                s_diagnostics.txDescriptorAccepted) {
                return DHCP_ERR_SEND_FAIL;
            }
            continue;
        }
        submitted = true;
        s_stats.discoversSent++;

        // Wait for OFFER
        s_diagnostics.waitForOfferBegun = true;
        uint32_t fromIP = 0;
        st = dhcp_receive(s_rxBuffer, sizeof(s_rxBuffer), &fromIP);
        if (st == DHCP_OK) {
            Packet* resp = reinterpret_cast<Packet*>(s_rxBuffer);

            // Validate: must be BOOTREPLY with our xid
            if (resp->op != BOOTREPLY) {
                s_diagnostics.rxDHCPMalformed++;
                continue;
            }
            if (dhcp_ntohl(resp->xid) != xid) continue;

            // Parse options
            ParsedOptions opts;
            if (parse_options(resp, &opts) != DHCP_OK) {
                s_diagnostics.rxDHCPMalformed++;
                continue;
            }
            if (opts.messageType != DHCPOFFER) {
                s_diagnostics.rxDHCPMalformed++;
                continue;
            }

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

        if (st == DHCP_ERR_TIMEOUT) {
            s_stats.timeouts++;
            s_diagnostics.waitForOfferTimeout = true;
            serial::puts("[DHCP] DISCOVER timeout, retrying...\n");
        } else {
            s_stats.errors++;
        }
    }

    if (!submitted) {
        if (s_diagnostics.failureStage == FAILURE_NONE) {
            set_failure(FAILURE_TX_SUBMIT,
                        "no DHCP DISCOVER reached NIC submission");
        }
        return DHCP_ERR_SEND_FAIL;
    }

    set_failure(FAILURE_OFFER_WAIT,
                "DISCOVER transmitted but no valid OFFER received");
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
    if (st != DHCP_OK) {
        set_failure(FAILURE_DHCP_BUILD, "DHCP REQUEST packet build failed");
        return DHCP_ERR_BUILD;
    }

    serial::puts("[DHCP] Sending REQUEST for IP: ");
    char ipStr[16];
    ipv4::ip_to_string(offeredIP, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');

    s_state = STATE_REQUESTING;

    bool submitted = false;
    for (uint8_t attempt = 0; attempt < MAX_REQUEST_RETRIES; ++attempt) {
        s_diagnostics.lastMessageType = DHCPREQUEST;
        st = dhcp_send(s_txBuffer, pktLen, ipv4::ADDR_BROADCAST);
        if (st != DHCP_OK) {
            if (st == DHCP_ERR_BUILD || st == DHCP_ERR_NO_NIC ||
                st == DHCP_ERR_NOT_READY || st == DHCP_ERR_LINK_DOWN) {
                return st;
            }
            continue;
        }
        submitted = true;
        s_stats.requestsSent++;

        // Wait for ACK or NAK
        s_diagnostics.waitForAckBegun = true;
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

        if (st == DHCP_ERR_TIMEOUT) {
            s_stats.timeouts++;
            s_diagnostics.waitForAckTimeout = true;
            serial::puts("[DHCP] REQUEST timeout, retrying...\n");
        } else {
            s_stats.errors++;
        }
    }

    if (!submitted) {
        set_failure(FAILURE_TX_SUBMIT,
                    "no DHCP REQUEST reached NIC submission");
        return DHCP_ERR_SEND_FAIL;
    }

    set_failure(FAILURE_ACK_WAIT,
                "DHCP REQUEST transmitted but no ACK was received");
    return DHCP_ERR_NO_ACK;
}

// Internal: Apply lease and configure the kernel networking state
static Status apply_lease(const Packet* ackPkt, const ParsedOptions* opts,
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
    // Configure IPv4 layer with the obtained parameters
    const ipv4::ConfigStatus configStatus = ipv4::configure_dhcp(
        s_lease.assignedIP, s_lease.subnetMask,
        s_lease.gateway, s_lease.dnsServer);
    if (configStatus != ipv4::CONFIG_OK) {
        s_lease.valid = false;
        serial::puts("[DHCP] Lease rejected by IPv4 configuration: ");
        serial::puts(ipv4::config_status_name(configStatus));
        serial::putc('\n');
        return DHCP_ERR_INVALID;
    }

    s_lease.valid = true;
    dns::set_server(s_lease.dnsServer);

    s_state = STATE_BOUND;
    return DHCP_OK;
}

// ================================================================
// Public: Full DHCP Discovery
// ================================================================

Status discover()
{
    reset_invocation_diagnostics();
    s_state = STATE_INIT;

    const nic::NICDevice* device = nic::get_device();
    s_diagnostics.suitableInterfaceFound =
        device && device->driverBound;
    if (!device || !s_diagnostics.suitableInterfaceFound) {
        set_failure(FAILURE_INTERFACE, "no suitable network interface found");
        s_state = STATE_ERROR;
        serial::puts("[DHCP] No suitable NIC available\n");
        return DHCP_ERR_NO_NIC;
    }

    s_diagnostics.interfaceReady = nic::is_driver_ready(*device);
    if (!s_diagnostics.interfaceReady) {
        set_failure(FAILURE_READY, "interface driver is not ready");
        s_state = STATE_ERROR;
        serial::puts("[DHCP] NIC driver is not ready\n");
        return DHCP_ERR_NOT_READY;
    }

    const nic::LinkState cachedLink = nic::get_link_state();
    s_diagnostics.linkRefreshState = cachedLink;
    s_diagnostics.linkUp = cachedLink == nic::NIC_LINK_UP;
    if (!s_diagnostics.linkUp) {
        s_diagnostics.linkRefreshAttempted = true;
        s_diagnostics.linkRefreshResult = nic::refresh_link_state();
        s_diagnostics.linkRefreshState = nic::get_link_state();
        s_diagnostics.linkUp =
            s_diagnostics.linkRefreshResult == nic::LinkRefreshResult::Up &&
            s_diagnostics.linkRefreshState == nic::NIC_LINK_UP;
        serial::puts("[DHCP] Link preflight refresh: ");
        serial::puts(nic::link_refresh_result_name(s_diagnostics.linkRefreshResult));
        serial::putc('\n');
        if (!s_diagnostics.linkUp) {
            if (s_diagnostics.linkRefreshResult == nic::LinkRefreshResult::Down &&
                s_diagnostics.linkRefreshState == nic::NIC_LINK_DOWN) {
                set_failure(FAILURE_LINK,
                            "interface link is down (confirmed by bounded refresh)");
                s_state = STATE_ERROR;
                serial::puts("[DHCP] NIC link is down\n");
                return DHCP_ERR_LINK_DOWN;
            }
            set_failure(FAILURE_LINK,
                        "interface link state unavailable; inspect link diagnostic");
            s_state = STATE_ERROR;
            serial::puts("[DHCP] NIC link state could not be trusted\n");
            return DHCP_ERR_LINK_UNKNOWN;
        }
    }

    // A DHCP attempt owns the active IPv4 mode until it succeeds. This clears
    // any stale static/QEMU projection but intentionally does not write a
    // placeholder address; the bootstrap frame below supplies 0.0.0.0.
    set_automatic_mode();

    const uint8_t* mac = nic::get_mac_address();
    if (!mac) {
        set_failure(FAILURE_INTERFACE, "interface has no station MAC");
        s_state = STATE_ERROR;
        return DHCP_ERR_NO_NIC;
    }

    uint32_t xid = generate_xid();

    // Phase 1: DISCOVER -> OFFER
    Packet offerPkt;
    ParsedOptions offerOpts;
    Status st = do_discover(mac, xid, &offerPkt, &offerOpts);
    if (st != DHCP_OK) {
        s_state = STATE_ERROR;
        serial::puts("[DHCP] Discovery failed\n");
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
            st = apply_lease(&ackPkt, &ackOpts, xid);
            if (st != DHCP_OK) {
                s_state = STATE_ERROR;
                return st;
            }
            dhcp_print_info();
            return DHCP_OK;
        }

        if (st == DHCP_ERR_NAK) {
            // NAK received - restart from DISCOVER
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
    return st;
}

// ================================================================
// Lease Management
// ================================================================

const LeaseInfo* get_lease()
{
    return &s_lease;
}

const Diagnostics* get_diagnostics()
{
    return &s_diagnostics;
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
                        if (apply_lease(resp, &opts, xid) != DHCP_OK) {
                            s_state = STATE_ERROR;
                            return;
                        }
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

        // Renewal failed - will retry on next check
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
    ipv4::clear_configuration();
    dns::set_server(0);

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

const char* failure_stage_name(FailureStage stage)
{
    switch (stage) {
    case FAILURE_INTERFACE:       return "interface";
    case FAILURE_READY:           return "ready";
    case FAILURE_LINK:            return "link";
    case FAILURE_DHCP_BUILD:      return "DHCP build";
    case FAILURE_UDP_BUILD:       return "UDP build";
    case FAILURE_IPV4_BUILD:      return "IPv4 build";
    case FAILURE_ETHERNET_BUILD:  return "Ethernet build";
    case FAILURE_TX_SUBMIT:       return "TX submit";
    case FAILURE_TX_COMPLETION:   return "TX completion";
    case FAILURE_OFFER_WAIT:      return "offer wait";
    case FAILURE_ACK_WAIT:        return "ACK wait";
    case FAILURE_OFFER_PARSE:     return "offer parse";
    default:                      return "none";
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
