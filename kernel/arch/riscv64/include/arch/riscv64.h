//
// RISC-V 64-bit (RV64) Architecture-Specific Code
//
// Targets RV64IMA running in S-mode (supervisor) on top of
// OpenSBI firmware.  QEMU virt machine is the primary platform.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace riscv64 {

// CPU control
void halt();
void enable_interrupts();
void disable_interrupts();
void wait_for_interrupt();

// CSR (Control and Status Register) operations
uint64_t read_sstatus();
void write_sstatus(uint64_t value);
uint64_t read_sie();
void write_sie(uint64_t value);
uint64_t read_sip();
uint64_t read_stvec();
void write_stvec(uint64_t value);
uint64_t read_sepc();
void write_sepc(uint64_t value);
uint64_t read_scause();
uint64_t read_stval();
uint64_t read_satp();
void write_satp(uint64_t value);
uint64_t read_time();

// Fence / barrier operations
void fence();
void fence_i();
void sfence_vma();

// Initialize architecture-specific features
void init();

} // namespace riscv64
} // namespace arch
} // namespace kernel
