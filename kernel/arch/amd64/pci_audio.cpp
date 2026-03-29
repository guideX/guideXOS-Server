// AMD64 PCI Audio Backend — Implementation
//
// Delegates to the core pci_audio driver after performing any
// AMD64-specific PCI setup.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/pci_audio.h"
#include "include/arch/amd64.h"
#include <kernel/pci_audio.h>

namespace kernel {
namespace arch {
namespace amd64 {
namespace pci_audio {

namespace {
static bool     s_available = false;
static uint64_t s_hdaMmio   = 0;
}

bool init()
{
    // The core driver handles PCI scanning and HDA init
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
} // namespace amd64
} // namespace arch
} // namespace kernel
