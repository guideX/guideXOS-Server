//
// MIPS64 USB Host Controller Interface
//
// Provides platform-specific USB HCI support for MIPS64.
// Stub implementation for initial kernel compile.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace mips64 {
namespace usb_hci {

// USB controller types
enum UsbControllerType : uint8_t {
    USB_NONE  = 0,
    USB_UHCI  = 1,    // USB 1.1 Universal HCI
    USB_OHCI  = 2,    // USB 1.1 Open HCI
    USB_EHCI  = 3,    // USB 2.0 Enhanced HCI
    USB_XHCI  = 4,    // USB 3.0 Extensible HCI
};

// USB controller information
struct UsbControllerInfo {
    UsbControllerType type;
    bool              available;
    uint64_t          base;       // MMIO base address
    uint32_t          irq;        // Interrupt line
    uint8_t           ports;      // Number of root hub ports
};

// USB device information
struct UsbDeviceInfo {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  address;             // Assigned USB address
    uint8_t  speed;               // 0=low, 1=full, 2=high, 3=super
};

// Initialize USB subsystem
void init();

// Check if USB controller is available
bool is_available();

// Get controller information
const UsbControllerInfo* get_controller_info();

// Get number of connected devices
uint32_t get_device_count();

// Get device information by index
const UsbDeviceInfo* get_device_info(uint32_t index);

// Reset USB bus
void reset_bus();

// Enable/disable port
void enable_port(uint8_t port);
void disable_port(uint8_t port);

} // namespace usb_hci
} // namespace mips64
} // namespace arch
} // namespace kernel
