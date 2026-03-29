// x86 (32-bit) PCI Audio Backend — Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/pci_audio.h"
#include "include/arch/x86.h"
#include <kernel/pci_audio.h>

namespace kernel {
namespace arch {
namespace x86 {
namespace pci_audio {

namespace {
static bool     s_available = false;
static uint32_t s_hdaMmio   = 0;
}

bool init()
{
    kernel::pci_audio::init();

    s_available = (kernel::pci_audio::controller_count() > 0);

    if (s_available) {
        const kernel::pci_audio::AudioController* ctrl =
            kernel::pci_audio::get_controller(0);
        if (ctrl) {
            s_hdaMmio = static_cast<uint32_t>(ctrl->mmioBase);
        }
    }

    return s_available;
}

bool is_available()          { return s_available; }
uint32_t get_hda_mmio_base() { return s_hdaMmio; }

} // namespace pci_audio
} // namespace x86
} // namespace arch
} // namespace kernel
