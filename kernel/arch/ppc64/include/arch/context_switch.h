//
// PowerPC64 Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the PowerPC64 architecture.
//
// PowerPC64 register set:
//   - 32 general-purpose 64-bit registers (r0-r31)
//   - r0 = volatile (used as temp, not hardwired to 0)
//   - r1 = stack pointer (SP)
//   - r2 = TOC pointer (Thread-local in ELFv2)
//   - r3-r10 = arguments/return values (volatile)
//   - r11-r12 = volatile (scratch/PLT)
//   - r13 = small data area pointer (reserved)
//   - r14-r31 = callee-saved (non-volatile)
//   - LR = Link Register (return address)
//   - CTR = Count Register (loop counter / branch target)
//   - CR = Condition Register (8 x 4-bit fields)
//   - XER = Fixed-Point Exception Register
//   - SRR0/SRR1 = Save/Restore Registers (exception PC/MSR)
//
// For context switch, we save:
//   - Callee-saved GPRs: r14-r31
//   - Stack pointer: r1
//   - TOC pointer: r2
//   - Link Register: LR
//   - Condition Register: CR
//
// Full context (for interrupts) also includes:
//   - All GPRs r0-r31
//   - CTR, XER
//   - SRR0, SRR1
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ppc64 {
namespace context {

// ================================================================
// Thread context structure
// ================================================================

// Full thread context (for exceptions/interrupts)
// Saved when entering exception handler
struct FullContext {
    // General purpose registers (r0-r31)
    uint64_t gpr[32];
    
    // Special purpose registers
    uint64_t lr;        // Link Register
    uint64_t ctr;       // Count Register
    uint64_t cr;        // Condition Register (only low 32 bits used)
    uint64_t xer;       // Fixed-Point Exception Register
    
    // Exception state
    uint64_t srr0;      // Save/Restore Register 0 (exception PC)
    uint64_t srr1;      // Save/Restore Register 1 (exception MSR)
    
    // Additional state for debugging
    uint64_t dar;       // Data Address Register (fault address)
    uint64_t dsisr;     // Data Storage Interrupt Status Register
};

// Minimal thread context (for voluntary context switch)
// Only callee-saved registers per ELFv2 ABI
struct SwitchContext {
    // Non-volatile GPRs (callee-saved)
    uint64_t r14;
    uint64_t r15;
    uint64_t r16;
    uint64_t r17;
    uint64_t r18;
    uint64_t r19;
    uint64_t r20;
    uint64_t r21;
    uint64_t r22;
    uint64_t r23;
    uint64_t r24;
    uint64_t r25;
    uint64_t r26;
    uint64_t r27;
    uint64_t r28;
    uint64_t r29;
    uint64_t r30;
    uint64_t r31;
    
    // Stack pointer
    uint64_t sp;        // r1
    
    // TOC pointer
    uint64_t toc;       // r2
    
    // Link Register (return address)
    uint64_t lr;
    
    // Condition Register (callee-saved fields: CR2, CR3, CR4)
    uint64_t cr;
};

// ================================================================
// Thread Control Block (TCB) extension for PowerPC64
// ================================================================

struct ArchThreadData {
    uint64_t kernel_sp;         // Kernel stack pointer
    SwitchContext* context;     // Saved context for switch
    void* fpu_state;            // FPU/VMX state (if used)
    uint64_t toc;               // TOC pointer for this thread
    uint64_t sprg3;             // Thread-local via SPRG3
    uint32_t cpu_id;            // CPU this thread is running on
};

// ================================================================
// Context Switch API
// ================================================================

// Initialize a new thread's context
// Returns pointer to the initialized SwitchContext
SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg);

// Switch from current context to new context
// Saves current state to *old_ctx, restores from new_ctx
extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx);

// Save full context (for interrupt entry)
extern "C" void save_full_context(FullContext* ctx);

// Restore full context (for interrupt return)
extern "C" void restore_full_context(FullContext* ctx);

// ================================================================
// Register access helpers
// ================================================================

inline uint64_t get_sp()
{
    uint64_t sp;
#if defined(_MSC_VER)
    sp = 0;
#else
    asm volatile ("mr %0, 1" : "=r"(sp));
#endif
    return sp;
}

inline uint64_t get_toc()
{
    uint64_t toc;
#if defined(_MSC_VER)
    toc = 0;
#else
    asm volatile ("mr %0, 2" : "=r"(toc));
#endif
    return toc;
}

inline void set_toc(uint64_t toc)
{
#if !defined(_MSC_VER)
    asm volatile ("mr 2, %0" : : "r"(toc));
#else
    (void)toc;
#endif
}

inline uint64_t get_lr()
{
    uint64_t lr;
#if defined(_MSC_VER)
    lr = 0;
#else
    asm volatile ("mflr %0" : "=r"(lr));
#endif
    return lr;
}

inline void set_lr(uint64_t lr)
{
#if !defined(_MSC_VER)
    asm volatile ("mtlr %0" : : "r"(lr));
#else
    (void)lr;
#endif
}

} // namespace context
} // namespace ppc64
} // namespace arch
} // namespace kernel
