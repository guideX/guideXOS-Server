// Hosted deterministic checks for the shared IPv4 configuration backend.
// Hardware I/O is replaced with a bounded NIC stub; this does not claim
// physical transmission.

#include <assert.h>
#include <string.h>

#include "kernel/ipv4.h"
#include "kernel/nic.h"
#include "kernel/network_config_cli.h"

using namespace kernel;

static bool g_nicActive = true;
static nic::Status g_sendStatus = nic::NIC_OK;

namespace kernel {
namespace serial {
// The test is built with ARCH_HAS_PORT_IO=0, so serial functions are inline
// no-ops from serial_debug.h.
}
namespace pit {
uint64_t ticks()
{
    static uint64_t value = 0;
    return ++value;
}
}
namespace nic {
bool is_active()
{
    return g_nicActive;
}

Status send_frame(const uint8_t*, uint16_t)
{
    return g_sendStatus;
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
    const uint32_t ip = ipv4::make_ip(192, 168, 0, 50);
    const uint32_t gateway = ipv4::make_ip(192, 168, 0, 1);
    const uint32_t dns = gateway;
    uint32_t parsed = 0;

    assert(ipv4::ip_from_string("192.168.0.50", &parsed));
    assert(parsed == ip);
    assert(!ipv4::ip_from_string("", &parsed));
    assert(!ipv4::ip_from_string("192..0.50", &parsed));
    assert(!ipv4::ip_from_string("192.168.0", &parsed));
    assert(!ipv4::ip_from_string("192.168.0.256", &parsed));
    assert(!ipv4::ip_from_string("192.168.0.1.2", &parsed));
    assert(!ipv4::ip_from_string("192.168.0.-1", &parsed));

    assert(network_config_cli::operation_from_string("show") ==
           network_config_cli::OP_SHOW);
    assert(network_config_cli::operation_from_string("static") ==
           network_config_cli::OP_STATIC);
    assert(network_config_cli::operation_from_string("automatic") ==
           network_config_cli::OP_DHCP);
    assert(network_config_cli::operation_from_string("bad") ==
           network_config_cli::OP_INVALID);
    assert(network_config_cli::parse_mask("24", &parsed) && parsed == ipv4::MASK_24);
    assert(network_config_cli::parse_mask("255.255.255.0", &parsed) &&
           parsed == ipv4::MASK_24);
    assert(!network_config_cli::parse_mask("255.0.255.0", &parsed));

    uint32_t mask = 0;
    assert(ipv4::mask_from_prefix(24, &mask));
    assert(mask == ipv4::MASK_24);
    assert(!ipv4::mask_from_prefix(33, &mask));
    assert(ipv4::is_valid_subnet_mask(ipv4::MASK_24));
    assert(!ipv4::is_valid_subnet_mask(0xFF00FF00u));
    uint8_t prefix = 0;
    assert(ipv4::prefix_from_mask(ipv4::MASK_24, &prefix) && prefix == 24);

    ipv4::init();
    const uint8_t mac[] = {0xBC, 0xE9, 0x2F, 0x9C, 0x9C, 0x91};
    ipv4::set_mac_address(mac);
    const ipv4::NetworkConfig* config = ipv4::get_config();
    assert(!config->configured);
    assert(config->mode == ipv4::CONFIG_UNCONFIGURED);

    assert(ipv4::configure_static(ip, mask, gateway, dns) == ipv4::CONFIG_OK);
    config = ipv4::get_config();
    assert(config->configured && config->mode == ipv4::CONFIG_STATIC);
    assert(config->ipAddr == ip && config->gateway == gateway && config->dns == dns);
    assert(ipv4::is_local(gateway));

    assert(ipv4::configure_static(ip, 0xFF00FF00u, gateway, dns) ==
           ipv4::CONFIG_ERR_INVALID_MASK);
    assert(ipv4::get_config()->configured && ipv4::get_config()->mode == ipv4::CONFIG_STATIC);
    assert(ipv4::configure_static(ip, mask, ipv4::make_ip(10, 0, 0, 1), dns) ==
           ipv4::CONFIG_ERR_INVALID_GATEWAY);

    ipv4::select_dhcp_mode();
    config = ipv4::get_config();
    assert(!config->configured && config->mode == ipv4::CONFIG_DHCP);
    assert(config->ipAddr == 0 && config->gateway == 0 && config->dns == 0);

    assert(ipv4::configure_dhcp(ip, mask, gateway, dns) == ipv4::CONFIG_OK);
    assert(ipv4::get_config()->mode == ipv4::CONFIG_DHCP);

    assert(ipv4::configure_qemu_defaults(ipv4::make_ip(10, 0, 2, 15), mask,
                                          ipv4::make_ip(10, 0, 2, 2),
                                          ipv4::make_ip(10, 0, 2, 3)) == ipv4::CONFIG_OK);
    assert(ipv4::get_config()->mode == ipv4::CONFIG_QEMU_DEFAULT);

    // An on-link gateway causes a generated ARP request before the IPv4
    // packet can be submitted; the no-reply result is intentional here.
    assert(ipv4::configure_static(ip, mask, gateway, dns) == ipv4::CONFIG_OK);
    ipv4::reset_stats();
    uint8_t payload[8] = {};
    g_sendStatus = nic::NIC_OK;
    assert(ipv4::send_packet(payload, sizeof(payload), gateway, ipv4::PROTO_ICMP) ==
           ipv4::IP_ERR_NO_ROUTE);
    const ipv4::Statistics* stats = ipv4::get_stats();
    assert(stats->txAttempts == 1);
    assert(stats->arpRequestsGenerated == 1);
    assert(stats->arpRequestsSent == 1);

    g_sendStatus = nic::NIC_ERR_INIT_FAIL;
    assert(ipv4::send_packet(payload, sizeof(payload), gateway, ipv4::PROTO_ICMP) ==
           ipv4::IP_ERR_NO_ROUTE);
    stats = ipv4::get_stats();
    assert(stats->txAttempts == 2);
    assert(stats->arpRequestsGenerated == 2);
    assert(stats->arpRequestsSent == 1);

    ipv4::clear_configuration();
    assert(!ipv4::get_config()->configured);
    assert(ipv4::get_config()->mode == ipv4::CONFIG_UNCONFIGURED);
    assert(ipv4::get_config()->macAddr[0] == mac[0]);

    return 0;
}
