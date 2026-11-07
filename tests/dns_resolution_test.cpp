// Hosted protocol and transport-contract tests for the kernel DNS client.
// The socket and NIC boundary is deterministic here; guest packet movement is
// qualified separately by the QEMU smoke harness.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/dns.h"
#include "kernel/ethernet.h"
#include "kernel/ipv4.h"
#include "kernel/pit.h"
#include "kernel/serial_debug.h"
#include "kernel/socket.h"
#include "kernel/udp.h"

using namespace kernel;

namespace {

enum class FixtureMode {
    ValidA,
    WrongSource,
    BadQuestionCount,
    WrongQuestion,
    WrongTransaction,
    Truncated,
    NxDomain,
    Oversized,
    Timeout,
    ForeignAnswerOwner
};

static FixtureMode g_mode = FixtureMode::ValidA;
static uint64_t g_ticks = 0;
static uint32_t g_dnsServer = ipv4::make_ip(10, 0, 2, 3);
static ipv4::NetworkConfig g_config = {};
static ipv4::Statistics g_ipv4Stats = {};
static udp::Statistics g_udpStats = {};
static uint8_t g_reply[socket::MAX_DGRAM_SIZE + 1] = {};
static uint16_t g_replyLength = 0;
static bool g_replyPending = false;
static socket::SockAddr g_replySource = {};
static uint32_t g_sendCalls = 0;
static uint32_t g_lastDestination = 0;
static uint16_t g_lastDestinationPort = 0;
static uint16_t g_lastSourcePort = 0;

static void write_u16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value >> 8);
    p[1] = static_cast<uint8_t>(value);
}

static void write_u32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

static uint16_t query_question_bytes(const uint8_t* query, uint16_t queryLen)
{
    uint16_t pos = sizeof(dns::Header);
    while (pos < queryLen && query[pos] != 0) {
        const uint8_t labelLen = query[pos];
        if (labelLen > dns::MAX_LABEL_LEN || pos + 1u + labelLen >= queryLen) return 0;
        pos = static_cast<uint16_t>(pos + 1u + labelLen);
    }
    if (pos >= queryLen || queryLen - pos < 5) return 0;
    return static_cast<uint16_t>(pos + 1u + 4u - sizeof(dns::Header));
}

static void make_reply(const uint8_t* query, uint16_t queryLen)
{
    memset(g_reply, 0, sizeof(g_reply));
    const uint16_t questionBytes = query_question_bytes(query, queryLen);
    assert(questionBytes != 0);
    memcpy(g_reply, query, 2);
    write_u16(g_reply + 2, static_cast<uint16_t>(
        dns::FLAG_QR_RESPONSE | dns::FLAG_RD | dns::FLAG_RA |
        (g_mode == FixtureMode::Truncated ? dns::FLAG_TC : 0) |
        (g_mode == FixtureMode::NxDomain ? dns::RCODE_NXDOMAIN : dns::RCODE_NOERROR)));
    write_u16(g_reply + 4, 1);
    const bool hasAnswer = g_mode != FixtureMode::NxDomain;
    write_u16(g_reply + 6, hasAnswer ? 1 : 0);
    write_u16(g_reply + 8, 0);
    write_u16(g_reply + 10, 0);
    memcpy(g_reply + sizeof(dns::Header), query + sizeof(dns::Header), questionBytes);
    g_replyLength = static_cast<uint16_t>(sizeof(dns::Header) + questionBytes);

    if (g_mode == FixtureMode::BadQuestionCount) {
        write_u16(g_reply + 4, 0);
    } else if (g_mode == FixtureMode::WrongQuestion) {
        g_reply[sizeof(dns::Header) + 1] ^= 1;
    }

    if (hasAnswer) {
        uint8_t* answer = g_reply + g_replyLength;
        answer[0] = 0xC0;
        answer[1] = 0x0C;
        write_u16(answer + 2, dns::TYPE_A);
        write_u16(answer + 4, dns::CLASS_IN);
        write_u32(answer + 6, 60);
        write_u16(answer + 10, 4);
        write_u32(answer + 12, ipv4::make_ip(203, 0, 113, 42));
        g_replyLength = static_cast<uint16_t>(g_replyLength + 16);
        if (g_mode == FixtureMode::ForeignAnswerOwner) {
            // Replace the compression pointer with the encoded unrelated owner.
            memmove(answer + 9, answer + 2, 14);
            answer[0] = 7;
            memcpy(answer + 1, "foreign", 7);
            answer[8] = 0;
            g_replyLength = static_cast<uint16_t>(g_replyLength + 7);
        }
    }

    if (g_mode == FixtureMode::WrongTransaction) g_reply[1] ^= 0x20;
    if (g_mode == FixtureMode::Oversized) {
        g_replyLength = dns::MAX_PACKET_SIZE + 1;
    }
    g_replySource.addr = g_mode == FixtureMode::WrongSource
        ? ipv4::make_ip(10, 0, 2, 99)
        : g_dnsServer;
    g_replySource.port = dns::DNS_PORT;
    g_replyPending = g_mode != FixtureMode::Timeout;
}

static void reset_fixture(FixtureMode mode)
{
    g_mode = mode;
    g_ticks = 0;
    g_replyLength = 0;
    g_replyPending = false;
    g_sendCalls = 0;
    g_lastDestination = 0;
    g_lastDestinationPort = 0;
    g_lastSourcePort = 0;
    g_config = {};
    g_config.configured = true;
    g_config.ipAddr = ipv4::make_ip(10, 0, 2, 15);
    g_config.subnetMask = ipv4::make_ip(255, 255, 255, 0);
    g_config.gateway = ipv4::make_ip(10, 0, 2, 2);
    g_config.dns = g_dnsServer;
    memset(&g_ipv4Stats, 0, sizeof(g_ipv4Stats));
    memset(&g_udpStats, 0, sizeof(g_udpStats));
    dns::init();
    dns::cache_flush();
}

static dns::Status resolve_fixture(FixtureMode mode, uint32_t* address)
{
    reset_fixture(mode);
    return dns::resolve("navigator-dns-fixture.example", address);
}

} // namespace

namespace kernel {
namespace pit {
uint64_t ticks() { return g_ticks; }
}
namespace serial {
void puts(const char*) {}
void putc(char) {}
void put_hex32(uint32_t) {}
}
namespace ipv4 {
const NetworkConfig* get_config() { return &g_config; }
const Statistics* get_stats() { return &g_ipv4Stats; }
bool is_configured() { return g_config.configured; }
void poll_network() { ++g_ticks; }
void ip_to_string(uint32_t ip, char* out)
{
    if (!out) return;
    char* cursor = out;
    for (uint8_t octet = 0; octet < 4; ++octet) {
        uint8_t value = ip_octet(ip, octet);
        char digits[3];
        uint8_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value = static_cast<uint8_t>(value / 10);
        } while (value != 0);
        while (count > 0) *cursor++ = digits[--count];
        if (octet != 3) *cursor++ = '.';
    }
    *cursor = '\0';
}
bool ip_from_string(const char* text, uint32_t* out)
{
    if (!text || !out) return false;
    uint8_t octets[4] = {};
    uint8_t index = 0;
    uint16_t value = 0;
    bool hasDigit = false;
    for (const char* cursor = text; ; ++cursor) {
        const char ch = *cursor;
        if (ch >= '0' && ch <= '9') {
            hasDigit = true;
            value = static_cast<uint16_t>(value * 10u + static_cast<uint16_t>(ch - '0'));
            if (value > 255) return false;
            continue;
        }
        if ((ch == '.' || ch == '\0') && hasDigit && index < 4) {
            octets[index++] = static_cast<uint8_t>(value);
            value = 0;
            hasDigit = false;
            if (ch == '\0') break;
            continue;
        }
        return false;
    }
    if (index != 4) return false;
    *out = make_ip(octets[0], octets[1], octets[2], octets[3]);
    return true;
}
}
namespace udp {
uint16_t alloc_ephemeral_port() { return 53000; }
void free_ephemeral_port(uint16_t) {}
const Statistics* get_stats() { return &g_udpStats; }
}
namespace socket {
int udp_socket() { return 1; }
int udp_close(int) { return SOCK_OK; }
int udp_bind(int, uint16_t port)
{
    g_lastSourcePort = port;
    return SOCK_OK;
}
int udp_sendto(int, const void* data, uint16_t len, const SockAddr* destination)
{
    if (!data || !destination) return SOCK_ERR_INVALID;
    g_lastDestination = destination->addr;
    g_lastDestinationPort = destination->port;
    ++g_sendCalls;
    make_reply(static_cast<const uint8_t*>(data), len);
    return len;
}
int udp_recvfrom(int, void* data, uint16_t maxLen, SockAddr* source)
{
    if (!g_replyPending) return SOCK_ERR_WOULDBLOCK;
    g_replyPending = false;
    if (g_replyLength > maxLen) return SOCK_ERR_MSGSIZE;
    memcpy(data, g_reply, g_replyLength);
    if (source) *source = g_replySource;
    return g_replyLength;
}
}
}

int main()
{
    uint32_t address = 0;
    assert(resolve_fixture(FixtureMode::ValidA, &address) == dns::DNS_OK);
    assert(address == ipv4::make_ip(203, 0, 113, 42));
    assert(g_lastDestination == g_dnsServer && g_lastDestinationPort == dns::DNS_PORT);
    assert(g_lastSourcePort == 53000 && g_sendCalls == 1);
    const dns::QueryDiagnostics* diag = dns::get_last_query_diagnostics();
    assert(diag && diag->replySourceValidated && diag->acceptedReplies == 1);
    assert(diag->replySourceIP == g_dnsServer && diag->replySourcePort == dns::DNS_PORT);

    address = 0;
    assert(resolve_fixture(FixtureMode::WrongSource, &address) == dns::DNS_ERR_TIMEOUT);
    diag = dns::get_last_query_diagnostics();
    assert(diag->unexpectedReplySources == dns::MAX_RETRIES);
    assert(diag->acceptedReplies == 0 && address == 0);

    address = 0;
    assert(resolve_fixture(FixtureMode::BadQuestionCount, &address) == dns::DNS_ERR_TIMEOUT);
    diag = dns::get_last_query_diagnostics();
    assert(diag->malformedReplies == dns::MAX_RETRIES && address == 0);

    address = 0;
    assert(resolve_fixture(FixtureMode::WrongQuestion, &address) == dns::DNS_ERR_TIMEOUT);
    diag = dns::get_last_query_diagnostics();
    assert(diag->malformedReplies == dns::MAX_RETRIES && address == 0);

    address = 0;
    assert(resolve_fixture(FixtureMode::WrongTransaction, &address) == dns::DNS_ERR_TIMEOUT);
    diag = dns::get_last_query_diagnostics();
    assert(diag->malformedReplies == dns::MAX_RETRIES && address == 0);

    address = 0;
    assert(resolve_fixture(FixtureMode::Truncated, &address) == dns::DNS_ERR_TIMEOUT);
    diag = dns::get_last_query_diagnostics();
    assert(diag->malformedReplies == dns::MAX_RETRIES && address == 0);

    address = 0;
    assert(resolve_fixture(FixtureMode::NxDomain, &address) == dns::DNS_ERR_NXDOMAIN);
    diag = dns::get_last_query_diagnostics();
    assert(diag->acceptedReplies == 1 && diag->result == dns::DNS_ERR_NXDOMAIN);

    address = 0;
    assert(resolve_fixture(FixtureMode::Oversized, &address) == dns::DNS_ERR_TIMEOUT);
    diag = dns::get_last_query_diagnostics();
    assert(diag->oversizedReplies == dns::MAX_RETRIES && address == 0);

    address = 0;
    assert(resolve_fixture(FixtureMode::Timeout, &address) == dns::DNS_ERR_TIMEOUT);
    diag = dns::get_last_query_diagnostics();
    assert(diag->sendAttempts == dns::MAX_RETRIES && g_ticks <= 3u * 302u);

    reset_fixture(FixtureMode::ForeignAnswerOwner);
    uint8_t foreignQuery[dns::MAX_PACKET_SIZE] = {};
    uint16_t foreignQueryLength = 0;
    if (dns::build_query(foreignQuery, sizeof(foreignQuery), "navigator-dns-fixture.example",
            dns::TYPE_A, 1, &foreignQueryLength) != dns::DNS_OK) return 250;
    make_reply(foreignQuery, foreignQueryLength);
    dns::QueryResult foreignParsed = {};
    const dns::Status foreignParseStatus = dns::parse_response(g_reply, g_replyLength, 1,
        "navigator-dns-fixture.example", dns::TYPE_A, &foreignParsed);
    assert(foreignParseStatus == dns::DNS_OK);

    address = 0;
    const dns::Status foreignOwnerStatus = resolve_fixture(FixtureMode::ForeignAnswerOwner, &address);
    assert(foreignOwnerStatus == dns::DNS_ERR_NOTFOUND);
    assert(address == 0);

    reset_fixture(FixtureMode::ValidA);
    dns::QueryResult fullResult = {};
    assert(dns::resolve_full("navigator-dns-fixture.example", dns::TYPE_A, &fullResult) == dns::DNS_OK);
    assert(fullResult.success && fullResult.answerCount == 1);
    assert(fullResult.answers[0].data.ipv4 == ipv4::make_ip(203, 0, 113, 42));

    return 0;
}
