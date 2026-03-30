// RISC-V 64 USB Host Controller Interface
//
// xHCI (USB 3.x) / OHCI (USB 1.1) driver for RISC-V virt platforms.
// On QEMU virt, USB controllers are PCI devices accessed through
// the ECAM PCI configuration mechanism at 0x30000000.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace riscv64 {
namespace usb_hci {

// Initialise PCI-USB host controller.
bool init();

// Return true if a USB controller was found and initialised.
bool is_available();

} // namespace usb_hci
} // namespace riscv64
} // namespace arch
} // namespace kernel
