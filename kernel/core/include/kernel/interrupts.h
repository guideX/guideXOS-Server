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

#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
namespace guideXOS { struct BootInfo; }
#endif

namespace kernel {
namespace interrupts {

// Initialize IDT, remap PIC, and enable interrupts
void init();

#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
// Cold BSP boot only, before IRQ registration or protected-task entry.
void init_with_bsp_apic_base_probe(const guideXOS::BootInfo* boot);
#endif

// Register handler callback for a given IRQ number (0–15)
typedef void (*irq_handler_t)();
void register_irq(uint8_t irq, irq_handler_t handler);

// Send End-of-Interrupt to PIC for a given IRQ
void eoi(uint8_t irq);

} // namespace interrupts
} // namespace kernel

#endif // KERNEL_INTERRUPTS_H
