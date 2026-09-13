// Hosted deterministic checks for the Phase 19 I219/SPT reset-flush policy.
// These tests exercise policy and fixture invariants; they do not emulate PCI
// configuration space, MMIO completion, or physical descriptor ownership.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

int main()
{
    // Current upstream e1000e's descriptor-ring status location and bit.
    assert(nic::PCI_CONFIG_DESC_RING_STATUS == 0x00E4u);
    assert(nic::PCI_CONFIG_FLUSH_DESC_REQUIRED == 0x0100u);
    assert(nic::E1000_FEXTNVM11 == 0x5BBCu);
    assert(nic::E1000_FEXTNVM11_DISABLE_MULR_FIX == 0x2000u);
    assert(nic::E1000_RXDCTL_THRESH_UNIT_DESC == 0x01000000u);

    // The correction is exact-I219/SPT only; QEMU's ordinary E1000 path is
    // intentionally outside this workaround gate.
    assert(nic::i219_spt_reset_flush_applies(
               nic::PCI_VENDOR_INTEL, nic::PCI_DEVICE_I219_LM));
    assert(!nic::i219_spt_reset_flush_applies(
               nic::PCI_VENDOR_INTEL, nic::PCI_DEVICE_E1000));
    assert(!nic::i219_spt_reset_flush_applies(0x1234u,
                                               nic::PCI_DEVICE_I219_LM));

    assert(!nic::i219_flush_desc_required(0u));
    assert(nic::i219_flush_desc_required(0x0100u));
    assert(!nic::i219_spt_flush_needed(0x0100u, 0u));
    assert(nic::i219_spt_flush_needed(0x0100u, 1024u));

    assert(nic::i219_reset_flush_decision(
               0x0100u, 1024u, nic::I219RingOwner::Guidexos) ==
           nic::I219ResetFlushDecision::TxFlush);
    assert(nic::i219_reset_flush_decision(
               0x0100u, 1024u, nic::I219RingOwner::None) ==
           nic::I219ResetFlushDecision::OwnershipUnknown);
    assert(nic::i219_reset_flush_decision(
               0x0100u, 1024u, nic::I219RingOwner::Unknown) ==
           nic::I219ResetFlushDecision::OwnershipUnknown);
    assert(nic::i219_reset_flush_decision(
               0u, 1024u, nic::I219RingOwner::Unknown) ==
           nic::I219ResetFlushDecision::NotRequired);
    assert(nic::i219_reset_status_transitioned(0x0100u, 0u));
    assert(!nic::i219_reset_status_transitioned(0u, 0u));
    assert(!nic::i219_reset_status_transitioned(0x0100u, 0x0100u));

    assert(strcmp(nic::i219_ring_owner_name(nic::I219RingOwner::None),
                  "none") == 0);
    assert(strcmp(nic::i219_ring_owner_name(nic::I219RingOwner::Guidexos),
                  "guidexos") == 0);
    assert(strcmp(nic::i219_ring_owner_name(nic::I219RingOwner::Unknown),
                  "unknown") == 0);
    assert(strcmp(nic::i219_reset_failure_reason_name(
                      nic::I219ResetFailureReason::RingOwnershipUnknown),
                  "I219_RESET_RING_OWNERSHIP_UNKNOWN") == 0);
    assert(strcmp(nic::i219_reset_failure_reason_name(
                      nic::I219ResetFailureReason::ResetHangStatePersists),
                  "I219_RESET_HANG_STATE_PERSISTS") == 0);

    // Phase 16/17 legacy DMA and raw-TX geometry remain unchanged.
    static_assert(sizeof(nic::TxDescriptor) == 16u,
                  "legacy TX descriptor ABI changed");
    static_assert(nic::NUM_TX_DESC == 64u,
                  "legacy TX descriptor count changed");
    static_assert(nic::TX_DMA_RING_LENGTH_BYTES == 1024u,
                  "legacy TX ring length changed");
    static_assert(nic::TX_DMA_REGION_SIZE == 0x2000ULL,
                  "constrained DMA region changed");
    static_assert(nic::TX_RAW_FRAME_LENGTH == 60u,
                  "raw frame length changed");
    assert(nic::tx_dma_experiment_active(nic::TxDmaMode::ConstrainedLow));

    // The reset audit has a fixed one-screen shell contract.
    assert(shell::nicinfo_mode_from_args("tx", "reset", nullptr) ==
           shell::NICINFO_MODE_TX_RESET);
    assert(shell::nicinfo_mode_from_args("tx", "reset", "extra") ==
           shell::NICINFO_MODE_INVALID);
    assert(shell::NICINFO_TX_RESET_EXPECTED_LINES <=
           shell::NICINFO_TX_RESET_MAX_LINES);

    // A fresh diagnostics record is fail-closed until a real boundary is
    // captured; no default state can claim a flush or reset completion.
    nic::I219ResetDiagnostics audit = {};
    assert(audit.resetCount == 0u);
    assert(!audit.preflushNeeded);
    assert(!audit.preflushAttempted);
    assert(!audit.preflushComplete);
    assert(!audit.resetPerformed);
    assert(!audit.resetCompleted);
    assert(!audit.strongerRecoveryRequired);
    assert(audit.failure == nic::I219ResetFailureReason::None);
    return 0;
}
