// Itanium (IA-64) PCI Audio Backend
//
// Platform-specific PCI audio initialisation for IA-64:
//   - PCI MMCFG config space access
//   - MMIO BAR access for HDA
//   - HP zx1/sx1000 chipset audio support
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <cstdint>

namespace kernel {
namespace arch {
namespace ia64 {
namespace pci_audio {

bool init();
bool is_available();
uint64_t get_hda_mmio_base();

} // namespace pci_audio
} // namespace ia64
} // namespace arch
} // namespace kernel
