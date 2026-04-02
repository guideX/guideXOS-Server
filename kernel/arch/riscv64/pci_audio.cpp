// RISC-V 64 PCI Audio Backend - Implementation
//
// Uses PCI ECAM for config space, delegates to core pci_audio.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/pci_audio.h"

// Only compile for RISC-V 64 target
#if defined(__riscv) && (__riscv_xlen == 64)

#include "include/arch/riscv64.h"
#include <kernel/pci_audio.h>

namespace kernel {
namespace arch {
namespace riscv64 {
namespace pci_audio {

namespace {
static bool     s_available = false;
static uint64_t s_hdaMmio   = 0;
}

bool init()
{
    kernel::pci_audio::init();

    s_available = (kernel::pci_audio::controller_count() > 0);

    if (s_available) {
        const kernel::pci_audio::AudioController* ctrl =
            kernel::pci_audio::get_controller(0);
        if (ctrl) {
            s_hdaMmio = ctrl->mmioBase;
        }
    }

    return s_available;
}

bool is_available()          { return s_available; }
uint64_t get_hda_mmio_base() { return s_hdaMmio; }

} // namespace pci_audio
} // namespace riscv64
} // namespace arch
} // namespace kernel

#else // Not RISC-V 64

// Stub implementation for non-RISC-V builds
namespace kernel {
namespace arch {
namespace riscv64 {
namespace pci_audio {

bool init() { return false; }
bool is_available() { return false; }
uint64_t get_hda_mmio_base() { return 0; }

} // namespace pci_audio
} // namespace riscv64
} // namespace arch
} // namespace kernel

#endif // __riscv
