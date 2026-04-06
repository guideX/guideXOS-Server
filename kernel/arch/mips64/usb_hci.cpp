//
// MIPS64 USB Host Controller Implementation (Stub)
//
// Provides USB HCI support for MIPS64 platforms.
// Currently a stub implementation for initial kernel compile.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/usb_hci.h"
#include "include/arch/serial_console.h"

namespace kernel {
namespace arch {
namespace mips64 {
namespace usb_hci {

namespace {

static UsbControllerInfo s_controller = {
    USB_NONE,   // type
    false,      // available
    0,          // base
    0,          // irq
    0           // ports
};

} // anonymous namespace

void init()
{
    serial_console::puts("[USB] MIPS64 USB HCI init (stub)\n");
    
    // TODO: Probe PCI bus for USB controllers
    s_controller.available = false;
    s_controller.type = USB_NONE;
    
    serial_console::puts("[USB] No USB controller detected\n");
}

bool is_available()
{
    return s_controller.available;
}

const UsbControllerInfo* get_controller_info()
{
    return &s_controller;
}

uint32_t get_device_count()
{
    return 0;  // No devices
}

const UsbDeviceInfo* get_device_info(uint32_t index)
{
    (void)index;
    return nullptr;  // No devices
}

void reset_bus()
{
    // Not implemented
}

void enable_port(uint8_t port)
{
    (void)port;
    // Not implemented
}

void disable_port(uint8_t port)
{
    (void)port;
    // Not implemented
}

} // namespace usb_hci
} // namespace mips64
} // namespace arch
} // namespace kernel
