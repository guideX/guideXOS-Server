//
// Itanium (IA-64) Architecture-Specific Code
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <cstdint>

namespace kernel {
namespace arch {
namespace ia64 {

// CPU control
void halt();
void enable_interrupts();
void disable_interrupts();
void break_instruction(uint64_t imm);

// PSR (Processor Status Register) operations
uint64_t read_psr();
void write_psr(uint64_t value);

// Application registers
uint64_t read_ar(uint32_t ar_num);
void write_ar(uint32_t ar_num, uint64_t value);

// Control registers
uint64_t read_cr(uint32_t cr_num);
void write_cr(uint32_t cr_num, uint64_t value);

// RSE (Register Stack Engine) operations
void flush_rse();
uint64_t read_bsp();   // Backing Store Pointer
uint64_t read_bspstore();
void write_bspstore(uint64_t value);

// IVA (Interrupt Vector Address) operations
uint64_t read_iva();
void write_iva(uint64_t value);

// Cache operations
void flush_cache();
void sync_instruction_cache();

// Initialize architecture-specific features
void init();

} // namespace ia64
} // namespace arch
} // namespace kernel
