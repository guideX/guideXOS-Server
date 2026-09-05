// Hosted deterministic checks for DHCP stage gating and failure projection.
// The NIC implementation is replaced with an explicit bounded stub; this
// test does not claim physical transmission.

#include <assert.h>
#include <string.h>

#include "kernel/dhcp.h"
#include "kernel/dns.h"
#include "kernel/ethernet.h"
#include "kernel/ipv4.h"
#include "kernel/nic.h"
#include "kernel/pit.h"

using namespace kernel;

static nic::NICDevice g_device = {};
static bool g_hasDevice = true;
static bool g_active = true;
static nic::LinkState g_link = nic::NIC_LINK_UP;
static uint32_t g_linkRefreshCalls = 0;
static nic::LinkRefreshResult g_linkRefreshResult = nic::LinkRefreshResult::Down;
static nic::Status g_sendStatus = nic::NIC_ERR_TX_FULL;
static const uint8_t g_mac[] = {0xEC, 0x8E, 0xB5, 0x9F, 0x36, 0x38};

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
bool is_active()
{
    return g_active;
}

const NICDevice* get_device()
{
    return g_hasDevice ? &g_device : nullptr;
}

const uint8_t* get_mac_address()
{
    return g_mac;
}

LinkState get_link_state()
{
    return g_link;
}

LinkRefreshResult refresh_link_state()
{
    ++g_linkRefreshCalls;
    if (g_linkRefreshResult == LinkRefreshResult::Up) {
        g_link = NIC_LINK_UP;
    } else if (g_linkRefreshResult == LinkRefreshResult::Down) {
        g_link = NIC_LINK_DOWN;
    } else {
        g_link = NIC_LINK_READ_ERROR;
    }
    return g_linkRefreshResult;
}

Status send_frame(const uint8_t*, uint16_t)
{
    if (g_sendStatus == NIC_OK) {
        g_device.tx.descriptorPublications++;
        g_device.tx.descriptorSubmissions++;
        g_device.tx.descriptorCompletions++;
        g_device.tx.tailBefore = 0;
        g_device.tx.tailAfter = 1;
        g_device.tx.doorbellReadbackMatches = true;
        g_device.tx.descriptorPublished = true;
        g_device.tx.lastStatus = NIC_OK;
    } else if (g_sendStatus == nic::NIC_ERR_INIT_FAIL) {
        // The descriptor was published and the doorbell was accepted, but
        // hardware never wrote DD. This is the physical Phase 12 boundary.
        g_device.tx.descriptorPublications++;
        g_device.tx.descriptorSubmissions++;
        g_device.tx.tailBefore = 0;
        g_device.tx.tailAfter = 1;
        g_device.tx.doorbellReadbackMatches = true;
        g_device.tx.descriptorPublished = true;
        g_device.tx.hardwareTimeouts++;
        g_device.tx.failureReason = nic::TxFailureReason::DescriptorNotConsumed;
        g_device.tx.lastStatus = g_sendStatus;
    } else {
        g_device.tx.driverErrors++;
        g_device.tx.lastStatus = g_sendStatus;
    }
    return g_sendStatus;
}

Status receive_frame(uint8_t*, uint16_t, uint16_t* received)
{
    if (received) *received = 0;
    return NIC_ERR_RX_EMPTY;
}
}
}

static void set_ready_i219()
{
    g_device = {};
    g_device.driverBound = true;
    g_device.mmioMapped = true;
    g_device.mmioProbeAttempted = true;
    g_device.mmioProbePassed = true;
    g_device.macReadAttempted = true;
    g_device.macValid = true;
    g_device.rxRingInitialized = true;
    g_device.txRingInitialized = true;
    g_device.vendorId = nic::PCI_VENDOR_INTEL;
    g_device.deviceId = nic::PCI_DEVICE_I219_LM;
    g_device.resetAttempted = true;
    g_device.resetCompleted = true;
    g_device.phyProbeAttempted = true;
    g_device.phyAccess = nic::NIC_PHY_OK;
    g_device.nicRegistered = true;
    g_device.active = true;
    g_device.driverReady = true;
    g_device.initStage = nic::NIC_INIT_READY;
    memcpy(g_device.macAddress, g_mac, sizeof(g_mac));
}

int main()
{
    ipv4::init();
    ipv4::set_mac_address(g_mac);
    dhcp::init();

    g_hasDevice = false;
    dhcp::note_command_invocation();
    assert(dhcp::discover() == dhcp::DHCP_ERR_NO_NIC);
    assert(dhcp::get_state() == dhcp::STATE_ERROR);
    assert(dhcp::get_diagnostics()->commandInvocations == 1);
    assert(!dhcp::get_diagnostics()->suitableInterfaceFound);

    g_hasDevice = true;
    g_active = true;
    g_link = nic::NIC_LINK_UP;
    g_device = {};
    g_device.driverBound = true;
    dhcp::note_command_invocation();
    assert(dhcp::discover() == dhcp::DHCP_ERR_NOT_READY);
    assert(dhcp::get_diagnostics()->suitableInterfaceFound);
    assert(!dhcp::get_diagnostics()->interfaceReady);
    assert(dhcp::get_diagnostics()->failureStage == dhcp::FAILURE_READY);

    set_ready_i219();
    g_link = nic::NIC_LINK_DOWN;
    g_linkRefreshCalls = 0;
    g_linkRefreshResult = nic::LinkRefreshResult::Down;
    dhcp::note_command_invocation();
    assert(dhcp::discover() == dhcp::DHCP_ERR_LINK_DOWN);
    assert(dhcp::get_diagnostics()->interfaceReady);
    assert(!dhcp::get_diagnostics()->linkUp);
    assert(g_linkRefreshCalls == 1);
    assert(dhcp::get_diagnostics()->linkRefreshAttempted);
    assert(dhcp::get_diagnostics()->linkRefreshResult ==
           nic::LinkRefreshResult::Down);
    assert(dhcp::get_diagnostics()->failureStage == dhcp::FAILURE_LINK);

    set_ready_i219();
    g_link = nic::NIC_LINK_DOWN;
    g_linkRefreshCalls = 0;
    g_linkRefreshResult = nic::LinkRefreshResult::ReadError;
    dhcp::note_command_invocation();
    assert(dhcp::discover() == dhcp::DHCP_ERR_LINK_UNKNOWN);
    assert(g_linkRefreshCalls == 1);
    assert(!dhcp::get_diagnostics()->linkUp);
    assert(dhcp::get_diagnostics()->failureStage == dhcp::FAILURE_LINK);
    assert(strstr(dhcp::get_diagnostics()->failureReason, "unavailable") != nullptr);

    set_ready_i219();
    g_link = nic::NIC_LINK_DOWN;
    g_linkRefreshCalls = 0;
    g_linkRefreshResult = nic::LinkRefreshResult::Up;
    g_sendStatus = nic::NIC_ERR_TX_FULL;
    dhcp::note_command_invocation();
    assert(dhcp::discover() == dhcp::DHCP_ERR_SEND_FAIL);
    const dhcp::Diagnostics* diagnostics = dhcp::get_diagnostics();
    const dhcp::Statistics* stats = dhcp::get_stats();
    assert(diagnostics->commandInvocations == 5);
    assert(g_linkRefreshCalls == 1);
    assert(diagnostics->linkRefreshAttempted);
    assert(diagnostics->linkRefreshResult == nic::LinkRefreshResult::Up);
    assert(diagnostics->linkUp);
    assert(diagnostics->discoverPacketBuilt);
    assert(diagnostics->transactionId != 0);
    assert(diagnostics->frameLength > diagnostics->dhcpPacketLength);
    assert(diagnostics->sourcePort == dhcp::DHCP_CLIENT_PORT);
    assert(diagnostics->destinationPort == dhcp::DHCP_SERVER_PORT);
    assert(!diagnostics->txDescriptorAccepted);
    assert(!diagnostics->txTailAdvanced);
    assert(!diagnostics->txCompletionObserved);
    assert(diagnostics->txDriverError);
    assert(diagnostics->failureStage == dhcp::FAILURE_TX_SUBMIT);
    assert(!diagnostics->waitForOfferBegun);
    assert(stats->discoverBuilt == 1);
    assert(stats->discoverAttempts == 4);
    assert(stats->discoverSubmissions == 0);
    assert(stats->discoverCompletions == 0);
    assert(stats->discoversSent == 0);
    assert(stats->discoverSendFailures == 4);
    assert(stats->timeouts == 0);
    assert(dhcp::get_lease()->valid == false);
    assert(!ipv4::get_config()->configured);
    assert(ipv4::get_config()->mode == ipv4::CONFIG_DHCP);
    assert(strcmp(dhcp::failure_stage_name(diagnostics->failureStage),
                  "TX submit") == 0);

    // A descriptor timeout is not a pre-submit failure. The upper DHCP
    // counters must retain publication/TDT evidence while keeping completion
    // and lease state false. The poisoned physical ring stops unsafe retries,
    // so one attempt is recorded rather than reusing an owned buffer.
    dhcp::init();
    set_ready_i219();
    g_link = nic::NIC_LINK_DOWN;
    g_linkRefreshCalls = 0;
    g_linkRefreshResult = nic::LinkRefreshResult::Up;
    g_sendStatus = nic::NIC_ERR_INIT_FAIL;
    dhcp::note_command_invocation();
    assert(dhcp::discover() == dhcp::DHCP_ERR_SEND_FAIL);
    diagnostics = dhcp::get_diagnostics();
    stats = dhcp::get_stats();
    assert(diagnostics->txDescriptorPublished);
    assert(diagnostics->txDescriptorAccepted);
    assert(diagnostics->txDoorbellObserved);
    assert(!diagnostics->txCompletionObserved);
    assert(diagnostics->txCompletionTimeout);
    assert(diagnostics->txFailureReason ==
           nic::TxFailureReason::DescriptorNotConsumed);
    assert(diagnostics->failureStage == dhcp::FAILURE_TX_COMPLETION);
    assert(stats->discoverBuilt == 1);
    assert(stats->discoverAttempts == 1);
    assert(stats->discoverSubmissions == 1);
    assert(stats->discoverCompletions == 0);
    assert(stats->discoversSent == 0);
    assert(stats->discoverSendFailures == 1);
    assert(stats->discoverTxTimeouts == 1);
    assert(!dhcp::get_lease()->valid);
    assert(!ipv4::get_config()->configured);

    return 0;
}
