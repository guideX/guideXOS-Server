//
// Zilog 8530 SCC – keyboard & mouse driver for Sun4u (SPARC v9)
//
// Identical protocol to the Sun4m v8 driver but uses 64-bit MMIO
// addresses in the EBus address space.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/zs_serial.h"
#include "include/arch/sparc64.h"

namespace kernel {
namespace arch {
namespace sparc64 {
namespace zs {

// ================================================================
// MMIO register addresses for the Z8530 on QEMU sun4u
// ================================================================

static const uint64_t ZS_BASE       = 0x1FFF1400000ULL;
static const uint64_t ZS_KBD_CTRL   = ZS_BASE + 0x00ULL;
static const uint64_t ZS_KBD_DATA   = ZS_BASE + 0x02ULL;
static const uint64_t ZS_MOUSE_CTRL = ZS_BASE + 0x04ULL;
static const uint64_t ZS_MOUSE_DATA = ZS_BASE + 0x06ULL;

// SCC read-register 0 bits
static const uint8_t RR0_RX_AVAIL  = 0x01u;

// ================================================================
// Internal state
// ================================================================

static int32_t  s_mouseX    = 0;
static int32_t  s_mouseY    = 0;
static uint8_t  s_buttons   = 0;
static bool     s_dirty     = false;
static int32_t  s_screenW   = 1024;
static int32_t  s_screenH   = 768;

// Sun Mouse Systems protocol: 5-byte packets
static uint8_t s_mpkt[5];
static int     s_mphase = 0;

// ================================================================
// Helpers
// ================================================================

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void write_reg(uint64_t ctrl_addr, uint8_t reg, uint8_t val)
{
    if (reg != 0) {
        mmio_write8(ctrl_addr, reg);
    }
    mmio_write8(ctrl_addr, val);
}

static uint8_t read_reg(uint64_t ctrl_addr, uint8_t reg)
{
    if (reg != 0) {
        mmio_write8(ctrl_addr, reg);
    }
    return mmio_read8(ctrl_addr);
}

// ================================================================
// Public API
// ================================================================

void init(uint32_t screen_width, uint32_t screen_height)
{
    s_screenW = static_cast<int32_t>(screen_width);
    s_screenH = static_cast<int32_t>(screen_height);
    s_mouseX  = s_screenW / 2;
    s_mouseY  = s_screenH / 2;
    s_mphase  = 0;
    s_dirty   = false;

    // Reset SCC channel A (keyboard)
    write_reg(ZS_KBD_CTRL, 9, 0xC0u);   // hardware reset both channels
    write_reg(ZS_KBD_CTRL, 4, 0x44u);   // x16 clock, 1 stop bit, no parity
    write_reg(ZS_KBD_CTRL, 3, 0xC1u);   // Rx 8 bits, enable Rx
    write_reg(ZS_KBD_CTRL, 5, 0x60u);   // Tx 8 bits (Tx off)
    write_reg(ZS_KBD_CTRL, 1, 0x10u);   // Rx int on all chars

    // Reset SCC channel B (mouse)
    write_reg(ZS_MOUSE_CTRL, 4, 0x44u);
    write_reg(ZS_MOUSE_CTRL, 3, 0xC1u);
    write_reg(ZS_MOUSE_CTRL, 5, 0x60u);
    write_reg(ZS_MOUSE_CTRL, 1, 0x10u);

    // Master interrupt enable (register 9, bit 3)
    write_reg(ZS_KBD_CTRL, 9, 0x08u);

    // Drain stale data
    for (int i = 0; i < 16; ++i) {
        uint8_t rr0 = read_reg(ZS_MOUSE_CTRL, 0);
        if (!(rr0 & RR0_RX_AVAIL)) break;
        (void)mmio_read8(ZS_MOUSE_DATA);
    }
    for (int i = 0; i < 16; ++i) {
        uint8_t rr0 = read_reg(ZS_KBD_CTRL, 0);
        if (!(rr0 & RR0_RX_AVAIL)) break;
        (void)mmio_read8(ZS_KBD_DATA);
    }
}

void irq_handler()
{
    // --- Mouse channel (B) ---
    while (read_reg(ZS_MOUSE_CTRL, 0) & RR0_RX_AVAIL) {
        uint8_t d = mmio_read8(ZS_MOUSE_DATA);

        if (s_mphase == 0) {
            if (d & 0x80) {
                s_mpkt[0] = d;
                s_mphase = 1;
            }
        } else {
            s_mpkt[s_mphase] = d;
            s_mphase++;
            if (s_mphase >= 5) {
                uint8_t raw = s_mpkt[0] & 0x07;
                s_buttons = 0;
                if (!(raw & 0x04)) s_buttons |= 0x01; // left
                if (!(raw & 0x02)) s_buttons |= 0x04; // middle
                if (!(raw & 0x01)) s_buttons |= 0x02; // right

                int32_t dx = static_cast<int8_t>(s_mpkt[1]) + static_cast<int8_t>(s_mpkt[3]);
                int32_t dy = static_cast<int8_t>(s_mpkt[2]) + static_cast<int8_t>(s_mpkt[4]);
                dy = -dy;

                if (dx != 0 || dy != 0 || s_buttons != 0) s_dirty = true;
                s_mouseX = clamp(s_mouseX + dx, 0, s_screenW - 1);
                s_mouseY = clamp(s_mouseY + dy, 0, s_screenH - 1);

                s_mphase = 0;
            }
        }
    }

    // --- Keyboard channel (A) ---
    while (read_reg(ZS_KBD_CTRL, 0) & RR0_RX_AVAIL) {
        (void)mmio_read8(ZS_KBD_DATA);
    }
}

int32_t  mouse_x()           { return s_mouseX; }
int32_t  mouse_y()           { return s_mouseY; }
uint8_t  mouse_buttons()     { return s_buttons; }
bool     mouse_dirty()       { return s_dirty; }
void     mouse_clear_dirty() { s_dirty = false; }

} // namespace zs
} // namespace sparc64
} // namespace arch
} // namespace kernel
