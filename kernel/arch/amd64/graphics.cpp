// AMD64 Graphics Backend — Implementation
//
// BGA (Bochs VBE) mode setting and PCI VGA BAR0 discovery.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/amd64.h"
#include <kernel/vesa.h>

namespace kernel {
namespace arch {
namespace amd64 {
namespace graphics {

namespace {

static bool s_hasBga    = false;
static bool s_hasPciVga = false;
static uint64_t s_lfb   = 0;

// VGA DAC ports
static const uint16_t VGA_DAC_WRITE_INDEX = 0x3C8;
static const uint16_t VGA_DAC_DATA        = 0x3C9;

} // anonymous namespace

bool init()
{
    // Use the VESA core driver for detection
    vesa::init();

    s_hasBga = vesa::is_bga_available();

    vesa::PciVgaDevice pciDev;
    s_hasPciVga = vesa::probe_pci_vga(&pciDev);

    if (s_hasPciVga) {
        s_lfb = pciDev.bar0;
    } else {
        s_lfb = vesa::BGA_LFB_ADDR_DEFAULT;
    }

    // If BGA is available, set a default mode
    if (s_hasBga) {
        vesa::set_mode_bga(1024, 768, 32);
    }

    return s_hasBga || s_hasPciVga;
}

bool has_bga()
{
    return s_hasBga;
}

bool has_pci_vga()
{
    return s_hasPciVga;
}

uint64_t get_lfb_address()
{
    return s_lfb;
}

bool set_mode(uint16_t width, uint16_t height, uint8_t bpp)
{
    if (!s_hasBga) return false;
    return vesa::set_mode_bga(width, height, bpp);
}

void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    amd64::outb(VGA_DAC_WRITE_INDEX, index);
    amd64::outb(VGA_DAC_DATA, r);
    amd64::outb(VGA_DAC_DATA, g);
    amd64::outb(VGA_DAC_DATA, b);
}

void load_palette(const uint8_t* data)
{
    if (!data) return;
    amd64::outb(VGA_DAC_WRITE_INDEX, 0);
    for (int i = 0; i < 768; ++i) {
        amd64::outb(VGA_DAC_DATA, data[i]);
    }
}

} // namespace graphics
} // namespace amd64
} // namespace arch
} // namespace kernel
