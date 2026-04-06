//
// LoongArch 64-bit USB Host Controller Implementation
//
// Scans PCI bus for USB controllers (xHCI, EHCI, OHCI) and
// initializes the first suitable one found.
//
// On QEMU loongarch64-virt, USB can be added with:
//   -device qemu-xhci -device usb-kbd -device usb-mouse
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/usb_hci.h"
#include "include/arch/loongarch64.h"
#include "include/arch/loongarch_console.h"

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace usb_hci {

namespace {

// ================================================================
// PCI ECAM base for LoongArch
// ================================================================

static const uint64_t PCI_ECAM_BASE = 0x20000000ULL;

// ================================================================
// Static state
// ================================================================

static bool              s_available      = false;
static UsbControllerType s_type           = USB_NONE;
static uint64_t          s_hci_mmio_base  = 0;
static uint64_t          s_hci_mmio_size  = 0;
static uint32_t          s_pci_bus        = 0;
static uint32_t          s_pci_dev        = 0;
static uint32_t          s_pci_func       = 0;

// ================================================================
// MMIO helpers
// ================================================================

static uint32_t pci_cfg_read32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset)
{
    uint64_t addr = PCI_ECAM_BASE +
                   (static_cast<uint64_t>(bus) << 20) |
                   (static_cast<uint64_t>(dev) << 15) |
                   (static_cast<uint64_t>(func) << 12) |
                   (offset & 0xFFC);
    
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    return *reg;
}

static void pci_cfg_write32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset, uint32_t value)
{
    uint64_t addr = PCI_ECAM_BASE +
                   (static_cast<uint64_t>(bus) << 20) |
                   (static_cast<uint64_t>(dev) << 15) |
                   (static_cast<uint64_t>(func) << 12) |
                   (offset & 0xFFC);
    
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    *reg = value;
}

static uint16_t pci_cfg_read16(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset)
{
    uint64_t addr = PCI_ECAM_BASE +
                   (static_cast<uint64_t>(bus) << 20) |
                   (static_cast<uint64_t>(dev) << 15) |
                   (static_cast<uint64_t>(func) << 12) |
                   (offset & 0xFFE);
    
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(addr);
    return *reg;
}

static void pci_cfg_write16(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset, uint16_t value)
{
    uint64_t addr = PCI_ECAM_BASE +
                   (static_cast<uint64_t>(bus) << 20) |
                   (static_cast<uint64_t>(dev) << 15) |
                   (static_cast<uint64_t>(func) << 12) |
                   (offset & 0xFFE);
    
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(addr);
    *reg = value;
}

// ================================================================
// HCI MMIO helpers
// ================================================================

static uint32_t hci_rd32(uint64_t offset)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(s_hci_mmio_base + offset);
    return *reg;
}

static void hci_wr32(uint64_t offset, uint32_t value)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(s_hci_mmio_base + offset);
    *reg = value;
}

// ================================================================
// Get BAR size by writing all 1s and reading back
// ================================================================

static uint64_t get_bar_size(uint32_t bus, uint32_t dev, uint32_t func, uint32_t bar_offset)
{
    uint32_t original = pci_cfg_read32(bus, dev, func, bar_offset);
    
    pci_cfg_write32(bus, dev, func, bar_offset, 0xFFFFFFFF);
    uint32_t size_mask = pci_cfg_read32(bus, dev, func, bar_offset);
    pci_cfg_write32(bus, dev, func, bar_offset, original);
    
    if ((original & 0x1) == 0) {
        // Memory BAR
        size_mask &= ~0xF;
        return (~size_mask) + 1;
    }
    
    return 0;
}

// ================================================================
// PCI bus scanning
// ================================================================

struct UsbControllerInfo {
    uint32_t bus;
    uint32_t dev;
    uint32_t func;
    UsbControllerType type;
    uint64_t mmio_base;
    uint64_t mmio_size;
};

static bool scan_for_usb(UsbControllerInfo& info)
{
    // Best controller found so far
    bool found_any = false;
    UsbControllerInfo best = {};
    
    // Scan PCI bus 0
    for (uint32_t dev = 0; dev < 32; ++dev) {
        for (uint32_t func = 0; func < 8; ++func) {
            uint32_t vendor_device = pci_cfg_read32(0, dev, func, 0x00);
            
            if (vendor_device == 0xFFFFFFFF || vendor_device == 0x00000000) {
                if (func == 0) break;
                continue;
            }
            
            uint32_t class_rev = pci_cfg_read32(0, dev, func, 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass   = (class_rev >> 16) & 0xFF;
            uint8_t progif     = (class_rev >> 8) & 0xFF;
            
            // Check for USB controller (class 0x0C, subclass 0x03)
            if (class_code == PCI_CLASS_SERIAL && subclass == PCI_SUBCLASS_USB) {
                UsbControllerType ctrl_type = USB_NONE;
                
                if (progif == PCI_PROGIF_XHCI) {
                    ctrl_type = USB_XHCI;
                } else if (progif == PCI_PROGIF_EHCI) {
                    ctrl_type = USB_EHCI;
                } else if (progif == PCI_PROGIF_OHCI) {
                    ctrl_type = USB_OHCI;
                }
                
                if (ctrl_type != USB_NONE) {
                    // Prefer xHCI > EHCI > OHCI
                    if (!found_any || ctrl_type > best.type) {
                        best.bus = 0;
                        best.dev = dev;
                        best.func = func;
                        best.type = ctrl_type;
                        
                        // Read BAR0
                        uint32_t bar0 = pci_cfg_read32(0, dev, func, 0x10);
                        if ((bar0 & 0x1) == 0) {
                            best.mmio_base = bar0 & ~0xFULL;
                            
                            // 64-bit BAR?
                            if ((bar0 & 0x6) == 0x4) {
                                uint32_t bar1 = pci_cfg_read32(0, dev, func, 0x14);
                                best.mmio_base |= (static_cast<uint64_t>(bar1) << 32);
                            }
                            
                            best.mmio_size = get_bar_size(0, dev, func, 0x10);
                            found_any = true;
                        }
                    }
                }
            }
            
            // Check for multi-function device
            if (func == 0) {
                uint32_t header = pci_cfg_read32(0, dev, func, 0x0C);
                if ((header & 0x00800000) == 0) {
                    break;
                }
            }
        }
    }
    
    if (found_any) {
        info = best;
    }
    
    return found_any;
}

// ================================================================
// Controller initialization
// ================================================================

static bool init_controller(const UsbControllerInfo& info)
{
    // Enable memory space and bus mastering
    uint16_t cmd = pci_cfg_read16(info.bus, info.dev, info.func, 0x04);
    cmd |= 0x06;  // Memory Space Enable + Bus Master Enable
    pci_cfg_write16(info.bus, info.dev, info.func, 0x04, cmd);
    
    s_pci_bus = info.bus;
    s_pci_dev = info.dev;
    s_pci_func = info.func;
    s_type = info.type;
    s_hci_mmio_base = info.mmio_base;
    s_hci_mmio_size = info.mmio_size;
    
    // Controller-specific initialization
    switch (info.type) {
        case USB_XHCI:
            // xHCI initialization
            // TODO: Full xHCI bring-up sequence
            // 1. Read capability registers
            // 2. Reset controller (USBCMD.HCRST)
            // 3. Wait for reset complete (USBSTS.CNR)
            // 4. Set up DCBAAP, command ring, etc.
            // 5. Start controller (USBCMD.RS)
            loongarch_console::puts("[USB] Found xHCI controller\n");
            break;
            
        case USB_EHCI:
            // EHCI initialization
            // TODO: Full EHCI bring-up
            loongarch_console::puts("[USB] Found EHCI controller\n");
            break;
            
        case USB_OHCI:
            // OHCI initialization
            // TODO: Full OHCI bring-up
            loongarch_console::puts("[USB] Found OHCI controller\n");
            break;
            
        default:
            return false;
    }
    
    s_available = true;
    return true;
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

bool init()
{
    if (s_available) {
        return true;
    }
    
    UsbControllerInfo info;
    if (scan_for_usb(info)) {
        return init_controller(info);
    }
    
    return false;
}

bool is_available()
{
    return s_available;
}

UsbControllerType get_controller_type()
{
    return s_type;
}

uint64_t get_hci_mmio_base()
{
    return s_hci_mmio_base;
}

uint64_t get_hci_mmio_size()
{
    return s_hci_mmio_size;
}

bool reset()
{
    if (!s_available) {
        return false;
    }
    
    // Controller-specific reset
    // TODO: Implement per-controller reset sequence
    
    return true;
}

bool start()
{
    if (!s_available) {
        return false;
    }
    
    // Controller-specific start
    // TODO: Implement per-controller start sequence
    
    return true;
}

bool stop()
{
    if (!s_available) {
        return false;
    }
    
    // Controller-specific stop
    // TODO: Implement per-controller stop sequence
    
    return true;
}

} // namespace usb_hci
} // namespace loongarch64
} // namespace arch
} // namespace kernel
