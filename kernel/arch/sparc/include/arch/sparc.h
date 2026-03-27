//
// SPARC v8 Architecture-Specific Code
//
// Targets SPARC v8 (32-bit, e.g. SPARCstation / Sun4m).
// No port I/O — all device access is memory-mapped.
//
// Copyright (c) 2024 guideX
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

// Initialize architecture-specific features
void init();

} // namespace sparc
} // namespace arch
} // namespace kernel
