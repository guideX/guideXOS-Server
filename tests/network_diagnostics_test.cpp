// Focused hosted checks for the Phase 1 network diagnostic helpers.

#include <assert.h>
#include <string.h>

#include "kernel/network_status.h"
#include "kernel/ethernet.h"
#include "kernel/nic.h"

using namespace kernel;

static_assert(sizeof(nic::RxDescriptor) == 16, "RX descriptor ABI must remain 16 bytes");
static_assert(sizeof(nic::TxDescriptor) == 16, "TX descriptor ABI must remain 16 bytes");
static_assert((nic::NUM_RX_DESC & (nic::NUM_RX_DESC - 1)) == 0,
              "RX ring arithmetic expects a power-of-two descriptor count");
static_assert((nic::NUM_TX_DESC & (nic::NUM_TX_DESC - 1)) == 0,
              "TX ring arithmetic expects a power-of-two descriptor count");

int main()
{
    network_status::Inputs input = {};
    assert(network_status::classify(input) == network_status::STATE_NO_ADAPTER);

    input.adapterPresent = true;
    assert(network_status::classify(input) == network_status::STATE_DRIVER_UNAVAILABLE);

    input.driverBound = true;
    assert(network_status::classify(input) == network_status::STATE_ADAPTER_DETECTED);

    input.driverReady = true;
    assert(network_status::classify(input) == network_status::STATE_DISCONNECTED);

    input.linkUp = true;
    assert(network_status::classify(input) == network_status::STATE_ACQUIRING_ADDRESS);

    input.ipv4Configured = true;
    assert(network_status::classify(input) == network_status::STATE_IPV4_CONFIGURED);

    input.gatewayConfigured = true;
    assert(network_status::classify(input) == network_status::STATE_LOCAL_NETWORK_CONFIGURED);

    input.connectivityVerified = true;
    assert(network_status::classify(input) == network_status::STATE_ONLINE);

    assert(network_status::is_supported_intel_e1000(0x8086, 0x100E));
    assert(network_status::is_supported_intel_e1000(0x8086, 0x10D3));
    assert(network_status::is_supported_intel_e1000(0x8086, 0x153A));
    assert(!network_status::is_supported_intel_e1000(0x8086, 0x156F));
    assert(!network_status::is_supported_intel_e1000(0x10EC, 0x8168));
    assert(!network_status::is_supported_intel_e1000(0x8086, 0x1234));

    const uint8_t validMac[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t zeroMac[] = {0, 0, 0, 0, 0, 0};
    const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t multicastMac[] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
    assert(nic::is_valid_station_mac(validMac));
    assert(!nic::is_valid_station_mac(zeroMac));
    assert(!nic::is_valid_station_mac(broadcastMac));
    assert(!nic::is_valid_station_mac(multicastMac));

    const uint8_t mac[] = {0xCA, 0xFE, 0xB0, 0x0C, 0xD0, 0x5E};
    char macString[18];
    ethernet::mac_to_string(mac, macString);
    assert(strcmp(macString, "CA:FE:B0:0C:D0:5E") == 0);

    return 0;
}
