//
// PowerPC64 USB Host Controller Interface
//
// xHCI (USB 3.x) / OHCI (USB 1.1) driver for PowerPC64 pseries.
// On QEMU pseries, USB controllers are PCI devices accessed through
// the PHB (PCI Host Bridge).
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ppc64 {
namespace usb_hci {

// Initialise PCI-USB host controller.
bool init();

// Return true if a USB controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace ppc64
} // namespace arch
} // namespace kernel
