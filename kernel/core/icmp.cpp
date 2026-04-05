// ICMP Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/icmp.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/pit.h"

namespace kernel {
namespace icmp {

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
static PingSession s_session;
static uint16_t s_nextId = 1;

// Timestamp for RTT calculation (simplified - uses PIT ticks)
static uint32_t s_pingStartTick = 0;

// ================================================================
// Checksum (same algorithm as IP, but over ICMP header + data)
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

bool verify_checksum(const uint8_t* data, uint16_t len)
{
    if (!data || len < sizeof(Header)) return false;
    
    // Calculate checksum - if correct, result should be 0
    uint16_t result = calculate_checksum(data, len);
    return result == 0;
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    memzero(&s_stats, sizeof(s_stats));
    memzero(&s_session, sizeof(s_session));
    s_nextId = 1;
    
    // Register with IPv4 layer to receive ICMP packets
    ipv4::register_handler(ipv4::PROTO_ICMP, handle_packet);
    
    serial::puts("[ICMP] Module initialized\n");
}

// ================================================================
// Packet Parsing
// ================================================================

Status parse_packet(const uint8_t* data, uint16_t len,
                    uint32_t srcIP, uint32_t dstIP,
                    ParsedPacket* parsed)
{
    if (!data || !parsed) return ICMP_ERR_NULL_PTR;
    if (len < sizeof(Header)) return ICMP_ERR_TOO_SHORT;
    
    memzero(parsed, sizeof(ParsedPacket));
    
    // Verify checksum first
    if (!verify_checksum(data, len)) {
        s_stats.checksumErrors++;
        return ICMP_ERR_BAD_CHECKSUM;
    }
    
    const Header* hdr = reinterpret_cast<const Header*>(data);
    
    parsed->type = hdr->type;
    parsed->code = hdr->code;
    parsed->checksum = ethernet::ntohs(hdr->checksum);
    parsed->id = ethernet::ntohs(hdr->rest.echo.id);
    parsed->sequence = ethernet::ntohs(hdr->rest.echo.sequence);
    parsed->srcIP = srcIP;
    parsed->dstIP = dstIP;
    
    // Data follows header
    if (len > sizeof(Header)) {
        parsed->data = data + sizeof(Header);
        parsed->dataLen = len - sizeof(Header);
    } else {
        parsed->data = nullptr;
        parsed->dataLen = 0;
    }
    
    parsed->isValid = true;
    parsed->checksumValid = true;
    
    return ICMP_OK;
}

// ================================================================
// Send Echo Request
// ================================================================

Status send_echo_request(uint32_t targetIP, uint16_t id,
                         uint16_t sequence, uint16_t dataLen)
{
    if (!ipv4::is_configured()) {
        return ICMP_ERR_NOT_CONFIGURED;
    }
    
    // Limit data length
    if (dataLen > 56) dataLen = 56;
    
    // Build ICMP echo request packet
    uint8_t packet[64];  // Header (8) + data (up to 56)
    uint16_t packetLen = sizeof(Header) + dataLen;
    
    memzero(packet, sizeof(packet));
    
    Header* hdr = reinterpret_cast<Header*>(packet);
    hdr->type = TYPE_ECHO_REQUEST;
    hdr->code = 0;
    hdr->checksum = 0;  // Will be filled after
    hdr->rest.echo.id = ethernet::htons(id);
    hdr->rest.echo.sequence = ethernet::htons(sequence);
    
    // Fill data with pattern (timestamp-like pattern)
    uint8_t* data = packet + sizeof(Header);
    for (uint16_t i = 0; i < dataLen; ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    // Calculate checksum
    hdr->checksum = calculate_checksum(packet, packetLen);
    
    // Send via IPv4
    ipv4::Status ipStatus = ipv4::send_packet(
        packet, packetLen, targetIP, ipv4::PROTO_ICMP);
    
    if (ipStatus != ipv4::IP_OK) {
        return ICMP_ERR_TX_FAILED;
    }
    
    s_stats.echoRequestsSent++;
    return ICMP_OK;
}

// ================================================================
// Send Echo Reply
// ================================================================

Status send_echo_reply(uint32_t destIP, uint16_t id,
                       uint16_t sequence, const uint8_t* data,
                       uint16_t dataLen)
{
    if (!ipv4::is_configured()) {
        return ICMP_ERR_NOT_CONFIGURED;
    }
    
    // Limit data length
    if (dataLen > 56) dataLen = 56;
    
    // Build ICMP echo reply packet
    uint8_t packet[64];
    uint16_t packetLen = sizeof(Header) + dataLen;
    
    memzero(packet, sizeof(packet));
    
    Header* hdr = reinterpret_cast<Header*>(packet);
    hdr->type = TYPE_ECHO_REPLY;
    hdr->code = 0;
    hdr->checksum = 0;
    hdr->rest.echo.id = ethernet::htons(id);
    hdr->rest.echo.sequence = ethernet::htons(sequence);
    
    // Copy original data
    if (data && dataLen > 0) {
        memcopy(packet + sizeof(Header), data, dataLen);
    }
    
    // Calculate checksum
    hdr->checksum = calculate_checksum(packet, packetLen);
    
    // Send via IPv4
    ipv4::Status ipStatus = ipv4::send_packet(
        packet, packetLen, destIP, ipv4::PROTO_ICMP);
    
    if (ipStatus != ipv4::IP_OK) {
        return ICMP_ERR_TX_FAILED;
    }
    
    s_stats.echoRepliesSent++;
    return ICMP_OK;
}

// ================================================================
// High-level Ping
// ================================================================

PingResult ping(uint32_t targetIP, uint16_t timeoutMs, PingReply* reply)
{
    if (!ipv4::is_configured()) {
        if (reply) reply->result = PING_NOT_CONFIGURED;
        return PING_NOT_CONFIGURED;
    }
    
    // Set up temporary session
    uint16_t id = s_nextId++;
    uint16_t seq = 1;
    
    // Record start time
    s_pingStartTick = pit::ticks();
    
    // Clear session state
    s_session.active = true;
    s_session.targetIP = targetIP;
    s_session.id = id;
    s_session.sequence = seq;
    s_session.replyReceived = false;
    
    // Send echo request
    Status status = send_echo_request(targetIP, id, seq, 56);
    if (status != ICMP_OK) {
        s_session.active = false;
        if (reply) reply->result = PING_TX_FAILED;
        return PING_TX_FAILED;
    }
    
    // Wait for reply (polling)
    // Convert timeout to approximate tick count (assuming 100 Hz PIT)
    uint32_t timeoutTicks = (timeoutMs * 100) / 1000;
    if (timeoutTicks == 0) timeoutTicks = 1;
    
    uint32_t startTick = pit::ticks();
    
    while (!s_session.replyReceived) {
        uint32_t elapsed = pit::ticks() - startTick;
        if (elapsed >= timeoutTicks) {
            s_session.active = false;
            if (reply) {
                reply->result = PING_TIMEOUT;
                reply->srcIP = 0;
                reply->sequence = seq;
                reply->ttl = 0;
                reply->rtt = timeoutMs;
            }
            return PING_TIMEOUT;
        }
        
        // Small delay to avoid busy loop
        // In a real implementation, we'd use proper waiting
        for (volatile int i = 0; i < 1000; ++i) {}
    }
    
    // Reply received
    s_session.active = false;
    
    if (reply) {
        *reply = s_session.lastReply;
    }
    
    return s_session.lastReply.result;
}

// ================================================================
// Ping Session Management
// ================================================================

Status start_ping_session(uint32_t targetIP, uint16_t timeoutMs)
{
    if (!ipv4::is_configured()) {
        return ICMP_ERR_NOT_CONFIGURED;
    }
    
    s_session.targetIP = targetIP;
    s_session.id = s_nextId++;
    s_session.sequence = 0;
    s_session.sent = 0;
    s_session.received = 0;
    s_session.timeoutMs = timeoutMs;
    s_session.active = true;
    s_session.replyReceived = false;
    
    return ICMP_OK;
}

Status ping_session_send()
{
    if (!s_session.active) {
        return ICMP_ERR_NO_SESSION;
    }
    
    s_session.sequence++;
    s_session.replyReceived = false;
    s_pingStartTick = pit::ticks();
    
    Status status = send_echo_request(s_session.targetIP,
                                      s_session.id,
                                      s_session.sequence, 56);
    
    if (status == ICMP_OK) {
        s_session.sent++;
    }
    
    return status;
}

bool ping_session_check_reply(PingReply* reply)
{
    if (!s_session.active || !s_session.replyReceived) {
        return false;
    }
    
    if (reply) {
        *reply = s_session.lastReply;
    }
    
    s_session.replyReceived = false;
    return true;
}

void ping_session_stats(uint16_t* sent, uint16_t* received)
{
    if (sent) *sent = s_session.sent;
    if (received) *received = s_session.received;
}

void end_ping_session()
{
    s_session.active = false;
}

// ================================================================
// Send ICMP Error Messages
// ================================================================

Status send_dest_unreachable(uint32_t destIP, uint8_t code,
                             const uint8_t* originalIP, uint16_t originalLen)
{
    if (!ipv4::is_configured()) {
        return ICMP_ERR_NOT_CONFIGURED;
    }
    
    // ICMP error includes: header (8) + IP header (20+) + 8 bytes of original data
    uint8_t packet[64];
    uint16_t includeLen = (originalLen > 28) ? 28 : originalLen;  // IP hdr + 8 bytes
    uint16_t packetLen = sizeof(Header) + includeLen;
    
    memzero(packet, sizeof(packet));
    
    Header* hdr = reinterpret_cast<Header*>(packet);
    hdr->type = TYPE_DEST_UNREACHABLE;
    hdr->code = code;
    hdr->checksum = 0;
    hdr->rest.unused = 0;
    
    // Copy original IP header + first 8 bytes of data
    if (originalIP && includeLen > 0) {
        memcopy(packet + sizeof(Header), originalIP, includeLen);
    }
    
    // Calculate checksum
    hdr->checksum = calculate_checksum(packet, packetLen);
    
    // Send
    ipv4::Status ipStatus = ipv4::send_packet(
        packet, packetLen, destIP, ipv4::PROTO_ICMP);
    
    if (ipStatus != ipv4::IP_OK) {
        return ICMP_ERR_TX_FAILED;
    }
    
    s_stats.destUnreachableSent++;
    return ICMP_OK;
}

Status send_time_exceeded(uint32_t destIP, uint8_t code,
                          const uint8_t* originalIP, uint16_t originalLen)
{
    if (!ipv4::is_configured()) {
        return ICMP_ERR_NOT_CONFIGURED;
    }
    
    uint8_t packet[64];
    uint16_t includeLen = (originalLen > 28) ? 28 : originalLen;
    uint16_t packetLen = sizeof(Header) + includeLen;
    
    memzero(packet, sizeof(packet));
    
    Header* hdr = reinterpret_cast<Header*>(packet);
    hdr->type = TYPE_TIME_EXCEEDED;
    hdr->code = code;
    hdr->checksum = 0;
    hdr->rest.unused = 0;
    
    if (originalIP && includeLen > 0) {
        memcopy(packet + sizeof(Header), originalIP, includeLen);
    }
    
    hdr->checksum = calculate_checksum(packet, packetLen);
    
    ipv4::Status ipStatus = ipv4::send_packet(
        packet, packetLen, destIP, ipv4::PROTO_ICMP);
    
    if (ipStatus != ipv4::IP_OK) {
        return ICMP_ERR_TX_FAILED;
    }
    
    return ICMP_OK;
}

// ================================================================
// Handle Incoming ICMP Packet
// ================================================================

void handle_packet(const ipv4::ParsedPacket* ipPacket)
{
    if (!ipPacket || !ipPacket->payload || ipPacket->payloadLen < sizeof(Header)) {
        s_stats.checksumErrors++;
        return;
    }
    
    ParsedPacket icmp;
    Status status = parse_packet(ipPacket->payload, ipPacket->payloadLen,
                                 ipPacket->srcAddr, ipPacket->dstAddr, &icmp);
    
    if (status != ICMP_OK) {
        return;
    }
    
    switch (icmp.type) {
        case TYPE_ECHO_REQUEST:
            // Respond with echo reply
            s_stats.echoRequestsReceived++;
            serial::puts("[ICMP] Echo request from ");
            {
                char ipStr[16];
                ipv4::ip_to_string(icmp.srcIP, ipStr);
                serial::puts(ipStr);
            }
            serial::putc('\n');
            
            send_echo_reply(icmp.srcIP, icmp.id, icmp.sequence,
                           icmp.data, icmp.dataLen);
            break;
            
        case TYPE_ECHO_REPLY:
            s_stats.echoRepliesReceived++;
            serial::puts("[ICMP] Echo reply from ");
            {
                char ipStr[16];
                ipv4::ip_to_string(icmp.srcIP, ipStr);
                serial::puts(ipStr);
            }
            serial::putc('\n');
            
            // Check if this matches our ping session
            if (s_session.active &&
                icmp.srcIP == s_session.targetIP &&
                icmp.id == s_session.id &&
                icmp.sequence == s_session.sequence) {
                
                // Calculate RTT
                uint32_t rttTicks = pit::ticks() - s_pingStartTick;
                uint16_t rttMs = (rttTicks * 1000) / 100;  // Assuming 100 Hz
                
                s_session.lastReply.result = PING_SUCCESS;
                s_session.lastReply.srcIP = icmp.srcIP;
                s_session.lastReply.sequence = icmp.sequence;
                s_session.lastReply.ttl = ipPacket->ttl;
                s_session.lastReply.rtt = rttMs;
                s_session.lastReply.dataLen = icmp.dataLen;
                s_session.received++;
                s_session.replyReceived = true;
            }
            break;
            
        case TYPE_DEST_UNREACHABLE:
            s_stats.destUnreachableReceived++;
            serial::puts("[ICMP] Destination unreachable from ");
            {
                char ipStr[16];
                ipv4::ip_to_string(icmp.srcIP, ipStr);
                serial::puts(ipStr);
            }
            serial::puts(" code=");
            serial::put_hex8(icmp.code);
            serial::putc('\n');
            
            // Update ping session if active
            if (s_session.active) {
                s_session.lastReply.result = PING_DEST_UNREACHABLE;
                if (icmp.code == CODE_HOST_UNREACHABLE) {
                    s_session.lastReply.result = PING_HOST_UNREACHABLE;
                } else if (icmp.code == CODE_NET_UNREACHABLE) {
                    s_session.lastReply.result = PING_NET_UNREACHABLE;
                }
                s_session.lastReply.srcIP = icmp.srcIP;
                s_session.replyReceived = true;
            }
            break;
            
        case TYPE_TIME_EXCEEDED:
            s_stats.timeExceededReceived++;
            serial::puts("[ICMP] Time exceeded from ");
            {
                char ipStr[16];
                ipv4::ip_to_string(icmp.srcIP, ipStr);
                serial::puts(ipStr);
            }
            serial::putc('\n');
            
            if (s_session.active) {
                s_session.lastReply.result = PING_TTL_EXCEEDED;
                s_session.lastReply.srcIP = icmp.srcIP;
                s_session.replyReceived = true;
            }
            break;
            
        default:
            s_stats.unknownTypes++;
            break;
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

const char* type_name(uint8_t type)
{
    switch (type) {
        case TYPE_ECHO_REPLY:        return "Echo Reply";
        case TYPE_DEST_UNREACHABLE:  return "Destination Unreachable";
        case TYPE_SOURCE_QUENCH:     return "Source Quench";
        case TYPE_REDIRECT:          return "Redirect";
        case TYPE_ECHO_REQUEST:      return "Echo Request";
        case TYPE_TIME_EXCEEDED:     return "Time Exceeded";
        case TYPE_PARAMETER_PROBLEM: return "Parameter Problem";
        case TYPE_TIMESTAMP:         return "Timestamp";
        case TYPE_TIMESTAMP_REPLY:   return "Timestamp Reply";
        default:                     return "Unknown";
    }
}

} // namespace icmp
} // namespace kernel
