// Focused hosted checks for the Phase 1 network diagnostic helpers.

#include <assert.h>
#include <string.h>

#include "kernel/network_status.h"
#include "kernel/ethernet.h"
#include "kernel/nic.h"
#include "kernel/shell.h"
#include "kernel/ipv4.h"
#include "kernel/text_field.h"

using namespace kernel;

static_assert(sizeof(nic::RxDescriptor) == 16, "RX descriptor ABI must remain 16 bytes");
static_assert(sizeof(nic::TxDescriptor) == 16, "TX descriptor ABI must remain 16 bytes");
static_assert(offsetof(nic::TxDescriptor, bufferAddr) == 0, "TX buffer offset changed");
static_assert(offsetof(nic::TxDescriptor, length) == 8, "TX length offset changed");
static_assert(offsetof(nic::TxDescriptor, cmd) == 11, "TX command offset changed");
static_assert(offsetof(nic::TxDescriptor, status) == 12, "TX status offset changed");
static_assert((nic::NUM_RX_DESC & (nic::NUM_RX_DESC - 1)) == 0,
              "RX ring arithmetic expects a power-of-two descriptor count");
static_assert((nic::NUM_TX_DESC & (nic::NUM_TX_DESC - 1)) == 0,
              "TX ring arithmetic expects a power-of-two descriptor count");
static_assert(shell::NICINFO_BRIEF_EXPECTED_LINES == 19,
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
    assert(network_status::classify(input) == network_status::STATE_IPV4_UNCONFIGURED);
    assert(strcmp(network_status::state_to_string(network_status::STATE_IPV4_UNCONFIGURED),
                  "IPv4 unconfigured") == 0);

    input.ipv4ConfigurationPending = true;
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

    // Keep the emulated discrete E1000 and the integrated I219/PCH path
    // explicit. The vendor is part of family identification.
    assert(nic::device_family_for(0x8086, 0x100E) == nic::DeviceFamily::E1000);
    assert(nic::device_family_for(0x8086, 0x10D3) == nic::DeviceFamily::E1000);
    assert(nic::device_family_for(0x8086, 0x153A) == nic::DeviceFamily::E1000);
    assert(nic::device_family_for(0x8086, 0x156F) == nic::DeviceFamily::I219Pch);
    assert(nic::device_family_for(0x10EC, 0x156F) == nic::DeviceFamily::Unsupported);
    assert(nic::device_family_for(0x8086, 0x1234) == nic::DeviceFamily::Unsupported);
    assert(strcmp(nic::device_family_name(nic::DeviceFamily::I219Pch),
                  "I219/PCH") == 0);

    uint64_t physical = 0;
    assert(nic::translate_kernel_dma_address(0x100000ULL, 0x400000ULL,
                                             &physical));
    assert(physical == 0x400000ULL);
    assert(nic::translate_kernel_dma_address(0x125678ULL, 0x800000ULL,
                                             &physical));
    assert(physical == 0x825678ULL);
    assert(!nic::translate_kernel_dma_address(0x0FFFFFULL, 0x400000ULL,
                                              &physical));
    assert(!nic::translate_kernel_dma_address(0x100000ULL, 0, &physical));
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::DescriptorNotConsumed),
                  "TX_DESCRIPTOR_NOT_CONSUMED") == 0);

    // I219 standard page-0 IEEE PHY registers use MDI address 2. Address 1
    // is reserved for the PCH general/high-page register view.
    assert(nic::I219_PHY_ADDRESS == 2);
    assert(nic::I219_GENERAL_PHY_ADDRESS == 1);
    assert(strcmp(nic::phy_address_source_name(nic::PhyAddressSource::FixedFamily),
                  "fixed-family") == 0);

    const uint32_t mdicCommand = nic::encode_mdic_read_command(2, 2);
    assert(mdicCommand == 0x08420000u);
    assert(nic::mdic_phy_address(mdicCommand) == 2);
    assert(nic::mdic_register_address(mdicCommand) == 2);
    const uint32_t maskedCommand = nic::encode_mdic_read_command(0xFFu, 0xFFu);
    assert(nic::mdic_phy_address(maskedCommand) == 31);
    assert(nic::mdic_register_address(maskedCommand) == 31);
    const uint32_t mdicResponse = mdicCommand | nic::E1000_MDIC_READY | 0x0154u;
    assert(nic::mdic_ready(mdicResponse));
    assert(!nic::mdic_error(mdicResponse));
    assert(nic::mdic_response_fields_match(mdicResponse, 2, 2));
    assert(nic::mdic_data_is_valid(0x0000u));
    assert(!nic::mdic_data_is_valid(0xFFFFu));
    assert(nic::phy_identifier_is_valid(0x0154u, 0x0C00u));
    assert(!nic::phy_identifier_is_valid(0x0000u, 0x0C00u));
    assert(!nic::phy_identifier_is_valid(0x0154u, 0x0000u));
    assert(!nic::phy_identifier_is_valid(0xFFFFu, 0x0C00u));
    assert(!nic::phy_identifier_is_valid(0x0154u, 0xFFFFu));
    assert(nic::mdic_error(mdicResponse | nic::E1000_MDIC_ERROR));

    assert(shell::nicinfo_mode_from_arg(nullptr) == shell::NICINFO_MODE_FULL);
    assert(shell::nicinfo_mode_from_arg("") == shell::NICINFO_MODE_FULL);
    assert(shell::nicinfo_mode_from_arg("brief") == shell::NICINFO_MODE_BRIEF);
    assert(shell::nicinfo_mode_from_arg("link") == shell::NICINFO_MODE_LINK);
    assert(shell::nicinfo_mode_from_arg("tx") == shell::NICINFO_MODE_TX);
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
    readyDevice.vendorId = nic::PCI_VENDOR_INTEL;
    readyDevice.deviceId = nic::PCI_DEVICE_I219_LM;
    readyDevice.resetAttempted = true;
    readyDevice.resetCompleted = true;
    readyDevice.phyProbeAttempted = true;
    readyDevice.phyAccess = nic::NIC_PHY_OK;
    readyDevice.phyAddress = nic::I219_PHY_ADDRESS;
    readyDevice.phyAddressSource = nic::PhyAddressSource::FixedFamily;
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
    assert(strcmp(nic::link_state_name(nic::NIC_LINK_READ_ERROR), "ERROR") == 0);

    // TX provenance is based on observable descriptor deltas. A completed
    // descriptor, a submission that never completes, and a pre-submit error
    // must remain distinguishable without touching hardware in this test.
    nic::TxDiagnostics before = {};
    nic::TxDiagnostics completed = before;
    completed.descriptorPublications = 1;
    completed.descriptorSubmissions = 1;
    completed.descriptorCompletions = 1;
    completed.tailBefore = 3;
    completed.tailAfter = 4;
    completed.doorbellReadbackMatches = true;
    nic::TxEvidence completedEvidence =
        nic::observe_tx(before, completed, nic::NIC_OK);
    assert(completedEvidence.descriptorAccepted);
    assert(completedEvidence.descriptorPublished);
    assert(completedEvidence.doorbellObserved);
    assert(completedEvidence.tailAdvanced);
    assert(completedEvidence.completionObserved);
    assert(!completedEvidence.completionTimedOut);
    assert(!completedEvidence.driverError);

    nic::TxDiagnostics timedOut = before;
    timedOut.descriptorPublications = 1;
    timedOut.descriptorSubmissions = 1;
    timedOut.tailBefore = 4;
    timedOut.tailAfter = 5;
    timedOut.doorbellReadbackMatches = true;
    timedOut.hardwareTimeouts = 1;
    timedOut.driverErrors = 0;
    nic::TxEvidence timeoutEvidence =
        nic::observe_tx(before, timedOut, nic::NIC_ERR_INIT_FAIL);
    assert(timeoutEvidence.descriptorAccepted);
    assert(timeoutEvidence.descriptorPublished);
    assert(timeoutEvidence.doorbellObserved);
    assert(timeoutEvidence.tailAdvanced);
    assert(!timeoutEvidence.completionObserved);
    assert(timeoutEvidence.completionTimedOut);
    assert(!timeoutEvidence.driverError);

    nic::TxDiagnostics rejected = before;
    rejected.driverErrors = 1;
    nic::TxEvidence rejectedEvidence =
        nic::observe_tx(before, rejected, nic::NIC_ERR_TX_FULL);
    assert(!rejectedEvidence.descriptorAccepted);
    assert(!rejectedEvidence.tailAdvanced);
    assert(!rejectedEvidence.completionObserved);
    assert(!rejectedEvidence.completionTimedOut);
    assert(rejectedEvidence.driverError);

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

    // The IPv4 dialog uses the same fixed-storage control semantics as the
    // hosted control check: focus gates edits, caret placement is explicit,
    // numeric input is bounded, and both deletion directions work.
    text_field::TextField field = {};
    field.enabled = true;
    text_field::set_text(field, "192.168.0.50");
    text_field::set_focus(field, true);
    text_field::set_caret(field, 3);
    assert(text_field::handle_key(field, '9'));
    assert(strcmp(field.text, "1929.168.0.50") == 0);
    assert(text_field::handle_key(field, '\b'));
    assert(strcmp(field.text, "192.168.0.50") == 0);
    text_field::set_caret(field, 4);
    assert(text_field::handle_key(field, 0x106u));
    assert(strcmp(field.text, "192.68.0.50") == 0);
    assert(!text_field::handle_key(field, 'x'));

    return 0;
}
