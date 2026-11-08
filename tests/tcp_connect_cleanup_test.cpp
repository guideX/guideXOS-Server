#include <assert.h>
#include <stdint.h>

#include "kernel/ethernet.h"
#include "kernel/ipv4.h"
#include "kernel/tcp.h"

using namespace kernel;

namespace {
static ipv4::NetworkConfig g_config = {};
static ipv4::Status g_sendStatus = ipv4::IP_ERR_NO_ROUTE;
static uint64_t g_ticks = 0;
static uint32_t g_sendCalls = 0;
static bool g_sawSyn = false;
static uint32_t g_registeredProtocol = 0;
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
namespace ipv4 {
const NetworkConfig* get_config() { return &g_config; }
bool is_configured() { return g_config.configured; }
Status send_packet(const uint8_t* payload, uint16_t length,
                   uint32_t, uint8_t protocol)
{
    (void)length;
    assert(payload != nullptr && protocol == PROTO_TCP);
    ++g_sendCalls;
    const tcp::Header* segment = reinterpret_cast<const tcp::Header*>(payload);
    g_sawSyn = (segment->flags & tcp::FLAG_SYN) != 0;
    return g_sendStatus;
}
void register_handler(uint8_t protocol, ProtocolHandler)
{
    g_registeredProtocol = protocol;
}
void unregister_handler(uint8_t) {}
void ip_to_string(uint32_t ip, char* output)
{
    (void)ip;
    if (output) output[0] = '\0';
}
}
}

int main()
{
    g_config = {};
    g_config.configured = true;
    g_config.ipAddr = ipv4::make_ip(10, 0, 2, 15);
    g_sendStatus = ipv4::IP_ERR_NO_ROUTE;
    g_ticks = 0;
    g_sendCalls = 0;
    g_sawSyn = false;
    g_registeredProtocol = 0;

    tcp::init();
    assert(g_registeredProtocol == ipv4::PROTO_TCP);

    const uint32_t destination = ipv4::make_ip(10, 0, 2, 2);
    const int failedSocket = tcp::tcp_socket();
    assert(failedSocket >= 0);
    assert(tcp::tcp_connect(failedSocket, destination, 443) == tcp::TCP_ERR_NETDOWN);
    assert(g_sendCalls == 1 && g_sawSyn);
    assert(tcp::tcp_getstate(failedSocket) == tcp::STATE_CLOSED);
    assert(tcp::get_stats()->activeConnections == 0);
    assert(tcp::tcp_close(failedSocket) == tcp::TCP_OK);
    assert(tcp::get_stats()->activeConnections == 0);
    assert(tcp::tcp_close(failedSocket) == tcp::TCP_ERR_INVALID);

    g_sendStatus = ipv4::IP_OK;
    const int pendingSocket = tcp::tcp_socket();
    assert(pendingSocket >= 0);
    assert(tcp::tcp_connect(pendingSocket, destination, 443) == tcp::TCP_OK);
    assert(tcp::tcp_getstate(pendingSocket) == tcp::STATE_SYN_SENT);
    assert(g_sendCalls == 2 && g_sawSyn);
    assert(tcp::tcp_close(pendingSocket) == tcp::TCP_OK);
    assert(tcp::get_stats()->activeConnections == 0);

    int sockets[tcp::MAX_SOCKETS];
    for (uint8_t i = 0; i < tcp::MAX_SOCKETS; ++i) {
        sockets[i] = tcp::tcp_socket();
        assert(sockets[i] >= 0);
    }
    assert(tcp::tcp_socket() == tcp::TCP_ERR_NOSOCK);
    for (uint8_t i = 0; i < tcp::MAX_SOCKETS; ++i) {
        assert(tcp::tcp_close(sockets[i]) == tcp::TCP_OK);
    }
    assert(tcp::get_stats()->activeConnections == 0);
    return 0;
}
