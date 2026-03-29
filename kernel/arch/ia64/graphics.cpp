// Itanium (IA-64) Graphics Backend — Implementation
//
// EFI GOP framebuffer and PCI VGA discovery for IA-64 platforms.
//
// On the HP ski simulator there is no graphics hardware, so init()
// returns false and the kernel uses the serial console.
//
// On real Itanium servers (rx2620, rx6600, Integrity, etc.) the
// EFI firmware configures a GOP framebuffer that the bootloader
// passes to the kernel.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/ia64.h"

namespace kernel {
namespace arch {
namespace ia64 {
namespace graphics {

namespace {

static bool     s_available = false;
static uint64_t s_lfb       = 0;
static uint32_t s_width     = 0;
static uint32_t s_height    = 0;
static uint32_t s_pitch     = 0;
static uint8_t  s_bpp       = 0;

// ================================================================
// PCI configuration space (MMIO-based on IA-64)
//
// Itanium uses SAL (System Abstraction Layer) or memory-mapped
// PCI config space.  For simplicity we use a well-known MMIO base.
// On HP rx-series servers the PCI config MMIO is typically at
// 0x00000000_F0000000 (set by firmware in ACPI MCFG table).
// ================================================================

static const uint64_t PCI_MMCFG_BASE = 0xF0000000ULL;

static uint32_t pci_mmcfg_read32(uint8_t bus, uint8_t dev,
                                  uint8_t func, uint16_t offset)
{
    uint64_t addr = PCI_MMCFG_BASE |
                    (static_cast<uint64_t>(bus)  << 20) |
                    (static_cast<uint64_t>(dev)  << 15) |
                    (static_cast<uint64_t>(func) << 12) |
                    (offset & 0xFFC);
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    return *reg;
}

static bool scan_pci_vga()
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_mmcfg_read32(
                    static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF || id == 0x00000000) continue;

                uint32_t classReg = pci_mmcfg_read32(
                    static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

                if (baseClass == 0x03 && subClass == 0x00) {
                    // VGA device found — read BAR0
                    uint32_t bar0Lo = pci_mmcfg_read32(
                        static_cast<uint8_t>(bus), dev, func, 0x10);
                    uint32_t bar0Hi = pci_mmcfg_read32(
                        static_cast<uint8_t>(bus), dev, func, 0x14);

                    if (!(bar0Lo & 0x01)) {
                        s_lfb = (static_cast<uint64_t>(bar0Hi) << 32) |
                                (bar0Lo & 0xFFFFFFF0u);
                        // Assume a standard mode was set by firmware
                        s_width  = 1024;
                        s_height = 768;
                        s_bpp    = 32;
                        s_pitch  = s_width * 4;
                        s_available = true;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // anonymous namespace

bool init()
{
    s_available = false;
    s_lfb = 0;

    // Try PCI VGA probe (works on real hardware with a graphics card)
    if (scan_pci_vga()) return true;

    // On ski simulator: no graphics — return false
    return false;
}

bool init_from_gop(const EfiGopInfo* gopInfo)
{
    if (!gopInfo) return false;
    if (gopInfo->framebufferBase == 0 || gopInfo->width == 0 || gopInfo->height == 0)
        return false;

    s_lfb    = gopInfo->framebufferBase;
    s_width  = gopInfo->width;
    s_height = gopInfo->height;
    s_pitch  = gopInfo->pitch;
    s_bpp    = 32; // GOP typically provides 32-bit BGRX or RGBX

    s_available = true;
    return true;
}

bool is_available()       { return s_available; }
uint64_t get_lfb_address() { return s_lfb; }
uint32_t get_width()       { return s_width; }
uint32_t get_height()      { return s_height; }
uint32_t get_pitch()       { return s_pitch; }
uint8_t  get_bpp()         { return s_bpp; }

} // namespace graphics
} // namespace ia64
} // namespace arch
} // namespace kernel
