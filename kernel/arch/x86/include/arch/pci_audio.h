// x86 (32-bit) PCI Audio Backend
//
// Platform-specific PCI audio initialisation for x86:
//   - PCI config space via port I/O (0xCF8/0xCFC)
//   - Legacy ISA DMA considerations for AC'97
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace arch {
namespace x86 {
namespace pci_audio {

bool init();
bool is_available();
uint32_t get_hda_mmio_base();

} // namespace pci_audio
} // namespace x86
} // namespace arch
} // namespace kernel
