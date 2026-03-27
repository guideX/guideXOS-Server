//
// Kernel Interrupt Infrastructure
//
// Provides IDT setup, PIC (8259) remapping, and IRQ dispatch
// for the x86-64 kernel.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#include "kernel/types.h"

namespace kernel {
namespace interrupts {

// Initialize IDT, remap PIC, and enable interrupts
void init();

// Register handler callback for a given IRQ number (0–15)
typedef void (*irq_handler_t)();
void register_irq(uint8_t irq, irq_handler_t handler);

// Send End-of-Interrupt to PIC for a given IRQ
void eoi(uint8_t irq);

} // namespace interrupts
} // namespace kernel

#endif // KERNEL_INTERRUPTS_H
