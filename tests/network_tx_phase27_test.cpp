#include <assert.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

static nic::I219Phase27GateState passing_gates()
{
    nic::I219Phase27GateState gates = {};
    gates.deviceIdentity = true;
    gates.familyPath = true;
    gates.driverReady = true;
    gates.initializationPassed = true;
    gates.linkUp = true;
    gates.dmaRegionValid = true;
    gates.dmaRingValid = true;
    gates.dmaBufferValid = true;
    gates.iommuMappingKnown = true;
    gates.txEngineReady = true;
    gates.ringIdle = true;
    gates.descriptorAvailable = true;
    gates.noCompetingTx = true;
    gates.notRecovering = true;
    gates.isolatedNetworkConfirmed = true;
    gates.notAlreadyAttempted = true;
    return gates;
}

int main()
{
    assert(shell::nicinfo_mode_from_args("tx", "iommu", nullptr) ==
           shell::NICINFO_MODE_TX_IOMMU);
    assert(shell::nicinfo_mode_from_args("tx", "iommu", "isolated") ==
           shell::NICINFO_MODE_TX_IOMMU_ISOLATED);
    assert(shell::nicinfo_mode_from_args("tx", "iommu", "retry") ==
           shell::NICINFO_MODE_INVALID);
    assert(shell::NICINFO_TX_IOMMU_EXPECTED_LINES <=
           shell::NICINFO_TX_IOMMU_MAX_LINES);

    nic::I219Phase27GateState gates = passing_gates();
    assert(nic::i219_phase27_gate_failure(gates) == nullptr);

    gates.deviceIdentity = false;
    assert(nic::i219_phase27_gate_failure(gates) &&
           shell::nicinfo_token_equals(
               nic::i219_phase27_gate_failure(gates),
               "unsupported PCI identity; expected 8086:156F"));
    gates = passing_gates();
    gates.familyPath = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "expected I219/PCH hardware-family path is not active"));
    gates = passing_gates();
    gates.driverReady = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "driver is not ready and registered"));
    gates = passing_gates();
    gates.initializationPassed = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "hardware initialization did not pass"));
    gates = passing_gates();
    gates.txEngineReady = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "TX engine or PCI DMA registers are invalid"));
    gates = passing_gates();
    gates.notRecovering = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "hardware recovery or poisoned TX ring is active"));
    gates = passing_gates();
    gates.linkUp = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "link is not UP"));
    gates = passing_gates();
    gates.dmaRegionValid = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "TX DMA handoff ownership or geometry is invalid"));
    gates = passing_gates();
    gates.dmaRingValid = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "TX descriptor ring DMA mapping/alignment/length is invalid"));
    gates = passing_gates();
    gates.dmaBufferValid = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "TX packet buffer DMA mapping is invalid"));
    gates = passing_gates();
    gates.iommuMappingKnown = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "active or unknown IOMMU mapping cannot be proven safe"));
    gates = passing_gates();
    gates.ringIdle = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "TX ring head/tail ownership is not idle"));
    gates = passing_gates();
    gates.descriptorAvailable = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "intended TX descriptor slot is not available"));
    gates = passing_gates();
    gates.noCompetingTx = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "TX activity was already observed this boot"));
    gates = passing_gates();
    gates.isolatedNetworkConfirmed = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "operator must confirm isolated test network with iommu isolated"));
    gates = passing_gates();
    gates.notAlreadyAttempted = false;
    assert(shell::nicinfo_token_equals(nic::i219_phase27_gate_failure(gates),
                                     "Phase 27 probe already attempted; cold boot before another probe"));

    nic::I219Phase27Evidence evidence = {};
    evidence.submitted = true;
    evidence.headBeforeValid = true;
    evidence.headAfterValid = true;
    evidence.descriptorStatusValid = true;
    evidence.descriptorStatus = 0u;
    evidence.headBefore = 0u;
    evidence.headAfter = 0u;
    evidence.timedOut = true;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::ANoProgress);

    evidence.headAfter = 1u;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::BProgressNoCompletion);
    evidence.headAfter = 0u;
    evidence.descriptorChanged = true;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::BProgressNoCompletion);

    evidence.descriptorChanged = false;
    evidence.descriptorStatus = nic::E1000_TXD_STAT_DD;
    evidence.softwareRetired = false;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::CCompletionNotRetired);
    evidence.softwareRetired = true;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::DCompletionRetired);

    evidence.softwareRetired = false;
    evidence.descriptorStatusValid = false;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::Inconclusive);
    evidence.descriptorStatus = 0xFFu;
    evidence.softwareRetired = false;
    evidence.descriptorStatusValid = false;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::Inconclusive);
    evidence.submitted = false;
    assert(nic::i219_phase27_classify(evidence) ==
           nic::I219Phase27Outcome::PrerequisiteNotMet);
    return 0;
}
