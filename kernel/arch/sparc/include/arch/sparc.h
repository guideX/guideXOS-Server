//
// SPARC Architecture-Specific Code
//
// Copyright (c) 2024 guideX
//

#pragma once

#include <cstdint>

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
