// Hosted checks that UDP sockets never report a truncated datagram as complete.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/ipv4.h"
#include "kernel/dns.h"
#include "kernel/socket.h"
#include "kernel/udp.h"

using namespace kernel;

namespace {
static udp::DatagramHandler g_handler = nullptr;
static uint16_t g_boundPort = 0;
static ipv4::NetworkConfig g_config = {};
}

namespace kernel {
namespace serial {
void puts(const char*) {}
void putc(char) {}
void put_hex32(uint32_t) {}
}
namespace ipv4 {
bool is_configured() { return g_config.configured; }
const NetworkConfig* get_config() { return &g_config; }
}
namespace udp {
Status bind(uint16_t port, uint32_t, DatagramHandler handler)
{
    g_boundPort = port;
    g_handler = handler;
    return UDP_OK;
}
Status unbind(uint16_t port)
{
    if (g_boundPort == port) {
        g_boundPort = 0;
        g_handler = nullptr;
    }
    return UDP_OK;
}
uint16_t alloc_ephemeral_port() { return 53000; }
void free_ephemeral_port(uint16_t) {}
Status send(uint16_t, uint32_t, uint16_t, const uint8_t*, uint16_t) { return UDP_OK; }
}
}

static void enqueue_datagram(uint16_t port, uint8_t* bytes, uint16_t length)
{
    assert(g_handler != nullptr && g_boundPort == port);
    udp::ParsedDatagram datagram = {};
    datagram.srcPort = dns::DNS_PORT;
    datagram.dstPort = port;
    datagram.srcIP = ipv4::make_ip(10, 0, 2, 3);
    datagram.dstIP = g_config.ipAddr;
    datagram.data = bytes;
    datagram.dataLen = length;
    datagram.isValid = true;
    datagram.checksumValid = true;
    g_handler(&datagram);
}

int main()
{
    g_config = {};
    g_config.configured = true;
    g_config.ipAddr = ipv4::make_ip(10, 0, 2, 15);
    socket::init();
    const int fd = socket::udp_socket();
    assert(fd >= 0);
    assert(socket::udp_bind(fd, 53000) == socket::SOCK_OK);

    uint8_t oversized[600];
    memset(oversized, 0xA5, sizeof(oversized));
    enqueue_datagram(53000, oversized, sizeof(oversized));
    uint8_t smallBuffer[512] = {};
    socket::SockAddr source = {};
    assert(socket::udp_recvfrom(fd, smallBuffer, sizeof(smallBuffer), &source) == socket::SOCK_ERR_MSGSIZE);
    assert(!socket::udp_readable(fd));

    const uint8_t normal[] = { 1, 2, 3, 4 };
    enqueue_datagram(53000, const_cast<uint8_t*>(normal), sizeof(normal));
    assert(socket::udp_recvfrom(fd, smallBuffer, sizeof(smallBuffer), &source) == sizeof(normal));
    assert(memcmp(smallBuffer, normal, sizeof(normal)) == 0);
    assert(source.addr == ipv4::make_ip(10, 0, 2, 3) && source.port == dns::DNS_PORT);
    assert(!socket::udp_readable(fd));
    assert(socket::udp_close(fd) == socket::SOCK_OK);
    return 0;
}
