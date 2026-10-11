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

// Bounded, read-only xHCI MMIO capability probe.  The capability register
// block is immutable hardware state, so this cannot disturb the controller.
static void report_xhci_capabilities(uint64_t mmioBase, int32_t diagnosticIndex)
{
    volatile const uint32_t* mmio =
        reinterpret_cast<volatile const uint32_t*>(
            static_cast<uintptr_t>(mmioBase));

    uint32_t capWords[XHCI_CAP_DWORD_COUNT];
    for (uint32_t i = 0u; i < XHCI_CAP_DWORD_COUNT; ++i) {
        capWords[i] = mmio[i];
    }

    XhciCapabilities caps = parse_xhci_capabilities(
        capWords, XHCI_CAP_DWORD_COUNT);
    kernel::modern_input_diagnostics::record_capabilities(
        diagnosticIndex, caps.caplength, caps.hciVersion, caps.maxSlots,
        caps.maxPorts, caps.csz ? 64u : 32u, caps.ac64, 0u,
        caps.maxScratchpadBufs);
    kernel::modern_input_diagnostics::record_stage(
        diagnosticIndex, kernel::modern_input_diagnostics::Stage::Capabilities,
        caps.valid ? kernel::modern_input_diagnostics::Status::Pass
                   : kernel::modern_input_diagnostics::Status::Fail,
        capWords[0], !caps.valid, 0u, false, 0u, 0u, 0u, 0u, false);

    char line[192];
    format_xhci_capabilities(caps, line, sizeof(line));
    kernel::serial::puts("[USB]   xHCI caps ");
    kernel::serial::puts(line);
    kernel::serial::putc('\n');

    if (!caps.valid || caps.xecp == 0u) {
        return;
    }

    // xECP is a dword offset into the BAR.  Bound it so a malformed value
    // cannot walk far outside the controller's register window.
    if (caps.xecp > 0x4000u) {
        kernel::serial::puts("[USB]   xHCI xECP out of range, skipped\n");
        return;
    }

    volatile const uint32_t* ext = mmio + caps.xecp;
    uint32_t extWords[XHCI_EXT_CAP_MAX_DWORDS];
    for (uint32_t i = 0u; i < XHCI_EXT_CAP_MAX_DWORDS; ++i) {
        extWords[i] = ext[i];
    }

    XhciProtocol protocols[XHCI_MAX_PROTOCOLS];
    const uint32_t count = parse_xhci_supported_protocols(
        extWords, XHCI_EXT_CAP_MAX_DWORDS, protocols, XHCI_MAX_PROTOCOLS);
    kernel::modern_input_diagnostics::record_capabilities(
        diagnosticIndex, caps.caplength, caps.hciVersion, caps.maxSlots,
        caps.maxPorts, caps.csz ? 64u : 32u, caps.ac64, count,
        caps.maxScratchpadBufs);

    for (uint32_t i = 0u; i < count; ++i) {
        char proto[96];
        format_xhci_protocol(protocols[i], proto, sizeof(proto));
        kernel::serial::puts("[USB]   xHCI proto ");
        kernel::serial::puts(proto);
        kernel::serial::putc('\n');
    }
}

// ================================================================
// PCI enumeration
// ================================================================

static uint32_t scan_port_io()
{
    uint32_t controllers = 0u;
    uint32_t xhciCount   = 0u;

    kernel::serial::puts("[USB] host-controller scan begin\n");
    kernel::modern_input_diagnostics::begin_pci_enumeration();

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

                // BAR0: report type/address; never write it.
                const uint32_t bar0 = pci_read32(static_cast<uint8_t>(bus),
                                                 dev, func, 0x10u);
                if ((bar0 & 0x01u) != 0u) {
                    kernel::serial::puts("[USB]   bar0=io base=0x");
                    serial_hex_n(bar0 & 0xFFFCu, 4u);
                    kernel::serial::putc('\n');
                    if (cls.type == UsbControllerType::xHCI) {
                        kernel::modern_input_diagnostics::record_stage(
                            diagnosticIndex,
                            kernel::modern_input_diagnostics::Stage::BarMmio,
                            kernel::modern_input_diagnostics::Status::Fail,
                            bar0, true, 0u, false, 0u, 0u, 0u, 0u, false);
                    }
                    continue;
                }

                const uint32_t barType = (bar0 >> 1) & 0x03u;
                const bool is64 = (barType == 0x02u);
                uint64_t mmioBase = static_cast<uint64_t>(bar0 & 0xFFFFFFF0u);
                if (is64) {
                    const uint32_t bar1 = pci_read32(static_cast<uint8_t>(bus),
                                                     dev, func, 0x14u);
                    mmioBase |= (static_cast<uint64_t>(bar1) << 32);
                }

                kernel::serial::puts("[USB]   bar0=mmio");
                kernel::serial::puts(is64 ? "64" : "32");
                kernel::serial::puts(" base=0x");
                kernel::serial::put_hex64(mmioBase);
                kernel::serial::putc('\n');

                if (cls.type == UsbControllerType::xHCI &&
                    mmioBase != 0u && mmioBase != 0xFFFFFFFFu) {
                    const uint32_t bar1 = is64
                        ? pci_read32(static_cast<uint8_t>(bus), dev, func, 0x14u) : 0u;
                    const xhci::XhciBarInfo bar =
                        xhci::validate_mmio_bar(bar0, bar1, 0x1000u);
                    if (!bar.valid) {
                        kernel::modern_input_diagnostics::record_stage(
                            diagnosticIndex,
                            kernel::modern_input_diagnostics::Stage::BarMmio,
                            kernel::modern_input_diagnostics::Status::Fail,
                            bar0, true, 0u, false, 0u, 0u, 0u, 0u, false);
                        continue;
                    }
                    kernel::modern_input_diagnostics::record_mmio_base(
                        diagnosticIndex, bar.base);
                    kernel::modern_input_diagnostics::record_stage(
                        diagnosticIndex,
                        kernel::modern_input_diagnostics::Stage::BarMmio,
                        kernel::modern_input_diagnostics::Status::Pass,
                        0u, false, 0u, false, 0u, 0u, 0u, 0u, false);
                    report_xhci_capabilities(bar.base, diagnosticIndex);
                } else if (cls.type == UsbControllerType::xHCI) {
                    kernel::modern_input_diagnostics::record_stage(
                        diagnosticIndex,
                        kernel::modern_input_diagnostics::Stage::BarMmio,
                        kernel::modern_input_diagnostics::Status::Fail,
                        bar0, true, 0u, false, 0u, 0u, 0u, 0u, false);
                }
            }
        }
    }

    kernel::serial::puts("[USB] host-controller scan end controllers=");
    serial_u32_dec(controllers);
    kernel::serial::puts(" xhci=");
    serial_u32_dec(xhciCount);
    kernel::serial::putc('\n');

    kernel::modern_input_diagnostics::complete_pci_enumeration(
        kernel::modern_input_diagnostics::Status::Pass);

    return controllers;
}

#endif // ARCH_HAS_PORT_IO

} // anonymous namespace

uint32_t scan_host_controllers()
{
#if ARCH_HAS_PORT_IO
    return scan_port_io();
#else
    // Pure classification/parsing remains available; PCI config access is
    // not present on this architecture yet.
    kernel::modern_input_diagnostics::begin_pci_enumeration();
    kernel::modern_input_diagnostics::complete_pci_enumeration(
        kernel::modern_input_diagnostics::Status::Unsupported);
    return 0u;
#endif
}

} // namespace diagnostics
} // namespace usb
} // namespace kernel
