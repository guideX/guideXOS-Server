//
// PowerPC64 Architecture-Specific Code
//
// Targets PowerPC64 (64-bit big-endian) running on QEMU pseries machine.
// Implements the Book III-S (Server) model with hypervisor extensions.
//
// Key PowerPC64 registers:
//   MSR  - Machine State Register (controls interrupts, privilege, etc.)
//   SRR0 - Save/Restore Register 0 (holds PC on interrupt)
//   SRR1 - Save/Restore Register 1 (holds MSR on interrupt)
//   DEC  - Decrementer (timer countdown register)
//   PIR  - Processor ID Register
//   SPRG0-SPRG3 - General-purpose SPRs for software use
//   CTR  - Count Register (branch target/loop counter)
//   LR   - Link Register (return address)
//   CR   - Condition Register
//   XER  - Fixed-Point Exception Register
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ppc64 {

// ---------------------------------------------------------------
// CPU control
// ---------------------------------------------------------------

void halt();
void enable_interrupts();
void disable_interrupts();
void wait_for_interrupt();

// ---------------------------------------------------------------
// Machine State Register (MSR)
//
// Bit layout (key fields):
//   Bit 48 (EE) : External Interrupt Enable
//   Bit 49 (PR) : Problem State (0=supervisor, 1=user)
//   Bit 50 (FP) : FP Available
//   Bit 51 (ME) : Machine Check Enable
//   Bit 55 (IR) : Instruction Relocate (MMU for instructions)
//   Bit 56 (DR) : Data Relocate (MMU for data)
//   Bit 58 (RI) : Recoverable Interrupt
//   Bit 63 (LE) : Little-Endian mode
// ---------------------------------------------------------------

uint64_t read_msr();
void write_msr(uint64_t value);

// ---------------------------------------------------------------
// Save/Restore Registers (for interrupt handling)
// ---------------------------------------------------------------

uint64_t read_srr0();
void write_srr0(uint64_t value);
uint64_t read_srr1();
void write_srr1(uint64_t value);

// ---------------------------------------------------------------
// Decrementer (timer countdown)
// Generates interrupt when it decrements past zero.
// ---------------------------------------------------------------

uint32_t read_dec();
void write_dec(uint32_t value);

// ---------------------------------------------------------------
// Processor ID Register
// ---------------------------------------------------------------

uint32_t read_pir();

// ---------------------------------------------------------------
// Special Purpose Registers for software use
// ---------------------------------------------------------------

uint64_t read_sprg0();
void write_sprg0(uint64_t value);
uint64_t read_sprg1();
void write_sprg1(uint64_t value);
uint64_t read_sprg2();
void write_sprg2(uint64_t value);
uint64_t read_sprg3();
void write_sprg3(uint64_t value);

// ---------------------------------------------------------------
// Link Register and Count Register
// ---------------------------------------------------------------

uint64_t read_lr();
void write_lr(uint64_t value);
uint64_t read_ctr();
void write_ctr(uint64_t value);

// ---------------------------------------------------------------
// Condition Register and Fixed-Point Exception Register
// ---------------------------------------------------------------

uint64_t read_cr();
void write_cr(uint64_t value);
uint64_t read_xer();
void write_xer(uint64_t value);

// ---------------------------------------------------------------
// Time Base Register (64-bit counter)
// ---------------------------------------------------------------

uint64_t read_tb();

// ---------------------------------------------------------------
// MMU / Page Table
// SDR1 holds the page table base address (hashed page table model)
// ---------------------------------------------------------------

uint64_t read_sdr1();
void write_sdr1(uint64_t value);

// ---------------------------------------------------------------
// Synchronization / Barriers
// ---------------------------------------------------------------

void sync();
void isync();
void eieio();

// ---------------------------------------------------------------
// Interrupt handling
// ---------------------------------------------------------------

// Interrupt vector offsets (Book III-S exception vectors)
constexpr uint64_t VECTOR_SYSTEM_RESET      = 0x0100;
constexpr uint64_t VECTOR_MACHINE_CHECK     = 0x0200;
constexpr uint64_t VECTOR_DATA_STORAGE      = 0x0300;
constexpr uint64_t VECTOR_DATA_SEGMENT      = 0x0380;
constexpr uint64_t VECTOR_INSTRUCTION_STORAGE = 0x0400;
constexpr uint64_t VECTOR_INSTRUCTION_SEGMENT = 0x0480;
constexpr uint64_t VECTOR_EXTERNAL          = 0x0500;
constexpr uint64_t VECTOR_ALIGNMENT         = 0x0600;
constexpr uint64_t VECTOR_PROGRAM           = 0x0700;
constexpr uint64_t VECTOR_FP_UNAVAILABLE    = 0x0800;
constexpr uint64_t VECTOR_DECREMENTER       = 0x0900;
constexpr uint64_t VECTOR_HYPERVISOR_DECREMENTER = 0x0980;
constexpr uint64_t VECTOR_DOORBELL          = 0x0A00;
constexpr uint64_t VECTOR_SYSTEM_CALL       = 0x0C00;
constexpr uint64_t VECTOR_TRACE             = 0x0D00;
constexpr uint64_t VECTOR_HYPERVISOR_DATA_STORAGE = 0x0E00;
constexpr uint64_t VECTOR_HYPERVISOR_INSTRUCTION_STORAGE = 0x0E20;
constexpr uint64_t VECTOR_HYPERVISOR_EMULATION = 0x0E40;
constexpr uint64_t VECTOR_HYPERVISOR_MAINTENANCE = 0x0E60;
constexpr uint64_t VECTOR_PERFORMANCE       = 0x0F00;

// Maximum number of interrupt handlers
constexpr int MAX_INTERRUPT_HANDLERS = 32;

// Interrupt handler type
typedef void (*interrupt_handler_t)();

// Interrupt management functions
void interrupt_init();
void interrupt_register_handler(int vector, interrupt_handler_t handler);

// ---------------------------------------------------------------
// MMU functions
// ---------------------------------------------------------------

void mmu_init();
void map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);
void unmap_page(uint64_t virtual_addr);

// ---------------------------------------------------------------
// CPU initialization
// ---------------------------------------------------------------

void cpu_init();
uint64_t cpu_get_id();
void cpu_enable_interrupts();
void cpu_disable_interrupts();

// ---------------------------------------------------------------
// Architecture initialization
// ---------------------------------------------------------------

void init();

} // namespace ppc64
} // namespace arch
} // namespace kernel
