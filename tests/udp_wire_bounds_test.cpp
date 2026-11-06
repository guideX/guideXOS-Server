// Hosted checks for UDP checksum validation and complete datagram handling.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/ethernet.h"
#include "kernel/ipv4.h"
#include "kernel/udp.h"

using namespace kernel;

namespace kernel {
namespace serial {
void puts(const char*) {}
void putc(char) {}
void put_hex32(uint32_t) {}
}
namespace ipv4 {
static NetworkConfig g_stubConfig = {};
const NetworkConfig* get_config() { return &g_stubConfig; }
bool is_configured() { return false; }
Status send_packet(const uint8_t*, uint16_t, uint32_t, uint8_t) { return IP_OK; }
void register_handler(uint8_t, ProtocolHandler) {}
}
}

int main()
{
    const uint32_t source = ipv4::make_ip(10, 0, 2, 15);
    const uint32_t destination = ipv4::make_ip(10, 0, 2, 3);
    const uint8_t payload[] = { 0x12, 0x34, 0x56, 0x78, 0x9A };
    uint8_t packet[udp::HEADER_LEN + sizeof(payload) + 8] = {};
    uint16_t packetLength = 0;

    assert(udp::build_datagram(packet, sizeof(packet), 53000, udp::PORT_DNS,
        source, destination, payload, sizeof(payload), &packetLength) == udp::UDP_OK);
    assert(packetLength == udp::HEADER_LEN + sizeof(payload));

    udp::ParsedDatagram parsed = {};
    assert(udp::parse_datagram(packet, packetLength, source, destination, &parsed) == udp::UDP_OK);
    assert(parsed.isValid && parsed.checksumValid && parsed.dataLen == sizeof(payload));
    assert(memcmp(parsed.data, payload, sizeof(payload)) == 0);

    // IP payload padding must not be folded into the checksum calculation.
    packet[packetLength] = 0xA5;
    packet[packetLength + 1] = 0x5A;
    assert(udp::parse_datagram(packet, static_cast<uint16_t>(packetLength + 2),
        source, destination, &parsed) == udp::UDP_OK);
    assert(parsed.dataLen == sizeof(payload));

    packet[udp::HEADER_LEN + 1] ^= 0x01;
    assert(udp::parse_datagram(packet, packetLength, source, destination, &parsed) == udp::UDP_ERR_BAD_CHECKSUM);
    assert(!parsed.isValid && !parsed.checksumValid);

    // IPv4 permits a zero UDP checksum; the rest of the datagram stays bounded.
    packet[udp::HEADER_LEN + 1] ^= 0x01;
    reinterpret_cast<udp::Header*>(packet)->checksum = 0;
    assert(udp::parse_datagram(packet, packetLength, source, destination, &parsed) == udp::UDP_OK);
    assert(parsed.isValid && parsed.checksumValid);
    return 0;
}
