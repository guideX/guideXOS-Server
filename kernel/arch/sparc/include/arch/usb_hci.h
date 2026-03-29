// SPARC v8 USB Host Controller Interface
//
// OpenHCI (OHCI) driver for SPARC Sun4m platforms.
// Sun4m machines with SBus USB cards use OHCI-compatible controllers
// accessed through memory-mapped I/O.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc {
namespace usb_hci {

// Initialise OHCI host controller.
bool init();

// Return true if an OHCI controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace sparc
} // namespace arch
} // namespace kernel
