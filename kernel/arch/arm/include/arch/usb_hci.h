// ARM USB Host Controller Interface
//
// DWC OTG (DesignWare Core On-The-Go) controller driver for ARM platforms.
// Common on Raspberry Pi, STM32, and many ARM SoCs.
// Accesses the controller through memory-mapped I/O registers.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm {
namespace usb_hci {

// Initialise DWC OTG host controller.
bool init();

// Return true if the DWC OTG controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace arm
} // namespace arch
} // namespace kernel
