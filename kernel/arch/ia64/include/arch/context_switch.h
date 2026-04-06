//
// IA-64 (Itanium) Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the IA-64 architecture.
//
// IA-64 register set:
//   - 128 general registers (GR0-GR127)
//     - GR0 = always 0
//     - GR1 = global pointer (gp)
//     - GR2-GR3 = scratch
//     - GR4-GR7 = preserved (callee-saved)
//     - GR8-GR11 = return values / scratch
//     - GR12 = stack pointer (sp)
//     - GR13 = thread pointer (tp)
//     - GR14-GR31 = scratch (stacked in RSE)
//     - GR32-GR127 = stacked registers (RSE managed)
//   - 128 floating-point registers (FR0-FR127)
//   - 64 predicate registers (PR0-PR63)
//   - 8 branch registers (BR0-BR7)
//   - Application registers (AR0-AR127)
//
// The Register Stack Engine (RSE) automatically saves/restores
// stacked registers on procedure calls.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {
namespace context {

// ================================================================
// Thread context structure
// ================================================================

// Full thread context (for exceptions/interrupts)
struct FullContext {
    // Preserved general registers (GR4-GR7)
    uint64_t gr4;
    uint64_t gr5;
    uint64_t gr6;
    uint64_t gr7;
    
    // Stack and thread pointers
    uint64_t sp;     // GR12
    uint64_t tp;     // GR13
    uint64_t gp;     // GR1 (global pointer)
    
    // Branch registers
    uint64_t rp;     // BR0 (return pointer)
    uint64_t br1;
    uint64_t br2;
    uint64_t br3;
    uint64_t br4;
    uint64_t br5;
    
    // Application registers
    uint64_t pfs;    // AR.PFS (previous function state)
    uint64_t lc;     // AR.LC (loop count)
    uint64_t ec;     // AR.EC (epilog count)
    uint64_t bsp;    // AR.BSP (backing store pointer)
    uint64_t bspstore;  // AR.BSPSTORE
    uint64_t rnat;   // AR.RNAT (RSE NaT collection)
    uint64_t unat;   // AR.UNAT (user NaT collection)
    uint64_t fpsr;   // AR.FPSR (FP status register)
    uint64_t ccv;    // AR.CCV (compare and exchange value)
    
    // Processor status
    uint64_t psr;    // PSR (processor status register)
    uint64_t iip;    // CR.IIP (interruption IP)
    uint64_t ipsr;   // CR.IPSR (interruption PSR)
    uint64_t ifs;    // CR.IFS (interruption function state)
    uint64_t isr;    // CR.ISR (interruption status register)
    uint64_t iim;    // CR.IIM (interruption immediate)
    uint64_t iha;    // CR.IHA (interruption hash address)
    uint64_t ifa;    // CR.IFA (interruption faulting address)
    
    // Predicate registers (64-bit mask)
    uint64_t pr;
};

// Minimal thread context (for voluntary context switch)
struct SwitchContext {
    // Preserved general registers
    uint64_t gr4;
    uint64_t gr5;
    uint64_t gr6;
    uint64_t gr7;
    
    // Stack pointer
    uint64_t sp;     // GR12
    
    // Global pointer
    uint64_t gp;     // GR1
    
    // Return pointer (branch register 0)
    uint64_t rp;     // BR0
    
    // Previous function state
    uint64_t pfs;    // AR.PFS
    
    // RSE state for context switch
    uint64_t bsp;
    uint64_t bspstore;
    uint64_t rnat;
    
    // Predicate registers
    uint64_t pr;
};

// ================================================================
// Thread Control Block (TCB) extension for IA-64
// ================================================================

struct ArchThreadData {
    uint64_t kernel_sp;
    SwitchContext* context;
    void* fpu_state;
    uint64_t tp;          // Thread pointer (GR13)
    uint64_t region_id;   // Region ID for TLB
    uint64_t pt_base;     // Page table base
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
    // TODO: IA-64 specific assembly to read GR12
    asm volatile ("mov %0 = r12" : "=r"(sp));
#endif
    return sp;
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
} // namespace ia64
} // namespace arch
} // namespace kernel
