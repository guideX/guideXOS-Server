// Focused hosted checks for the Phase 1 network diagnostic helpers.

#include <assert.h>
#include <string.h>

#include "kernel/network_status.h"
#include "kernel/ethernet.h"
#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

static_assert(sizeof(nic::RxDescriptor) == 16, "RX descriptor ABI must remain 16 bytes");
static_assert(sizeof(nic::TxDescriptor) == 16, "TX descriptor ABI must remain 16 bytes");
static_assert((nic::NUM_RX_DESC & (nic::NUM_RX_DESC - 1)) == 0,
              "RX ring arithmetic expects a power-of-two descriptor count");
static_assert((nic::NUM_TX_DESC & (nic::NUM_TX_DESC - 1)) == 0,
              "TX ring arithmetic expects a power-of-two descriptor count");
static_assert(shell::NICINFO_BRIEF_EXPECTED_LINES == 18,
              "nicinfo brief device-present output line count changed");
static_assert(shell::NICINFO_BRIEF_EXPECTED_LINES <= shell::NICINFO_BRIEF_MAX_LINES,
              "nicinfo brief must fit its declared bounded output");

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
    assert(network_status::is_supported_intel_e1000(0x8086, 0x156F));
    assert(strcmp(network_status::driver_name(0x8086, 0x156F),
                  "intel-i219-lm (PCH)") == 0);
    assert(!network_status::is_supported_intel_e1000(0x8086, 0x24F3));
    assert(!network_status::is_supported_intel_e1000(0x10EC, 0x8168));
    assert(!network_status::is_supported_intel_e1000(0x8086, 0x1234));

    assert(shell::nicinfo_mode_from_arg(nullptr) == shell::NICINFO_MODE_FULL);
    assert(shell::nicinfo_mode_from_arg("") == shell::NICINFO_MODE_FULL);
    assert(shell::nicinfo_mode_from_arg("brief") == shell::NICINFO_MODE_BRIEF);
    assert(shell::nicinfo_mode_from_arg("Brief") == shell::NICINFO_MODE_INVALID);
    assert(shell::nicinfo_mode_from_arg("unknown") == shell::NICINFO_MODE_INVALID);

    const uint8_t validMac[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t zeroMac[] = {0, 0, 0, 0, 0, 0};
    const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t multicastMac[] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
    assert(nic::is_valid_station_mac(validMac));
    assert(!nic::is_valid_station_mac(zeroMac));
    assert(!nic::is_valid_station_mac(broadcastMac));
    assert(!nic::is_valid_station_mac(multicastMac));

    nic::NICDevice readyDevice = {};
    readyDevice.driverBound = true;
    readyDevice.mmioMapped = true;
    readyDevice.mmioProbeAttempted = true;
    readyDevice.mmioProbePassed = true;
    readyDevice.macReadAttempted = true;
    readyDevice.macValid = true;
    readyDevice.rxRingInitialized = true;
    readyDevice.txRingInitialized = true;
    readyDevice.deviceId = nic::PCI_DEVICE_I219_LM;
    readyDevice.resetAttempted = true;
    readyDevice.resetCompleted = true;
    readyDevice.phyProbeAttempted = true;
    readyDevice.phyAccess = nic::NIC_PHY_OK;
    readyDevice.nicRegistered = true;
    readyDevice.active = true;
    readyDevice.driverReady = true;
    readyDevice.initStage = nic::NIC_INIT_READY;
    assert(nic::hardware_init_complete(readyDevice));
    assert(nic::is_driver_ready(readyDevice));

    // A discovered/bound I219 with a failed PHY stage must not project as
    // driver-ready, even if a stale registration/active bit is present.
    readyDevice.phyAccess = nic::NIC_PHY_FAILED;
    readyDevice.initStage = nic::NIC_INIT_PHY;
    readyDevice.driverReady = false;
    assert(!nic::hardware_init_complete(readyDevice));
    assert(!nic::is_driver_ready(readyDevice));
    assert(strcmp(nic::init_stage_name(readyDevice.initStage), "PHY") == 0);
    assert(strcmp(nic::init_stage_name(nic::NIC_INIT_PHY), "PHY") == 0);
    assert(strcmp(nic::phy_access_state_name(nic::NIC_PHY_FAILED), "failed") == 0);

    readyDevice.lastFailureStage = nic::NIC_INIT_PHY;
    readyDevice.lastInitFailure[0] = 'P';
    readyDevice.lastInitFailure[1] = 'H';
    readyDevice.lastInitFailure[2] = 'Y';
    readyDevice.lastInitFailure[3] = '\0';
    assert(readyDevice.lastFailureStage == nic::NIC_INIT_PHY);
    assert(strcmp(readyDevice.lastInitFailure, "PHY") == 0);

    assert(strcmp(nic::link_state_name(nic::NIC_LINK_UP), "UP") == 0);
    assert(strcmp(nic::link_state_name(nic::NIC_LINK_DOWN), "DOWN") == 0);
    assert(strcmp(nic::link_state_name(nic::NIC_LINK_UNKNOWN), "UNKNOWN") == 0);

    // Registration and readiness are distinct state gates.
    readyDevice.phyAccess = nic::NIC_PHY_OK;
    readyDevice.initStage = nic::NIC_INIT_TX_RING;
    readyDevice.driverReady = false;
    assert(nic::hardware_init_complete(readyDevice));
    assert(!nic::is_driver_ready(readyDevice));

    const uint8_t mac[] = {0xCA, 0xFE, 0xB0, 0x0C, 0xD0, 0x5E};
    char macString[18];
    ethernet::mac_to_string(mac, macString);
    assert(strcmp(macString, "CA:FE:B0:0C:D0:5E") == 0);

    return 0;
}
