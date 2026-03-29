// AMD64 USB Host Controller Interface
//
// UHCI / EHCI driver for AMD64 (x86-64) platforms.
// Accesses PCI-based USB controllers via port I/O registers.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace arch {
namespace amd64 {
namespace usb_hci {

// Initialise USB host controller (scan PCI for USB controllers).
bool init();

// Return true if a USB host controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace amd64
} // namespace arch
} // namespace kernel
