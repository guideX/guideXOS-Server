#pragma once

#include <stdint.h>

namespace kernel {
namespace amd_iommu {

// Parse only the loader-owned, versioned copy. Null/unavailable handoff data
// remains unknown and never relaxes the xHCI DMA guard.
void initialize(const uint8_t* ivrsCopy, uint32_t ivrsLength,
                uint32_t ivrsTableCount, bool acpiScanValid,
                bool remappingTablePresent);

// Resolve and inspect each discovered xHCI controller independently. MMIO is
// read only and is attempted only for a unique, validated route.
void qualify_xhci_controllers();

} // namespace amd_iommu
} // namespace kernel
