#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/ethernet.h"
#include "kernel/ipv4.h"
#include "kernel/nic.h"

using namespace kernel;

namespace {
static uint64_t g_ticks = 0;
static bool g_nicActive = true;
static bool g_answerArp = false;
static bool g_pendingFrame = false;
static uint8_t g_receiveFrame[ethernet::MAX_FRAME_LEN];
static uint16_t g_receiveLength = 0;
static nic::Status g_ipv4NicStatus = nic::NIC_OK;
static uint32_t g_arpFramesSent = 0;
static uint32_t g_ipv4FramesSent = 0;

static const uint8_t kGuestMac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const uint8_t kRouterMac[6] = { 0x52, 0x54, 0x00, 0xAB, 0xCD, 0xEF };

#if defined(__GNUC__) || defined(__clang__)
struct __attribute__((packed)) TestArpPacket {
#else
#pragma pack(push, 1)
struct TestArpPacket {
#endif
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint32_t spa;
    uint8_t tha[6];
    uint32_t tpa;
};
#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

static uint16_t frame_type(const uint8_t* frame)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(frame[12]) << 8) | frame[13]);
}

static void queue_arp_reply(const uint8_t* requestFrame)
{
    const TestArpPacket* request = reinterpret_cast<const TestArpPacket*>(
        requestFrame + ethernet::HEADER_LEN);
    TestArpPacket reply = {};
    reply.htype = ethernet::htons(1);
    reply.ptype = ethernet::htons(ethernet::ETHERTYPE_IPV4);
    reply.hlen = 6;
    reply.plen = 4;
    reply.oper = ethernet::htons(2);
    memcpy(reply.sha, kRouterMac, sizeof(reply.sha));
    reply.spa = request->tpa;
    memcpy(reply.tha, request->sha, sizeof(reply.tha));
    reply.tpa = request->spa;

    assert(ethernet::build_frame(g_receiveFrame, sizeof(g_receiveFrame),
        request->sha, kRouterMac, ethernet::ETHERTYPE_ARP,
        reinterpret_cast<const uint8_t*>(&reply), sizeof(reply),
        &g_receiveLength) == ethernet::ETH_OK);
    g_pendingFrame = true;
}

static void reset_transport()
{
    g_ticks = 0;
    g_nicActive = true;
    g_answerArp = false;
    g_pendingFrame = false;
    g_receiveLength = 0;
    g_ipv4NicStatus = nic::NIC_OK;
    g_arpFramesSent = 0;
    g_ipv4FramesSent = 0;
}

static void configure_guest(uint32_t gateway)
{
    assert(ipv4::configure_static(ipv4::make_ip(10, 0, 2, 15),
        ipv4::make_ip(255, 255, 255, 0), gateway, 0) == ipv4::CONFIG_OK);
    ipv4::set_mac_address(kGuestMac);
}
}

namespace kernel {
namespace serial {
void puts(const char*) {}
void putc(char) {}
void put_hex8(uint8_t) {}
void put_hex16(uint16_t) {}
void put_hex32(uint32_t) {}
}
namespace pit {
uint64_t ticks() { return g_ticks++; }
}
namespace nic {
bool is_active() { return g_nicActive; }
LinkState get_link_state() { return g_nicActive ? NIC_LINK_UP : NIC_LINK_DOWN; }
Status send_frame(const uint8_t* frame, uint16_t length)
{
    assert(frame != nullptr && length >= ethernet::HEADER_LEN);
    const uint16_t type = frame_type(frame);
    if (type == ethernet::ETHERTYPE_ARP) {
        ++g_arpFramesSent;
        if (g_answerArp) queue_arp_reply(frame);
        return NIC_OK;
    }
    if (type == ethernet::ETHERTYPE_IPV4) {
        ++g_ipv4FramesSent;
        return g_ipv4NicStatus;
    }
    return NIC_OK;
}
Status receive_frame(uint8_t* buffer, uint16_t capacity, uint16_t* length)
{
    if (!g_pendingFrame) return NIC_ERR_RX_EMPTY;
    assert(buffer != nullptr && length != nullptr && capacity >= g_receiveLength);
    memcpy(buffer, g_receiveFrame, g_receiveLength);
    *length = g_receiveLength;
    g_pendingFrame = false;
    return NIC_OK;
}
}
}

int main()
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03 };
    const uint32_t guestIP = ipv4::make_ip(10, 0, 2, 15);
    const uint32_t gatewayIP = ipv4::make_ip(10, 0, 2, 2);

    reset_transport();
    ipv4::init();
    assert(ipv4::send_packet(payload, sizeof(payload), gatewayIP, ipv4::PROTO_TCP) ==
        ipv4::IP_ERR_NOT_CONFIGURED);
    const ipv4::SendDiagnostics* diag = ipv4::get_last_send_diagnostics();
    assert(diag->statusValid && diag->stage == ipv4::SEND_STAGE_CONFIGURATION_CHECK);
    assert(diag->status == ipv4::IP_ERR_NOT_CONFIGURED && !diag->configured);

    reset_transport();
    ipv4::init();
    configure_guest(0);
    uint8_t oversized[ipv4::MAX_PAYLOAD + 1] = {};
    assert(ipv4::send_packet(oversized, sizeof(oversized), gatewayIP, ipv4::PROTO_TCP) ==
        ipv4::IP_ERR_TOO_LONG);
    diag = ipv4::get_last_send_diagnostics();
    assert(diag->stage == ipv4::SEND_STAGE_PACKET_VALIDATION);
    assert(diag->status == ipv4::IP_ERR_TOO_LONG && diag->sourceIP == guestIP);

    reset_transport();
    ipv4::init();
    configure_guest(0);
    const uint32_t unroutedIP = ipv4::make_ip(198, 51, 100, 9);
    assert(ipv4::send_packet(payload, sizeof(payload), unroutedIP, ipv4::PROTO_TCP) ==
        ipv4::IP_ERR_NO_ROUTE);
    diag = ipv4::get_last_send_diagnostics();
    assert(diag->stage == ipv4::SEND_STAGE_ROUTE_SELECTION);
    assert(diag->status == ipv4::IP_ERR_NO_ROUTE && diag->nextHop == 0);
    assert(g_arpFramesSent == 0 && g_ipv4FramesSent == 0);

    reset_transport();
    ipv4::init();
    configure_guest(0);
    assert(ipv4::send_packet(payload, sizeof(payload), gatewayIP, ipv4::PROTO_TCP) ==
        ipv4::IP_ERR_NO_ROUTE);
    diag = ipv4::get_last_send_diagnostics();
    assert(diag->stage == ipv4::SEND_STAGE_NEIGHBOR_RESOLUTION);
    assert(diag->status == ipv4::IP_ERR_NO_ROUTE);
    assert(diag->neighborOutcome == ipv4::NEIGHBOR_OUTCOME_ARP_REPLY_TIMEOUT);
    assert(diag->arpRequestStatusValid && diag->arpRequestStatus == ipv4::IP_OK);
    assert(diag->arpRequestsSent == 1 && diag->arpRepliesReceived == 0);
    assert(diag->nicActive && diag->linkState == nic::NIC_LINK_UP);

    reset_transport();
    ipv4::init();
    configure_guest(gatewayIP);
    g_answerArp = true;
    g_ipv4NicStatus = nic::NIC_ERR_TX_FULL;
    assert(ipv4::send_packet(payload, sizeof(payload), gatewayIP, ipv4::PROTO_TCP) ==
        ipv4::IP_ERR_TX_FAILED);
    diag = ipv4::get_last_send_diagnostics();
    assert(diag->stage == ipv4::SEND_STAGE_NIC_SUBMISSION);
    assert(diag->status == ipv4::IP_ERR_TX_FAILED);
    assert(diag->neighborOutcome == ipv4::NEIGHBOR_OUTCOME_ARP_REPLY);
    assert(diag->nicStatusValid && diag->nicStatus == nic::NIC_ERR_TX_FULL);
    assert(diag->arpRequestsSent == 1 && diag->arpRepliesReceived == 1);
    assert(g_ipv4FramesSent == 1);

    assert(strcmp(ipv4::status_name(ipv4::IP_ERR_NO_ROUTE), "IP_ERR_NO_ROUTE") == 0);
    assert(strcmp(ipv4::send_stage_name(ipv4::SEND_STAGE_NEIGHBOR_RESOLUTION),
        "neighbor_resolution") == 0);
    assert(strcmp(ipv4::neighbor_resolution_outcome_name(
        ipv4::NEIGHBOR_OUTCOME_ARP_REPLY_TIMEOUT), "arp_reply_timeout") == 0);
    return 0;
}
