// Focused hosted checks for the Phase 1 network diagnostic helpers.

#include <assert.h>
#include <string.h>

#include "kernel/network_status.h"
#include "kernel/ethernet.h"

using namespace kernel;

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
    assert(!network_status::is_supported_intel_e1000(0x10EC, 0x8168));
    assert(!network_status::is_supported_intel_e1000(0x8086, 0x1234));

    const uint8_t mac[] = {0xCA, 0xFE, 0xB0, 0x0C, 0xD0, 0x5E};
    char macString[18];
    ethernet::mac_to_string(mac, macString);
    assert(strcmp(macString, "CA:FE:B0:0C:D0:5E") == 0);

    return 0;
}
