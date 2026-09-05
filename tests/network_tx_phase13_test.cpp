// Hosted deterministic checks for the Phase 13 TX descriptor evidence model.
// These tests exercise encoding and counter projection only; they do not
// fabricate physical NIC completion.

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "kernel/nic.h"

using namespace kernel;

int main()
{
    static_assert(sizeof(nic::TxDescriptor) == 16u,
                  "legacy TX descriptor must remain 16 bytes");
    static_assert(offsetof(nic::TxDescriptor, bufferAddr) == 0u,
                  "TX buffer address ABI changed");
    static_assert(offsetof(nic::TxDescriptor, length) == 8u,
                  "TX length ABI changed");
    static_assert(offsetof(nic::TxDescriptor, cmd) == 11u,
                  "TX command ABI changed");
    static_assert(offsetof(nic::TxDescriptor, status) == 12u,
                  "TX status ABI changed");
    static_assert((nic::NUM_TX_DESC * sizeof(nic::TxDescriptor)) % 128u == 0u,
                  "TDLEN must be 128-byte aligned");

    assert(nic::E1000_TDBAL == 0x3800u);
    assert(nic::E1000_TDBAH == 0x3804u);
    assert(nic::E1000_TDLEN == 0x3808u);
    assert(nic::E1000_TDH == 0x3810u);
    assert(nic::E1000_TDT == 0x3818u);
    assert(nic::E1000_TXDCTL == 0x3828u);
    assert(nic::E1000_TARC0 == 0x3840u);
    assert((nic::E1000_TXD_CMD_EOP | nic::E1000_TXD_CMD_IFCS |
            nic::E1000_TXD_CMD_RS) == 0x0Bu);
    assert((nic::E1000_TARC0_CB_MULTIQ_3_REQ &
            nic::E1000_TARC0_CB_MULTIQ_2_REQ) ==
           nic::E1000_TARC0_CB_MULTIQ_2_REQ);

    const uint64_t ringPhysical = 0x1234567887654320ULL;
    assert(static_cast<uint32_t>(ringPhysical & 0xFFFFFFFFULL) ==
           0x87654320u);
    assert(static_cast<uint32_t>(ringPhysical >> 32) == 0x12345678u);

    uint64_t physical = 0;
    assert(nic::translate_kernel_dma_address(0x100000ULL, 0x200000ULL,
                                             &physical));
    assert(physical == 0x200000ULL);
    assert(nic::translate_kernel_dma_address(0x102010ULL, 0x400000ULL,
                                             &physical));
    assert(physical == 0x402010ULL);
    assert(!nic::translate_kernel_dma_address(0x0FFFFFULL, 0x200000ULL,
                                              &physical));
    assert(!nic::translate_kernel_dma_address(0x100000ULL, 0, &physical));
    assert(!nic::translate_kernel_dma_address(0x100001ULL, ~0ULL,
                                              &physical));

    nic::TxDiagnostics baseline = {};
    nic::TxDiagnostics timeout = baseline;
    timeout.descriptorPublications = 1;
    timeout.descriptorSubmissions = 1;
    timeout.tailBefore = 0;
    timeout.tailAfter = 1;
    timeout.tdtWritten = 1;
    timeout.doorbellReadbackMatches = true;
    timeout.hardwareTimeouts = 1;
    timeout.failureReason = nic::TxFailureReason::DescriptorNotConsumed;
    timeout.descriptorPublished = true;
    nic::TxEvidence evidence = nic::observe_tx(
        baseline, timeout, nic::NIC_ERR_INIT_FAIL);
    assert(evidence.descriptorPublished);
    assert(evidence.descriptorAccepted);
    assert(evidence.doorbellObserved);
    assert(evidence.tailAdvanced);
    assert(!evidence.completionObserved);
    assert(evidence.completionTimedOut);
    assert(!evidence.driverError);

    // A poisoned retry records an error but no new descriptor publication or
    // submission, which prevents reuse of a possibly hardware-owned buffer.
    nic::TxDiagnostics poisoned = timeout;
    poisoned.driverErrors = 1;
    poisoned.descriptorPublished = false;
    poisoned.doorbellReadbackMatches = false;
    poisoned.failureReason = nic::TxFailureReason::RingInvalid;
    nic::TxEvidence retryEvidence = nic::observe_tx(
        timeout, poisoned, nic::NIC_ERR_TX_FULL);
    assert(!retryEvidence.descriptorPublished);
    assert(!retryEvidence.descriptorAccepted);
    assert(!retryEvidence.doorbellObserved);
    assert(retryEvidence.driverError);
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::DoorbellNotObserved),
                  "TX_DOORBELL_NOT_OBSERVED") == 0);
    return 0;
}
