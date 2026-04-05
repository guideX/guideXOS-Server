// UDP Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/udp.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/icmp.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace udp {

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

static Statistics s_stats;
static Binding s_bindings[MAX_BINDINGS];
static uint16_t s_nextEphemeral = EPHEMERAL_START;

// ================================================================
// Checksum calculation
// ================================================================

// Internal: calculate one's complement sum
static uint32_t checksum_add(const void* data, uint16_t len)
{
    const uint16_t* ptr = static_cast<const uint16_t*>(data);
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Add remaining byte (if any)
    if (len > 0) {
        sum += *reinterpret_cast<const uint8_t*>(ptr);
    }
    
    return sum;
}

static uint16_t checksum_fold(uint32_t sum)
{
    // Fold 32-bit sum into 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return static_cast<uint16_t>(~sum);
}

uint16_t calculate_checksum(uint32_t srcIP, uint32_t dstIP,
                            const uint8_t* udpPacket, uint16_t udpLen)
{
    if (!udpPacket || udpLen < HEADER_LEN) return 0;
    
    // Build pseudo-header
    PseudoHeader pseudo;
    pseudo.srcAddr = ethernet::htonl(srcIP);
    pseudo.dstAddr = ethernet::htonl(dstIP);
    pseudo.zero = 0;
    pseudo.protocol = ipv4::PROTO_UDP;
    pseudo.udpLength = ethernet::htons(udpLen);
    
    // Sum pseudo-header
    uint32_t sum = checksum_add(&pseudo, sizeof(PseudoHeader));
    
    // Sum UDP header and data
    sum += checksum_add(udpPacket, udpLen);
    
    // Fold and complement
    uint16_t result = checksum_fold(sum);
    
    // Per RFC 768: if checksum is 0, use 0xFFFF
    if (result == 0) {
        result = 0xFFFF;
    }
    
    return result;
}

bool verify_checksum(uint32_t srcIP, uint32_t dstIP,
                     const uint8_t* udpPacket, uint16_t udpLen)
{
    if (!udpPacket || udpLen < HEADER_LEN) return false;
    
    const Header* hdr = reinterpret_cast<const Header*>(udpPacket);
    
    // Checksum of 0 means checksum is disabled
    if (hdr->checksum == 0) {
        return true;  // Disabled checksum is valid
    }
    
    // Calculate checksum - if valid, result should be 0xFFFF
    // (because we're summing including the checksum field)
    PseudoHeader pseudo;
    pseudo.srcAddr = ethernet::htonl(srcIP);
    pseudo.dstAddr = ethernet::htonl(dstIP);
    pseudo.zero = 0;
    pseudo.protocol = ipv4::PROTO_UDP;
    pseudo.udpLength = ethernet::htons(udpLen);
    
    uint32_t sum = checksum_add(&pseudo, sizeof(PseudoHeader));
    sum += checksum_add(udpPacket, udpLen);
    
    uint16_t result = checksum_fold(sum);
    
    // After folding, valid checksum should give 0 (because ~0xFFFF = 0)
    return result == 0;
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    memzero(&s_stats, sizeof(s_stats));
    memzero(s_bindings, sizeof(s_bindings));
    s_nextEphemeral = EPHEMERAL_START;
    
    // Register with IPv4 layer to receive UDP packets
    ipv4::register_handler(ipv4::PROTO_UDP, handle_packet);
    
    serial::puts("[UDP] Layer initialized\n");
}

// ================================================================
// Port Binding
// ================================================================

Status bind(uint16_t port, uint32_t localAddr, DatagramHandler handler)
{
    if (!handler) return UDP_ERR_NULL_PTR;
    if (port == 0) return UDP_ERR_NO_PORT;
    
    // Check if port is already bound
    for (uint8_t i = 0; i < MAX_BINDINGS; ++i) {
        if (s_bindings[i].active && s_bindings[i].port == port) {
            return UDP_ERR_PORT_IN_USE;
        }
    }
    
    // Find empty slot
    for (uint8_t i = 0; i < MAX_BINDINGS; ++i) {
        if (!s_bindings[i].active) {
            s_bindings[i].port = port;
            s_bindings[i].localAddr = localAddr;
            s_bindings[i].handler = handler;
            s_bindings[i].active = true;
            
            serial::puts("[UDP] Bound port ");
            serial::put_hex32(port);
            serial::putc('\n');
            
            return UDP_OK;
        }
    }
    
    return UDP_ERR_NO_BINDING;  // No available slots
}

Status unbind(uint16_t port)
{
    for (uint8_t i = 0; i < MAX_BINDINGS; ++i) {
        if (s_bindings[i].active && s_bindings[i].port == port) {
            s_bindings[i].active = false;
            serial::puts("[UDP] Unbound port ");
            serial::put_hex32(port);
            serial::putc('\n');
            return UDP_OK;
        }
    }
    
    return UDP_ERR_NO_BINDING;
}

bool is_bound(uint16_t port)
{
    for (uint8_t i = 0; i < MAX_BINDINGS; ++i) {
        if (s_bindings[i].active && s_bindings[i].port == port) {
            return true;
        }
    }
    return false;
}

uint16_t alloc_ephemeral_port()
{
    uint16_t startPort = s_nextEphemeral;
    
    do {
        if (!is_bound(s_nextEphemeral)) {
            uint16_t port = s_nextEphemeral;
            s_nextEphemeral++;
            if (s_nextEphemeral > EPHEMERAL_END) {
                s_nextEphemeral = EPHEMERAL_START;
            }
            return port;
        }
        
        s_nextEphemeral++;
        if (s_nextEphemeral > EPHEMERAL_END) {
            s_nextEphemeral = EPHEMERAL_START;
        }
    } while (s_nextEphemeral != startPort);
    
    return 0;  // No available ports
}

void free_ephemeral_port(uint16_t port)
{
    if (port >= EPHEMERAL_START && port <= EPHEMERAL_END) {
        unbind(port);
    }
}

// ================================================================
// Datagram Parsing
// ================================================================

Status parse_datagram(const uint8_t* data, uint16_t len,
                      uint32_t srcIP, uint32_t dstIP,
                      ParsedDatagram* parsed)
{
    if (!data || !parsed) return UDP_ERR_NULL_PTR;
    if (len < HEADER_LEN) return UDP_ERR_TOO_SHORT;
    
    memzero(parsed, sizeof(ParsedDatagram));
    
    const Header* hdr = reinterpret_cast<const Header*>(data);
    
    // Parse header fields (convert from network byte order)
    parsed->srcPort = ethernet::ntohs(hdr->srcPort);
    parsed->dstPort = ethernet::ntohs(hdr->dstPort);
    parsed->length = ethernet::ntohs(hdr->length);
    parsed->checksum = ethernet::ntohs(hdr->checksum);
    parsed->srcIP = srcIP;
    parsed->dstIP = dstIP;
    
    // Validate length
    if (parsed->length < HEADER_LEN || parsed->length > len) {
        return UDP_ERR_TOO_SHORT;
    }
    
    // Verify checksum if present
    if (hdr->checksum != 0) {
        parsed->checksumValid = verify_checksum(srcIP, dstIP, data, len);
        if (!parsed->checksumValid) {
            s_stats.checksumErrors++;
            return UDP_ERR_BAD_CHECKSUM;
        }
    } else {
        parsed->checksumValid = true;  // Checksum disabled
    }
    
    // Set payload pointer
    parsed->data = data + HEADER_LEN;
    parsed->dataLen = parsed->length - HEADER_LEN;
    parsed->isValid = true;
    
    return UDP_OK;
}

// ================================================================
// Datagram Transmission
// ================================================================

Status send(uint16_t srcPort, uint32_t dstIP, uint16_t dstPort,
            const uint8_t* data, uint16_t len)
{
    if (!ipv4::is_configured()) {
        s_stats.txErrors++;
        return UDP_ERR_NOT_CONFIGURED;
    }
    
    if (len > MTU_PAYLOAD) {
        s_stats.txErrors++;
        return UDP_ERR_TOO_LONG;
    }
    
    // Build UDP packet
    uint16_t udpLen = HEADER_LEN + len;
    uint8_t packet[1500];  // Max Ethernet payload
    
    if (udpLen > sizeof(packet)) {
        s_stats.txErrors++;
        return UDP_ERR_TOO_LONG;
    }
    
    memzero(packet, sizeof(packet));
    
    // Fill UDP header
    Header* hdr = reinterpret_cast<Header*>(packet);
    hdr->srcPort = ethernet::htons(srcPort);
    hdr->dstPort = ethernet::htons(dstPort);
    hdr->length = ethernet::htons(udpLen);
    hdr->checksum = 0;  // Calculate after filling
    
    // Copy payload
    if (data && len > 0) {
        memcopy(packet + HEADER_LEN, data, len);
    }
    
    // Calculate checksum
    const ipv4::NetworkConfig* cfg = ipv4::get_config();
    if (cfg) {
        hdr->checksum = calculate_checksum(cfg->ipAddr, dstIP, packet, udpLen);
    }
    
    // Send via IPv4
    ipv4::Status ipStatus = ipv4::send_packet(
        packet, udpLen, dstIP, ipv4::PROTO_UDP);
    
    if (ipStatus != ipv4::IP_OK) {
        s_stats.txErrors++;
        return UDP_ERR_TX_FAILED;
    }
    
    s_stats.txDatagrams++;
    s_stats.txBytes += len;
    
    return UDP_OK;
}

Status send_auto(uint32_t dstIP, uint16_t dstPort,
                 const uint8_t* data, uint16_t len,
                 uint16_t* srcPort)
{
    if (srcPort) *srcPort = 0;
    
    uint16_t port = alloc_ephemeral_port();
    if (port == 0) {
        return UDP_ERR_NO_PORT;
    }
    
    Status status = send(port, dstIP, dstPort, data, len);
    
    if (status == UDP_OK) {
        if (srcPort) *srcPort = port;
    } else {
        // Don't keep the port on failure
        // (ephemeral ports are typically freed after use anyway)
    }
    
    return status;
}

// ================================================================
// Datagram Reception
// ================================================================

void handle_packet(const ipv4::ParsedPacket* ipPacket)
{
    if (!ipPacket || !ipPacket->payload || ipPacket->payloadLen < HEADER_LEN) {
        s_stats.rxErrors++;
        return;
    }
    
    // Parse datagram
    ParsedDatagram datagram;
    Status status = parse_datagram(ipPacket->payload, ipPacket->payloadLen,
                                   ipPacket->srcAddr, ipPacket->dstAddr,
                                   &datagram);
    
    if (status != UDP_OK) {
        s_stats.rxErrors++;
        return;
    }
    
    s_stats.rxDatagrams++;
    s_stats.rxBytes += datagram.dataLen;
    
    // Find binding for destination port
    bool delivered = false;
    for (uint8_t i = 0; i < MAX_BINDINGS; ++i) {
        if (!s_bindings[i].active) continue;
        if (s_bindings[i].port != datagram.dstPort) continue;
        
        // Check local address filter
        if (s_bindings[i].localAddr != 0 &&
            s_bindings[i].localAddr != datagram.dstIP &&
            datagram.dstIP != ipv4::ADDR_BROADCAST) {
            continue;
        }
        
        // Deliver to handler
        if (s_bindings[i].handler) {
            s_bindings[i].handler(&datagram);
            delivered = true;
        }
        break;
    }
    
    if (!delivered) {
        s_stats.noPortErrors++;
        
        // Send ICMP Port Unreachable (if not broadcast)
        if (datagram.dstIP != ipv4::ADDR_BROADCAST &&
            !ipv4::is_multicast(datagram.dstIP)) {
            // Note: In a full implementation, we'd send ICMP port unreachable
            // icmp::send_dest_unreachable(datagram.srcIP, icmp::CODE_PORT_UNREACHABLE,
            //                             ipPacket->payload - ipv4::MIN_HEADER_LEN,
            //                             ipv4::MIN_HEADER_LEN + 8);
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

void reset_stats()
{
    memzero(&s_stats, sizeof(s_stats));
}

// ================================================================
// Utility
// ================================================================

const char* port_name(uint16_t port)
{
    switch (port) {
        case PORT_ECHO:     return "echo";
        case PORT_DISCARD:  return "discard";
        case PORT_DAYTIME:  return "daytime";
        case PORT_FTP_DATA: return "ftp-data";
        case PORT_FTP:      return "ftp";
        case PORT_SSH:      return "ssh";
        case PORT_TELNET:   return "telnet";
        case PORT_SMTP:     return "smtp";
        case PORT_DNS:      return "dns";
        case PORT_BOOTPS:   return "bootps";
        case PORT_BOOTPC:   return "bootpc";
        case PORT_TFTP:     return "tftp";
        case PORT_HTTP:     return "http";
        case PORT_NTP:      return "ntp";
        case PORT_SNMP:     return "snmp";
        case PORT_HTTPS:    return "https";
        case PORT_SYSLOG:   return "syslog";
        default:            return "unknown";
    }
}

} // namespace udp
} // namespace kernel
