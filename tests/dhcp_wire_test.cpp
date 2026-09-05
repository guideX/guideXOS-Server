// Deterministic hosted checks for the Phase 11 DHCP bootstrap wire path.
// These validate byte-level builders and parser invariants; they do not claim
// that a hosted process has transmitted on a physical NIC.

#include <assert.h>
#include <string.h>

#include "kernel/dhcp.h"
#include "kernel/ethernet.h"
#include "kernel/ipv4.h"
#include "kernel/nic.h"
#include "kernel/udp.h"
#include "kernel/pit.h"

using namespace kernel;

namespace kernel {
namespace dns {
void set_server(uint32_t) {}
}

namespace pit {
uint64_t ticks()
{
    static uint64_t value = 0;
    return ++value;
}
}

namespace nic {
static NICDevice g_device = {};

bool is_active()
{
    return true;
}

LinkState get_link_state()
{
    return NIC_LINK_UP;
}

LinkRefreshResult refresh_link_state()
{
    return LinkRefreshResult::Up;
}

const NICDevice* get_device()
{
    return &g_device;
}

const uint8_t* get_mac_address()
{
    return g_device.macAddress;
}

Status send_frame(const uint8_t*, uint16_t)
{
    return NIC_OK;
}

Status receive_frame(uint8_t*, uint16_t, uint16_t* received)
{
    if (received) *received = 0;
    return NIC_ERR_RX_EMPTY;
}
}
}

int main()
{
    const uint8_t mac[] = {0xEC, 0x8E, 0xB5, 0x9F, 0x36, 0x38};
    const uint32_t xid = 0x1234ABCDu;
    uint8_t dhcpPacket[sizeof(dhcp::Packet)];
    uint16_t dhcpLen = 0;

    assert(sizeof(dhcp::Packet) == 548);
    assert(dhcp::build_discover(dhcpPacket, sizeof(dhcpPacket), mac, xid,
                                &dhcpLen) == dhcp::DHCP_OK);
    assert(dhcpLen >= dhcp::DHCP_PACKET_MIN_SIZE);
    assert(reinterpret_cast<const dhcp::Packet*>(dhcpPacket)->op ==
           dhcp::BOOTREQUEST);
    const dhcp::Packet* discover =
        reinterpret_cast<const dhcp::Packet*>(dhcpPacket);
    assert(ethernet::ntohl(discover->xid) == xid);
    assert((ethernet::ntohs(discover->flags) & dhcp::FLAG_BROADCAST) != 0);
    assert(memcmp(discover->chaddr, mac, sizeof(mac)) == 0);
    const uint16_t optionsOffset = sizeof(dhcp::Packet) - dhcp::DHCP_OPTIONS_MAX;
    assert(discover->options[0] == 0x63);
    assert(discover->options[1] == 0x82);
    assert(discover->options[2] == 0x53);
    assert(discover->options[3] == 0x63);
    assert(discover->options[4] == dhcp::OPT_MSG_TYPE);
    assert(discover->options[5] == 1);
    assert(discover->options[6] == dhcp::DHCPDISCOVER);
    assert(optionsOffset == 236);

    uint8_t udpPacket[1500];
    uint16_t udpLen = 0;
    assert(udp::build_datagram(udpPacket, sizeof(udpPacket),
                               dhcp::DHCP_CLIENT_PORT,
                               dhcp::DHCP_SERVER_PORT,
                               ipv4::ADDR_ANY, ipv4::ADDR_BROADCAST,
                               dhcpPacket, dhcpLen, &udpLen) == udp::UDP_OK);
    assert(udpLen == static_cast<uint16_t>(udp::HEADER_LEN + dhcpLen));
    const udp::Header* udpHeader =
        reinterpret_cast<const udp::Header*>(udpPacket);
    assert(ethernet::ntohs(udpHeader->srcPort) == dhcp::DHCP_CLIENT_PORT);
    assert(ethernet::ntohs(udpHeader->dstPort) == dhcp::DHCP_SERVER_PORT);
    assert(ethernet::ntohs(udpHeader->length) == udpLen);
    assert(udp::verify_checksum(ipv4::ADDR_ANY, ipv4::ADDR_BROADCAST,
                                udpPacket, udpLen));

    uint8_t ipPacket[1500];
    uint16_t ipLen = 0;
    ipv4::init();
    ipv4::set_mac_address(mac);
    assert(!ipv4::is_configured());
    assert(ipv4::build_packet_from_source(
               ipPacket, sizeof(ipPacket), ipv4::ADDR_ANY,
               ipv4::ADDR_BROADCAST, ipv4::PROTO_UDP, udpPacket, udpLen,
               &ipLen) == ipv4::IP_OK);
    assert(ipLen == static_cast<uint16_t>(ipv4::MIN_HEADER_LEN + udpLen));
    ipv4::ParsedPacket parsed = {};
    assert(ipv4::parse_packet(ipPacket, ipLen, &parsed) == ipv4::IP_OK);
    assert(parsed.srcAddr == ipv4::ADDR_ANY);
    assert(parsed.dstAddr == ipv4::ADDR_BROADCAST);
    assert(parsed.protocol == ipv4::PROTO_UDP);
    assert(parsed.checksumValid);
    assert(ipv4::build_packet(ipPacket, sizeof(ipPacket), ipv4::ADDR_BROADCAST,
                              ipv4::PROTO_UDP, udpPacket, udpLen, &ipLen) ==
           ipv4::IP_ERR_NOT_CONFIGURED);

    uint8_t frame[ethernet::MAX_FRAME_LEN];
    uint16_t frameLen = 0;
    assert(ethernet::build_broadcast_frame(
               frame, sizeof(frame), mac, ethernet::ETHERTYPE_IPV4,
               ipPacket, ipLen, &frameLen) == ethernet::ETH_OK);
    ethernet::ParsedFrame parsedFrame = {};
    assert(ethernet::parse_frame(frame, frameLen, &parsedFrame) ==
           ethernet::ETH_OK);
    assert(parsedFrame.isBroadcast);
    assert(parsedFrame.etherType == ethernet::ETHERTYPE_IPV4);
    assert(memcmp(parsedFrame.srcMAC, mac, sizeof(mac)) == 0);
    assert(parsedFrame.payloadLen == ipLen);
    assert(frameLen == static_cast<uint16_t>(ethernet::HEADER_LEN + ipLen));

    return 0;
}
