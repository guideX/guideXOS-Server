// Hosted deterministic checks for the Phase 16 I219 TX DMA-placement
// experiment. These tests prove the handoff geometry and ownership contract;
// they do not emulate or fake descriptor consumption.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "kernel/nic.h"

using namespace kernel;

struct TestMemoryDescriptor {
    uint32_t type;
    uint32_t reserved;
    uint64_t physicalStart;
    uint64_t virtualStart;
    uint64_t numberOfPages;
    uint64_t attribute;
};

int main()
{
    static_assert(sizeof(nic::TxDescriptor) == 16u,
                  "Phase 16 must preserve the legacy descriptor ABI");
    static_assert(nic::NUM_TX_DESC == 64u,
                  "Phase 16 must preserve the 64-entry TX ring");
    static_assert(nic::TX_DMA_RING_LENGTH_BYTES == 1024u,
                  "Phase 16 must preserve the 1024-byte ring");

    const uint64_t regionBase = 0x04000000ULL;
    const uint64_t regionSize = nic::TX_DMA_REGION_SIZE;
    uint64_t ringPhysical = 0;
    uint64_t bufferPhysical = 0;
    assert(nic::tx_dma_region_layout_valid(
        regionBase, regionSize, &ringPhysical, &bufferPhysical));
    assert(ringPhysical == regionBase);
    assert(bufferPhysical == regionBase + nic::TX_DMA_REGION_BUFFER_OFFSET);
    assert(nic::tx_dma_region_below_4g(regionBase, regionSize));
    assert(!nic::tx_dma_region_below_4g(0xFFFFF000ULL, regionSize));
    assert(!nic::tx_dma_region_layout_valid(regionBase + 0x10u, regionSize));
    assert(!nic::tx_dma_region_layout_valid(regionBase, 0x1000u));
    assert(!nic::tx_dma_region_layout_valid(0xFFFFFFFFFFFFFFF0ULL,
                                             regionSize));

    assert(nic::tx_dma_identity_mapping_valid(regionBase, regionBase,
                                              regionSize));
    assert(!nic::tx_dma_identity_mapping_valid(regionBase,
                                               regionBase + 0x1000u,
                                               regionSize));
    assert(nic::dma_ranges_overlap(regionBase, 0x100u,
                                   regionBase + 0x80u, 0x100u));
    assert(!nic::dma_ranges_overlap(regionBase, 0x100u,
                                    regionBase + 0x100u, 0x100u));
    assert(!nic::dma_ranges_overlap(~0ULL, 2u, 0u, 1u));

    nic::TxDescriptor descriptor = {};
    descriptor.bufferAddr = bufferPhysical;
    descriptor.length = 342u;
    descriptor.cmd = nic::E1000_TXD_CMD_EOP |
                     nic::E1000_TXD_CMD_IFCS |
                     nic::E1000_TXD_CMD_RS;
    descriptor.status = 0u;
    assert(nic::tx_descriptor_buffer_matches(descriptor, bufferPhysical));
    assert(nic::dma_address_register_value(
               nic::dma_address_register_low(ringPhysical),
               nic::dma_address_register_high(ringPhysical)) == ringPhysical);

    TestMemoryDescriptor loaderData = {};
    loaderData.type = nic::TX_DMA_EFI_LOADER_DATA_TYPE;
    loaderData.physicalStart = regionBase - 0x1000u;
    loaderData.numberOfPages = 3u;
    assert(nic::tx_dma_region_owned_by_loader_memory_map(
        &loaderData, 1u, sizeof(loaderData), regionBase, regionSize));

    const uint32_t requiredFlags =
        nic::TX_DMA_REGION_FLAG_VALID |
        nic::TX_DMA_REGION_FLAG_OWNED |
        nic::TX_DMA_REGION_FLAG_CONTIGUOUS |
        nic::TX_DMA_REGION_FLAG_IDENTITY |
        nic::TX_DMA_REGION_FLAG_BELOW_4G |
        nic::TX_DMA_REGION_FLAG_CACHEABLE;
    assert(nic::tx_dma_region_handoff_valid(
        regionBase, regionSize, requiredFlags,
        nic::TX_DMA_EFI_LOADER_DATA_TYPE,
        &loaderData, 1u, sizeof(loaderData)));

    loaderData.type = 7u; // EfiConventionalMemory, not loader-owned.
    assert(!nic::tx_dma_region_owned_by_loader_memory_map(
        &loaderData, 1u, sizeof(loaderData), regionBase, regionSize));
    assert(!nic::tx_dma_region_handoff_valid(
        regionBase, regionSize, requiredFlags,
        nic::TX_DMA_EFI_LOADER_DATA_TYPE,
        &loaderData, 1u, sizeof(loaderData)));

    loaderData.type = nic::TX_DMA_EFI_LOADER_DATA_TYPE;
    assert(!nic::tx_dma_region_handoff_valid(
        regionBase, regionSize,
        requiredFlags & ~nic::TX_DMA_REGION_FLAG_IDENTITY,
        nic::TX_DMA_EFI_LOADER_DATA_TYPE,
        &loaderData, 1u, sizeof(loaderData)));
    assert(!nic::tx_dma_region_handoff_valid(
        regionBase, regionSize, requiredFlags, 7u,
        &loaderData, 1u, sizeof(loaderData)));
    assert(!nic::tx_dma_region_handoff_valid(
        regionBase, regionSize, requiredFlags,
        nic::TX_DMA_EFI_LOADER_DATA_TYPE,
        nullptr, 0u, sizeof(loaderData)));

    // Mode names are part of the physical observability contract. An absent
    // or invalid handoff is not allowed to be presented as constrained-low.
    assert(strcmp(nic::tx_dma_mode_name(nic::TxDmaMode::ConstrainedLow),
                  "constrained-low") == 0);
    assert(strcmp(nic::tx_dma_mode_name(nic::TxDmaMode::KernelImage),
                  "kernel-image") == 0);
    assert(strcmp(nic::tx_dma_mode_name(nic::TxDmaMode::Unavailable),
                  "unavailable") == 0);
    return 0;
}
