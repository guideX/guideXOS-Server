// Hosted deterministic checks for the Phase 17 minimal raw-TX fixture.
// These tests prove frame and descriptor semantics; they do not fake hardware
// DD completion or claim a physical I219 result.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

int main()
{
    static_assert(sizeof(nic::TxDescriptor) == 16u,
                  "Phase 17 must preserve the legacy descriptor ABI");
    static_assert(nic::NUM_TX_DESC == 64u,
                  "Phase 17 must preserve the 64-entry TX ring");
    static_assert(nic::TX_DMA_RING_LENGTH_BYTES == 1024u,
                  "Phase 17 must preserve the 1024-byte ring");
    static_assert(nic::TX_RAW_FRAME_LENGTH == 60u,
                  "Phase 17 fixture must be the minimum Ethernet frame");

    const uint8_t sourceMac[nic::ETH_ALEN] =
        { 0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u };
    uint8_t frame[nic::TX_RAW_FRAME_LENGTH] = {};
    uint16_t frameLength = 0u;
    assert(nic::build_raw_tx_frame(frame, sizeof(frame), sourceMac,
                                   &frameLength));
    assert(frameLength == nic::ETH_FRAME_MIN);

    const uint8_t broadcast[nic::ETH_ALEN] =
        { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };
    assert(memcmp(frame, broadcast, nic::ETH_ALEN) == 0);
    assert(memcmp(frame + nic::ETH_ALEN, sourceMac, nic::ETH_ALEN) == 0);
    assert(frame[12] == 0x88u && frame[13] == 0xB5u);
    assert(((static_cast<uint16_t>(frame[12]) << 8) | frame[13]) ==
           nic::TX_RAW_ETHERTYPE);

    const uint16_t markerLength =
        static_cast<uint16_t>(sizeof(nic::TX_RAW_PAYLOAD_MARKER) - 1u);
    assert(markerLength == 13u);
    assert(memcmp(frame + nic::ETH_HLEN, nic::TX_RAW_PAYLOAD_MARKER,
                  markerLength) == 0);
    for (uint16_t i = nic::ETH_HLEN + markerLength;
         i < nic::TX_RAW_FRAME_LENGTH; ++i) {
        assert(frame[i] == 0u);
    }

    uint16_t rejectedLength = 0xFFFFu;
    assert(!nic::build_raw_tx_frame(frame, nic::TX_RAW_FRAME_LENGTH - 1u,
                                    sourceMac, &rejectedLength));
    assert(!nic::build_raw_tx_frame(nullptr, sizeof(frame), sourceMac,
                                    &rejectedLength));
    const uint8_t zeroMac[nic::ETH_ALEN] = {};
    assert(!nic::build_raw_tx_frame(frame, sizeof(frame), zeroMac,
                                    &rejectedLength));
    const uint8_t multicastMac[nic::ETH_ALEN] =
        { 0x01u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u };
    assert(!nic::build_raw_tx_frame(frame, sizeof(frame), multicastMac,
                                    &rejectedLength));

    // One legacy data descriptor only: buffer address, length, EOP|IFCS|RS,
    // cleared status, and no checksum/VLAN fields.
    nic::TxDescriptor descriptor = {};
    descriptor.bufferAddr = 0x0000000412345000ULL;
    descriptor.length = frameLength;
    descriptor.cso = 0u;
    descriptor.cmd = nic::E1000_TXD_CMD_EOP |
                     nic::E1000_TXD_CMD_IFCS |
                     nic::E1000_TXD_CMD_RS;
    descriptor.status = 0u;
    descriptor.css = 0u;
    descriptor.special = 0u;
    assert(descriptor.cmd == 0x0Bu);
    assert(descriptor.status == 0u);
    assert(descriptor.cso == 0u && descriptor.css == 0u &&
           descriptor.special == 0u);
    assert(nic::tx_descriptor_raw_word1(descriptor) ==
           (static_cast<uint64_t>(frameLength) | (0x0BULL << 24)));
    assert(nic::tx_descriptor_buffer_matches(
        descriptor, 0x0000000412345000ULL));
    assert(!nic::tx_descriptor_buffer_matches(
        descriptor, 0x0000000412346000ULL));

    // The Phase 16 constrained handoff is the only accepted mode for Phase
    // 17; no inactive mode may be presented as the experiment.
    assert(nic::tx_dma_experiment_active(nic::TxDmaMode::ConstrainedLow));
    assert(!nic::tx_dma_experiment_active(nic::TxDmaMode::KernelImage));
    assert(!nic::tx_dma_experiment_active(nic::TxDmaMode::Unavailable));
    assert(strcmp(nic::tx_raw_path_name(nic::TxRawPath::Normal),
                  "normal") == 0);
    assert(strcmp(nic::tx_raw_path_name(nic::TxRawPath::Direct),
                  "direct") == 0);

    // Command parsing is deliberately finite: no shell pipes, options, or
    // unbounded argument forms are part of the experiment.
    assert(shell::nicinfo_mode_from_args("tx", "raw", nullptr) ==
           shell::NICINFO_MODE_TX_RAW);
    assert(shell::nicinfo_mode_from_args("tx", "raw", "direct") ==
           shell::NICINFO_MODE_TX_RAW_DIRECT);
    assert(shell::nicinfo_mode_from_args("tx", "raw", "status") ==
           shell::NICINFO_MODE_TX_RAW_STATUS);
    assert(shell::nicinfo_mode_from_args("tx", "raw", "retry") ==
           shell::NICINFO_MODE_INVALID);
    assert(shell::nicinfo_mode_from_args("dhcp", "raw", nullptr) ==
           shell::NICINFO_MODE_INVALID);

    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::DmaExperimentNotActive),
                  "TX_DMA_EXPERIMENT_NOT_ACTIVE") == 0);
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::RawFrameInvalid),
                  "TX_RAW_FRAME_INVALID") == 0);
    assert(shell::NICINFO_TX_BRIEF_EXPECTED_LINES <=
           shell::NICINFO_TX_BRIEF_MAX_LINES);

    // The host suite intentionally stops at deterministic pre-submit state;
    // hardware completion is reserved for QEMU/physical evidence.
    return 0;
}
