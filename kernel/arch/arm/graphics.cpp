//
// Probes for PL111 CLCD controller at the well-known MMIO
// address and configures a framebuffer for graphics output.
//
// The PL111 is the standard display controller on ARM Versatile,
// RealView, and Integrator boards.  QEMU versatilepb emulates
// the PL111 at 0x10120000.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/arm.h"

namespace kernel {
namespace arch {
namespace arm {
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
// MMIO helpers
// ================================================================

static uint32_t mmio_rd32(uint32_t addr)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    return *reg;
}

static void mmio_wr32(uint32_t addr, uint32_t val)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    *reg = val;
}

static void spin_delay(uint32_t iterations)
{
    for (volatile uint32_t i = 0; i < iterations; ++i) {}
}

// ================================================================
// PL111 horizontal timing helpers
//
// TIM0: [7:2]=HBP-1, [15:8]=HSW-1, [23:16]=HFP-1, [31:24]=PPL/16-1
//        PPL = pixels per line
// ================================================================

static uint32_t pl111_encode_tim0(uint16_t width,
                                   uint8_t hfp, uint8_t hsw, uint8_t hbp)
{
    return (static_cast<uint32_t>((width / 16) - 1) << 2) |
           (static_cast<uint32_t>(hsw - 1) << 8) |
           (static_cast<uint32_t>(hfp - 1) << 16) |
           (static_cast<uint32_t>(hbp - 1) << 24);
}

// TIM1: [9:0]=LPP-1, [15:10]=VSW-1, [23:16]=VFP, [31:24]=VBP
static uint32_t pl111_encode_tim1(uint16_t height,
                                   uint8_t vfp, uint8_t vsw, uint8_t vbp)
{
    return (static_cast<uint32_t>(height - 1)) |
           (static_cast<uint32_t>(vsw - 1) << 10) |
           (static_cast<uint32_t>(vfp) << 16) |
           (static_cast<uint32_t>(vbp) << 24);
}

// ================================================================
// Probe for PL111 (read peripheral ID register)
// ================================================================

static bool probe_pl111()
{
    uint32_t periph_id = mmio_rd32(PL111_BASE + CLCD_PERIPHID0);
    if (periph_id == 0 || periph_id == 0xFFFFFFFF) return false;

    // PL111 peripheral ID0 should be 0x11 for the CLCD
    if ((periph_id & 0xFF) != 0x11) return false;

    s_type = FB_PL111;
    return true;
}

// ================================================================
// Configure the PL111 for a specific resolution and bpp
// ================================================================

static bool pl111_set_mode_internal(uint16_t width, uint16_t height, uint8_t bpp)
{
    // Disable the controller first
    mmio_wr32(PL111_BASE + CLCD_CTRL, 0);
    spin_delay(10000);

    // Set horizontal timing (HFP=24, HSW=136, HBP=160 — typical 1024x768)
    // For 640x480: HFP=16, HSW=96, HBP=48
    uint8_t hfp, hsw, hbp;
    uint8_t vfp, vsw, vbp;

    if (width <= 640) {
        hfp = 16; hsw = 96; hbp = 48;
        vfp = 10; vsw = 2;  vbp = 33;
    } else if (width <= 800) {
        hfp = 40; hsw = 128; hbp = 88;
        vfp = 1;  vsw = 4;   vbp = 23;
    } else {
        hfp = 24; hsw = 136; hbp = 160;
        vfp = 3;  vsw = 6;   vbp = 29;
    }

    mmio_wr32(PL111_BASE + CLCD_TIM0, pl111_encode_tim0(width, hfp, hsw, hbp));
    mmio_wr32(PL111_BASE + CLCD_TIM1, pl111_encode_tim1(height, vfp, vsw, vbp));

    // TIM2: clock divisor and polarity — use defaults
    mmio_wr32(PL111_BASE + CLCD_TIM2, 0);

    // Set framebuffer base address
    s_lfb = PL111_FB_DMA_BASE;
    mmio_wr32(PL111_BASE + CLCD_UPBASE, s_lfb);
    mmio_wr32(PL111_BASE + CLCD_LPBASE, 0);

    // Store properties
    s_width  = width;
    s_height = height;
    s_bpp    = bpp;

    // Calculate pitch
    switch (bpp) {
        case 8:  s_pitch = width;     break;
        case 16: s_pitch = width * 2; break;
        case 24: s_pitch = width * 3; break;
        case 32: s_pitch = width * 4; break;
        default: s_pitch = width * 4; s_bpp = 32; break;
    }

    // Build control register
    uint32_t ctrl = CLCD_CTRL_EN | CLCD_CTRL_TFT | CLCD_CTRL_POWER;

    switch (bpp) {
        case 8:  ctrl |= CLCD_CTRL_BPP8;  break;
        case 16: ctrl |= CLCD_CTRL_BPP16; break;
        case 24: ctrl |= CLCD_CTRL_BPP24; break;
        default: ctrl |= CLCD_CTRL_BPP16; break;
    }

    // Enable BGR pixel order for consistent colour rendering
    ctrl |= CLCD_CTRL_BGR;

    // Enable the CLCD
    mmio_wr32(PL111_BASE + CLCD_CTRL, ctrl);
    spin_delay(10000);

    // Configure the Versatile system register for CLCD if available
    // Bit 0 = 1: use the DMA framebuffer address
    mmio_wr32(VERSATILE_SYS_BASE + SYS_CLCD, 0x01);

    return true;
}

// ================================================================
// PL111 CLUT (palette) — the PL111 has a 256-entry palette RAM
// at offset 0x200 from the base.
// ================================================================

static const uint32_t CLCD_PALETTE_BASE = 0x200;

} // anonymous namespace

// ================================================================
// Public API
// ================================================================

FramebufferType probe()
{
    if (probe_pl111()) return FB_PL111;
    return FB_NONE;
}

bool init()
{
    s_available = false;
    s_type = probe();

    if (s_type == FB_NONE) return false;

    // Set a default mode: 640x480 @ 16bpp (conservative for QEMU)
    if (!pl111_set_mode_internal(640, 480, 16)) return false;

    s_available = true;
    return true;
}

FramebufferType get_type()    { return s_type; }
bool is_available()           { return s_available; }
uint32_t get_lfb_address()    { return s_lfb; }
uint32_t get_width()          { return s_width; }
uint32_t get_height()         { return s_height; }
uint32_t get_pitch()          { return s_pitch; }
uint8_t  get_bpp()            { return s_bpp; }

bool set_mode(uint16_t width, uint16_t height, uint8_t bpp)
{
    if (s_type != FB_PL111) return false;
    return pl111_set_mode_internal(width, height, bpp);
}

void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_type != FB_PL111 || s_bpp != 8) return;

    // PL111 palette entry: bits [15:11]=R, [10:5]=G, [4:0]=B  (5:6:5)
    // Actually PL111 uses 16-bit palette entries at offset 0x200 + index*2
    // For true-colour modes, the palette is not used.
    uint16_t entry = (static_cast<uint16_t>(r >> 3) << 11) |
                     (static_cast<uint16_t>(g >> 2) << 5) |
                     (static_cast<uint16_t>(b >> 3));

    volatile uint16_t* pal = reinterpret_cast<volatile uint16_t*>(
        PL111_BASE + CLCD_PALETTE_BASE + index * 2);
    *pal = entry;
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
    // Generate a simple 6x6x6 colour cube + grey ramp palette

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
} // namespace arm
} // namespace arch
} // namespace kernel
