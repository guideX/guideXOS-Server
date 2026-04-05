// IPv4 Network Layer — Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ipv4.h"
#include "include/kernel/nic.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace ipv4 {

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

static NetworkConfig s_config;
static RouteEntry    s_routes[MAX_ROUTES];
static Statistics    s_stats;
static uint16_t      s_identification = 0;  // Packet ID counter

// Protocol handlers (indexed by protocol number for common protocols)
static const uint8_t MAX_HANDLERS = 8;
struct HandlerEntry {
    uint8_t protocol;
    ProtocolHandler handler;
    bool active;
};
static HandlerEntry s_handlers[MAX_HANDLERS];

// ARP cache (simplified - normally would be separate module)
static const uint8_t ARP_CACHE_SIZE = 16;
struct ArpEntry {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
};
static ArpEntry s_arpCache[ARP_CACHE_SIZE];

// ================================================================
// IP Address Utilities
// ================================================================

void ip_to_string(uint32_t ip, char* str)
{
    if (!str) return;
    
    uint8_t idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (i > 0) str[idx++] = '.';
        
        uint8_t octet = ip_octet(ip, i);
        
        // Convert octet to decimal
        if (octet >= 100) {
            str[idx++] = '0' + (octet / 100);
            octet %= 100;
            str[idx++] = '0' + (octet / 10);
            str[idx++] = '0' + (octet % 10);
        } else if (octet >= 10) {
            str[idx++] = '0' + (octet / 10);
            str[idx++] = '0' + (octet % 10);
        } else {
            str[idx++] = '0' + octet;
        }
    }
    str[idx] = '\0';
}

bool ip_from_string(const char* str, uint32_t* ip)
{
    if (!str || !ip) return false;
    
    uint32_t result = 0;
    uint8_t octet = 0;
    uint8_t octetCount = 0;
    
    for (const char* p = str; ; ++p) {
        if (*p >= '0' && *p <= '9') {
            octet = octet * 10 + (*p - '0');
            if (octet > 255) return false;
        } else if (*p == '.' || *p == '\0') {
            result = (result << 8) | octet;
            octet = 0;
            octetCount++;
            
            if (*p == '\0') break;
            if (octetCount >= 4) return false;
        } else {
            return false;
        }
    }
    
    if (octetCount != 4) return false;
    
    *ip = result;
    return true;
}

bool is_private(uint32_t ip)
{
    // 10.0.0.0/8
    if ((ip & 0xFF000000) == 0x0A000000) return true;
    
    // 172.16.0.0/12 (172.16.x.x - 172.31.x.x)
    if ((ip & 0xFFF00000) == 0xAC100000) return true;
    
    // 192.168.0.0/16
    if ((ip & 0xFFFF0000) == 0xC0A80000) return true;
    
    return false;
}

// ================================================================
// Checksum Functions
// ================================================================

uint16_t calculate_checksum(const void* data, uint16_t len)
{
    const uint16_t* ptr = static_cast<const uint16_t*>(data);
    uint32_t sum = 0;
    
    // Sum all 16-bit words
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Add remaining byte (if any)
    if (len > 0) {
        sum += *reinterpret_cast<const uint8_t*>(ptr);
    }
    
    // Fold 32-bit sum into 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    // Return one's complement
    return static_cast<uint16_t>(~sum);
}

bool verify_checksum(const Header* hdr)
{
    if (!hdr) return false;
    
    uint8_t headerLen = get_header_len(hdr);
    
    // Calculate checksum over entire header
    // If checksum is correct, result should be 0
    uint16_t result = calculate_checksum(hdr, headerLen);
    
    return result == 0;
}

// ================================================================
// Configuration
// ================================================================

void init()
{
    memzero(&s_config, sizeof(s_config));
    memzero(s_routes, sizeof(s_routes));
    memzero(&s_stats, sizeof(s_stats));
    memzero(s_handlers, sizeof(s_handlers));
    memzero(s_arpCache, sizeof(s_arpCache));
    
    s_identification = 1;
    
    serial::puts("[IPv4] Layer initialized\n");
}

void configure(uint32_t ip, uint32_t mask, uint32_t gateway, uint32_t dns)
{
    s_config.ipAddr = ip;
    s_config.subnetMask = mask;
    s_config.gateway = gateway;
    s_config.dns = dns;
    s_config.configured = true;
    
    // Add default route via gateway
    if (gateway != 0) {
        add_route(0, 0, gateway, 100);
    }
    
    // Add local network route (direct)
    add_route(ip & mask, mask, 0, 1);
    
    serial::puts("[IPv4] Configured: IP=");
    char ipStr[16];
    ip_to_string(ip, ipStr);
    serial::puts(ipStr);
    serial::puts(" Mask=");
    ip_to_string(mask, ipStr);
    serial::puts(ipStr);
    serial::puts(" GW=");
    ip_to_string(gateway, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');
}

void set_mac_address(const uint8_t* mac)
{
    if (!mac) return;
    ethernet::mac_copy(s_config.macAddr, mac);
}

const NetworkConfig* get_config()
{
    return &s_config;
}

bool is_configured()
{
    return s_config.configured;
}

// ================================================================
// Routing
// ================================================================

Status add_route(uint32_t network, uint32_t mask, uint32_t gateway, uint8_t metric)
{
    // Find empty slot
    for (uint8_t i = 0; i < MAX_ROUTES; ++i) {
        if (!s_routes[i].active) {
            s_routes[i].network = network;
            s_routes[i].mask = mask;
            s_routes[i].gateway = gateway;
            s_routes[i].metric = metric;
            s_routes[i].active = true;
            return IP_OK;
        }
    }
    return IP_ERR_NO_ROUTE;  // Table full
}

Status remove_route(uint32_t network, uint32_t mask)
{
    for (uint8_t i = 0; i < MAX_ROUTES; ++i) {
        if (s_routes[i].active &&
            s_routes[i].network == network &&
            s_routes[i].mask == mask) {
            s_routes[i].active = false;
            return IP_OK;
        }
    }
    return IP_ERR_NO_ROUTE;
}

uint32_t lookup_route(uint32_t dstAddr)
{
    // Check if destination is local
    if (is_local(dstAddr)) {
        return 0;  // Direct connection
    }
    
    // Find best matching route (longest prefix match)
    uint32_t bestGateway = 0;
    uint32_t bestMask = 0;
    uint8_t bestMetric = 255;
    
    for (uint8_t i = 0; i < MAX_ROUTES; ++i) {
        if (!s_routes[i].active) continue;
        
        // Check if destination matches this route
        if ((dstAddr & s_routes[i].mask) == s_routes[i].network) {
            // Prefer longer prefix match, then lower metric
            if (s_routes[i].mask > bestMask ||
                (s_routes[i].mask == bestMask && s_routes[i].metric < bestMetric)) {
                bestGateway = s_routes[i].gateway;
                bestMask = s_routes[i].mask;
                bestMetric = s_routes[i].metric;
            }
        }
    }
    
    return bestGateway;
}

bool is_local(uint32_t dstAddr)
{
    if (!s_config.configured) return false;
    
    // Same subnet?
    return is_same_subnet(s_config.ipAddr, dstAddr, s_config.subnetMask);
}

// ================================================================
// ARP Cache (simplified)
// ================================================================

static bool arp_lookup(uint32_t ip, uint8_t* mac)
{
    for (uint8_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (s_arpCache[i].valid && s_arpCache[i].ip == ip) {
            ethernet::mac_copy(mac, s_arpCache[i].mac);
            return true;
        }
    }
    return false;
}

static void arp_add(uint32_t ip, const uint8_t* mac)
{
    // Find existing entry or empty slot
    uint8_t slot = 0;
    for (uint8_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (s_arpCache[i].valid && s_arpCache[i].ip == ip) {
            slot = i;
            break;
        }
        if (!s_arpCache[i].valid) {
            slot = i;
            break;
        }
    }
    
    s_arpCache[slot].ip = ip;
    ethernet::mac_copy(s_arpCache[slot].mac, mac);
    s_arpCache[slot].valid = true;
}

// For now, use broadcast if not in cache (real ARP would be separate)
static bool resolve_mac(uint32_t ip, uint8_t* mac)
{
    // Check ARP cache first
    if (arp_lookup(ip, mac)) {
        return true;
    }
    
    // Broadcast address
    if (ip == ADDR_BROADCAST) {
        ethernet::mac_copy(mac, ethernet::BROADCAST_MAC);
        return true;
    }
    
    // Subnet broadcast
    if (s_config.configured) {
        uint32_t subnetBcast = (s_config.ipAddr & s_config.subnetMask) | 
                               ~s_config.subnetMask;
        if (ip == subnetBcast) {
            ethernet::mac_copy(mac, ethernet::BROADCAST_MAC);
            return true;
        }
    }
    
    // For now, use broadcast (proper ARP request would be needed)
    ethernet::mac_copy(mac, ethernet::BROADCAST_MAC);
    return true;
}

// ================================================================
// Packet Parsing
// ================================================================

Status parse_packet(const uint8_t* data, uint16_t len, ParsedPacket* parsed)
{
    if (!data || !parsed) return IP_ERR_NULL_PTR;
    if (len < MIN_HEADER_LEN) return IP_ERR_TOO_SHORT;
    
    memzero(parsed, sizeof(ParsedPacket));
    
    const Header* hdr = reinterpret_cast<const Header*>(data);
    
    // Check version
    parsed->version = get_version(hdr);
    if (parsed->version != 4) {
        return IP_ERR_BAD_VERSION;
    }
    
    // Get header length
    parsed->headerLen = get_header_len(hdr);
    if (parsed->headerLen < MIN_HEADER_LEN || parsed->headerLen > len) {
        return IP_ERR_BAD_HEADER;
    }
    
    // Verify checksum
    parsed->checksumValid = verify_checksum(hdr);
    if (!parsed->checksumValid) {
        s_stats.checksumErrors++;
        return IP_ERR_BAD_CHECKSUM;
    }
    
    // Parse remaining fields
    parsed->tos = hdr->tos;
    parsed->totalLen = ethernet::ntohs(hdr->totalLength);
    parsed->identification = ethernet::ntohs(hdr->identification);
    parsed->flags = get_flags(hdr);
    parsed->fragOffset = get_frag_offset(hdr);
    parsed->ttl = hdr->ttl;
    parsed->protocol = hdr->protocol;
    parsed->checksum = ethernet::ntohs(hdr->checksum);
    parsed->srcAddr = ethernet::ntohl(hdr->srcAddr);
    parsed->dstAddr = ethernet::ntohl(hdr->dstAddr);
    
    // Validate total length
    if (parsed->totalLen < parsed->headerLen || parsed->totalLen > len) {
        return IP_ERR_BAD_HEADER;
    }
    
    // Set payload pointer
    parsed->payload = data + parsed->headerLen;
    parsed->payloadLen = parsed->totalLen - parsed->headerLen;
    
    // Set flags
    parsed->isValid = true;
    parsed->isFragmented = is_fragmented(hdr);
    parsed->isBroadcast = (parsed->dstAddr == ADDR_BROADCAST);
    parsed->isMulticast = is_multicast(parsed->dstAddr);
    parsed->isLoopback = is_loopback(parsed->dstAddr);
    
    return IP_OK;
}

Status validate_header(const uint8_t* data, uint16_t len)
{
    if (!data) return IP_ERR_NULL_PTR;
    if (len < MIN_HEADER_LEN) return IP_ERR_TOO_SHORT;
    
    const Header* hdr = reinterpret_cast<const Header*>(data);
    
    // Check version
    if (get_version(hdr) != 4) return IP_ERR_BAD_VERSION;
    
    // Check header length
    uint8_t headerLen = get_header_len(hdr);
    if (headerLen < MIN_HEADER_LEN || headerLen > len) return IP_ERR_BAD_HEADER;
    
    // Verify checksum
    if (!verify_checksum(hdr)) return IP_ERR_BAD_CHECKSUM;
    
    return IP_OK;
}

Status extract_header(const uint8_t* data, uint16_t len, Header* header)
{
    if (!data || !header) return IP_ERR_NULL_PTR;
    if (len < MIN_HEADER_LEN) return IP_ERR_TOO_SHORT;
    
    memcopy(header, data, MIN_HEADER_LEN);
    return IP_OK;
}

Status get_payload(const uint8_t* data, uint16_t len,
                   const uint8_t** payload, uint16_t* payloadLen)
{
    if (!data) return IP_ERR_NULL_PTR;
    if (len < MIN_HEADER_LEN) return IP_ERR_TOO_SHORT;
    
    const Header* hdr = reinterpret_cast<const Header*>(data);
    uint8_t headerLen = get_header_len(hdr);
    
    if (headerLen > len) return IP_ERR_BAD_HEADER;
    
    if (payload) *payload = data + headerLen;
    if (payloadLen) *payloadLen = ethernet::ntohs(hdr->totalLength) - headerLen;
    
    return IP_OK;
}

// ================================================================
// Packet Building
// ================================================================

Status build_header(Header* hdr,
                    uint32_t srcAddr,
                    uint32_t dstAddr,
                    uint8_t protocol,
                    uint16_t payloadLen)
{
    if (!hdr) return IP_ERR_NULL_PTR;
    if (payloadLen > MAX_PAYLOAD) return IP_ERR_TOO_LONG;
    
    memzero(hdr, sizeof(Header));
    
    // Version (4) and IHL (5 = 20 bytes, no options)
    hdr->versionIHL = (IP_VERSION << 4) | 5;
    
    // Type of Service (default 0)
    hdr->tos = 0;
    
    // Total length
    uint16_t totalLen = MIN_HEADER_LEN + payloadLen;
    hdr->totalLength = ethernet::htons(totalLen);
    
    // Identification (increment for each packet)
    hdr->identification = ethernet::htons(s_identification++);
    
    // Flags: Don't Fragment, no offset
    hdr->flagsFragment = ethernet::htons(FLAG_DF);
    
    // TTL
    hdr->ttl = DEFAULT_TTL;
    
    // Protocol
    hdr->protocol = protocol;
    
    // Addresses (convert to network byte order)
    hdr->srcAddr = ethernet::htonl(srcAddr);
    hdr->dstAddr = ethernet::htonl(dstAddr);
    
    // Calculate checksum (must be 0 during calculation)
    hdr->checksum = 0;
    hdr->checksum = calculate_checksum(hdr, MIN_HEADER_LEN);
    
    return IP_OK;
}

Status build_packet(uint8_t* buffer,
                    uint16_t bufferSize,
                    uint32_t dstAddr,
                    uint8_t protocol,
                    const uint8_t* payload,
                    uint16_t payloadLen,
                    uint16_t* packetLen)
{
    if (!buffer) return IP_ERR_NULL_PTR;
    if (!s_config.configured) return IP_ERR_NOT_CONFIGURED;
    
    uint16_t totalLen = MIN_HEADER_LEN + payloadLen;
    if (totalLen > bufferSize) return IP_ERR_BUFFER_SMALL;
    if (payloadLen > MAX_PAYLOAD) return IP_ERR_TOO_LONG;
    
    // Build header
    Header* hdr = reinterpret_cast<Header*>(buffer);
    Status status = build_header(hdr, s_config.ipAddr, dstAddr, protocol, payloadLen);
    if (status != IP_OK) return status;
    
    // Copy payload
    if (payload && payloadLen > 0) {
        memcopy(buffer + MIN_HEADER_LEN, payload, payloadLen);
    }
    
    if (packetLen) *packetLen = totalLen;
    
    return IP_OK;
}

// ================================================================
// Packet Transmission
// ================================================================

Status send_packet(const uint8_t* payload,
                   uint16_t len,
                   uint32_t dstAddr,
                   uint8_t protocol)
{
    if (!s_config.configured) {
        s_stats.txErrors++;
        return IP_ERR_NOT_CONFIGURED;
    }
    
    if (len > MAX_PAYLOAD) {
        s_stats.txErrors++;
        return IP_ERR_TOO_LONG;
    }
    
    // Build IP packet
    uint8_t ipPacket[MTU];
    uint16_t ipLen;
    
    Status status = build_packet(ipPacket, sizeof(ipPacket),
                                 dstAddr, protocol, payload, len, &ipLen);
    if (status != IP_OK) {
        s_stats.txErrors++;
        return status;
    }
    
    return send_raw_packet(ipPacket, ipLen, dstAddr);
}

Status send_raw_packet(const uint8_t* packet, uint16_t len, uint32_t dstAddr)
{
    if (!packet) return IP_ERR_NULL_PTR;
    if (!s_config.configured) return IP_ERR_NOT_CONFIGURED;
    
    // Determine next hop
    uint32_t nextHop = dstAddr;
    if (!is_local(dstAddr)) {
        nextHop = lookup_route(dstAddr);
        if (nextHop == 0 && !is_local(dstAddr)) {
            // Use default gateway if no specific route
            nextHop = s_config.gateway;
        }
        if (nextHop == 0) {
            s_stats.noRouteErrors++;
            return IP_ERR_NO_ROUTE;
        }
    }
    
    // Resolve MAC address
    uint8_t dstMAC[6];
    if (!resolve_mac(nextHop, dstMAC)) {
        s_stats.txErrors++;
        return IP_ERR_NO_ROUTE;
    }
    
    // Build Ethernet frame
    uint8_t frame[ethernet::MAX_FRAME_LEN];
    uint16_t frameLen;
    
    ethernet::Status ethStatus = ethernet::build_frame(
        frame, sizeof(frame),
        dstMAC, s_config.macAddr,
        ethernet::ETHERTYPE_IPV4,
        packet, len,
        &frameLen);
    
    if (ethStatus != ethernet::ETH_OK) {
        s_stats.txErrors++;
        return IP_ERR_TX_FAILED;
    }
    
    // Send via NIC
    nic::Status nicStatus = nic::send_frame(frame, frameLen);
    if (nicStatus != nic::NIC_OK) {
        s_stats.txErrors++;
        return IP_ERR_TX_FAILED;
    }
    
    s_stats.txPackets++;
    s_stats.txBytes += len;
    
    return IP_OK;
}

// ================================================================
// Packet Reception
// ================================================================

void register_handler(uint8_t protocol, ProtocolHandler handler)
{
    // Find existing or empty slot
    for (uint8_t i = 0; i < MAX_HANDLERS; ++i) {
        if (s_handlers[i].active && s_handlers[i].protocol == protocol) {
            s_handlers[i].handler = handler;
            return;
        }
        if (!s_handlers[i].active) {
            s_handlers[i].protocol = protocol;
            s_handlers[i].handler = handler;
            s_handlers[i].active = true;
            return;
        }
    }
}

void unregister_handler(uint8_t protocol)
{
    for (uint8_t i = 0; i < MAX_HANDLERS; ++i) {
        if (s_handlers[i].active && s_handlers[i].protocol == protocol) {
            s_handlers[i].active = false;
            return;
        }
    }
}

void handle_packet(const uint8_t* data, uint16_t len)
{
    if (!data || len < MIN_HEADER_LEN) {
        s_stats.rxErrors++;
        return;
    }
    
    // Parse packet
    ParsedPacket parsed;
    Status status = parse_packet(data, len, &parsed);
    
    if (status != IP_OK) {
        s_stats.rxErrors++;
        return;
    }
    
    s_stats.rxPackets++;
    s_stats.rxBytes += len;
    
    // Check if packet is for us
    if (s_config.configured) {
        if (parsed.dstAddr != s_config.ipAddr &&
            parsed.dstAddr != ADDR_BROADCAST &&
            !is_multicast(parsed.dstAddr) &&
            !is_subnet_broadcast(parsed.dstAddr, 
                                 s_config.ipAddr & s_config.subnetMask,
                                 s_config.subnetMask)) {
            // Not for us
            return;
        }
    }
    
    // Check TTL
    if (parsed.ttl == 0) {
        s_stats.ttlExpired++;
        return;
    }
    
    // Check for fragments (not supported)
    if (parsed.isFragmented) {
        s_stats.fragmentsDropped++;
        return;
    }
    
    // Update ARP cache with source
    // (In real implementation, this would be in ARP layer)
    // We'd need the source MAC from Ethernet header
    
    // Dispatch to protocol handler
    for (uint8_t i = 0; i < MAX_HANDLERS; ++i) {
        if (s_handlers[i].active && s_handlers[i].protocol == parsed.protocol) {
            s_handlers[i].handler(&parsed);
            return;
        }
    }
    
    // No handler registered for this protocol
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

} // namespace ipv4
} // namespace kernel
