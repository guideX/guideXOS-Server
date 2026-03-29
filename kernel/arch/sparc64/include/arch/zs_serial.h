//
// Zilog 8530 SCC (Serial Communications Controller) Driver — SPARC v9
//
// On Sun4u / UltraSPARC machines the keyboard and mouse are still
// attached via a Z8530 SCC, but the MMIO addresses are in the
// 64-bit EBus address space.
//
// QEMU sun4u mappings:
//   Keyboard/mouse SCC base: 0x1FFF1400000
//     Channel A (keyboard) : base + 0x00 (control), base + 0x02 (data)
//     Channel B (mouse)    : base + 0x04 (control), base + 0x06 (data)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc64 {
namespace zs {

// Initialise both SCC channels (keyboard + mouse) and enable
// receive-interrupt mode.
void init(uint32_t screen_width, uint32_t screen_height);

// IRQ handler called from the interrupt dispatch.
void irq_handler();

// Mouse state accessors (same interface as ps2mouse / sparc::zs)
int32_t  mouse_x();
int32_t  mouse_y();
uint8_t  mouse_buttons();
bool     mouse_dirty();
void     mouse_clear_dirty();

} // namespace zs
} // namespace sparc64
} // namespace arch
} // namespace kernel
