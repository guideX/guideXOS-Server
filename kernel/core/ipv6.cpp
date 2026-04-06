// IPv6 Core Implementation
//
// Implements IPv6 packet processing, address handling, and neighbor discovery.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ipv6.h"
#include "include/kernel/icmpv6.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/serial_debug.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace ipv6 {

// ================================================================
// Internal State
// ================================================================

static Statistics s_stats;
static bool s_initialized = false;

// Local IPv6 addresses
static const int MAX_LOCAL_ADDRS = 8;
static Address s_localAddrs[MAX_LOCAL_ADDRS];
static int s_localAddrCount = 0;

// Default hop limit
static uint8_t s_defaultHopLimit = DEFAULT_HOP_LIMIT;

// ================================================================
// Memory Helpers
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

static void memcopy(void* dst, const void* src, size_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
}

// ================================================================
// Checksum Helpers
// ================================================================

static uint32_t checksum_add(const void* data, size_t len)
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

// ================================================================
// IPv6 Address Functions
// ================================================================

bool addr_equals(const Address* a, const Address* b)
{
    if (!a || !b) return false;
    
    return a->qwords[0] == b->qwords[0] && a->qwords[1] == b->qwords[1];
}

bool is_unspecified(const Address* addr)
{
    if (!addr) return true;
    return addr->qwords[0] == 0 && addr->qwords[1] == 0;
}

bool is_loopback(const Address* addr)
{
    if (!addr) return false;
    return addr->qwords[0] == 0 && addr->qwords[1] == ethernet::htonll(1);
}

bool is_multicast(const Address* addr)
{
    if (!addr) return false;
    return addr->bytes[0] == 0xFF;
}

bool is_link_local(const Address* addr)
{
    if (!addr) return false;
    return (addr->bytes[0] == 0xFE) && ((addr->bytes[1] & 0xC0) == 0x80);
}

bool is_site_local(const Address* addr)
{
    if (!addr) return false;
    return (addr->bytes[0] == 0xFE) && ((addr->bytes[1] & 0xC0) == 0xC0);
}

bool is_global_unicast(const Address* addr)
{
    if (!addr) return false;
    
    // Global unicast: 2000::/3
    return (addr->bytes[0] & 0xE0) == 0x20;
}

bool is_solicited_node_multicast(const Address* addr)
{
    if (!addr) return false;
    
    // ff02::1:ff00:0/104
    return addr->bytes[0] == 0xFF &&
           addr->bytes[1] == 0x02 &&
           addr->dwords[1] == 0 &&
           addr->dwords[2] == ethernet::htonl(1) &&
           addr->bytes[12] == 0xFF;
}

void make_solicited_node_multicast(const Address* unicast, Address* multicast)
{
    if (!unicast || !multicast) return;
    
    // ff02::1:ff00:0000 | (unicast & 0x00ffffff)
    memzero(multicast, sizeof(Address));
    multicast->bytes[0] = 0xFF;
    multicast->bytes[1] = 0x02;
    multicast->bytes[11] = 0x01;
    multicast->bytes[12] = 0xFF;
    multicast->bytes[13] = unicast->bytes[13];
    multicast->bytes[14] = unicast->bytes[14];
    multicast->bytes[15] = unicast->bytes[15];
}

void make_link_local_from_mac(const uint8_t* mac, Address* addr)
{
    if (!mac || !addr) return;
    
    memzero(addr, sizeof(Address));
    
    // fe80::
    addr->bytes[0] = 0xFE;
    addr->bytes[1] = 0x80;
    
    // EUI-64 from MAC
    // Insert ff:fe in the middle, flip universal/local bit
    addr->bytes[8] = mac[0] ^ 0x02;  // Flip U/L bit
    addr->bytes[9] = mac[1];
    addr->bytes[10] = mac[2];
    addr->bytes[11] = 0xFF;
    addr->bytes[12] = 0xFE;
    addr->bytes[13] = mac[3];
    addr->bytes[14] = mac[4];
    addr->bytes[15] = mac[5];
}

// Static buffer for address string conversion
static char s_addrStringBuf[40];  // Max: 8 groups * 4 chars + 7 colons + null

const char* addr_to_string(const Address* addr)
{
    if (!addr) {
        s_addrStringBuf[0] = '\0';
        return s_addrStringBuf;
    }
    
    const char* hex = "0123456789abcdef";
    char* p = s_addrStringBuf;
    
    for (int i = 0; i < 8; ++i) {
        if (i > 0) *p++ = ':';
        
        uint16_t word = ethernet::ntohs(addr->words[i]);
        
        // Simple formatting (no zero compression)
        *p++ = hex[(word >> 12) & 0xF];
        *p++ = hex[(word >> 8) & 0xF];
        *p++ = hex[(word >> 4) & 0xF];
        *p++ = hex[word & 0xF];
    }
    
    *p = '\0';
    return s_addrStringBuf;
}

bool string_to_addr(const char* str, Address* addr)
{
    if (!str || !addr) return false;
    
    memzero(addr, sizeof(Address));
    
    // Simple parser (doesn't handle :: compression fully)
    int wordIdx = 0;
    uint16_t word = 0;
    int digits = 0;
    
    while (*str && wordIdx < 8) {
        char c = *str++;
        
        if (c == ':') {
            addr->words[wordIdx++] = ethernet::htons(word);
            word = 0;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            word = (word << 4) | (c - '0');
            digits++;
        } else if (c >= 'a' && c <= 'f') {
            word = (word << 4) | (c - 'a' + 10);
            digits++;
        } else if (c >= 'A' && c <= 'F') {
            word = (word << 4) | (c - 'A' + 10);
            digits++;
        } else {
            return false;  // Invalid character
        }
        
        if (digits > 4) return false;
    }
    
    if (wordIdx < 8) {
        addr->words[wordIdx] = ethernet::htons(word);
    }
    
    return true;
}

// ================================================================
// Checksum Calculation
// ================================================================

uint16_t calculate_checksum(const Address* srcAddr, const Address* dstAddr,
                            uint8_t protocol, const uint8_t* data, uint16_t len)
{
    PseudoHeader pseudo;
    memcopy(&pseudo.srcAddr, srcAddr, sizeof(Address));
    memcopy(&pseudo.dstAddr, dstAddr, sizeof(Address));
    pseudo.upperLayerLength = ethernet::htonl(len);
    pseudo.zeros[0] = 0;
    pseudo.zeros[1] = 0;
    pseudo.zeros[2] = 0;
    pseudo.nextHeader = protocol;
    
    uint32_t sum = checksum_add(&pseudo, sizeof(PseudoHeader));
    sum += checksum_add(data, len);
    
    uint16_t result = checksum_fold(sum);
    if (result == 0) result = 0xFFFF;
    
    return result;
}

// ================================================================
// Header Validation
// ================================================================

bool validate_header(const Header* hdr, size_t totalLen)
{
    if (!hdr) return false;
    if (totalLen < HEADER_LEN) return false;
    
    // Check version
    if (get_version(hdr) != IP_VERSION) {
        s_stats.badVersion++;
        return false;
    }
    
    // Check payload length
    uint16_t payloadLen = get_payload_length(hdr);
    if (HEADER_LEN + payloadLen > totalLen) {
        s_stats.badLength++;
        return false;
    }
    
    return true;
}

// ================================================================
// Extension Header Processing
// ================================================================

const uint8_t* skip_extension_headers(const uint8_t* data, size_t len,
                                       uint8_t firstNextHeader, uint8_t* nextProto)
{
    if (!data || !nextProto) return nullptr;
    
    uint8_t nh = firstNextHeader;
    size_t offset = 0;
    
    while (offset < len) {
        switch (nh) {
            case PROTO_HOP_BY_HOP:
            case PROTO_ROUTING:
            case PROTO_DEST_OPTIONS:
            {
                // Variable length extension header
                if (offset + 2 > len) return nullptr;
                
                uint8_t extLen = data[offset + 1];
                size_t hdrLen = (extLen + 1) * 8;
                
                nh = data[offset];
                offset += hdrLen;
                break;
            }
            
            case PROTO_FRAGMENT:
            {
                // Fixed 8-byte fragment header
                if (offset + 8 > len) return nullptr;
                
                nh = data[offset];
                offset += 8;
                break;
            }
            
            case PROTO_AH:
            {
                // Authentication Header
                if (offset + 2 > len) return nullptr;
                
                uint8_t extLen = data[offset + 1];
                size_t hdrLen = (extLen + 2) * 4;
                
                nh = data[offset];
                offset += hdrLen;
                break;
            }
            
            case PROTO_NONE:
                // No next header
                *nextProto = PROTO_NONE;
                return nullptr;
            
            default:
                // Upper-layer protocol
                *nextProto = nh;
                return data + offset;
        }
    }
    
    return nullptr;
}

// ================================================================
// Packet Processing
// ================================================================

void process_packet(const uint8_t* packet, size_t len)
{
    if (!packet || len < HEADER_LEN) {
        s_stats.rxErrors++;
        return;
    }
    
    const Header* hdr = reinterpret_cast<const Header*>(packet);
    
    if (!validate_header(hdr, len)) {
        return;
    }
    
    s_stats.rxPackets++;
    s_stats.rxBytes += len;
    
    // Check hop limit
    if (hdr->hopLimit == 0) {
        s_stats.hopLimitExceeded++;
        // TODO: Send ICMPv6 Time Exceeded
        return;
    }
    
    // Get payload
    const uint8_t* payload = packet + HEADER_LEN;
    size_t payloadLen = get_payload_length(hdr);
    
    // Process extension headers
    uint8_t protocol;
    const uint8_t* upperData = skip_extension_headers(
        payload, payloadLen, hdr->nextHeader, &protocol);
    
    if (!upperData && protocol != PROTO_NONE) {
        s_stats.unknownNextHeader++;
        return;
    }
    
    // Dispatch to protocol handler
    switch (protocol) {
        case PROTO_ICMPV6:
            icmpv6::process_message(&hdr->srcAddr, &hdr->dstAddr,
                                    upperData, payloadLen);
            break;
            
        case PROTO_TCP:
            // TODO: Process TCP
            break;
            
        case PROTO_UDP:
            // TODO: Process UDP
            break;
            
        case PROTO_NONE:
            // Packet consumed by extension headers
            break;
            
        default:
            s_stats.unknownNextHeader++;
            break;
    }
}

// ================================================================
// Packet Transmission
// ================================================================

int send_packet(const Address* dstAddr, uint8_t protocol,
                const uint8_t* payload, uint16_t payloadLen)
{
    if (!dstAddr || (!payload && payloadLen > 0)) {
        return -1;
    }
    
    // Build IPv6 header
    uint8_t packet[MTU_DEFAULT];
    Header* hdr = reinterpret_cast<Header*>(packet);
    
    memzero(hdr, sizeof(Header));
    set_version_class_flow(hdr, 0, 0);
    hdr->payloadLength = ethernet::htons(payloadLen);
    hdr->nextHeader = protocol;
    hdr->hopLimit = s_defaultHopLimit;
    
    // Select source address
    // For now, use first local address or unspecified
    if (s_localAddrCount > 0) {
        memcopy(&hdr->srcAddr, &s_localAddrs[0], sizeof(Address));
    } else {
        memzero(&hdr->srcAddr, sizeof(Address));
    }
    
    // Set destination
    memcopy(&hdr->dstAddr, dstAddr, sizeof(Address));
    
    // Copy payload
    if (payload && payloadLen > 0) {
        if (HEADER_LEN + payloadLen > MTU_DEFAULT) {
            return -2;  // Packet too large
        }
        memcopy(packet + HEADER_LEN, payload, payloadLen);
    }
    
    // TODO: Resolve destination MAC and send via ethernet
    
    s_stats.txPackets++;
    s_stats.txBytes += HEADER_LEN + payloadLen;
    
    return 0;
}

// ================================================================
// Statistics
// ================================================================

void get_statistics(Statistics* stats)
{
    if (stats) {
        *stats = s_stats;
    }
}

void reset_statistics()
{
    memzero(&s_stats, sizeof(s_stats));
}

// ================================================================
// Initialization
// ================================================================

bool init()
{
    if (s_initialized) return true;
    
    memzero(&s_stats, sizeof(s_stats));
    memzero(s_localAddrs, sizeof(s_localAddrs));
    s_localAddrCount = 0;
    s_defaultHopLimit = DEFAULT_HOP_LIMIT;
    
    // Initialize ICMPv6
    icmpv6::init();
    
    kernel::serial::puts("IPv6: Initialized\n");
    
    s_initialized = true;
    return true;
}

} // namespace ipv6
} // namespace kernel
