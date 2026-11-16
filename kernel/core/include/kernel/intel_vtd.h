#pragma once

#include <stdint.h>
#include "kernel/dma_memory.h"
#include "kernel/intel_dmar.h"

namespace kernel {
namespace intel_vtd {

// The loader supplies a validated, identity-mapped DMAR copy. This module
// parses it and performs read-only VT-d register inspection only.
void initialize(const uint8_t* dmarCopy, uint32_t dmarLength,
                uint32_t dmarTableCount, bool acpiScanValid,
                bool remappingTablePresent);
void qualify_xhci_controllers();
intel_dmar::DirectDmaDecision qualify_direct_physical_dma(
    int32_t controllerIndex, const dma_memory::MemoryMapView& memoryMap,
    const dma_memory::PhysicalRange* dmaRanges, uint32_t dmaRangeCount,
    bool busMasterDisabled, bool deviceAddressabilityValidated,
    bool workspaceExclusive, intel_dmar::RmrrMemorySummary* rmrrSummary);

} // namespace intel_vtd
} // namespace kernel
