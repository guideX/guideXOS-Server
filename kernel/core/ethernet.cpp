// Ethernet Frame Parser and Builder — Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ethernet.h"

namespace kernel {
namespace ethernet {

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

// Convert a hex character to its value (0-15), or 0xFF on error
static uint8_t hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    return 0xFF;
}

// Convert a nibble (0-15) to hex character
static char val_to_hex_char(uint8_t val)
{
    static const char hex[] = "0123456789AB";
    return hex[val & 0x0F];
}

// ================================================================
// MAC Address Utilities
// ================================================================

void mac_copy(uint8_t* dest, const uint8_t* src)
{
    if (!dest || !src) return;
    for (uint16_t i = 0; i < MAC_ADDR_LEN; ++i) {
        dest[i] = src[i];
    }
}

bool mac_equals(const uint8_t* mac1, const uint8_t* mac2)
{
    if (!mac1 || !mac2) return false;
    for (uint16_t i = 0; i < MAC_ADDR_LEN; ++i) {
        if (mac1[i] != mac2[i]) return false;
    }
    return true;
}

bool mac_is_broadcast(const uint8_t* mac)
{
    if (!mac) return false;
    return mac_equals(mac, BROADCAST_MAC);
}

bool mac_is_multicast(const uint8_t* mac)
{
    if (!mac) return false;
    // Multicast bit is bit 0 of the first byte
    // (but not broadcast, which is all 1s)
    return (mac[0] & 0x01) && !mac_is_broadcast(mac);
}

bool mac_is_zero(const uint8_t* mac)
{
    if (!mac) return false;
    return mac_equals(mac, ZERO_MAC);
}

void mac_to_string(const uint8_t* mac, char* str)
{
    if (!mac || !str) return;
    
    // Format: XX:XX:XX:XX:XX:XX (17 chars + null)
    uint8_t idx = 0;
    for (uint16_t i = 0; i < MAC_ADDR_LEN; ++i) {
        if (i > 0) str[idx++] = ':';
        str[idx++] = val_to_hex_char((mac[i] >> 4) & 0x0F);
        str[idx++] = val_to_hex_char(mac[i] & 0x0F);
    }
    str[idx] = '\0';
}

bool mac_from_string(const char* str, uint8_t* mac)
{
    if (!str || !mac) return false;
    
    // Expected format: XX:XX:XX:XX:XX:XX or XX-XX-XX-XX-XX-XX
    uint8_t bytes[MAC_ADDR_LEN];
    uint8_t byteIdx = 0;
    uint8_t charIdx = 0;
    
    while (byteIdx < MAC_ADDR_LEN) {
        // Get high nibble
        uint8_t hi = hex_char_to_val(str[charIdx++]);
        if (hi == 0xFF) return false;
        
        // Get low nibble
        uint8_t lo = hex_char_to_val(str[charIdx++]);
        if (lo == 0xFF) return false;
        
        bytes[byteIdx++] = (hi << 4) | lo;
        
        // Skip separator (: or -) if not at end
        if (byteIdx < MAC_ADDR_LEN) {
            char sep = str[charIdx++];
            if (sep != ':' && sep != '-') return false;
        }
    }
    
    // Verify we consumed exactly the right amount
    if (str[charIdx] != '\0') return false;
    
    mac_copy(mac, bytes);
    return true;
}

// ================================================================
// Frame Parsing
// ================================================================

Status parse_frame(const uint8_t* frame, uint16_t len, ParsedFrame* parsed)
{
    if (!frame || !parsed) return ETH_ERR_NULL_PTR;
    if (len < HEADER_LEN) return ETH_ERR_TOO_SHORT;
    
    // Clear output structure
    memzero(parsed, sizeof(ParsedFrame));
    
    // Cast frame to header structure
    const Header* hdr = reinterpret_cast<const Header*>(frame);
    
    // Copy MAC addresses
    mac_copy(parsed->destMAC, hdr->destMAC);
    mac_copy(parsed->srcMAC, hdr->srcMAC);
    
    // Convert EtherType to host byte order
    parsed->etherType = ntohs(hdr->etherType);
    
    // Set payload pointer and length
    parsed->payload = frame + HEADER_LEN;
    parsed->payloadLen = len - HEADER_LEN;
    
    // Set metadata flags
    parsed->isBroadcast = mac_is_broadcast(parsed->destMAC);
    parsed->isMulticast = mac_is_multicast(parsed->destMAC);
    parsed->isValid = true;
    
    return ETH_OK;
}

Status extract_header(const uint8_t* frame, uint16_t len, Header* header)
{
    if (!frame || !header) return ETH_ERR_NULL_PTR;
    if (len < HEADER_LEN) return ETH_ERR_TOO_SHORT;
    
    // Copy header bytes directly
    memcopy(header, frame, HEADER_LEN);
    
    return ETH_OK;
}

Status get_payload(const uint8_t* frame, uint16_t len,
                   const uint8_t** payload, uint16_t* payloadLen)
{
    if (!frame) return ETH_ERR_NULL_PTR;
    if (len < HEADER_LEN) return ETH_ERR_TOO_SHORT;
    
    if (payload) *payload = frame + HEADER_LEN;
    if (payloadLen) *payloadLen = len - HEADER_LEN;
    
    return ETH_OK;
}

uint16_t get_ethertype(const uint8_t* frame, uint16_t len)
{
    if (!frame || len < HEADER_LEN) return 0;
    
    const Header* hdr = reinterpret_cast<const Header*>(frame);
    return ntohs(hdr->etherType);
}

// ================================================================
// Frame Building
// ================================================================

Status builder_init(FrameBuilder* builder, uint8_t* buffer, uint16_t bufferSize)
{
    if (!builder || !buffer) return ETH_ERR_NULL_PTR;
    if (bufferSize < MIN_FRAME_LEN) return ETH_ERR_BUFFER_SMALL;
    
    builder->buffer = buffer;
    builder->bufferSize = bufferSize;
    builder->frameLen = 0;
    builder->headerSet = false;
    
    // Zero the buffer
    memzero(buffer, bufferSize);
    
    return ETH_OK;
}

Status builder_set_header(FrameBuilder* builder,
                          const uint8_t* destMAC,
                          const uint8_t* srcMAC,
                          uint16_t etherType)
{
    if (!builder || !builder->buffer) return ETH_ERR_NULL_PTR;
    if (!destMAC || !srcMAC) return ETH_ERR_NULL_PTR;
    if (builder->bufferSize < HEADER_LEN) return ETH_ERR_BUFFER_SMALL;
    
    Header* hdr = reinterpret_cast<Header*>(builder->buffer);
    
    // Set destination and source MAC
    mac_copy(hdr->destMAC, destMAC);
    mac_copy(hdr->srcMAC, srcMAC);
    
    // Convert EtherType to network byte order
    hdr->etherType = htons(etherType);
    
    builder->frameLen = HEADER_LEN;
    builder->headerSet = true;
    
    return ETH_OK;
}

Status builder_append_payload(FrameBuilder* builder,
                              const uint8_t* data,
                              uint16_t len)
{
    if (!builder || !builder->buffer) return ETH_ERR_NULL_PTR;
    if (!builder->headerSet) return ETH_ERR_NO_HEADER;
    if (len == 0) return ETH_OK;  // Nothing to append
    if (!data) return ETH_ERR_NULL_PTR;
    
    // Check if payload would exceed MTU
    uint16_t currentPayload = builder->frameLen - HEADER_LEN;
    if (currentPayload + len > MAX_PAYLOAD) return ETH_ERR_TOO_LONG;
    
    // Check buffer capacity
    if (builder->frameLen + len > builder->bufferSize) return ETH_ERR_BUFFER_SMALL;
    
    // Copy payload data
    memcopy(builder->buffer + builder->frameLen, data, len);
    builder->frameLen += len;
    
    return ETH_OK;
}

Status builder_finalize(FrameBuilder* builder, uint16_t* frameLen)
{
    if (!builder || !builder->buffer) return ETH_ERR_NULL_PTR;
    if (!builder->headerSet) return ETH_ERR_NO_HEADER;
    
    // Pad frame to minimum length if necessary
    if (builder->frameLen < MIN_FRAME_LEN) {
        // Zero-pad the remaining bytes
        uint16_t padLen = MIN_FRAME_LEN - builder->frameLen;
        memzero(builder->buffer + builder->frameLen, padLen);
        builder->frameLen = MIN_FRAME_LEN;
    }
    
    if (frameLen) *frameLen = builder->frameLen;
    
    return ETH_OK;
}

void builder_reset(FrameBuilder* builder)
{
    if (!builder) return;
    
    builder->frameLen = 0;
    builder->headerSet = false;
    
    if (builder->buffer && builder->bufferSize > 0) {
        memzero(builder->buffer, builder->bufferSize);
    }
}

// ================================================================
// Convenience Functions
// ================================================================

Status build_frame(uint8_t* buffer,
                   uint16_t bufferSize,
                   const uint8_t* destMAC,
                   const uint8_t* srcMAC,
                   uint16_t etherType,
                   const uint8_t* payload,
                   uint16_t payloadLen,
                   uint16_t* frameLen)
{
    FrameBuilder builder;
    Status status;
    
    status = builder_init(&builder, buffer, bufferSize);
    if (status != ETH_OK) return status;
    
    status = builder_set_header(&builder, destMAC, srcMAC, etherType);
    if (status != ETH_OK) return status;
    
    if (payload && payloadLen > 0) {
        status = builder_append_payload(&builder, payload, payloadLen);
        if (status != ETH_OK) return status;
    }
    
    return builder_finalize(&builder, frameLen);
}

Status build_broadcast_frame(uint8_t* buffer,
                             uint16_t bufferSize,
                             const uint8_t* srcMAC,
                             uint16_t etherType,
                             const uint8_t* payload,
                             uint16_t payloadLen,
                             uint16_t* frameLen)
{
    return build_frame(buffer, bufferSize,
                       BROADCAST_MAC, srcMAC,
                       etherType, payload, payloadLen, frameLen);
}

// ================================================================
// EtherType Utilities
// ================================================================

const char* ethertype_name(uint16_t etherType)
{
    switch (etherType) {
        case ETHERTYPE_IPV4:     return "IPv4";
        case ETHERTYPE_ARP:      return "ARP";
        case ETHERTYPE_RARP:     return "RARP";
        case ETHERTYPE_VLAN:     return "802.1Q VLAN";
        case ETHERTYPE_IPV6:     return "IPv6";
        case ETHERTYPE_LLDP:     return "LLDP";
        case ETHERTYPE_LOOPBACK: return "Loopback";
        default:                 return "Unknown";
    }
}

} // namespace ethernet
} // namespace kernel
