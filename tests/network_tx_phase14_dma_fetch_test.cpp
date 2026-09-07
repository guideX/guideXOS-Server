// Hosted deterministic checks for Phase 14 TX DMA/fetch provenance. These
// tests validate address and descriptor contracts without pretending to have
// observed a physical NIC fetch or completion.

#include <assert.h>
#include <string.h>

#include "kernel/nic.h"

using namespace kernel;

int main()
{
    static_assert(sizeof(nic::TxDescriptor) == 16u,
                  "legacy TX descriptor must remain 16 bytes");
    static_assert(nic::NUM_TX_DESC == nic::E1000E_MIN_TX_DESC,
                  "TX ring must use the established e1000e minimum");
    static_assert((nic::NUM_TX_DESC * sizeof(nic::TxDescriptor)) == 1024u,
                  "Phase 14 TX ring length changed");

    assert(nic::tx_ring_length_bytes(8) == 128u);
    assert(nic::tx_ring_length_bytes(nic::NUM_TX_DESC) == 1024u);
    assert(nic::tx_ring_configuration_valid(nic::NUM_TX_DESC,
                                            0x12345000ULL));
    assert(!nic::tx_ring_configuration_valid(8u, 0x12345000ULL));
    assert(!nic::tx_ring_configuration_valid(65u, 0x12345000ULL));
    assert(!nic::tx_ring_configuration_valid(nic::NUM_TX_DESC,
                                             0x12345008ULL));
    assert(!nic::tx_ring_configuration_valid(nic::NUM_TX_DESC, 0));

    const uint64_t ringPhysical = 0x1234567887654320ULL;
    uint64_t descriptorPhysical = 0;
    assert(nic::tx_descriptor_physical_address(ringPhysical, 0,
                                               &descriptorPhysical));
    assert(descriptorPhysical == ringPhysical);
    assert(nic::tx_descriptor_physical_address(ringPhysical,
                                               nic::NUM_TX_DESC - 1u,
                                               &descriptorPhysical));
    assert(descriptorPhysical == ringPhysical + 63u * 16u);
    assert(!nic::tx_descriptor_physical_address(ringPhysical,
                                                nic::NUM_TX_DESC,
                                                &descriptorPhysical));
    assert(!nic::tx_descriptor_physical_address(~0ULL, 1u,
                                                &descriptorPhysical));

    assert(nic::dma_address_register_low(ringPhysical) == 0x87654320u);
    assert(nic::dma_address_register_high(ringPhysical) == 0x12345678u);
    assert(nic::dma_address_register_value(
               nic::dma_address_register_low(ringPhysical),
               nic::dma_address_register_high(ringPhysical)) == ringPhysical);

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

    assert(nic::dma_range_contains(0x1000ULL, 0x100ULL,
                                   0x1000ULL, 0x100ULL));
    assert(nic::dma_range_contains(0x1000ULL, 0x100ULL,
                                   0x10FFULL, 1ULL));
    assert(!nic::dma_range_contains(0x1000ULL, 0x100ULL,
                                    0x1100ULL, 1ULL));
    assert(!nic::dma_range_contains(~0ULL, 2ULL, ~0ULL, 1ULL));
    assert(nic::kernel_image_range_contains(0x21770000ULL, 0x100ULL,
                                            0x100000ULL, 0x2284AA60ULL));
    assert(!nic::kernel_image_range_contains(0x2284AA60ULL, 1ULL,
                                             0x100000ULL, 0x2284AA60ULL));
    assert(!nic::kernel_image_range_contains(0x0FFFF0ULL, 0x100ULL,
                                             0x100000ULL, 0x2284AA60ULL));

    nic::TxDescriptor descriptor = {};
    descriptor.bufferAddr = 0xFEDCBA9876543210ULL;
    descriptor.length = 0x1234u;
    descriptor.cso = 0x56u;
    descriptor.cmd = 0x78u;
    descriptor.status = 0x9Au;
    descriptor.css = 0xBCu;
    descriptor.special = 0xDEF0u;
    assert(nic::tx_descriptor_buffer_matches(
        descriptor, 0xFEDCBA9876543210ULL));
    assert(!nic::tx_descriptor_buffer_matches(descriptor, 0x10ULL));
    assert(descriptor.bufferAddr == 0xFEDCBA9876543210ULL);
    assert(nic::tx_descriptor_raw_word1(descriptor) ==
           0xDEF0BC9A78561234ULL);

    assert(nic::tx_engine_enabled(nic::E1000_TCTL_EN));
    assert(!nic::tx_engine_enabled(0));
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::DmaTranslationInvalid),
                  "TX_DMA_TRANSLATION_INVALID") == 0);
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::RingAddressMismatch),
                  "TX_RING_ADDRESS_MISMATCH") == 0);
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::RingAlignmentInvalid),
                  "TX_RING_ALIGNMENT_INVALID") == 0);
    assert(strcmp(nic::tx_failure_reason_name(
                      nic::TxFailureReason::RingLengthInvalid),
                  "TX_RING_LENGTH_INVALID") == 0);

    // A submission remains accepted only after the doorbell readback, and a
    // timeout remains a poisoned ring rather than an unsafe retry.
    nic::TxDiagnostics before = {};
    nic::TxDiagnostics timedOut = before;
    timedOut.descriptorPublications = 1;
    timedOut.descriptorSubmissions = 1;
    timedOut.tailBefore = 0;
    timedOut.tailAfter = 1;
    timedOut.doorbellReadbackMatches = true;
    timedOut.hardwareTimeouts = 1;
    timedOut.ringPoisoned = true;
    timedOut.failureReason = nic::TxFailureReason::DescriptorNotConsumed;
    nic::TxEvidence evidence = nic::observe_tx(
        before, timedOut, nic::NIC_ERR_INIT_FAIL);
    assert(evidence.descriptorPublished);
    assert(evidence.descriptorAccepted);
    assert(evidence.doorbellObserved);
    assert(evidence.completionTimedOut);
    assert(!evidence.completionObserved);

    return 0;
}
