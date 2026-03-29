// SPARC v8 (Sun4m) Graphics Backend — Implementation
//
// Probes for TCX / CG3 / CG6 framebuffers at well-known SBus
// MMIO addresses and programs the RAMDAC colour lookup table.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/sparc.h"

namespace kernel {
namespace arch {
namespace sparc {
namespace graphics {

namespace {

static FramebufferType s_type  = FB_NONE;
static bool     s_available    = false;
static uint32_t s_lfb          = 0;
static uint32_t s_width        = 0;
static uint32_t s_height       = 0;
static uint32_t s_pitch        = 0;
static uint8_t  s_bpp          = 0;

// ================================================================
// MMIO helpers (delegate to arch MMIO functions)
// ================================================================

static uint32_t mmio_rd32(uint32_t addr)
{
    return sparc::mmio_read32(addr);
}

static void mmio_wr32(uint32_t addr, uint32_t val)
{
    sparc::mmio_write32(addr, val);
}

static void mmio_wr8(uint32_t addr, uint8_t val)
{
    sparc::mmio_write8(addr, val);
}

// ================================================================
// Probe for TCX (read THC register to detect presence)
// ================================================================

static bool probe_tcx()
{
    // THC has a control register at offset 0x00.
    // A non-0xFFFFFFFF read indicates presence.
    uint32_t thcCtrl = mmio_rd32(TCX_THC_BASE);
    if (thcCtrl == 0xFFFFFFFF || thcCtrl == 0x00000000) return false;

    // TCX 24-bit mode: 1024x768 x 32bpp (QEMU default)
    s_type   = FB_TCX;
    s_lfb    = TCX_FB24_BASE;
    s_width  = 1024;
    s_height = 768;
    s_bpp    = 32;
    s_pitch  = s_width * 4;
    return true;
}

// ================================================================
// Probe for CG3 (read RAMDAC presence)
// ================================================================

static bool probe_cg3()
{
    // CG3 BT458 RAMDAC: reading the pixel mask should give 0xFF
    uint32_t mask = mmio_rd32(CG3_DAC_BASE + BT458_PIXEL_MASK);
    if ((mask & 0xFF) != 0xFF) return false;

    // CG3: 1152x900 x 8bpp (standard Sun resolution)
    s_type   = FB_CG3;
    s_lfb    = CG3_FB_BASE;
    s_width  = 1152;
    s_height = 900;
    s_bpp    = 8;
    s_pitch  = s_width;
    return true;
}

// ================================================================
// BT458 RAMDAC palette programming
// ================================================================

static void bt458_set_entry(uint32_t dacBase,
                            uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    mmio_wr8(dacBase + BT458_ADDR_WRITE, index);
    mmio_wr8(dacBase + BT458_COLOR_DATA, r);
    mmio_wr8(dacBase + BT458_COLOR_DATA, g);
    mmio_wr8(dacBase + BT458_COLOR_DATA, b);
}

} // anonymous namespace

// ================================================================
// Public API
// ================================================================

FramebufferType probe()
{
    if (probe_tcx()) return FB_TCX;
    if (probe_cg3()) return FB_CG3;
    return FB_NONE;
}

bool init()
{
    s_available = false;
    s_type = probe();

    if (s_type == FB_NONE) return false;

    s_available = true;

    // For 8-bit modes, load the default palette
    if (s_bpp == 8) {
        load_default_palette();
    }

    return true;
}

FramebufferType get_type()    { return s_type; }
bool is_available()           { return s_available; }
uint32_t get_lfb_address()    { return s_lfb; }
uint32_t get_width()          { return s_width; }
uint32_t get_height()         { return s_height; }
uint32_t get_pitch()          { return s_pitch; }
uint8_t  get_bpp()            { return s_bpp; }

void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_type == FB_CG3 || s_type == FB_CG6) {
        bt458_set_entry(CG3_DAC_BASE, index, r, g, b);
    }
    // TCX 8-bit mode uses a different DAC — add if needed
}

void load_palette(const uint8_t* data)
{
    if (!data) return;
    for (int i = 0; i < 256; ++i) {
        set_palette_entry(static_cast<uint8_t>(i),
                          data[i * 3], data[i * 3 + 1], data[i * 3 + 2]);
    }
}

void load_default_palette()
{
    // Generate a simple 6-8-5 colour cube (216 colours) +
    // 16 grey levels + 24 extra shades, similar to the standard
    // VGA palette.

    uint8_t palette[768];
    int idx = 0;

    // First 16 entries: standard CGA/VGA colours
    static const uint8_t vga16[16][3] = {
        {  0,   0,   0}, {  0,   0, 170}, {  0, 170,   0}, {  0, 170, 170},
        {170,   0,   0}, {170,   0, 170}, {170,  85,   0}, {170, 170, 170},
        { 85,  85,  85}, { 85,  85, 255}, { 85, 255,  85}, { 85, 255, 255},
        {255,  85,  85}, {255,  85, 255}, {255, 255,  85}, {255, 255, 255},
    };
    for (int i = 0; i < 16; ++i) {
        palette[idx++] = vga16[i][0];
        palette[idx++] = vga16[i][1];
        palette[idx++] = vga16[i][2];
    }

    // Entries 16-231: 6x6x6 colour cube
    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) {
                palette[idx++] = static_cast<uint8_t>(r * 51);
                palette[idx++] = static_cast<uint8_t>(g * 51);
                palette[idx++] = static_cast<uint8_t>(b * 51);
            }
        }
    }

    // Entries 232-255: 24 grey levels
    for (int i = 0; i < 24; ++i) {
        uint8_t v = static_cast<uint8_t>(8 + i * 10);
        palette[idx++] = v;
        palette[idx++] = v;
        palette[idx++] = v;
    }

    load_palette(palette);
}

} // namespace graphics
} // namespace sparc
} // namespace arch
} // namespace kernel
