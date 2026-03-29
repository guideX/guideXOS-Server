// IA-64 (Itanium) USB Host Controller Interface
//
// OHCI/EHCI driver for IA-64 platforms.
// On Itanium systems, USB controllers are PCI devices accessed
// through the standard IA-64 PCI configuration mechanism.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <cstdint>

namespace kernel {
namespace arch {
namespace ia64 {
namespace usb_hci {

// Initialise PCI-USB host controller.
bool init();

// Return true if a USB controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace ia64
} // namespace arch
} // namespace kernel
