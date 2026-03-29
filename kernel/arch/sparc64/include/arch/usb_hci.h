// SPARC v9 (UltraSPARC) USB Host Controller Interface
//
// OHCI/EHCI driver for SPARC64 Sun4u platforms.
// Sun4u machines provide PCI-based OHCI/EHCI controllers accessible
// through memory-mapped I/O on the PCI bus.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc64 {
namespace usb_hci {

// Initialise PCI-USB host controller.
bool init();

// Return true if a USB controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace sparc64
} // namespace arch
} // namespace kernel
