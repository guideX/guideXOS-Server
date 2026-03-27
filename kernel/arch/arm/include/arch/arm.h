//
// ARM Architecture-Specific Code
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm {

// CPU control
void halt();
void enable_interrupts();
void disable_interrupts();
void wait_for_interrupt();

// CP15 system control coprocessor operations
uint32_t read_sctlr();  // System Control Register
void write_sctlr(uint32_t value);
uint32_t read_ttbr0();  // Translation Table Base Register 0
void write_ttbr0(uint32_t value);
uint32_t read_ttbr1();  // Translation Table Base Register 1
void write_ttbr1(uint32_t value);
uint32_t read_dacr();   // Domain Access Control Register
void write_dacr(uint32_t value);

// Cache operations
void invalidate_icache();
void invalidate_dcache();
void flush_dcache();
void invalidate_tlb();

// Initialize architecture-specific features
void init();

} // namespace arm
} // namespace arch
} // namespace kernel
