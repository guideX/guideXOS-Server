//
// x86 Architecture-Specific Code
//
// Copyright (c) 2024 guideX
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace x86 {

// CPU control
void halt();
void enable_interrupts();
void disable_interrupts();

// Port I/O
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);

// MSR operations
uint64_t read_msr(uint32_t msr);
void write_msr(uint32_t msr, uint64_t value);

// CR register operations
uint32_t read_cr0();
void write_cr0(uint32_t value);
uint32_t read_cr2();
uint32_t read_cr3();
void write_cr3(uint32_t value);
uint32_t read_cr4();
void write_cr4(uint32_t value);

// Initialize architecture-specific features
void init();

} // namespace x86
} // namespace arch
} // namespace kernel
