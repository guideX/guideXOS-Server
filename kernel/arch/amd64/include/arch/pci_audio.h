// AMD64 PCI Audio Backend
//
// Platform-specific PCI audio initialisation for AMD64:
//   - PCI config space via port I/O (0xCF8/0xCFC)
//   - MSI / MSI-X interrupt routing (optional)
//   - IOMMU considerations for DMA buffers
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace arch {
namespace amd64 {
namespace pci_audio {

// Initialise the PCI audio backend for AMD64.
// Scans PCI for audio devices and starts the core pci_audio driver.
bool init();

// Return true if PCI audio hardware was found.
bool is_available();

// Get the MMIO BAR address of the first HDA controller.
uint64_t get_hda_mmio_base();

} // namespace pci_audio
} // namespace amd64
} // namespace arch
} // namespace kernel
