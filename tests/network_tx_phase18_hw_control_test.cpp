// Hosted deterministic checks for the Phase 18 I219-SPT ownership policy.
// These tests exercise policy, state gates, and preserved Phase 17 fixtures;
// they do not emulate CTRL_EXT hardware or fake descriptor completion.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

static nic::NICDevice complete_device(nic::DeviceFamily family)
{
    nic::NICDevice device = {};
    device.vendorId = nic::PCI_VENDOR_INTEL;
    device.deviceId = family == nic::DeviceFamily::I219Pch
        ? nic::PCI_DEVICE_I219_LM : nic::PCI_DEVICE_E1000;
    device.driverBound = true;
    device.mmioMapped = true;
    device.mmioProbeAttempted = true;
    device.mmioProbePassed = true;
    device.macReadAttempted = true;
    device.macValid = true;
    device.rxRingInitialized = true;
    device.txRingInitialized = true;
    device.resetAttempted = true;
    device.resetCompleted = true;
    device.phyProbeAttempted = true;
    device.phyAccess = nic::NIC_PHY_OK;
    device.phase5Stage = 8u;
    device.phase6Stage = 0u;
    device.phase7Stage = family == nic::DeviceFamily::I219Pch ? 4u : 0xFFu;
    return device;
}

int main()
{
    // Exact PCI identity/generation classification.
    assert(nic::device_family_for(nic::PCI_VENDOR_INTEL,
                                  nic::PCI_DEVICE_I219_LM) ==
           nic::DeviceFamily::I219Pch);
    assert(nic::device_family_for(0x1234u, nic::PCI_DEVICE_I219_LM) ==
           nic::DeviceFamily::Unsupported);
    assert(nic::i219_spt_hw_control_supported(
               nic::PCI_VENDOR_INTEL, nic::PCI_DEVICE_I219_LM));

    // Current upstream e1000_pch_spt policy: AMT plus CTRL_EXT-on-load,
    // with the generic E1000/QEMU path untouched.
    assert(nic::i219_spt_has_amt(nic::PCI_VENDOR_INTEL,
                                 nic::PCI_DEVICE_I219_LM));
    assert(nic::i219_spt_has_ctrl_ext_on_load(
               nic::PCI_VENDOR_INTEL, nic::PCI_DEVICE_I219_LM));
    assert(!nic::i219_spt_hw_control_supported(
               nic::PCI_VENDOR_INTEL, nic::PCI_DEVICE_E1000));
    assert(!nic::i219_spt_has_amt(nic::PCI_VENDOR_INTEL,
                                  nic::PCI_DEVICE_E1000));
    assert(!nic::i219_spt_has_ctrl_ext_on_load(
               nic::PCI_VENDOR_INTEL, nic::PCI_DEVICE_E1000));

    // The exact upstream bit is bit 28. Only that bit is added; every other
    // CTRL_EXT bit in the physical Phase 17 value is retained.
    assert(nic::E1000_CTRL_EXT_DRV_LOAD == 0x10000000u);
    const uint32_t before = 0x014A1027u;
    const uint32_t requested = nic::i219_spt_ctrl_ext_request(before);
    assert(requested == 0x114A1027u);
    assert(nic::i219_spt_ctrl_ext_unrelated_bits_preserved(before, requested));
    assert(nic::i219_spt_drv_load_readback_valid(requested));
    assert(!nic::i219_spt_drv_load_readback_valid(0x014A1027u));
    assert(!nic::i219_spt_drv_load_readback_valid(0xFFFFFFFFu));
    assert(!nic::i219_spt_ctrl_ext_unrelated_bits_preserved(
               before, requested ^ (1u << 7)));

    // The ownership write belongs only to the permanent P7 registration path.
    assert(nic::i219_spt_ownership_path_selected(8u, 0u, 4u));
    assert(!nic::i219_spt_ownership_path_selected(8u, 0u, 3u));
    assert(!nic::i219_spt_ownership_path_selected(7u, 0u, 4u));
    assert(!nic::i219_spt_ownership_path_selected(8u, 1u, 4u));

    // Fail closed until the immediate readback and final-state observation
    // are both present. This is not a TX completion failure.
    nic::NICDevice i219 = complete_device(nic::DeviceFamily::I219Pch);
    assert(!nic::hardware_init_complete(i219));
    i219.hwControl.supported = true;
    i219.hwControl.hasAmt = true;
    i219.hwControl.hasCtrlExtOnLoad = true;
    i219.hwControl.ownershipRequested = true;
    i219.hwControl.ownershipReadbackAttempted = true;
    i219.hwControl.ownershipReadback = true;
    i219.hwControl.finalValid = true;
    i219.hwControl.finalDrvLoad = true;
    assert(nic::hardware_init_complete(i219));
    i219.hwControl.finalDrvLoad = false;
    assert(!nic::hardware_init_complete(i219));
    assert(strcmp(nic::hw_control_failure_reason_name(
                      nic::HwControlFailureReason::DrvLoadReadbackFailed),
                  "I219_DRV_LOAD_READBACK_FAILED") == 0);
    assert(strcmp(nic::hw_control_failure_reason_name(
                      nic::HwControlFailureReason::CtrlExtReadFailed),
                  "I219_CTRL_EXT_READ_FAILED") == 0);

    // Generic E1000 readiness has no SPT ownership gate.
    nic::NICDevice qemu = complete_device(nic::DeviceFamily::E1000);
    assert(nic::hardware_init_complete(qemu));

    // Reset persistence is a pure observation: the implementation records the
    // pre-reset and post-reset values, then performs the one post-reset write.
    nic::I219HwControlDiagnostics control = {};
    control.initialValid = true;
    control.afterResetValid = true;
    control.ctrlExtInitial = before;
    control.ctrlExtAfterReset = before;
    control.resetPersistenceValid = true;
    control.resetPreserved =
        ((control.ctrlExtInitial & nic::E1000_CTRL_EXT_DRV_LOAD) ==
         (control.ctrlExtAfterReset & nic::E1000_CTRL_EXT_DRV_LOAD));
    assert(control.resetPreserved);

    // Phase 17 raw frame, descriptor ABI, constrained DMA geometry, and
    // command/status semantics remain unchanged.
    static_assert(sizeof(nic::TxDescriptor) == 16u,
                  "Phase 17 legacy descriptor ABI changed");
    static_assert(nic::NUM_TX_DESC == 64u,
                  "Phase 17 TX descriptor count changed");
    static_assert(nic::TX_DMA_RING_LENGTH_BYTES == 1024u,
                  "Phase 17 TX ring length changed");
    static_assert(nic::TX_DMA_REGION_SIZE == 0x2000ULL,
                  "Phase 16 constrained DMA region changed");
    static_assert(nic::TX_RAW_FRAME_LENGTH == 60u,
                  "Phase 17 raw frame length changed");
    const uint8_t sourceMac[nic::ETH_ALEN] =
        { 0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u };
    uint8_t frame[nic::TX_RAW_FRAME_LENGTH] = {};
    uint16_t frameLength = 0u;
    assert(nic::build_raw_tx_frame(frame, sizeof(frame), sourceMac,
                                   &frameLength));
    assert(frameLength == 60u);
    assert(frame[12] == 0x88u && frame[13] == 0xB5u);
    assert(memcmp(frame + nic::ETH_HLEN, nic::TX_RAW_PAYLOAD_MARKER,
                  sizeof(nic::TX_RAW_PAYLOAD_MARKER) - 1u) == 0);

    nic::TxDescriptor descriptor = {};
    descriptor.bufferAddr = 0x00000000AAB03000ULL;
    descriptor.length = frameLength;
    descriptor.cso = 0u;
    descriptor.cmd = nic::E1000_TXD_CMD_EOP |
                     nic::E1000_TXD_CMD_IFCS |
                     nic::E1000_TXD_CMD_RS;
    descriptor.status = 0u;
    descriptor.css = 0u;
    descriptor.special = 0u;
    assert(descriptor.cmd == 0x0Bu);
    assert((descriptor.cmd & 0x20u) == 0u); // DEXT is absent in legacy byte ABI.
    assert(nic::tx_descriptor_raw_word1(descriptor) ==
           (60ULL | (0x0BULL << 24)));
    assert(nic::tx_dma_experiment_active(nic::TxDmaMode::ConstrainedLow));

    // Existing timeout evidence remains distinct from ownership failure and
    // still reports the unchanged safe-poisoning contract.
    nic::TxDiagnostics txBefore = {};
    nic::TxDiagnostics txAfter = {};
    txAfter.hardwareTimeouts = 1u;
    txAfter.driverErrors = 1u;
    txAfter.ringPoisoned = true;
    const nic::TxEvidence evidence =
        nic::observe_tx(txBefore, txAfter, nic::NIC_ERR_INIT_FAIL);
    assert(evidence.completionTimedOut);
    assert(evidence.driverError);
    assert(txAfter.ringPoisoned);
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::DescriptorNotConsumed),
                  "TX_DESCRIPTOR_NOT_CONSUMED") == 0);

    assert(shell::nicinfo_mode_from_args("tx", "owner", nullptr) ==
           shell::NICINFO_MODE_TX_OWNER);
    assert(shell::nicinfo_mode_from_args("tx", "owner", "extra") ==
           shell::NICINFO_MODE_INVALID);
    assert(shell::NICINFO_TX_OWNER_EXPECTED_LINES <=
           shell::NICINFO_TX_OWNER_MAX_LINES);
    return 0;
}
