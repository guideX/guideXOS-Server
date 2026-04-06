//
// ARM (32-bit) Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the ARM architecture. Context switching is
// fundamental to multitasking - it saves the current thread's
// register state and restores another thread's state.
//
// ARM register set (ARMv7-A):
//   - 16 general-purpose registers (R0-R15)
//   - R0-R3   = arguments/scratch (caller-saved)
//   - R4-R11  = callee-saved (v1-v8)
//   - R12 (IP) = intra-procedure scratch
//   - R13 (SP) = stack pointer
//   - R14 (LR) = link register (return address)
//   - R15 (PC) = program counter
//   - CPSR = Current Program Status Register
//
// For context switch, we only need to save callee-saved registers
// (R4-R11), SP, LR, and optionally CPSR.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm {
namespace context {

// ================================================================
// Thread context structure
//
// Full context (for exception handling):
//   - All 16 GPRs (R0-R15)
//   - CPSR (status register)
//   - SPSR (saved status, if from exception)
//
// Minimal context (for voluntary context switch):
//   - Callee-saved registers (R4-R11)
//   - Stack pointer (R13/SP)
//   - Link register (R14/LR)
// ================================================================

// Full thread context (used for exceptions/interrupts)
struct FullContext {
    // General-purpose registers
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;   // FP (frame pointer) in APCS
    uint32_t r12;   // IP (intra-procedure scratch)
    uint32_t sp;    // R13
    uint32_t lr;    // R14
    uint32_t pc;    // R15
    
    // Status registers
    uint32_t cpsr;
    uint32_t spsr;  // Saved PSR (valid in exception modes)
    
    // Exception info
    uint32_t exception_type;
    uint32_t fault_address;  // FAR (Fault Address Register)
};

// Minimal thread context (used for voluntary context switch)
// Only callee-saved registers need to be preserved across calls
struct SwitchContext {
    uint32_t r4;     // v1 - callee-saved
    uint32_t r5;     // v2 - callee-saved
    uint32_t r6;     // v3 - callee-saved
    uint32_t r7;     // v4 - callee-saved (thumb frame pointer)
    uint32_t r8;     // v5 - callee-saved
    uint32_t r9;     // v6/SB - callee-saved (platform register)
    uint32_t r10;    // v7/SL - callee-saved
    uint32_t r11;    // v8/FP - frame pointer
    uint32_t sp;     // R13 - stack pointer
    uint32_t lr;     // R14 - return address
};

// ================================================================
// Thread Control Block (TCB) extension for ARM
// ================================================================

struct ArchThreadData {
    // Kernel stack pointer
    uint32_t kernel_sp;
    
    // Saved context
    SwitchContext* context;
    
    // VFP/NEON state pointer (if FPU enabled)
    void* fpu_state;
    
    // Thread-local storage pointer (for TPIDRURW)
    uint32_t tls_base;
    
    // Page table base (TTBR0 value)
    uint32_t ttbr0;
    
    // Context ID (CONTEXTIDR for TLB tagging)
    uint32_t context_id;
};

// ================================================================
// Context Switch API
// ================================================================

// Initialize a new thread's context.
SwitchContext* init_context(uint32_t stack_top, void (*entry_point)(void*), void* arg);

// Perform a context switch from 'old_ctx' to 'new_ctx'.
extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx);

// Save/restore full context for exception handling.
extern "C" void save_full_context(FullContext* ctx);
extern "C" void restore_full_context(FullContext* ctx);

// Get the current stack pointer.
inline uint32_t get_sp()
{
    uint32_t sp;
#if defined(_MSC_VER)
    sp = 0;
#else
    asm volatile ("mov %0, sp" : "=r"(sp));
#endif
    return sp;
}

// Get the current link register.
inline uint32_t get_lr()
{
    uint32_t lr;
#if defined(_MSC_VER)
    lr = 0;
#else
    asm volatile ("mov %0, lr" : "=r"(lr));
#endif
    return lr;
}

// Get TLS base (TPIDRURW - User Read/Write Thread ID Register)
inline uint32_t get_tls_base()
{
    uint32_t tls;
#if defined(_MSC_VER)
    tls = 0;
#else
    asm volatile ("mrc p15, 0, %0, c13, c0, 2" : "=r"(tls));
#endif
    return tls;
}

// Set TLS base
inline void set_tls_base(uint32_t tls)
{
#if defined(_MSC_VER)
    (void)tls;
#else
    asm volatile ("mcr p15, 0, %0, c13, c0, 2" : : "r"(tls));
#endif
}

// ================================================================
// Scheduler hooks
// ================================================================

void arch_thread_create(ArchThreadData* data, uint32_t stack, 
                        void (*entry)(void*), void* arg);
void arch_thread_destroy(ArchThreadData* data);
void arch_switch_to(ArchThreadData* current, ArchThreadData* next);
void arch_timer_tick();
void arch_set_timer(uint32_t ticks);

} // namespace context
} // namespace arm
} // namespace arch
} // namespace kernel
