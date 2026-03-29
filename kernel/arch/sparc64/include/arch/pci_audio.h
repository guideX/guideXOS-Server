// SPARC v9 (UltraSPARC / Sun4u) PCI Audio Backend
//
// Platform-specific PCI audio initialisation for SPARC v9:
//   - PCI config via psycho/sabre bridge MMIO
//   - MMIO BAR access for HDA controllers
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc64 {
namespace pci_audio {

bool init();
bool is_available();
uint64_t get_hda_mmio_base();

} // namespace pci_audio
} // namespace sparc64
} // namespace arch
} // namespace kernel
