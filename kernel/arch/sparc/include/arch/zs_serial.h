//
// Zilog 8530 SCC (Serial Communications Controller) Driver
//
// On Sun4m / SPARCstation machines the keyboard and mouse are attached
// to channel A and channel B of a Z8530 SCC chip mapped at well-known
// MMIO addresses.  This driver provides basic interrupt-driven input.
//
// QEMU SS-5 mappings:
//   Keyboard/mouse SCC base: 0x71000000
//     Channel A (keyboard) : base + 0x00 (control), base + 0x02 (data)
//     Channel B (mouse)    : base + 0x04 (control), base + 0x06 (data)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc {
namespace zs {

// Initialise both SCC channels (keyboard + mouse) and enable
// receive-interrupt mode.
void init(uint32_t screen_width, uint32_t screen_height);

// IRQ handler called from the interrupt dispatch when IRQ 12
// (SBus level 6 — serial) fires on Sun4m.
void irq_handler();

// Mouse state accessors (same interface as ps2mouse)
int32_t  mouse_x();
int32_t  mouse_y();
uint8_t  mouse_buttons();
bool     mouse_dirty();
void     mouse_clear_dirty();

} // namespace zs
} // namespace sparc
} // namespace arch
} // namespace kernel
