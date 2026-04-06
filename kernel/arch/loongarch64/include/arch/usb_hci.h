//
// LoongArch 64-bit USB Host Controller Interface
//
// xHCI (USB 3.x) / OHCI (USB 1.1) / EHCI (USB 2.0) driver for
// LoongArch platforms.
//
// On QEMU loongarch64-virt, USB controllers are PCI devices accessed
// through the ECAM PCI configuration mechanism.
//
// On real Loongson hardware, USB controllers may be integrated into
// the SoC or connected via PCI/PCIe.
//
// Supported controller types:
//   - xHCI: USB 3.x (modern, preferred)
//   - EHCI: USB 2.0 High-Speed
//   - OHCI: USB 1.1 (legacy, fallback)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace usb_hci {

// ================================================================
// USB controller types
// ================================================================

enum UsbControllerType : uint8_t {
    USB_NONE = 0,
    USB_OHCI = 1,    // USB 1.1 (Open Host Controller Interface)
    USB_EHCI = 2,    // USB 2.0 (Enhanced Host Controller Interface)
    USB_XHCI = 3,    // USB 3.x (Extensible Host Controller Interface)
};

// ================================================================
// PCI class codes for USB controllers
// ================================================================

static const uint8_t PCI_CLASS_SERIAL     = 0x0C;
static const uint8_t PCI_SUBCLASS_USB     = 0x03;
static const uint8_t PCI_PROGIF_OHCI      = 0x10;
static const uint8_t PCI_PROGIF_EHCI      = 0x20;
static const uint8_t PCI_PROGIF_XHCI      = 0x30;

// ================================================================
// Public API
// ================================================================

// Initialize the USB host controller subsystem.
// Scans PCI bus for USB controllers and initializes the best one found.
// Preference order: xHCI > EHCI > OHCI
// Returns true if a USB controller was found and initialized.
bool init();

// Return true if a USB controller was found and initialized.
bool is_available();

// Get the type of USB controller that was initialized.
UsbControllerType get_controller_type();

// Get the MMIO base address of the USB controller.
uint64_t get_hci_mmio_base();

// Get the size of the MMIO region.
uint64_t get_hci_mmio_size();

// Reset the USB controller.
bool reset();

// Start the USB controller.
bool start();

// Stop the USB controller.
bool stop();

} // namespace usb_hci
} // namespace loongarch64
} // namespace arch
} // namespace kernel
