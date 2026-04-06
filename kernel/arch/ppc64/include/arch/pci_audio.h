//
// Platform-specific PCI audio initialisation for PowerPC64:
//   - PCI config space access for pseries
//   - MMIO BAR access for HDA
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ppc64 {
namespace pci_audio {

bool init();
bool is_available();
uint64_t get_hda_mmio_base();

} // namespace pci_audio
} // namespace ppc64
} // namespace arch
} // namespace kernel
