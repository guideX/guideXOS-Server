//
// SPARC v8 (32-bit) Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the SPARC v8 architecture.
//
// SPARC register set:
//   - 32 general registers visible at a time (from 136 total)
//   - Register windows: 8 globals (g0-g7), 8 ins (i0-i7),
//     8 locals (l0-l7), 8 outs (o0-o7)
//   - g0 = always 0
//   - o6 = stack pointer (sp)
//   - i6 = frame pointer (fp)
//   - o7 = return address
//   - i7 = return address from caller
//
// SPARC uses overlapping register windows. On SAVE, outs become ins
// of the new window. Register window overflow/underflow is handled
// by trap handlers.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc {
namespace context {

// ================================================================
// Thread context structure
// ================================================================

// Full thread context (for exceptions/interrupts)
struct FullContext {
    // Global registers (g0-g7, g0 is always 0)
    uint32_t g1, g2, g3, g4, g5, g6, g7;
    
    // Out registers (o0-o7)
    uint32_t o0, o1, o2, o3, o4, o5, o6, o7;
    
    // Local registers (l0-l7)
    uint32_t l0, l1, l2, l3, l4, l5, l6, l7;
    
    // In registers (i0-i7)
    uint32_t i0, i1, i2, i3, i4, i5, i6, i7;
    
    // Program counter and next PC (for delayed branch)
    uint32_t pc;
    uint32_t npc;
    
    // Processor State Register
    uint32_t psr;
    
    // Window Invalid Mask
    uint32_t wim;
    
    // Y register (for multiply/divide)
    uint32_t y;
    
    // Trap type
    uint32_t tbr;
};

// Minimal thread context (for voluntary context switch)
// SPARC requires saving all register windows for context switch
struct SwitchContext {
    // Stack pointer
    uint32_t sp;     // o6
    
    // Frame pointer
    uint32_t fp;     // i6
    
    // Return address
    uint32_t i7;
    
    // PC for return
    uint32_t pc;
    
    // PSR (for CWP - current window pointer)
    uint32_t psr;
    
    // Local and in registers of current window
    uint32_t l0, l1, l2, l3, l4, l5, l6, l7;
    uint32_t i0, i1, i2, i3, i4, i5;
};

// ================================================================
// Thread Control Block (TCB) extension for SPARC
// ================================================================

struct ArchThreadData {
    uint32_t kernel_sp;
    SwitchContext* context;
    void* fpu_state;
    uint32_t context_id;   // For MMU context
    uint32_t ctx_table;    // Context table pointer
};

// ================================================================
// Context Switch API
// ================================================================

SwitchContext* init_context(uint32_t stack_top, void (*entry_point)(void*), void* arg);
extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx);
extern "C" void save_full_context(FullContext* ctx);
extern "C" void restore_full_context(FullContext* ctx);

inline uint32_t get_sp()
{
    uint32_t sp;
#if defined(_MSC_VER)
    sp = 0;
#else
    asm volatile ("mov %%sp, %0" : "=r"(sp));
#endif
    return sp;
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
} // namespace sparc
} // namespace arch
} // namespace kernel
