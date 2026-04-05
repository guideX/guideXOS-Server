//
// DNS Client Implementation for guideXOS
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/dns.h"
#include "include/kernel/udp.h"
#include "include/kernel/socket.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace dns {

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

static uint32_t strlen_safe(const char* s)
{
    if (!s) return 0;
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static void strcpy_safe(char* dst, const char* src, uint32_t maxLen)
{
    if (!dst || !src || maxLen == 0) return;
    uint32_t i = 0;
    while (src[i] && i < maxLen - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool str_eq_case_insensitive(const char* a, const char* b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        // Convert to lowercase
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == *b;
}

// ================================================================
// Internal state
// ================================================================

static uint32_t s_dnsServer = DNS_GOOGLE_PRIMARY;  // Default to Google DNS
static CacheEntry s_cache[MAX_CACHE_ENTRIES];
static Statistics s_stats;
static uint16_t s_nextQueryId = 1;
static uint32_t s_systemTicks = 0;  // Simple tick counter for TTL

// ================================================================
// Byte order helpers (use ethernet's ntohs/htons)
// ================================================================

static inline uint16_t dns_htons(uint16_t val)
{
    return ethernet::htons(val);
}

static inline uint16_t dns_ntohs(uint16_t val)
{
    return ethernet::ntohs(val);
}

static inline uint32_t dns_htonl(uint32_t val)
{
    return ethernet::htonl(val);
}

static inline uint32_t dns_ntohl(uint32_t val)
{
    return ethernet::ntohl(val);
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    memzero(s_cache, sizeof(s_cache));
    memzero(&s_stats, sizeof(s_stats));
    s_nextQueryId = 1;
    s_systemTicks = 0;
    
    // Use configured DNS server if available
    const ipv4::NetworkConfig* cfg = ipv4::get_config();
    if (cfg && cfg->configured && cfg->dns != 0) {
        s_dnsServer = cfg->dns;
    }
    
    serial::puts("[DNS] Client initialized, server: ");
    char ipStr[16];
    ipv4::ip_to_string(s_dnsServer, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');
}

// ================================================================
// DNS Server Configuration
// ================================================================

void set_server(uint32_t serverIP)
{
    s_dnsServer = serverIP;
    
    serial::puts("[DNS] Server set to: ");
    char ipStr[16];
    ipv4::ip_to_string(s_dnsServer, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');
}

uint32_t get_server()
{
    return s_dnsServer;
}

// ================================================================
// Domain Name Encoding
// ================================================================

uint16_t encode_domain(const char* domain, uint8_t* buffer, uint16_t bufferSize)
{
    if (!domain || !buffer || bufferSize < 2) return 0;
    
    uint32_t domainLen = strlen_safe(domain);
    if (domainLen == 0 || domainLen > MAX_DOMAIN_LEN) return 0;
    
    uint16_t pos = 0;
    uint16_t labelStart = 0;
    
    for (uint32_t i = 0; i <= domainLen; ++i) {
        if (domain[i] == '.' || domain[i] == '\0') {
            uint16_t labelLen = i - labelStart;
            
            if (labelLen == 0 && domain[i] != '\0') {
                // Empty label (consecutive dots)
                return 0;
            }
            
            if (labelLen > MAX_LABEL_LEN) {
                // Label too long
                return 0;
            }
            
            if (labelLen > 0) {
                if (pos + labelLen + 1 >= bufferSize) {
                    // Buffer overflow
                    return 0;
                }
                
                // Write label length
                buffer[pos++] = static_cast<uint8_t>(labelLen);
                
                // Write label
                for (uint16_t j = 0; j < labelLen; ++j) {
                    buffer[pos++] = domain[labelStart + j];
                }
            }
            
            labelStart = i + 1;
        }
    }
    
    // Write terminating zero
    if (pos >= bufferSize) return 0;
    buffer[pos++] = 0;
    
    return pos;
}

// ================================================================
// Domain Name Decoding
// ================================================================

uint16_t decode_domain(const uint8_t* packet, uint16_t packetLen,
                       uint16_t offset, char* domain, uint16_t domainSize)
{
    if (!packet || !domain || domainSize == 0) return 0;
    
    domain[0] = '\0';
    uint16_t domainPos = 0;
    uint16_t currentOffset = offset;
    uint16_t bytesRead = 0;
    bool jumped = false;
    uint8_t jumpCount = 0;  // Prevent infinite loops
    
    while (currentOffset < packetLen) {
        uint8_t labelLen = packet[currentOffset];
        
        if (labelLen == 0) {
            // End of domain name
            if (!jumped) bytesRead = currentOffset - offset + 1;
            break;
        }
        
        // Check for compression pointer (bits 11xxxxxx)
        if ((labelLen & 0xC0) == 0xC0) {
            if (currentOffset + 1 >= packetLen) return 0;
            
            // Get pointer offset
            uint16_t pointerOffset = ((labelLen & 0x3F) << 8) | packet[currentOffset + 1];
            
            if (pointerOffset >= packetLen) return 0;
            
            if (!jumped) {
                bytesRead = currentOffset - offset + 2;
                jumped = true;
            }
            
            currentOffset = pointerOffset;
            
            // Prevent infinite loops
            if (++jumpCount > 64) return 0;
            continue;
        }
        
        // Regular label
        if (labelLen > MAX_LABEL_LEN) return 0;
        if (currentOffset + 1 + labelLen > packetLen) return 0;
        
        // Add dot separator if not first label
        if (domainPos > 0) {
            if (domainPos >= domainSize - 1) return 0;
            domain[domainPos++] = '.';
        }
        
        // Copy label
        for (uint8_t i = 0; i < labelLen; ++i) {
            if (domainPos >= domainSize - 1) return 0;
            domain[domainPos++] = packet[currentOffset + 1 + i];
        }
        
        currentOffset += 1 + labelLen;
    }
    
    domain[domainPos] = '\0';
    
    if (!jumped) {
        bytesRead = currentOffset - offset + 1;
    }
    
    return bytesRead;
}

// ================================================================
// DNS Query Building
// ================================================================

Status build_query(uint8_t* buffer, uint16_t bufferSize,
                   const char* domain, RecordType type,
                   uint16_t queryId, uint16_t* packetLen)
{
    if (!buffer || !domain || !packetLen) return DNS_ERR_INVALID;
    if (bufferSize < sizeof(Header) + 4) return DNS_ERR_INVALID;
    
    *packetLen = 0;
    
    // Build header
    Header* hdr = reinterpret_cast<Header*>(buffer);
    memzero(hdr, sizeof(Header));
    
    hdr->id = dns_htons(queryId);
    hdr->flags = dns_htons(FLAG_RD);  // Recursion Desired
    hdr->qdcount = dns_htons(1);      // One question
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    
    uint16_t pos = sizeof(Header);
    
    // Encode domain name
    uint16_t nameLen = encode_domain(domain, buffer + pos, bufferSize - pos);
    if (nameLen == 0) return DNS_ERR_TOOLONG;
    pos += nameLen;
    
    // Add QTYPE
    if (pos + 4 > bufferSize) return DNS_ERR_INVALID;
    buffer[pos++] = static_cast<uint8_t>((type >> 8) & 0xFF);
    buffer[pos++] = static_cast<uint8_t>(type & 0xFF);
    
    // Add QCLASS (IN = Internet)
    buffer[pos++] = 0;
    buffer[pos++] = CLASS_IN;
    
    *packetLen = pos;
    return DNS_OK;
}

// ================================================================
// DNS Response Parsing
// ================================================================

Status parse_response(const uint8_t* packet, uint16_t packetLen,
                      uint16_t expectedId, QueryResult* result)
{
    if (!packet || !result) return DNS_ERR_INVALID;
    if (packetLen < sizeof(Header)) return DNS_ERR_FORMAT;
    
    memzero(result, sizeof(QueryResult));
    
    const Header* hdr = reinterpret_cast<const Header*>(packet);
    
    // Check transaction ID
    uint16_t id = dns_ntohs(hdr->id);
    if (id != expectedId) {
        strcpy_safe(result->errorMsg, "Transaction ID mismatch", 64);
        return DNS_ERR_FORMAT;
    }
    
    // Check QR flag (must be response)
    uint16_t flags = dns_ntohs(hdr->flags);
    if ((flags & FLAG_QR_RESPONSE) == 0) {
        strcpy_safe(result->errorMsg, "Not a response", 64);
        return DNS_ERR_FORMAT;
    }
    
    // Check response code
    result->rcode = flags & RCODE_MASK;
    
    if (result->rcode == RCODE_NXDOMAIN) {
        strcpy_safe(result->errorMsg, "Domain not found", 64);
        return DNS_ERR_NXDOMAIN;
    } else if (result->rcode == RCODE_SERVFAIL) {
        strcpy_safe(result->errorMsg, "Server failure", 64);
        return DNS_ERR_SERVFAIL;
    } else if (result->rcode == RCODE_REFUSED) {
        strcpy_safe(result->errorMsg, "Query refused", 64);
        return DNS_ERR_REFUSED;
    } else if (result->rcode != RCODE_NOERROR) {
        strcpy_safe(result->errorMsg, "DNS error", 64);
        return DNS_ERR_FORMAT;
    }
    
    uint16_t qdcount = dns_ntohs(hdr->qdcount);
    uint16_t ancount = dns_ntohs(hdr->ancount);
    
    // Skip questions section
    uint16_t pos = sizeof(Header);
    for (uint16_t i = 0; i < qdcount; ++i) {
        // Skip QNAME
        while (pos < packetLen) {
            uint8_t labelLen = packet[pos];
            if (labelLen == 0) {
                pos++;
                break;
            }
            if ((labelLen & 0xC0) == 0xC0) {
                pos += 2;  // Compression pointer
                break;
            }
            pos += 1 + labelLen;
        }
        pos += 4;  // QTYPE + QCLASS
    }
    
    // Parse answers
    result->answerCount = 0;
    
    for (uint16_t i = 0; i < ancount && result->answerCount < MAX_ANSWERS; ++i) {
        if (pos >= packetLen) break;
        
        ResourceRecord* rr = &result->answers[result->answerCount];
        memzero(rr, sizeof(ResourceRecord));
        
        // Decode name
        uint16_t nameLen = decode_domain(packet, packetLen, pos, rr->name, MAX_DOMAIN_LEN + 1);
        if (nameLen == 0) break;
        pos += nameLen;
        
        // Check remaining bytes
        if (pos + 10 > packetLen) break;
        
        // Read TYPE, CLASS, TTL, RDLENGTH
        rr->type = (packet[pos] << 8) | packet[pos + 1];
        pos += 2;
        rr->rclass = (packet[pos] << 8) | packet[pos + 1];
        pos += 2;
        rr->ttl = (packet[pos] << 24) | (packet[pos + 1] << 16) |
                  (packet[pos + 2] << 8) | packet[pos + 3];
        pos += 4;
        rr->rdlength = (packet[pos] << 8) | packet[pos + 1];
        pos += 2;
        
        if (pos + rr->rdlength > packetLen) break;
        
        // Parse record data based on type
        switch (rr->type) {
            case TYPE_A:
                if (rr->rdlength == 4) {
                    rr->data.ipv4 = (packet[pos] << 24) |
                                    (packet[pos + 1] << 16) |
                                    (packet[pos + 2] << 8) |
                                    packet[pos + 3];
                }
                break;
                
            case TYPE_AAAA:
                if (rr->rdlength == 16) {
                    memcopy(rr->data.ipv6, packet + pos, 16);
                }
                break;
                
            case TYPE_CNAME:
            case TYPE_NS:
            case TYPE_PTR:
                decode_domain(packet, packetLen, pos, rr->data.cname, MAX_DOMAIN_LEN + 1);
                break;
                
            default:
                // Copy raw data
                if (rr->rdlength <= sizeof(rr->data.raw)) {
                    memcopy(rr->data.raw, packet + pos, rr->rdlength);
                }
                break;
        }
        
        pos += rr->rdlength;
        result->answerCount++;
    }
    
    result->success = (result->answerCount > 0);
    if (!result->success) {
        strcpy_safe(result->errorMsg, "No answers in response", 64);
        return DNS_ERR_NOTFOUND;
    }
    
    return DNS_OK;
}

// ================================================================
// DNS Cache
// ================================================================

Status cache_lookup(const char* domain, uint32_t* ipv4)
{
    if (!domain || !ipv4) return DNS_ERR_INVALID;
    
    for (uint8_t i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        CacheEntry* entry = &s_cache[i];
        
        if (!entry->valid) continue;
        
        // Check expiration
        if (s_systemTicks > entry->expireTime) {
            entry->valid = false;
            continue;
        }
        
        // Case-insensitive domain comparison
        if (str_eq_case_insensitive(entry->domain, domain)) {
            *ipv4 = entry->ipv4;
            s_stats.cacheHits++;
            return DNS_OK;
        }
    }
    
    s_stats.cacheMisses++;
    return DNS_ERR_NOCACHE;
}

void cache_add(const char* domain, uint32_t ipv4, uint32_t ttl)
{
    if (!domain) return;
    
    // Find existing entry or empty slot
    CacheEntry* slot = nullptr;
    CacheEntry* oldest = &s_cache[0];
    
    for (uint8_t i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        CacheEntry* entry = &s_cache[i];
        
        // Check for existing entry
        if (entry->valid && str_eq_case_insensitive(entry->domain, domain)) {
            slot = entry;
            break;
        }
        
        // Track empty slot
        if (!entry->valid && !slot) {
            slot = entry;
        }
        
        // Track oldest entry for LRU eviction
        if (entry->expireTime < oldest->expireTime) {
            oldest = entry;
        }
    }
    
    // Use oldest entry if no empty slot
    if (!slot) {
        slot = oldest;
    }
    
    // Update entry
    strcpy_safe(slot->domain, domain, MAX_DOMAIN_LEN + 1);
    slot->ipv4 = ipv4;
    slot->expireTime = s_systemTicks + (ttl > 0 ? ttl : DEFAULT_TTL);
    slot->valid = true;
    
    serial::puts("[DNS] Cached: ");
    serial::puts(domain);
    serial::puts(" -> ");
    char ipStr[16];
    ipv4::ip_to_string(ipv4, ipStr);
    serial::puts(ipStr);
    serial::putc('\n');
}

void cache_flush()
{
    for (uint8_t i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        s_cache[i].valid = false;
    }
    
    serial::puts("[DNS] Cache flushed\n");
}

uint32_t cache_size()
{
    uint32_t count = 0;
    for (uint8_t i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        if (s_cache[i].valid && s_systemTicks <= s_cache[i].expireTime) {
            count++;
        }
    }
    return count;
}

// ================================================================
// DNS Resolution
// ================================================================

Status resolve(const char* domain, uint32_t* ipv4)
{
    if (!domain || !ipv4) return DNS_ERR_INVALID;
    
    *ipv4 = 0;
    
    // Check cache first
    if (cache_lookup(domain, ipv4) == DNS_OK) {
        return DNS_OK;
    }
    
    // Check if domain is actually an IP address
    uint32_t directIP;
    if (ipv4::ip_from_string(domain, &directIP)) {
        *ipv4 = directIP;
        return DNS_OK;
    }
    
    // Need to perform DNS query
    if (!ipv4::is_configured()) {
        return DNS_ERR_NETWORK;
    }
    
    // Build query packet
    uint8_t queryPacket[MAX_PACKET_SIZE];
    uint16_t queryLen;
    uint16_t queryId = s_nextQueryId++;
    
    Status status = build_query(queryPacket, MAX_PACKET_SIZE, domain, TYPE_A, queryId, &queryLen);
    if (status != DNS_OK) {
        s_stats.errors++;
        return status;
    }
    
    // Create UDP socket
    int sock = socket::udp_socket();
    if (sock < 0) {
        s_stats.errors++;
        return DNS_ERR_NETWORK;
    }
    
    // Bind to ephemeral port
    uint16_t localPort = udp::alloc_ephemeral_port();
    if (localPort == 0) {
        socket::udp_close(sock);
        s_stats.errors++;
        return DNS_ERR_NETWORK;
    }
    
    socket::udp_bind(sock, localPort);
    
    // Try up to MAX_RETRIES times
    uint8_t responsePacket[MAX_PACKET_SIZE];
    bool gotResponse = false;
    QueryResult result;
    
    for (uint8_t attempt = 0; attempt < MAX_RETRIES && !gotResponse; ++attempt) {
        // Send query
        socket::SockAddr dnsAddr = socket::make_sockaddr(s_dnsServer, DNS_PORT);
        int sent = socket::udp_sendto(sock, queryPacket, queryLen, &dnsAddr);
        
        if (sent < 0) {
            continue;
        }
        
        s_stats.queriesSent++;
        
        // Wait for response (simplified polling)
        uint32_t waitCount = 0;
        const uint32_t maxWait = 300000;  // Approximate timeout
        
        while (waitCount < maxWait && !gotResponse) {
            socket::SockAddr fromAddr;
            int recvLen = socket::udp_recvfrom(sock, responsePacket, MAX_PACKET_SIZE, &fromAddr);
            
            if (recvLen > 0) {
                s_stats.responsesReceived++;
                
                // Parse response
                status = parse_response(responsePacket, recvLen, queryId, &result);
                if (status == DNS_OK) {
                    gotResponse = true;
                }
            }
            
            // Small delay
            for (volatile int d = 0; d < 100; ++d) {}
            waitCount++;
        }
        
        if (!gotResponse) {
            s_stats.timeouts++;
        }
    }
    
    // Cleanup
    socket::udp_close(sock);
    udp::free_ephemeral_port(localPort);
    
    if (!gotResponse) {
        return DNS_ERR_TIMEOUT;
    }
    
    // Find A record in answers
    for (uint8_t i = 0; i < result.answerCount; ++i) {
        if (result.answers[i].type == TYPE_A) {
            *ipv4 = result.answers[i].data.ipv4;
            
            // Add to cache
            cache_add(domain, *ipv4, result.answers[i].ttl);
            
            return DNS_OK;
        }
    }
    
    // Check for CNAME and follow it
    for (uint8_t i = 0; i < result.answerCount; ++i) {
        if (result.answers[i].type == TYPE_CNAME) {
            // Recursively resolve CNAME (with depth limit)
            static uint8_t cnameDepth = 0;
            if (cnameDepth < 5) {
                cnameDepth++;
                status = resolve(result.answers[i].data.cname, ipv4);
                cnameDepth--;
                
                if (status == DNS_OK) {
                    // Cache original domain as well
                    cache_add(domain, *ipv4, result.answers[i].ttl);
                }
                return status;
            }
        }
    }
    
    return DNS_ERR_NOTFOUND;
}

Status resolve_full(const char* domain, RecordType type, QueryResult* result)
{
    if (!domain || !result) return DNS_ERR_INVALID;
    
    memzero(result, sizeof(QueryResult));
    
    // Check if domain is actually an IP address (only for A queries)
    if (type == TYPE_A) {
        uint32_t directIP;
        if (ipv4::ip_from_string(domain, &directIP)) {
            result->success = true;
            result->answerCount = 1;
            result->answers[0].type = TYPE_A;
            result->answers[0].data.ipv4 = directIP;
            strcpy_safe(result->answers[0].name, domain, MAX_DOMAIN_LEN + 1);
            return DNS_OK;
        }
    }
    
    if (!ipv4::is_configured()) {
        strcpy_safe(result->errorMsg, "Network not configured", 64);
        return DNS_ERR_NETWORK;
    }
    
    // Build query packet
    uint8_t queryPacket[MAX_PACKET_SIZE];
    uint16_t queryLen;
    uint16_t queryId = s_nextQueryId++;
    
    Status status = build_query(queryPacket, MAX_PACKET_SIZE, domain, type, queryId, &queryLen);
    if (status != DNS_OK) {
        strcpy_safe(result->errorMsg, "Failed to build query", 64);
        s_stats.errors++;
        return status;
    }
    
    // Create UDP socket
    int sock = socket::udp_socket();
    if (sock < 0) {
        strcpy_safe(result->errorMsg, "Failed to create socket", 64);
        s_stats.errors++;
        return DNS_ERR_NETWORK;
    }
    
    // Bind to ephemeral port
    uint16_t localPort = udp::alloc_ephemeral_port();
    if (localPort == 0) {
        socket::udp_close(sock);
        strcpy_safe(result->errorMsg, "No available port", 64);
        s_stats.errors++;
        return DNS_ERR_NETWORK;
    }
    
    socket::udp_bind(sock, localPort);
    
    // Try up to MAX_RETRIES times
    uint8_t responsePacket[MAX_PACKET_SIZE];
    bool gotResponse = false;
    
    for (uint8_t attempt = 0; attempt < MAX_RETRIES && !gotResponse; ++attempt) {
        // Send query
        socket::SockAddr dnsAddr = socket::make_sockaddr(s_dnsServer, DNS_PORT);
        int sent = socket::udp_sendto(sock, queryPacket, queryLen, &dnsAddr);
        
        if (sent < 0) continue;
        
        s_stats.queriesSent++;
        
        // Wait for response
        uint32_t waitCount = 0;
        const uint32_t maxWait = 300000;
        
        while (waitCount < maxWait && !gotResponse) {
            socket::SockAddr fromAddr;
            int recvLen = socket::udp_recvfrom(sock, responsePacket, MAX_PACKET_SIZE, &fromAddr);
            
            if (recvLen > 0) {
                s_stats.responsesReceived++;
                status = parse_response(responsePacket, recvLen, queryId, result);
                if (status == DNS_OK || status == DNS_ERR_NXDOMAIN ||
                    status == DNS_ERR_SERVFAIL || status == DNS_ERR_REFUSED) {
                    gotResponse = true;
                }
            }
            
            for (volatile int d = 0; d < 100; ++d) {}
            waitCount++;
        }
        
        if (!gotResponse) {
            s_stats.timeouts++;
        }
    }
    
    // Cleanup
    socket::udp_close(sock);
    udp::free_ephemeral_port(localPort);
    
    if (!gotResponse) {
        strcpy_safe(result->errorMsg, "Query timed out", 64);
        return DNS_ERR_TIMEOUT;
    }
    
    return status;
}

// ================================================================
// Utility Functions
// ================================================================

const char* type_to_string(RecordType type)
{
    switch (type) {
        case TYPE_A:     return "A";
        case TYPE_NS:    return "NS";
        case TYPE_CNAME: return "CNAME";
        case TYPE_SOA:   return "SOA";
        case TYPE_PTR:   return "PTR";
        case TYPE_MX:    return "MX";
        case TYPE_TXT:   return "TXT";
        case TYPE_AAAA:  return "AAAA";
        case TYPE_SRV:   return "SRV";
        case TYPE_ANY:   return "ANY";
        default:         return "UNKNOWN";
    }
}

const char* rcode_to_string(uint16_t rcode)
{
    switch (rcode) {
        case RCODE_NOERROR:  return "NOERROR";
        case RCODE_FORMERR:  return "FORMERR";
        case RCODE_SERVFAIL: return "SERVFAIL";
        case RCODE_NXDOMAIN: return "NXDOMAIN";
        case RCODE_NOTIMP:   return "NOTIMP";
        case RCODE_REFUSED:  return "REFUSED";
        default:             return "UNKNOWN";
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

} // namespace dns
} // namespace kernel
