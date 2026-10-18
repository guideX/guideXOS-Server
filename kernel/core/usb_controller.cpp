//
// USB Host Controller Diagnostics � Implementation
//
// Read-only PCI enumeration that classifies every USB host controller and
// reports the xHCI capability/protocol milestones needed to diagnose
// bare-metal modern-input bring-up.  No controller state is modified: this
// file only reads PCI configuration space and (for xHCI) the read-only
// capability register block.
//
// On architectures without PCI configuration access (ARCH_HAS_PORT_IO == 0)
// the scan is a no-op; the pure classification/parsing layer remains
// available to every architecture.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_controller.h"
#include "include/kernel/xhci.h"
#include "include/kernel/arch.h"
#include "include/kernel/modern_input_diagnostics.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace usb {
namespace diagnostics {

namespace {

#if ARCH_HAS_PORT_IO

// ================================================================
// PCI configuration space access (port I/O 0xCF8/0xCFC)
// ================================================================

static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset)
{
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus)  << 16) |
                             (static_cast<uint32_t>(dev)  << 11) |
                             (static_cast<uint32_t>(func) << 8)  |
                             (offset & 0xFCu);
    kernel::arch::outl(PCI_CONFIG_ADDR, address);
    return kernel::arch::inl(PCI_CONFIG_DATA);
}

// ================================================================
// Minimal serial number helpers (the kernel has no printf)
// ================================================================

static void serial_u32_dec(uint32_t value)
{
    char tmp[10];
    uint32_t n = 0u;
    if (value == 0u) {
        kernel::serial::putc('0');
        return;
    }
    while (value > 0u) {
        tmp[n++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0u) {
        kernel::serial::putc(tmp[--n]);
    }
}

static void serial_hex_n(uint32_t value, uint32_t digits)
{
    const char* hex = "0123456789abcdef";
    for (uint32_t i = 0u; i < digits; ++i) {
        const uint32_t shift = (digits - 1u - i) * 4u;
        kernel::serial::putc(hex[(value >> shift) & 0xFu]);
    }
}

static void serial_bdf(uint8_t bus, uint8_t dev, uint8_t func)
{
    serial_hex_n(bus, 2u);
    kernel::serial::putc(':');
    serial_hex_n(dev, 2u);
    kernel::serial::putc('.');
    kernel::serial::putc(static_cast<char>('0' + (func & 0x7u)));
}

// ================================================================
// PCI enumeration
// ================================================================

static uint32_t scan_port_io()
{
    uint32_t controllers = 0u;
    uint32_t xhciCount   = 0u;

    kernel::serial::puts("[USB] host-controller scan begin\n");

    for (uint16_t bus = 0u; bus < 256u; ++bus) {
        for (uint8_t dev = 0u; dev < 32u; ++dev) {
            for (uint8_t func = 0u; func < 8u; ++func) {
                const uint32_t id = pci_read32(static_cast<uint8_t>(bus),
                                               dev, func, 0x00u);
                if (id == 0xFFFFFFFFu || id == 0u) {
                    continue;
                }

                const uint32_t classReg = pci_read32(
                    static_cast<uint8_t>(bus), dev, func, 0x08u);
                const uint8_t baseClass = static_cast<uint8_t>((classReg >> 24) & 0xFFu);
                const uint8_t subClass  = static_cast<uint8_t>((classReg >> 16) & 0xFFu);
                const uint8_t progIf    = static_cast<uint8_t>((classReg >> 8) & 0xFFu);

                const UsbControllerClass cls =
                    classify_usb_controller(baseClass, subClass, progIf);
                if (!cls.isHostController) {
                    continue;
                }

                const uint16_t vendor = static_cast<uint16_t>(id & 0xFFFFu);
                const uint16_t device = static_cast<uint16_t>((id >> 16) & 0xFFFFu);
                kernel::modern_input_diagnostics::ControllerKind diagnosticKind =
                    kernel::modern_input_diagnostics::ControllerKind::Other;
                switch (cls.type) {
                    case UsbControllerType::UHCI:
                        diagnosticKind = kernel::modern_input_diagnostics::ControllerKind::UHCI;
                        break;
                    case UsbControllerType::OHCI:
                        diagnosticKind = kernel::modern_input_diagnostics::ControllerKind::OHCI;
                        break;
                    case UsbControllerType::EHCI:
                        diagnosticKind = kernel::modern_input_diagnostics::ControllerKind::EHCI;
                        break;
                    case UsbControllerType::xHCI:
                        diagnosticKind = kernel::modern_input_diagnostics::ControllerKind::XHCI;
                        break;
                    default:
                        break;
                }
                const int32_t diagnosticIndex =
                    kernel::modern_input_diagnostics::record_controller(
                        static_cast<uint8_t>(bus), dev, func, vendor, device,
                        baseClass, subClass, progIf, diagnosticKind);
                if (cls.type == UsbControllerType::xHCI) ++xhciCount;
                kernel::modern_input_diagnostics::record_stage(
                    diagnosticIndex,
                    kernel::modern_input_diagnostics::Stage::PciDiscovery,
                    kernel::modern_input_diagnostics::Status::Pass,
                    0u, false, 0u, false, 0u, 0u, 0u, 0u, false);

                kernel::serial::puts("[USB] controller ");
                serial_u32_dec(controllers);
                kernel::serial::puts(" bdf=");
                serial_bdf(static_cast<uint8_t>(bus), dev, func);
                kernel::serial::puts(" ven=0x");
                serial_hex_n(vendor, 4u);
                kernel::serial::puts(" dev=0x");
                serial_hex_n(device, 4u);
                kernel::serial::puts(" class=0x");
                serial_hex_n(baseClass, 2u);
                kernel::serial::putc('/');
                serial_hex_n(subClass, 2u);
                kernel::serial::putc('/');
                serial_hex_n(progIf, 2u);
                kernel::serial::puts(" type=");
                kernel::serial::puts(usb_controller_type_name(cls.type));
                kernel::serial::putc('\n');

                ++controllers;

                // Read both BAR dwords and PCI command state, but do not
                // dereference a BAR here. xHCI initialization maps and proves
                // the full register span before any capability read.
                const uint32_t bar0 = pci_read32(static_cast<uint8_t>(bus),
                                                 dev, func, 0x10u);
                const uint32_t bar1 = pci_read32(static_cast<uint8_t>(bus),
                                                 dev, func, 0x14u);
                const uint16_t pciCommand = static_cast<uint16_t>(
                    pci_read32(static_cast<uint8_t>(bus), dev, func, 0x04u) & 0xFFFFu);
                const bool ioSpace = (bar0 & 0x01u) != 0u;
                const uint8_t barType = static_cast<uint8_t>((bar0 >> 1) & 0x03u);
                const bool is64 = !ioSpace && barType == 0x02u;
                const bool prefetchable = !ioSpace && (bar0 & 0x08u) != 0u;
                const xhci::XhciBarInfo bar =
                    xhci::validate_mmio_bar(bar0, bar1, 1u);

                if (cls.type == UsbControllerType::xHCI) {
                    kernel::modern_input_diagnostics::record_mmio_configuration(
                        diagnosticIndex, bar0, bar1, pciCommand, barType,
                        ioSpace, is64, prefetchable);
                    if (bar.valid) {
                        kernel::modern_input_diagnostics::record_mmio_base(
                            diagnosticIndex, bar.base);
                    }
                }

                kernel::serial::puts("[USB]   BAR0 raw=0x");
                kernel::serial::put_hex64(bar0);
                kernel::serial::puts(" BAR1 raw=0x");
                kernel::serial::put_hex64(bar1);
                kernel::serial::puts(ioSpace ? " type=I/O" : (is64 ? " type=64-bit" :
                    (barType == 0u ? " type=32-bit" :
                        (barType == 1u ? " type=below-1MiB" : " type=reserved"))));
                kernel::serial::puts(" decoded=0x");
                kernel::serial::put_hex64(bar.base);
                kernel::serial::puts(" PCI-command=0x");
                kernel::serial::put_hex64(pciCommand);
                kernel::serial::putc('\n');
            }
        }
    }

    kernel::modern_input_diagnostics::complete_pci_enumeration(
        kernel::modern_input_diagnostics::Status::Pass);

    kernel::serial::puts("[USB] host-controller scan end controllers=");
    serial_u32_dec(controllers);
    kernel::serial::puts(" xhci=");
    serial_u32_dec(xhciCount);
    kernel::serial::puts(" state=");
    kernel::serial::puts(controllers == 0u ? "NO CONTROLLERS" : "SCAN COMPLETE");
    kernel::serial::putc('\n');

    return controllers;
}

#endif // ARCH_HAS_PORT_IO

} // anonymous namespace

uint32_t scan_host_controllers()
{
    kernel::modern_input_diagnostics::begin_pci_enumeration();
    kernel::modern_input_diagnostics::record_boot_milestone(
        kernel::modern_input_diagnostics::BootStage::UsbDiscoveryEntered);
    kernel::modern_input_diagnostics::Snapshot snapshot = {};
    kernel::modern_input_diagnostics::copy_snapshot(&snapshot);
    kernel::serial::puts("BOOT: USB_DISCOVERY_ENTERED sequence=");
    kernel::serial::put_hex64(snapshot.bootSequence);
    kernel::serial::puts(" snapshot=");
    kernel::serial::put_hex64(snapshot.snapshotSequence);
    kernel::serial::putc('\n');
    kernel::serial::puts("[USB] PCI discovery ENTRY REACHED\n");
    uint32_t controllers = 0u;
#if ARCH_HAS_PORT_IO
    controllers = scan_port_io();
#else
    // Pure classification/parsing remains available; PCI config access is
    // not present on this architecture yet.
    kernel::modern_input_diagnostics::complete_pci_enumeration(
        kernel::modern_input_diagnostics::Status::Unsupported,
        "PCI configuration access is unavailable on this architecture");
    kernel::serial::puts("[USB] PCI discovery FAILURE: configuration access unsupported\n");
#endif
    kernel::modern_input_diagnostics::record_boot_milestone(
        kernel::modern_input_diagnostics::BootStage::UsbDiscoveryCompleted);
    kernel::modern_input_diagnostics::copy_snapshot(&snapshot);
    kernel::serial::puts("BOOT: USB_DISCOVERY_COMPLETED sequence=");
    kernel::serial::put_hex64(snapshot.bootSequence);
    kernel::serial::puts(" snapshot=");
    kernel::serial::put_hex64(snapshot.snapshotSequence);
    kernel::serial::putc('\n');
    return controllers;
}

} // namespace diagnostics
} // namespace usb
} // namespace kernel
