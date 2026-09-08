// Hosted semantic checks for the Phase 15 I219/PCH TX-engine audit.
// These tests validate field meaning and lifecycle evidence; they do not
// claim to emulate or prove a physical I219 descriptor fetch.

#include <assert.h>
#include <string.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

int main()
{
    // 8086:156F is the SPT PCH I219-LM generation. The older I217 remains on
    // the generic E1000 family path and must not receive the SPT correction.
    assert(nic::device_family_for(nic::PCI_VENDOR_INTEL,
                                  nic::PCI_DEVICE_I219_LM) ==
           nic::DeviceFamily::I219Pch);
    assert(nic::device_family_for(nic::PCI_VENDOR_INTEL,
                                  nic::PCI_DEVICE_I217) ==
           nic::DeviceFamily::E1000);
    assert(nic::pci_dma_access_enabled(
        nic::PCI_COMMAND_MEMORY_SPACE | nic::PCI_COMMAND_BUS_MASTER));
    assert(!nic::pci_dma_access_enabled(nic::PCI_COMMAND_MEMORY_SPACE));
    assert(!nic::pci_dma_access_enabled(nic::PCI_COMMAND_BUS_MASTER));

    assert(nic::E1000_TXDCTL1 == 0x3928u);
    assert(nic::E1000_TARC1 == 0x3940u);
    assert(nic::E1000_PBA == 0x1000u);
    assert(nic::E1000_FWSM == 0x5B54u);

    // The upstream SPT policy must replace PTHRESH/WTHRESH, set GRAN and
    // COUNT_DESC, and leave HTHRESH untouched because upstream does not
    // program that field in e1000_init_hw_ich8lan().
    const uint32_t currentTxdctl = 0x80000000u |
        (7u << 8) | (2u << 16) | 3u;
    const uint32_t configuredTxdctl =
        nic::i219_spt_txdctl_configuration(currentTxdctl);
    assert((configuredTxdctl & nic::E1000_TXDCTL_COUNT_DESC) != 0u);
    assert((configuredTxdctl & nic::E1000_TXDCTL_GRAN) != 0u);
    assert((configuredTxdctl & nic::E1000_TXDCTL_PTHRESH_MASK) == 31u);
    assert(((configuredTxdctl & nic::E1000_TXDCTL_HTHRESH_MASK) >> 8) == 7u);
    assert(((configuredTxdctl & nic::E1000_TXDCTL_WTHRESH_MASK) >> 16) == 1u);
    assert((configuredTxdctl & 0x80000000u) != 0u);
    assert(nic::i219_spt_txdctl_configuration_valid(configuredTxdctl));
    assert(!nic::i219_spt_txdctl_configuration_valid(0u));

    // TCTL field semantics used by the physical diagnostic.
    const uint32_t upstreamLikeTctl = nic::E1000_TCTL_EN |
        nic::E1000_TCTL_PSP | (15u << nic::E1000_TCTL_CT_SHIFT) |
        (63u << nic::E1000_TCTL_COLD_SHIFT) | nic::E1000_TCTL_RTLC;
    assert((upstreamLikeTctl & nic::E1000_TCTL_EN) != 0u);
    assert((upstreamLikeTctl & nic::E1000_TCTL_PSP) != 0u);
    assert((upstreamLikeTctl & nic::E1000_TCTL_RTLC) != 0u);
    assert((upstreamLikeTctl & nic::E1000_TCTL_MULR) == 0u);
    assert(((upstreamLikeTctl & nic::E1000_TCTL_CT_MASK) >>
            nic::E1000_TCTL_CT_SHIFT) == 15u);
    assert(((upstreamLikeTctl & nic::E1000_TCTL_COLD_MASK) >>
            nic::E1000_TCTL_COLD_SHIFT) == 63u);
    assert((nic::E1000_TIPG_DEFAULT & 0x3FFu) == 10u);
    assert(((nic::E1000_TIPG_DEFAULT >> 10) & 0x3FFu) == 10u);
    assert(((nic::E1000_TIPG_DEFAULT >> 20) & 0x3FFu) == 10u);

    // Register snapshots prove both address/length correctness and
    // persistence across the pre-doorbell, doorbell and timeout boundaries.
    const uint64_t ring = 0x1234567887654320ULL;
    const uint32_t length = 1024u;
    nic::TxRegisterSnapshot initial = {};
    initial.tdbal = nic::dma_address_register_low(ring);
    initial.tdbah = nic::dma_address_register_high(ring);
    initial.tdlen = length;
    initial.valid = true;
    nic::TxRegisterSnapshot before = initial;
    nic::TxRegisterSnapshot preDoorbell = initial;
    nic::TxRegisterSnapshot afterDoorbell = initial;
    nic::TxRegisterSnapshot final = initial;
    afterDoorbell.tdt = 1u;
    final.tdt = 1u;
    assert(nic::tx_ring_registers_match(initial, ring, length));
    assert(nic::tx_ring_registers_persisted(
        initial, before, preDoorbell, afterDoorbell, final, ring, length));
    final.tdlen = 128u;
    assert(!nic::tx_ring_registers_persisted(
        initial, before, preDoorbell, afterDoorbell, final, ring, length));

    // The raw descriptor evidence remains a two-word 16-byte legacy ABI.
    nic::TxDescriptor descriptor = {};
    descriptor.bufferAddr = 0xFEDCBA9876543210ULL;
    descriptor.length = 0x1234u;
    descriptor.cmd = nic::E1000_TXD_CMD_EOP | nic::E1000_TXD_CMD_IFCS |
                     nic::E1000_TXD_CMD_RS;
    descriptor.status = 0u;
    assert(nic::tx_descriptor_raw_word1(descriptor) ==
           (0x1234ULL | (0x0BULL << 24)));

    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::FetchControlInvalid),
                  "TX_FETCH_CONTROL_INVALID") == 0);
    assert(shell::NICINFO_TX_BRIEF_EXPECTED_LINES <=
           shell::NICINFO_TX_BRIEF_MAX_LINES);
    assert(shell::nicinfo_mode_from_arg("tx") == shell::NICINFO_MODE_TX);

    // A descriptor timeout remains a poisoned, non-retryable evidence state.
    nic::TxDiagnostics beforeTx = {};
    nic::TxDiagnostics timeoutTx = beforeTx;
    timeoutTx.descriptorPublications = 1;
    timeoutTx.descriptorSubmissions = 1;
    timeoutTx.doorbellReadbackMatches = true;
    timeoutTx.hardwareTimeouts = 1;
    timeoutTx.ringPoisoned = true;
    timeoutTx.failureReason = nic::TxFailureReason::DescriptorNotConsumed;
    const nic::TxEvidence evidence = nic::observe_tx(
        beforeTx, timeoutTx, nic::NIC_ERR_INIT_FAIL);
    assert(evidence.doorbellObserved);
    assert(evidence.completionTimedOut);
    assert(!evidence.completionObserved);
    return 0;
}
