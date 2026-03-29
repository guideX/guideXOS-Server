// x86 USB Host Controller Interface
//
// UHCI (Universal Host Controller Interface) driver for x86 platforms.
// Accesses PCI-based UHCI controllers via port I/O registers.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace x86 {
namespace usb_hci {

// Initialise UHCI host controller (scan PCI for USB controllers).
bool init();

// Return true if a UHCI controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace x86
} // namespace arch
} // namespace kernel
