//
// PIT (Programmable Interval Timer) Driver
//
// Configures the 8253/8254 PIT channel 0 to generate periodic IRQ0
// interrupts.  This gives the kernel a heartbeat so the main loop
// can wake from HLT periodically to poll input and perform
// housekeeping.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_PIT_H
#define KERNEL_PIT_H

#include "kernel/types.h"

namespace kernel {
namespace pit {

// Initialize PIT channel 0 to fire IRQ0 at approximately `hz` Hz.
// Typical value: 100 (10 ms tick).
void init(uint32_t hz);

// IRQ0 handler (registered via interrupts::register_irq)
void irq_handler();

// Number of ticks since boot
uint64_t ticks();

} // namespace pit
} // namespace kernel

#endif // KERNEL_PIT_H
