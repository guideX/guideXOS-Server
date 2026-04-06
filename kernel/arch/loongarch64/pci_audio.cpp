//
// LoongArch 64-bit PCI Audio Implementation
//
// Scans PCI bus via ECAM for HDA (High Definition Audio) controllers
// and provides MMIO base address for the audio driver.
//
// On QEMU loongarch64-virt, HDA can be added with:
//   -device intel-hda -device hda-duplex
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/pci_audio.h"
#include "include/arch/loongarch64.h"

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace pci_audio {

namespace {

// ================================================================
// Static state
// ================================================================

static bool     s_available     = false;
static uint64_t s_hda_mmio_base = 0;
static uint16_t s_vendor_id     = 0;
static uint16_t s_device_id     = 0;

// ================================================================
// MMIO helpers for PCI config space access
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
// PCI bus scanning
// ================================================================

static bool scan_for_hda()
{
    // Scan PCI bus 0 for HDA controllers
    // TODO: Support multi-bus scanning if needed
    
    for (uint32_t dev = 0; dev < 32; ++dev) {
        for (uint32_t func = 0; func < 8; ++func) {
            uint32_t vendor_device = pci_cfg_read32(0, dev, func, 0x00);
            
            if (vendor_device == 0xFFFFFFFF || vendor_device == 0x00000000) {
                if (func == 0) break;  // No device
                continue;
            }
            
            uint32_t class_rev = pci_cfg_read32(0, dev, func, 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass   = (class_rev >> 16) & 0xFF;
            
            // Check for multimedia audio device (class 04, subclass 03 = HDA)
            if (class_code == PCI_CLASS_MULTIMEDIA && subclass == PCI_SUBCLASS_HDA) {
                // Found HDA controller
                s_vendor_id = vendor_device & 0xFFFF;
                s_device_id = (vendor_device >> 16) & 0xFFFF;
                
                // Read BAR0 for MMIO base
                uint32_t bar0 = pci_cfg_read32(0, dev, func, 0x10);
                
                if ((bar0 & 0x1) == 0) {
                    // Memory BAR
                    uint64_t mmio_base = bar0 & ~0xFULL;
                    
                    // Check for 64-bit BAR
                    if ((bar0 & 0x6) == 0x4) {
                        uint32_t bar1 = pci_cfg_read32(0, dev, func, 0x14);
                        mmio_base |= (static_cast<uint64_t>(bar1) << 32);
                    }
                    
                    // Enable memory space and bus mastering
                    uint16_t cmd = pci_cfg_read16(0, dev, func, 0x04);
                    cmd |= 0x06;  // Memory Space Enable + Bus Master Enable
                    pci_cfg_write16(0, dev, func, 0x04, cmd);
                    
                    s_hda_mmio_base = mmio_base;
                    s_available = true;
                    
                    return true;
                }
            }
            
            // Check for multi-function device
            if (func == 0) {
                uint32_t header = pci_cfg_read32(0, dev, func, 0x0C);
                if ((header & 0x00800000) == 0) {
                    break;  // Single-function device
                }
            }
        }
    }
    
    return false;
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

bool init()
{
    if (s_available) {
        return true;  // Already initialized
    }
    
    return scan_for_hda();
}

bool is_available()
{
    return s_available;
}

uint64_t get_hda_mmio_base()
{
    return s_hda_mmio_base;
}

uint16_t get_vendor_id()
{
    return s_vendor_id;
}

uint16_t get_device_id()
{
    return s_device_id;
}

} // namespace pci_audio
} // namespace loongarch64
} // namespace arch
} // namespace kernel
