//
// SPARC v8 Architecture-Specific Code
//
// Targets SPARC v8 (32-bit, e.g. SPARCstation / Sun4m).
// No port I/O — all device access is memory-mapped.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc {

// CPU control
void halt();
void enable_interrupts();
void disable_interrupts();

// ASI (Address Space Identifier) operations
uint32_t read_asi(uint32_t asi, uint32_t address);
void write_asi(uint32_t asi, uint32_t address, uint32_t value);

// Register window operations
void flush_windows();

// PSR (Processor State Register) operations
uint32_t read_psr();
void write_psr(uint32_t value);

// TBR (Trap Base Register) operations
uint32_t read_tbr();
void write_tbr(uint32_t value);

// WIM (Window Invalid Mask) operations
uint32_t read_wim();
void write_wim(uint32_t value);

// Memory-mapped I/O helpers (SPARC has no port I/O)
uint32_t mmio_read32(uint32_t address);
void     mmio_write32(uint32_t address, uint32_t value);
uint8_t  mmio_read8(uint32_t address);
void     mmio_write8(uint32_t address, uint8_t value);

// ---------------------------------------------------------------
// Sun4m SLAVIO interrupt controller helpers
// ---------------------------------------------------------------

// Initialize the SLAVIO interrupt controller (mask all, clear pending)
void slavio_init();

// Enable / disable a specific IRQ line on the SLAVIO
void slavio_irq_enable(uint32_t irq);
void slavio_irq_disable(uint32_t irq);

// Send EOI (clear pending) for a SLAVIO IRQ
void slavio_eoi(uint32_t irq);

// ---------------------------------------------------------------
// Sun4m TCX framebuffer helpers
// ---------------------------------------------------------------

// Probe the Sun4m TCX framebuffer at its well-known MMIO address.
// Returns true if a usable framebuffer was found and configures
// the global kernel framebuffer state.
bool probe_tcx_framebuffer();

// Initialize architecture-specific features
void init();

} // namespace sparc
} // namespace arch
} // namespace kernel
