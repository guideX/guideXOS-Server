// VESA / VBE / BGA Graphics Driver — Implementation
//
// Core mode-setting logic.  Delegates to arch-specific graphics
// backends for the actual hardware access:
//
//   x86/amd64 : BGA via port I/O  +  PCI VGA BAR0 scan
//   IA-64     : PCI VGA MMIO probe (EFI GOP passed separately)
//   SPARC v9  : PCI VGA MMIO probe (psycho/sabre PCI bridge)
//   SPARC v8  : no-op (TCX handled by existing framebuffer code)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/vesa.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace vesa {

// ================================================================
// Internal state
// ================================================================

static VideoMode   s_modes[MAX_MODES];
static uint8_t     s_modeCount    = 0;
static VideoMode   s_currentMode;
static bool        s_bgaAvailable = false;
static PciVgaDevice s_pciVga;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// ================================================================
// BGA register access (x86 / amd64 — port I/O)
// ================================================================

#if ARCH_HAS_PORT_IO

static void bga_write(uint16_t index, uint16_t value)
{
    arch::outw(BGA_IOPORT_INDEX, index);
    arch::outw(BGA_IOPORT_DATA, value);
}

static uint16_t bga_read(uint16_t index)
{
    arch::outw(BGA_IOPORT_INDEX, index);
    return arch::inw(BGA_IOPORT_DATA);
}

static bool detect_bga()
{
    uint16_t id = bga_read(BGA_INDEX_ID);
    return (id >= BGA_ID_MIN && id <= BGA_ID_MAX);
}

// ================================================================
// PCI configuration access (port I/O — x86/amd64)
// ================================================================

static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(PCI_CONFIG_ADDR, addr);
    return arch::inl(PCI_CONFIG_DATA);
}

static bool scan_pci_vga(PciVgaDevice* out)
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF) continue;

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

                // VGA compatible controller: class 03/00
                if (baseClass == 0x03 && subClass == 0x00) {
                    out->found    = true;
                    out->bus      = static_cast<uint8_t>(bus);
                    out->dev      = dev;
                    out->func     = func;
                    out->vendorId = static_cast<uint16_t>(id);
                    out->deviceId = static_cast<uint16_t>(id >> 16);

                    uint32_t bar0 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x10);
                    if (!(bar0 & 0x01)) {
                        // Memory BAR
                        bool is64bit = ((bar0 >> 1) & 0x03) == 0x02;
                        uint32_t bar0Hi = 0;
                        if (is64bit) {
                            bar0Hi = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x14);
                        }
                        out->bar0 = (static_cast<uint64_t>(bar0Hi) << 32) |
                                    (bar0 & 0xFFFFFFF0u);
                    }

                    uint32_t bar2 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x18);
                    if (!(bar2 & 0x01)) {
                        out->bar2 = bar2 & 0xFFFFFFF0u;
                    }

                    return true;
                }
            }
        }
    }
    return false;
}

#endif // ARCH_HAS_PORT_IO

// ================================================================
// Add a mode to the internal list
// ================================================================

static void add_mode(uint16_t w, uint16_t h, uint8_t bpp, uint64_t lfb)
{
    if (s_modeCount >= MAX_MODES) return;
    VideoMode& m = s_modes[s_modeCount];
    m.width      = w;
    m.height     = h;
    m.bpp        = bpp;
    m.pitch      = static_cast<uint32_t>(w) * (bpp / 8);
    m.lfbAddress = lfb;
    m.valid      = true;
    ++s_modeCount;
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_modes, sizeof(s_modes));
    memzero(&s_currentMode, sizeof(s_currentMode));
    memzero(&s_pciVga, sizeof(s_pciVga));
    s_modeCount    = 0;
    s_bgaAvailable = false;

#if ARCH_HAS_PORT_IO
    // Detect BGA
    s_bgaAvailable = detect_bga();

    // Scan PCI for VGA device to get LFB address
    scan_pci_vga(&s_pciVga);

    uint64_t lfb = s_pciVga.found ? s_pciVga.bar0 : BGA_LFB_ADDR_DEFAULT;

    // Populate standard modes (all BGA-capable adapters support these)
    if (s_bgaAvailable) {
        add_mode(640,  480,  32, lfb);
        add_mode(800,  600,  32, lfb);
        add_mode(1024, 768,  32, lfb);
        add_mode(1280, 1024, 32, lfb);
        add_mode(1920, 1080, 32, lfb);
        // Also add 24-bit and 16-bit variants
        add_mode(1024, 768,  24, lfb);
        add_mode(1024, 768,  16, lfb);
        add_mode(800,  600,  16, lfb);
    } else if (s_pciVga.found) {
        // Non-BGA PCI VGA — provide default mode only
        add_mode(1024, 768, 32, lfb);
    }
#endif // ARCH_HAS_PORT_IO

    // Non-x86 architectures: PCI VGA probe is handled by arch-specific
    // graphics backends (graphics.cpp per arch).  They call add_mode()
    // or set_current_mode() directly.
}

bool is_bga_available()
{
    return s_bgaAvailable;
}

bool probe_pci_vga(PciVgaDevice* out)
{
    if (!out) return false;
#if ARCH_HAS_PORT_IO
    return scan_pci_vga(out);
#else
    memzero(out, sizeof(PciVgaDevice));
    return false;
#endif
}

bool set_mode_bga(uint16_t width, uint16_t height, uint8_t bpp)
{
#if ARCH_HAS_PORT_IO
    if (!s_bgaAvailable) return false;

    bga_write(BGA_INDEX_ENABLE,      BGA_DISABLED);
    bga_write(BGA_INDEX_XRES,        width);
    bga_write(BGA_INDEX_YRES,        height);
    bga_write(BGA_INDEX_BPP,         bpp);
    bga_write(BGA_INDEX_VIRT_WIDTH,  width);
    bga_write(BGA_INDEX_VIRT_HEIGHT, height);
    bga_write(BGA_INDEX_X_OFFSET,    0);
    bga_write(BGA_INDEX_Y_OFFSET,    0);
    bga_write(BGA_INDEX_ENABLE,      BGA_ENABLED | BGA_LFB_ENABLED);

    // Verify the mode took effect
    uint16_t readBack = bga_read(BGA_INDEX_XRES);
    if (readBack != width) return false;

    // Update current mode record
    uint64_t lfb = s_pciVga.found ? s_pciVga.bar0 : BGA_LFB_ADDR_DEFAULT;
    s_currentMode.width      = width;
    s_currentMode.height     = height;
    s_currentMode.bpp        = bpp;
    s_currentMode.pitch      = static_cast<uint32_t>(width) * (bpp / 8);
    s_currentMode.lfbAddress = lfb;
    s_currentMode.valid      = true;

    return true;
#else
    (void)width; (void)height; (void)bpp;
    return false;
#endif
}

VideoMode get_current_mode()
{
    return s_currentMode;
}

uint64_t get_lfb_address()
{
    return s_currentMode.lfbAddress;
}

uint8_t get_mode_count()
{
    return s_modeCount;
}

const VideoMode* get_mode(uint8_t index)
{
    if (index >= s_modeCount) return nullptr;
    return &s_modes[index];
}

bool apply_mode(uint8_t modeIndex)
{
    if (modeIndex >= s_modeCount) return false;
    const VideoMode& m = s_modes[modeIndex];
    if (!m.valid) return false;

#if ARCH_HAS_PORT_IO
    if (s_bgaAvailable) {
        return set_mode_bga(m.width, m.height, m.bpp);
    }
#endif

    // For non-BGA / non-port-IO architectures the framebuffer is
    // already configured by firmware; just record the mode.
    s_currentMode = m;
    return true;
}

} // namespace vesa
} // namespace kernel
