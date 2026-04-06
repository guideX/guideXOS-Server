//
// RISC-V 64-bit Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the RISC-V 64-bit architecture.
//
// RISC-V register set (RV64):
//   - 32 general-purpose registers (x0-x31)
//   - x0 (zero) = always 0
//   - x1 (ra) = return address
//   - x2 (sp) = stack pointer
//   - x3 (gp) = global pointer
//   - x4 (tp) = thread pointer
//   - x5-x7 (t0-t2) = temporaries
//   - x8 (s0/fp) = saved register / frame pointer
//   - x9 (s1) = saved register
//   - x10-x17 (a0-a7) = arguments / return values
//   - x18-x27 (s2-s11) = saved registers (callee-saved)
//   - x28-x31 (t3-t6) = temporaries
//
// Callee-saved: ra, sp, s0-s11 (x8-x9, x18-x27)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace riscv64 {
namespace context {

// ================================================================
// Thread context structure
// ================================================================

// Full thread context (for exceptions/interrupts)
struct FullContext {
    // All 31 GPRs (x1-x31, x0 is always zero)
    uint64_t ra;     // x1
    uint64_t sp;     // x2
    uint64_t gp;     // x3
    uint64_t tp;     // x4
    uint64_t t0;     // x5
    uint64_t t1;     // x6
    uint64_t t2;     // x7
    uint64_t s0;     // x8 / fp
    uint64_t s1;     // x9
    uint64_t a0;     // x10
    uint64_t a1;     // x11
    uint64_t a2;     // x12
    uint64_t a3;     // x13
    uint64_t a4;     // x14
    uint64_t a5;     // x15
    uint64_t a6;     // x16
    uint64_t a7;     // x17
    uint64_t s2;     // x18
    uint64_t s3;     // x19
    uint64_t s4;     // x20
    uint64_t s5;     // x21
    uint64_t s6;     // x22
    uint64_t s7;     // x23
    uint64_t s8;     // x24
    uint64_t s9;     // x25
    uint64_t s10;    // x26
    uint64_t s11;    // x27
    uint64_t t3;     // x28
    uint64_t t4;     // x29
    uint64_t t5;     // x30
    uint64_t t6;     // x31
    
    // CSR state
    uint64_t sepc;   // Supervisor exception PC
    uint64_t sstatus; // Supervisor status
    uint64_t scause; // Supervisor cause
    uint64_t stval;  // Supervisor trap value
};

// Minimal thread context (for voluntary context switch)
struct SwitchContext {
    uint64_t ra;     // Return address
    uint64_t sp;     // Stack pointer
    uint64_t s0;     // x8 / fp
    uint64_t s1;     // x9
    uint64_t s2;     // x18
    uint64_t s3;     // x19
    uint64_t s4;     // x20
    uint64_t s5;     // x21
    uint64_t s6;     // x22
    uint64_t s7;     // x23
    uint64_t s8;     // x24
    uint64_t s9;     // x25
    uint64_t s10;    // x26
    uint64_t s11;    // x27
};

// ================================================================
// Thread Control Block (TCB) extension for RISC-V 64
// ================================================================

struct ArchThreadData {
    uint64_t kernel_sp;
    SwitchContext* context;
    void* fpu_state;
    uint64_t tp;          // Thread pointer
    uint64_t satp;        // Page table root (SATP register)
    uint64_t hartid;      // Hart ID
};

// ================================================================
// Context Switch API
// ================================================================

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg);
extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx);
extern "C" void save_full_context(FullContext* ctx);
extern "C" void restore_full_context(FullContext* ctx);

inline uint64_t get_sp()
{
    uint64_t sp;
#if defined(_MSC_VER)
    sp = 0;
#else
    asm volatile ("mv %0, sp" : "=r"(sp));
#endif
    return sp;
}

inline uint64_t get_tp()
{
    uint64_t tp;
#if defined(_MSC_VER)
    tp = 0;
#else
    asm volatile ("mv %0, tp" : "=r"(tp));
#endif
    return tp;
}

inline void set_tp(uint64_t tp)
{
#if !defined(_MSC_VER)
    asm volatile ("mv tp, %0" : : "r"(tp));
#else
    (void)tp;
#endif
}

// ================================================================
// Scheduler hooks
// ================================================================

void arch_thread_create(ArchThreadData* data, uint64_t stack, 
                        void (*entry)(void*), void* arg);
void arch_thread_destroy(ArchThreadData* data);
void arch_switch_to(ArchThreadData* current, ArchThreadData* next);
void arch_timer_tick();
void arch_set_timer(uint64_t ticks);

} // namespace context
} // namespace riscv64
} // namespace arch
} // namespace kernel
