//
// DNS Client for guideXOS
//
// Provides:
//   - DNS query packet building
//   - DNS response parsing for A, AAAA, and CNAME records
//   - Synchronous DNS resolution using UDP transport
//   - DNS cache with flush capability
//
// Reference: RFC 1035 - Domain Names - Implementation and Specification
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_DNS_H
#define KERNEL_DNS_H

#include "kernel/types.h"

namespace kernel {
namespace dns {

// ================================================================
// DNS Constants
// ================================================================

static const uint16_t DNS_PORT = 53;
static const uint16_t MAX_DOMAIN_LEN = 255;
static const uint16_t MAX_LABEL_LEN = 63;
static const uint16_t MAX_PACKET_SIZE = 512;  // Standard DNS UDP packet size
static const uint16_t QUERY_TIMEOUT_MS = 3000;
static const uint8_t  MAX_RETRIES = 3;
static const uint8_t  MAX_CACHE_ENTRIES = 16;
static const uint32_t DEFAULT_TTL = 300;      // 5 minutes default cache TTL

// Default DNS servers
static const uint32_t DNS_GOOGLE_PRIMARY   = 0x08080808;  // 8.8.8.8
static const uint32_t DNS_GOOGLE_SECONDARY = 0x08080404;  // 8.8.4.4
static const uint32_t DNS_CLOUDFLARE       = 0x01010101;  // 1.1.1.1

// ================================================================
// DNS Header Flags
// ================================================================

// QR (Query/Response) flag
static const uint16_t FLAG_QR_QUERY    = 0x0000;
static const uint16_t FLAG_QR_RESPONSE = 0x8000;

// Opcode (bits 11-14)
static const uint16_t OPCODE_QUERY     = 0x0000;
static const uint16_t OPCODE_IQUERY    = 0x0800;
static const uint16_t OPCODE_STATUS    = 0x1000;

// Flags
static const uint16_t FLAG_AA = 0x0400;  // Authoritative Answer
static const uint16_t FLAG_TC = 0x0200;  // Truncation
static const uint16_t FLAG_RD = 0x0100;  // Recursion Desired
static const uint16_t FLAG_RA = 0x0080;  // Recursion Available

// Response codes (RCODE, bits 0-3)
static const uint16_t RCODE_NOERROR    = 0;
static const uint16_t RCODE_FORMERR    = 1;  // Format error
static const uint16_t RCODE_SERVFAIL   = 2;  // Server failure
static const uint16_t RCODE_NXDOMAIN   = 3;  // Name does not exist
static const uint16_t RCODE_NOTIMP     = 4;  // Not implemented
static const uint16_t RCODE_REFUSED    = 5;  // Refused
static const uint16_t RCODE_MASK       = 0x000F;

// ================================================================
// DNS Record Types
// ================================================================

enum RecordType : uint16_t {
    TYPE_A      = 1,    // IPv4 address
    TYPE_NS     = 2,    // Name server
    TYPE_CNAME  = 5,    // Canonical name (alias)
    TYPE_SOA    = 6,    // Start of authority
    TYPE_PTR    = 12,   // Pointer (reverse DNS)
    TYPE_MX     = 15,   // Mail exchange
    TYPE_TXT    = 16,   // Text record
    TYPE_AAAA   = 28,   // IPv6 address
    TYPE_SRV    = 33,   // Service locator
    TYPE_ANY    = 255,  // Any type
};

// ================================================================
// DNS Class
// ================================================================

enum RecordClass : uint16_t {
    CLASS_IN    = 1,    // Internet
    CLASS_CS    = 2,    // CSNET (obsolete)
    CLASS_CH    = 3,    // Chaos
    CLASS_HS    = 4,    // Hesiod
    CLASS_ANY   = 255,  // Any class
};

// ================================================================
// DNS Header Structure (12 bytes, packed)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define DNS_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define DNS_PACKED
#endif

struct Header {
    uint16_t id;            // Transaction ID
    uint16_t flags;         // Flags and codes
    uint16_t qdcount;       // Question count
    uint16_t ancount;       // Answer count
    uint16_t nscount;       // Authority count
    uint16_t arcount;       // Additional count
} DNS_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef DNS_PACKED

// ================================================================
// Parsed DNS Resource Record
// ================================================================

struct ResourceRecord {
    char     name[MAX_DOMAIN_LEN + 1];    // Domain name
    uint16_t type;                         // Record type
    uint16_t rclass;                       // Record class
    uint32_t ttl;                          // Time to live (seconds)
    uint16_t rdlength;                     // Data length
    
    // Record data (depends on type)
    union {
        uint32_t ipv4;                     // A record: IPv4 address
        uint8_t  ipv6[16];                 // AAAA record: IPv6 address
        char     cname[MAX_DOMAIN_LEN + 1]; // CNAME record: canonical name
        uint8_t  raw[256];                 // Raw data for other types
    } data;
};

// ================================================================
// DNS Query Result
// ================================================================

static const uint8_t MAX_ANSWERS = 8;

struct QueryResult {
    bool     success;                       // Query was successful
    uint16_t rcode;                         // Response code
    uint8_t  answerCount;                   // Number of answers
    ResourceRecord answers[MAX_ANSWERS];    // Answer records
    char     errorMsg[64];                  // Error description
};

// ================================================================
// DNS Cache Entry
// ================================================================

struct CacheEntry {
    char     domain[MAX_DOMAIN_LEN + 1];   // Domain name
    uint32_t ipv4;                          // Resolved IPv4 address
    uint32_t expireTime;                    // Expiration time (system ticks)
    bool     valid;                         // Entry is valid
};

// ================================================================
// Status Codes
// ================================================================

enum Status : uint8_t {
    DNS_OK              = 0,
    DNS_ERR_INVALID     = 1,   // Invalid parameter
    DNS_ERR_TOOLONG     = 2,   // Domain name too long
    DNS_ERR_NXDOMAIN    = 3,   // Domain does not exist
    DNS_ERR_SERVFAIL    = 4,   // Server failure
    DNS_ERR_TIMEOUT     = 5,   // Query timed out
    DNS_ERR_NETWORK     = 6,   // Network error
    DNS_ERR_FORMAT      = 7,   // Malformed response
    DNS_ERR_REFUSED     = 8,   // Query refused
    DNS_ERR_NOTFOUND    = 9,   // No records found
    DNS_ERR_NOCACHE     = 10,  // Not in cache
};

// ================================================================
// Statistics
// ================================================================

struct Statistics {
    uint32_t queriesSent;
    uint32_t responsesReceived;
    uint32_t cacheHits;
    uint32_t cacheMisses;
    uint32_t timeouts;
    uint32_t errors;
};

// ================================================================
// Initialization
// ================================================================

// Initialize DNS client
void init();

// ================================================================
// DNS Server Configuration
// ================================================================

// Set the DNS server address (host byte order)
void set_server(uint32_t serverIP);

// Get the current DNS server address
uint32_t get_server();

// ================================================================
// DNS Resolution
// ================================================================

// Resolve a domain name to IPv4 address
// This is a synchronous (blocking) operation
//
// Parameters:
//   domain  - Domain name to resolve (e.g., "example.com")
//   ipv4    - Output: resolved IPv4 address (host byte order)
//
// Returns DNS_OK on success
Status resolve(const char* domain, uint32_t* ipv4);

// Resolve with full query result (includes CNAME, TTL, etc.)
Status resolve_full(const char* domain, RecordType type, QueryResult* result);

// ================================================================
// DNS Query Building
// ================================================================

// Build a DNS query packet
//
// Parameters:
//   buffer      - Output buffer for packet
//   bufferSize  - Size of output buffer
//   domain      - Domain name to query
//   type        - Record type (TYPE_A, TYPE_AAAA, etc.)
//   queryId     - Transaction ID
//   packetLen   - Output: length of built packet
//
// Returns DNS_OK on success
Status build_query(uint8_t* buffer, uint16_t bufferSize,
                   const char* domain, RecordType type,
                   uint16_t queryId, uint16_t* packetLen);

// ================================================================
// DNS Response Parsing
// ================================================================

// Parse a DNS response packet
//
// Parameters:
//   packet      - DNS response packet
//   packetLen   - Length of packet
//   expectedId  - Expected transaction ID
//   result      - Output: parsed result
//
// Returns DNS_OK on success
Status parse_response(const uint8_t* packet, uint16_t packetLen,
                      uint16_t expectedId, QueryResult* result);

// ================================================================
// DNS Cache
// ================================================================

// Look up domain in cache
// Returns DNS_OK if found and not expired
Status cache_lookup(const char* domain, uint32_t* ipv4);

// Add entry to cache
void cache_add(const char* domain, uint32_t ipv4, uint32_t ttl);

// Flush DNS cache (clear all entries)
void cache_flush();

// Get cache statistics
uint32_t cache_size();

// ================================================================
// Utility Functions
// ================================================================

// Encode domain name to DNS format (label length + label + ...)
// Returns encoded length, or 0 on error
uint16_t encode_domain(const char* domain, uint8_t* buffer, uint16_t bufferSize);

// Decode domain name from DNS packet
// Returns decoded length, or 0 on error
uint16_t decode_domain(const uint8_t* packet, uint16_t packetLen,
                       uint16_t offset, char* domain, uint16_t domainSize);

// Get record type name as string
const char* type_to_string(RecordType type);

// Get response code description
const char* rcode_to_string(uint16_t rcode);

// ================================================================
// Statistics
// ================================================================

const Statistics* get_stats();
void reset_stats();

} // namespace dns
} // namespace kernel

#endif // KERNEL_DNS_H
