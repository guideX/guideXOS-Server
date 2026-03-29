// SPARC v9 (UltraSPARC / Sun4u) Graphics Backend — Implementation
//
// PCI VGA discovery via the psycho/sabre PCI host bridge.
// On QEMU sun4u the PCI config space is MMIO-mapped starting
// at 0x1FE01000000.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/sparc64.h"

namespace kernel {
namespace arch {
namespace sparc64 {
namespace graphics {

namespace {

static bool     s_available = false;
static uint64_t s_lfb       = 0;
static uint32_t s_width     = 0;
static uint32_t s_height    = 0;
static uint32_t s_pitch     = 0;
static uint8_t  s_bpp       = 0;

// ================================================================
// PCI MMIO configuration access via psycho/sabre bridge
//
// PCI Type 0 config space address:
//   base + (dev << 11) + (func << 8) + offset
// (bus always 0 for single-segment psycho)
// ================================================================

static uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev,
                                uint8_t func, uint16_t offset)
{
    uint64_t addr = PCI_CONFIG_BASE |
                    (static_cast<uint64_t>(bus)  << 16) |
                    (static_cast<uint64_t>(dev)  << 11) |
                    (static_cast<uint64_t>(func) << 8)  |
                    (offset & 0xFC);
    return sparc64::mmio_read32(addr);
}

static bool scan_pci_vga(PciVgaInfo* out)
{
    // QEMU sun4u has a small PCI bus (bus 0 only in most configs)
    for (uint8_t dev = 0; dev < 32; ++dev) {
        for (uint8_t func = 0; func < 8; ++func) {
            uint32_t id = pci_cfg_read32(0, dev, func, 0);
            if (id == 0xFFFFFFFF || id == 0x00000000) continue;

            uint32_t classReg = pci_cfg_read32(0, dev, func, 0x08);
            uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
            uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

            // VGA compatible controller: class 03/00
            if (baseClass == 0x03 && subClass == 0x00) {
                out->found    = true;
                out->vendorId = static_cast<uint16_t>(id);
                out->deviceId = static_cast<uint16_t>(id >> 16);

                uint32_t bar0 = pci_cfg_read32(0, dev, func, 0x10);
                if (!(bar0 & 0x01)) {
                    // Memory BAR — check if 64-bit
                    bool is64 = ((bar0 >> 1) & 0x03) == 0x02;
                    uint32_t bar0Hi = 0;
                    if (is64) {
                        bar0Hi = pci_cfg_read32(0, dev, func, 0x14);
                    }
                    out->bar0 = (static_cast<uint64_t>(bar0Hi) << 32) |
                                (bar0 & 0xFFFFFFF0u);
                }

                uint32_t bar2 = pci_cfg_read32(0, dev, func, 0x18);
                if (!(bar2 & 0x01)) {
                    out->bar2 = bar2 & 0xFFFFFFF0u;
                }

                return true;
            }
        }
    }
    return false;
}

// ================================================================
// BGA-style mode setting via MMIO
//
// If the PCI VGA device is a Bochs/QEMU VBE adapter (stdvga),
// BGA registers are at BAR2 + 0x500 (QEMU VBE MMIO region).
// ================================================================

static const uint32_t BGA_MMIO_OFFSET = 0x500;

static bool try_bga_set_mode(uint64_t bar2,
                              uint16_t width, uint16_t height, uint8_t bpp)
{
    if (bar2 == 0) return false;

    uint64_t bgaBase = bar2 + BGA_MMIO_OFFSET;

    // Read BGA ID
    uint16_t id = static_cast<uint16_t>(sparc64::mmio_read16(bgaBase + 0x00));
    if (id < 0xB0C0 || id > 0xB0CF) return false;

    // Disable
    sparc64::mmio_write16(bgaBase + 0x08, 0x0000);  // INDEX_ENABLE
    // Set resolution
    sparc64::mmio_write16(bgaBase + 0x02, width);    // INDEX_XRES
    sparc64::mmio_write16(bgaBase + 0x04, height);   // INDEX_YRES
    sparc64::mmio_write16(bgaBase + 0x06, bpp);      // INDEX_BPP
    // Enable with LFB
    sparc64::mmio_write16(bgaBase + 0x08, 0x0041);   // ENABLED | LFB_ENABLED

    return true;
}

} // anonymous namespace

// ================================================================
// Public API
// ================================================================

bool init()
{
    s_available = false;

    PciVgaInfo vga;
    vga.found = false;
    vga.bar0  = 0;
    vga.bar2  = 0;

    if (!scan_pci_vga(&vga)) {
        // No PCI VGA — use default address (firmware-configured)
        s_lfb    = DEFAULT_LFB;
        s_width  = 1024;
        s_height = 768;
        s_bpp    = 32;
        s_pitch  = s_width * 4;
        s_available = true;
        return true;
    }

    s_lfb = vga.bar0 ? vga.bar0 : DEFAULT_LFB;

    // Try BGA mode setting
    if (try_bga_set_mode(vga.bar2, 1024, 768, 32)) {
        s_width  = 1024;
        s_height = 768;
        s_bpp    = 32;
        s_pitch  = s_width * 4;
    } else {
        // Assume firmware-set mode (OpenBoot or QEMU default)
        s_width  = 1024;
        s_height = 768;
        s_bpp    = 32;
        s_pitch  = s_width * 4;
    }

    s_available = true;
    return true;
}

bool is_available()        { return s_available; }

bool probe_pci_vga(PciVgaInfo* out)
{
    if (!out) return false;
    out->found = false;
    out->bar0  = 0;
    out->bar2  = 0;
    return scan_pci_vga(out);
}

uint64_t get_lfb_address() { return s_lfb; }
uint32_t get_width()       { return s_width; }
uint32_t get_height()      { return s_height; }
uint32_t get_pitch()       { return s_pitch; }
uint8_t  get_bpp()         { return s_bpp; }

bool set_mode(uint16_t width, uint16_t height, uint8_t bpp)
{
    PciVgaInfo vga;
    vga.found = false;
    vga.bar0  = 0;
    vga.bar2  = 0;
    scan_pci_vga(&vga);

    if (!try_bga_set_mode(vga.bar2, width, height, bpp))
        return false;

    s_width  = width;
    s_height = height;
    s_bpp    = bpp;
    s_pitch  = static_cast<uint32_t>(width) * (bpp / 8);
    s_lfb    = vga.bar0 ? vga.bar0 : DEFAULT_LFB;
    return true;
}

} // namespace graphics
} // namespace sparc64
} // namespace arch
} // namespace kernel
