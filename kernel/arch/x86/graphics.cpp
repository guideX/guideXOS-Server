// x86 (32-bit) Graphics Backend — Implementation
//
// BGA (Bochs VBE) mode setting, PCI VGA BAR0 discovery, and
// standard VGA register access for 32-bit x86.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/x86.h"
#include <kernel/vesa.h>

namespace kernel {
namespace arch {
namespace x86 {
namespace graphics {

namespace {

static bool     s_hasBga    = false;
static bool     s_hasPciVga = false;
static uint32_t s_lfb       = 0;

// VGA standard ports
static const uint16_t VGA_MISC_READ       = 0x3CC;
static const uint16_t VGA_MISC_WRITE      = 0x3C2;
static const uint16_t VGA_SEQ_INDEX       = 0x3C4;
static const uint16_t VGA_SEQ_DATA        = 0x3C5;
static const uint16_t VGA_CRTC_INDEX      = 0x3D4;
static const uint16_t VGA_CRTC_DATA       = 0x3D5;
static const uint16_t VGA_GC_INDEX        = 0x3CE;
static const uint16_t VGA_GC_DATA         = 0x3CF;
static const uint16_t VGA_AC_INDEX        = 0x3C0;
static const uint16_t VGA_AC_READ         = 0x3C1;
static const uint16_t VGA_INSTAT_READ     = 0x3DA;
static const uint16_t VGA_DAC_WRITE_INDEX = 0x3C8;
static const uint16_t VGA_DAC_DATA        = 0x3C9;

} // anonymous namespace

bool init()
{
    vesa::init();

    s_hasBga = vesa::is_bga_available();

    vesa::PciVgaDevice pciDev;
    s_hasPciVga = vesa::probe_pci_vga(&pciDev);

    if (s_hasPciVga) {
        s_lfb = static_cast<uint32_t>(pciDev.bar0);
    } else {
        s_lfb = vesa::BGA_LFB_ADDR_DEFAULT;
    }

    if (s_hasBga) {
        vesa::set_mode_bga(1024, 768, 32);
    }

    return s_hasBga || s_hasPciVga;
}

bool has_bga()       { return s_hasBga; }
bool has_pci_vga()   { return s_hasPciVga; }
uint32_t get_lfb_address() { return s_lfb; }

bool set_mode(uint16_t width, uint16_t height, uint8_t bpp)
{
    if (!s_hasBga) return false;
    return vesa::set_mode_bga(width, height, bpp);
}

void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    x86::outb(VGA_DAC_WRITE_INDEX, index);
    x86::outb(VGA_DAC_DATA, r);
    x86::outb(VGA_DAC_DATA, g);
    x86::outb(VGA_DAC_DATA, b);
}

void load_palette(const uint8_t* data)
{
    if (!data) return;
    x86::outb(VGA_DAC_WRITE_INDEX, 0);
    for (int i = 0; i < 768; ++i) {
        x86::outb(VGA_DAC_DATA, data[i]);
    }
}

// ================================================================
// Standard VGA register access
// ================================================================

uint8_t read_misc_output()
{
    return x86::inb(VGA_MISC_READ);
}

void write_misc_output(uint8_t val)
{
    x86::outb(VGA_MISC_WRITE, val);
}

uint8_t read_sequencer(uint8_t index)
{
    x86::outb(VGA_SEQ_INDEX, index);
    return x86::inb(VGA_SEQ_DATA);
}

void write_sequencer(uint8_t index, uint8_t val)
{
    x86::outb(VGA_SEQ_INDEX, index);
    x86::outb(VGA_SEQ_DATA, val);
}

uint8_t read_crtc(uint8_t index)
{
    x86::outb(VGA_CRTC_INDEX, index);
    return x86::inb(VGA_CRTC_DATA);
}

void write_crtc(uint8_t index, uint8_t val)
{
    x86::outb(VGA_CRTC_INDEX, index);
    x86::outb(VGA_CRTC_DATA, val);
}

uint8_t read_graphics(uint8_t index)
{
    x86::outb(VGA_GC_INDEX, index);
    return x86::inb(VGA_GC_DATA);
}

void write_graphics(uint8_t index, uint8_t val)
{
    x86::outb(VGA_GC_INDEX, index);
    x86::outb(VGA_GC_DATA, val);
}

uint8_t read_attribute(uint8_t index)
{
    // Reset flip-flop by reading input status register
    (void)x86::inb(VGA_INSTAT_READ);
    x86::outb(VGA_AC_INDEX, index);
    return x86::inb(VGA_AC_READ);
}

void write_attribute(uint8_t index, uint8_t val)
{
    (void)x86::inb(VGA_INSTAT_READ);
    x86::outb(VGA_AC_INDEX, index);
    x86::outb(VGA_AC_INDEX, val);
}

} // namespace graphics
} // namespace x86
} // namespace arch
} // namespace kernel
