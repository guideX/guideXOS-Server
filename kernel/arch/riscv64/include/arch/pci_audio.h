//
// Platform-specific PCI audio initialisation for RISC-V 64:
//   - PCI ECAM config space access (0x30000000 on QEMU virt)
//   - MMIO BAR access for HDA
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace riscv64 {
namespace pci_audio {

bool init();
bool is_available();
uint64_t get_hda_mmio_base();

} // namespace pci_audio
} // namespace riscv64
} // namespace arch
} // namespace kernel
