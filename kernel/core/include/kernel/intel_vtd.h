#pragma once

#include <stdint.h>

namespace kernel {
namespace intel_vtd {

// The loader supplies a validated, identity-mapped DMAR copy. This module
// parses it and performs read-only VT-d register inspection only.
void initialize(const uint8_t* dmarCopy, uint32_t dmarLength,
                uint32_t dmarTableCount, bool acpiScanValid,
                bool remappingTablePresent);
void qualify_xhci_controllers();

} // namespace intel_vtd
} // namespace kernel
