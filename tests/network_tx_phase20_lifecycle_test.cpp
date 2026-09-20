// Hosted deterministic checks for the Phase 20 I219/SPT lifecycle contract.
// These checks validate parser, decoding, fixture, and fail-closed policy
// invariants; physical reset and descriptor ownership remain AIDA_LPT tests.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

int main()
{
    assert(nic::PCI_CONFIG_DESC_RING_STATUS == 0x00E4u);
    assert(nic::PCI_CONFIG_FLUSH_DESC_REQUIRED == 0x0100u);
    assert(nic::E1000_CTRL_GIO_MASTER_DISABLE == 0x00000004u);
    assert(nic::E1000_STATUS_GIO_MASTER_ENABLE == 0x00080000u);

    // The raw PCI bit is reported independently from the upstream action
    // predicate, which additionally requires a programmed TX ring length.
    assert(nic::i219_flush_desc_required(0x0100u));
    assert(!nic::i219_flush_desc_required(0x0000u));
    assert(!nic::i219_spt_flush_needed(0x0100u, 0u));
    assert(nic::i219_spt_flush_needed(0x0100u, 0x400u));

    assert(shell::nicinfo_mode_from_args("tx", "reset", "brief") ==
           shell::NICINFO_MODE_TX_RESET_BRIEF);
    assert(shell::nicinfo_mode_from_args("tx", "reset", "run") ==
           shell::NICINFO_MODE_TX_RESET_RUN);
    assert(shell::nicinfo_mode_from_args("tx", "rearm", nullptr) ==
           shell::NICINFO_MODE_TX_REARM);
    assert(shell::nicinfo_mode_from_args("tx", "lifecycle", nullptr) ==
           shell::NICINFO_MODE_TX_LIFECYCLE);
    assert(shell::nicinfo_mode_from_args("tx", "reset", "extra") ==
           shell::NICINFO_MODE_INVALID);
    assert(shell::NICINFO_TX_RESET_BRIEF_EXPECTED_LINES <=
           shell::NICINFO_TX_RESET_BRIEF_MAX_LINES);
    assert(shell::NICINFO_TX_LIFECYCLE_EXPECTED_LINES <=
           shell::NICINFO_TX_LIFECYCLE_MAX_LINES);

    assert(strcmp(nic::i219_rearm_failure_reason_name(
                      nic::I219RearmFailureReason::PciMasterDisabled),
                  "TX_PCI_MASTER_DISABLED") == 0);
    assert(strcmp(nic::i219_rearm_failure_reason_name(
                      nic::I219RearmFailureReason::HwControlNotRestored),
                  "TX_HW_CONTROL_NOT_RESTORED") == 0);
    assert(strcmp(nic::i219_rearm_failure_reason_name(
                      nic::I219RearmFailureReason::RingReadbackFailed),
                  "TX_RING_REARM_READBACK_FAILED") == 0);

    // Phase 16/17 contracts are unchanged.
    static_assert(sizeof(nic::TxDescriptor) == 16u,
                  "legacy TX descriptor ABI changed");
    static_assert(nic::NUM_TX_DESC == 64u,
                  "legacy TX ring count changed");
    static_assert(nic::TX_DMA_RING_LENGTH_BYTES == 1024u,
                  "legacy TX ring length changed");
    static_assert(nic::TX_DMA_REGION_SIZE == 0x2000ULL,
                  "constrained DMA region changed");
    static_assert(nic::TX_RAW_FRAME_LENGTH == 60u,
                  "raw frame length changed");
    static_assert(nic::TX_RAW_ETHERTYPE == 0x88B5u,
                  "raw EtherType changed");
    assert(strcmp(nic::TX_RAW_PAYLOAD_MARKER, "GXOS-I219-P17") == 0);

    uint8_t source[ nic::ETH_ALEN ] = {0xEC, 0x8E, 0xB5, 0x9F, 0x36, 0x38};
    uint8_t frame[nic::TX_RAW_FRAME_LENGTH] = {};
    uint16_t length = 0u;
    assert(nic::build_raw_tx_frame(frame, sizeof(frame), source, &length));
    assert(length == 60u);
    for (uint8_t i = 0; i < nic::ETH_ALEN; ++i) {
        assert(frame[i] == 0xFFu);
        assert(frame[nic::ETH_ALEN + i] == source[i]);
    }
    assert(frame[12] == 0x88u && frame[13] == 0xB5u);
    assert(frame[14] == 'G' && frame[15] == 'X' &&
           frame[16] == 'O' && frame[17] == 'S');

    nic::I219ResetDiagnostics reset = {};
    assert(!reset.rearmAttempted);
    assert(!reset.rearmCompleted);
    assert(!reset.rearmPciDmaVerified);
    assert(!reset.rearmOwnershipRestored);
    assert(!reset.rearmRegisterReadback);
    assert(!reset.rearmRxRestored);
    assert(!reset.rearmTxDisabledBeforeBuild);
    assert(reset.rearmFailure == nic::I219RearmFailureReason::None);

    return 0;
}
