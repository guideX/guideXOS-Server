// ICMPv6 Implementation
//
// Implements ICMPv6 message handling and Neighbor Discovery Protocol.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/icmpv6.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace icmpv6 {

// ================================================================
// Internal State
// ================================================================

static Statistics s_stats;
static bool s_initialized = false;

// Neighbor Cache
static const int MAX_NEIGHBORS = 32;

struct NeighborEntry {
    ipv6::Address ipAddr;
    uint8_t       macAddr[6];
    uint8_t       state;          // Incomplete, Reachable, Stale, etc.
    uint32_t      lastUsed;       // Timestamp
    bool          isRouter;
};

static NeighborEntry s_neighborCache[MAX_NEIGHBORS];
static int s_neighborCount = 0;

// Default Router List
static const int MAX_ROUTERS = 4;

struct RouterEntry {
    ipv6::Address addr;
    uint16_t      lifetime;       // Seconds remaining
    uint8_t       preference;     // Router preference
};

static RouterEntry s_routers[MAX_ROUTERS];
static int s_routerCount = 0;

// Neighbor states
static const uint8_t NEIGHBOR_INCOMPLETE = 0;
static const uint8_t NEIGHBOR_REACHABLE  = 1;
static const uint8_t NEIGHBOR_STALE      = 2;
static const uint8_t NEIGHBOR_DELAY      = 3;
static const uint8_t NEIGHBOR_PROBE      = 4;

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
// Checksum
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

uint16_t calculate_checksum(const ipv6::Address* srcAddr,
                            const ipv6::Address* dstAddr,
                            const uint8_t* data, size_t len)
{
    return ipv6::calculate_checksum(srcAddr, dstAddr, ipv6::PROTO_ICMPV6, data, len);
}

bool verify_checksum(const ipv6::Address* srcAddr,
                     const ipv6::Address* dstAddr,
                     const uint8_t* data, size_t len)
{
    if (!srcAddr || !dstAddr || !data || len < sizeof(Header)) {
        return false;
    }
    
    const Header* hdr = reinterpret_cast<const Header*>(data);
    if (hdr->checksum == 0) return false;
    
    // Calculate checksum over entire message
    ipv6::PseudoHeader pseudo;
    memcopy(&pseudo.srcAddr, srcAddr, sizeof(ipv6::Address));
    memcopy(&pseudo.dstAddr, dstAddr, sizeof(ipv6::Address));
    pseudo.upperLayerLength = ethernet::htonl(static_cast<uint32_t>(len));
    pseudo.zeros[0] = 0;
    pseudo.zeros[1] = 0;
    pseudo.zeros[2] = 0;
    pseudo.nextHeader = ipv6::PROTO_ICMPV6;
    
    uint32_t sum = checksum_add(&pseudo, sizeof(pseudo));
    sum += checksum_add(data, len);
    
    return checksum_fold(sum) == 0;
}

// ================================================================
// Neighbor Cache Management
// ================================================================

static NeighborEntry* find_neighbor(const ipv6::Address* addr)
{
    for (int i = 0; i < s_neighborCount; ++i) {
        if (ipv6::addr_equals(&s_neighborCache[i].ipAddr, addr)) {
            return &s_neighborCache[i];
        }
    }
    return nullptr;
}

static NeighborEntry* add_neighbor(const ipv6::Address* addr, const uint8_t* mac)
{
    // Find existing or free slot
    NeighborEntry* entry = find_neighbor(addr);
    
    if (!entry && s_neighborCount < MAX_NEIGHBORS) {
        entry = &s_neighborCache[s_neighborCount++];
        memzero(entry, sizeof(NeighborEntry));
        memcopy(&entry->ipAddr, addr, sizeof(ipv6::Address));
    }
    
    if (entry && mac) {
        memcopy(entry->macAddr, mac, 6);
        entry->state = NEIGHBOR_REACHABLE;
    }
    
    return entry;
}

// ================================================================
// Message Processing
// ================================================================

void process_message(const ipv6::Address* srcAddr, const ipv6::Address* dstAddr,
                     const uint8_t* data, size_t len)
{
    if (!srcAddr || !dstAddr || !data || len < sizeof(Header)) {
        return;
    }
    
    // Verify checksum
    if (!verify_checksum(srcAddr, dstAddr, data, len)) {
        s_stats.checksumErrors++;
        return;
    }
    
    const Header* hdr = reinterpret_cast<const Header*>(data);
    
    switch (hdr->type) {
        case TYPE_ECHO_REQUEST:
            s_stats.echoRequests++;
            // Send echo reply
            if (len >= sizeof(EchoMessage)) {
                const EchoMessage* echo = reinterpret_cast<const EchoMessage*>(data);
                size_t dataLen = len - sizeof(EchoMessage);
                send_echo_reply(srcAddr, ethernet::ntohs(echo->identifier),
                               ethernet::ntohs(echo->sequence),
                               echo->data, dataLen);
            }
            break;
            
        case TYPE_ECHO_REPLY:
            s_stats.echoReplies++;
            // TODO: Handle echo reply (for ping implementation)
            break;
            
        case TYPE_DEST_UNREACHABLE:
            s_stats.destUnreachables++;
            break;
            
        case TYPE_PACKET_TOO_BIG:
            s_stats.packetTooBig++;
            // TODO: Update path MTU
            break;
            
        case TYPE_TIME_EXCEEDED:
            s_stats.timeExceeded++;
            break;
            
        case TYPE_PARAMETER_PROBLEM:
            s_stats.parameterProblems++;
            break;
            
        case TYPE_ROUTER_SOLICITATION:
            s_stats.routerSolicitations++;
            // Only routers process this
            break;
            
        case TYPE_ROUTER_ADVERTISEMENT:
            s_stats.routerAdvertisements++;
            if (len >= sizeof(RouterAdvertisement)) {
                process_router_advertisement(srcAddr,
                    reinterpret_cast<const RouterAdvertisement*>(data), len);
            }
            break;
            
        case TYPE_NEIGHBOR_SOLICITATION:
            s_stats.neighborSolicitations++;
            if (len >= sizeof(NeighborSolicitation)) {
                process_neighbor_solicitation(srcAddr,
                    reinterpret_cast<const NeighborSolicitation*>(data), len);
            }
            break;
            
        case TYPE_NEIGHBOR_ADVERTISEMENT:
            s_stats.neighborAdvertisements++;
            if (len >= sizeof(NeighborAdvertisement)) {
                process_neighbor_advertisement(srcAddr,
                    reinterpret_cast<const NeighborAdvertisement*>(data), len);
            }
            break;
            
        case TYPE_REDIRECT:
            s_stats.redirects++;
            break;
            
        default:
            s_stats.unknownTypes++;
            break;
    }
}

// ================================================================
// Echo Messages
// ================================================================

int send_echo_request(const ipv6::Address* dstAddr, uint16_t identifier,
                      uint16_t sequence, const void* data, size_t dataLen)
{
    if (!dstAddr) return -1;
    
    uint8_t buffer[128];
    EchoMessage* msg = reinterpret_cast<EchoMessage*>(buffer);
    
    size_t totalLen = sizeof(EchoMessage) + dataLen;
    if (totalLen > sizeof(buffer)) return -2;
    
    msg->type = TYPE_ECHO_REQUEST;
    msg->code = 0;
    msg->checksum = 0;
    msg->identifier = ethernet::htons(identifier);
    msg->sequence = ethernet::htons(sequence);
    
    if (data && dataLen > 0) {
        memcopy(msg->data, data, dataLen);
    }
    
    // TODO: Calculate checksum with source address
    // msg->checksum = calculate_checksum(srcAddr, dstAddr, buffer, totalLen);
    
    return ipv6::send_packet(dstAddr, ipv6::PROTO_ICMPV6, buffer, totalLen);
}

int send_echo_reply(const ipv6::Address* dstAddr, uint16_t identifier,
                    uint16_t sequence, const void* data, size_t dataLen)
{
    if (!dstAddr) return -1;
    
    uint8_t buffer[128];
    EchoMessage* msg = reinterpret_cast<EchoMessage*>(buffer);
    
    size_t totalLen = sizeof(EchoMessage) + dataLen;
    if (totalLen > sizeof(buffer)) return -2;
    
    msg->type = TYPE_ECHO_REPLY;
    msg->code = 0;
    msg->checksum = 0;
    msg->identifier = ethernet::htons(identifier);
    msg->sequence = ethernet::htons(sequence);
    
    if (data && dataLen > 0) {
        memcopy(msg->data, data, dataLen);
    }
    
    return ipv6::send_packet(dstAddr, ipv6::PROTO_ICMPV6, buffer, totalLen);
}

// ================================================================
// Error Messages
// ================================================================

int send_dest_unreachable(const ipv6::Address* dstAddr, uint8_t code,
                          const uint8_t* invoking, size_t invokingLen)
{
    if (!dstAddr) return -1;
    
    uint8_t buffer[1280];  // Minimum MTU
    DestUnreachable* msg = reinterpret_cast<DestUnreachable*>(buffer);
    
    // Limit invoking packet to fit in minimum MTU
    size_t maxInvoking = 1280 - 40 - 8;  // IPv6 header + ICMPv6 header
    if (invokingLen > maxInvoking) invokingLen = maxInvoking;
    
    size_t totalLen = sizeof(DestUnreachable) + invokingLen;
    
    msg->type = TYPE_DEST_UNREACHABLE;
    msg->code = code;
    msg->checksum = 0;
    msg->unused = 0;
    
    if (invoking && invokingLen > 0) {
        memcopy(msg->invoking, invoking, invokingLen);
    }
    
    return ipv6::send_packet(dstAddr, ipv6::PROTO_ICMPV6, buffer, totalLen);
}

int send_packet_too_big(const ipv6::Address* dstAddr, uint32_t mtu,
                        const uint8_t* invoking, size_t invokingLen)
{
    if (!dstAddr) return -1;
    
    uint8_t buffer[1280];
    PacketTooBig* msg = reinterpret_cast<PacketTooBig*>(buffer);
    
    size_t maxInvoking = 1280 - 40 - 8;
    if (invokingLen > maxInvoking) invokingLen = maxInvoking;
    
    size_t totalLen = sizeof(PacketTooBig) + invokingLen;
    
    msg->type = TYPE_PACKET_TOO_BIG;
    msg->code = 0;
    msg->checksum = 0;
    msg->mtu = ethernet::htonl(mtu);
    
    if (invoking && invokingLen > 0) {
        memcopy(msg->invoking, invoking, invokingLen);
    }
    
    return ipv6::send_packet(dstAddr, ipv6::PROTO_ICMPV6, buffer, totalLen);
}

int send_time_exceeded(const ipv6::Address* dstAddr, uint8_t code,
                       const uint8_t* invoking, size_t invokingLen)
{
    if (!dstAddr) return -1;
    
    uint8_t buffer[1280];
    TimeExceeded* msg = reinterpret_cast<TimeExceeded*>(buffer);
    
    size_t maxInvoking = 1280 - 40 - 8;
    if (invokingLen > maxInvoking) invokingLen = maxInvoking;
    
    size_t totalLen = sizeof(TimeExceeded) + invokingLen;
    
    msg->type = TYPE_TIME_EXCEEDED;
    msg->code = code;
    msg->checksum = 0;
    msg->unused = 0;
    
    if (invoking && invokingLen > 0) {
        memcopy(msg->invoking, invoking, invokingLen);
    }
    
    return ipv6::send_packet(dstAddr, ipv6::PROTO_ICMPV6, buffer, totalLen);
}

// ================================================================
// Neighbor Discovery
// ================================================================

int send_router_solicitation()
{
    uint8_t buffer[64];
    RouterSolicitation* rs = reinterpret_cast<RouterSolicitation*>(buffer);
    
    memzero(rs, sizeof(RouterSolicitation));
    rs->type = TYPE_ROUTER_SOLICITATION;
    rs->code = 0;
    
    // Add source link-layer address option if we have one
    // TODO: Add option
    
    size_t totalLen = sizeof(RouterSolicitation);
    
    // Send to all-routers multicast
    return ipv6::send_packet(&ipv6::ADDR_ALL_ROUTERS_LINK, ipv6::PROTO_ICMPV6,
                             buffer, totalLen);
}

int send_neighbor_solicitation(const ipv6::Address* targetAddr)
{
    if (!targetAddr) return -1;
    
    uint8_t buffer[64];
    NeighborSolicitation* ns = reinterpret_cast<NeighborSolicitation*>(buffer);
    
    memzero(ns, sizeof(NeighborSolicitation));
    ns->type = TYPE_NEIGHBOR_SOLICITATION;
    ns->code = 0;
    memcopy(&ns->targetAddr, targetAddr, sizeof(ipv6::Address));
    
    // TODO: Add source link-layer address option
    
    size_t totalLen = sizeof(NeighborSolicitation);
    
    // Send to solicited-node multicast
    ipv6::Address solicited;
    ipv6::make_solicited_node_multicast(targetAddr, &solicited);
    
    return ipv6::send_packet(&solicited, ipv6::PROTO_ICMPV6, buffer, totalLen);
}

int send_neighbor_advertisement(const ipv6::Address* dstAddr,
                                const ipv6::Address* targetAddr,
                                bool router, bool solicited, bool override)
{
    if (!dstAddr || !targetAddr) return -1;
    
    uint8_t buffer[64];
    NeighborAdvertisement* na = reinterpret_cast<NeighborAdvertisement*>(buffer);
    
    memzero(na, sizeof(NeighborAdvertisement));
    na->type = TYPE_NEIGHBOR_ADVERTISEMENT;
    na->code = 0;
    
    uint32_t flags = 0;
    if (router) flags |= NA_FLAG_ROUTER;
    if (solicited) flags |= NA_FLAG_SOLICITED;
    if (override) flags |= NA_FLAG_OVERRIDE;
    na->flags = ethernet::htonl(flags);
    
    memcopy(&na->targetAddr, targetAddr, sizeof(ipv6::Address));
    
    // TODO: Add target link-layer address option
    
    size_t totalLen = sizeof(NeighborAdvertisement);
    
    return ipv6::send_packet(dstAddr, ipv6::PROTO_ICMPV6, buffer, totalLen);
}

void process_router_advertisement(const ipv6::Address* srcAddr,
                                  const RouterAdvertisement* ra, size_t len)
{
    if (!srcAddr || !ra) return;
    if (len < sizeof(RouterAdvertisement)) return;
    
    // Extract router information
    uint16_t lifetime = ethernet::ntohs(ra->routerLifetime);
    
    // Add or update router entry
    RouterEntry* router = nullptr;
    for (int i = 0; i < s_routerCount; ++i) {
        if (ipv6::addr_equals(&s_routers[i].addr, srcAddr)) {
            router = &s_routers[i];
            break;
        }
    }
    
    if (!router && s_routerCount < MAX_ROUTERS && lifetime > 0) {
        router = &s_routers[s_routerCount++];
        memcopy(&router->addr, srcAddr, sizeof(ipv6::Address));
    }
    
    if (router) {
        router->lifetime = lifetime;
        router->preference = (ra->flags >> 3) & 0x03;
        
        if (lifetime == 0) {
            // Router is no longer default
            // TODO: Remove from list
        }
    }
    
    // Process options
    const uint8_t* opt = ra->options;
    size_t optLen = len - sizeof(RouterAdvertisement);
    
    while (optLen >= 8) {
        uint8_t type = opt[0];
        uint8_t length = opt[1];
        
        if (length == 0) break;
        
        size_t optSize = length * 8;
        if (optSize > optLen) break;
        
        switch (type) {
            case OPT_PREFIX_INFO:
                // TODO: Process prefix information for SLAAC
                break;
                
            case OPT_MTU:
                // TODO: Update link MTU
                break;
                
            case OPT_RDNSS:
                // TODO: Configure DNS servers
                break;
        }
        
        opt += optSize;
        optLen -= optSize;
    }
}

void process_neighbor_solicitation(const ipv6::Address* srcAddr,
                                   const NeighborSolicitation* ns, size_t len)
{
    if (!srcAddr || !ns) return;
    if (len < sizeof(NeighborSolicitation)) return;
    
    // Check if target is our address
    // TODO: Compare against our local addresses
    
    // Process options (look for source link-layer address)
    const uint8_t* opt = ns->options;
    size_t optLen = len - sizeof(NeighborSolicitation);
    const uint8_t* srcLLAddr = nullptr;
    
    while (optLen >= 8) {
        uint8_t type = opt[0];
        uint8_t length = opt[1];
        
        if (length == 0) break;
        
        size_t optSize = length * 8;
        if (optSize > optLen) break;
        
        if (type == OPT_SOURCE_LL_ADDR && length == 1) {
            srcLLAddr = opt + 2;
        }
        
        opt += optSize;
        optLen -= optSize;
    }
    
    // Update neighbor cache
    if (srcLLAddr && !ipv6::is_unspecified(srcAddr)) {
        add_neighbor(srcAddr, srcLLAddr);
    }
    
    // Send Neighbor Advertisement
    // TODO: Only if target is our address
    // send_neighbor_advertisement(srcAddr, &ns->targetAddr, false, true, true);
}

void process_neighbor_advertisement(const ipv6::Address* srcAddr,
                                    const NeighborAdvertisement* na, size_t len)
{
    if (!srcAddr || !na) return;
    if (len < sizeof(NeighborAdvertisement)) return;
    
    uint32_t flags = ethernet::ntohl(na->flags);
    bool isRouter = (flags & NA_FLAG_ROUTER) != 0;
    bool solicited = (flags & NA_FLAG_SOLICITED) != 0;
    bool override = (flags & NA_FLAG_OVERRIDE) != 0;
    
    // Process options (look for target link-layer address)
    const uint8_t* opt = na->options;
    size_t optLen = len - sizeof(NeighborAdvertisement);
    const uint8_t* targetLLAddr = nullptr;
    
    while (optLen >= 8) {
        uint8_t type = opt[0];
        uint8_t length = opt[1];
        
        if (length == 0) break;
        
        size_t optSize = length * 8;
        if (optSize > optLen) break;
        
        if (type == OPT_TARGET_LL_ADDR && length == 1) {
            targetLLAddr = opt + 2;
        }
        
        opt += optSize;
        optLen -= optSize;
    }
    
    // Update neighbor cache
    NeighborEntry* entry = find_neighbor(&na->targetAddr);
    
    if (entry) {
        if (override || solicited) {
            if (targetLLAddr) {
                memcopy(entry->macAddr, targetLLAddr, 6);
            }
            entry->state = NEIGHBOR_REACHABLE;
        }
        entry->isRouter = isRouter;
    } else if (targetLLAddr) {
        entry = add_neighbor(&na->targetAddr, targetLLAddr);
        if (entry) {
            entry->isRouter = isRouter;
        }
    }
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

// ================================================================
// Initialization
// ================================================================

bool init()
{
    if (s_initialized) return true;
    
    memzero(&s_stats, sizeof(s_stats));
    memzero(s_neighborCache, sizeof(s_neighborCache));
    memzero(s_routers, sizeof(s_routers));
    s_neighborCount = 0;
    s_routerCount = 0;
    
    serial_debug::print("ICMPv6: Initialized\n");
    
    s_initialized = true;
    return true;
}

} // namespace icmpv6
} // namespace kernel
