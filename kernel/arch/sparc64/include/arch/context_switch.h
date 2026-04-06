//
// SPARC v9 (64-bit) Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the SPARC v9 (UltraSPARC) architecture.
//
// SPARC v9 differences from v8:
//   - 64-bit registers
//   - PSTATE replaces PSR (no ET bit; uses PSTATE.IE)
//   - TBA replaces TBR (bits [63:15])
//   - flushw instruction for window flush
//   - Register windows: CLEANWIN/OTHERWIN/CANRESTORE/CANSAVE
//   - TICK register for cycle counting
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc64 {
namespace context {

// ================================================================
// Thread context structure
// ================================================================

// Full thread context (for exceptions/interrupts)
struct FullContext {
    // Global registers (g1-g7, g0 is always 0)
    uint64_t g1, g2, g3, g4, g5, g6, g7;
    
    // Out registers (o0-o7)
    uint64_t o0, o1, o2, o3, o4, o5, o6, o7;
    
    // Local registers (l0-l7)
    uint64_t l0, l1, l2, l3, l4, l5, l6, l7;
    
    // In registers (i0-i7)
    uint64_t i0, i1, i2, i3, i4, i5, i6, i7;
    
    // Trap state
    uint64_t tpc;    // Trap PC
    uint64_t tnpc;   // Trap nPC
    uint64_t tstate; // Trap state
    uint64_t tt;     // Trap type
    
    // Processor state
    uint64_t pstate;
    uint64_t pil;    // Processor interrupt level
    uint64_t cwp;    // Current window pointer
    
    // Y register
    uint64_t y;
    
    // Trap level
    uint64_t tl;
};

// Minimal thread context (for voluntary context switch)
struct SwitchContext {
    // Stack pointer
    uint64_t sp;     // o6
    
    // Frame pointer
    uint64_t fp;     // i6
    
    // Return address
    uint64_t i7;
    
    // PC for return
    uint64_t pc;
    
    // PSTATE
    uint64_t pstate;
    
    // Current window pointer
    uint64_t cwp;
    
    // Local and in registers of current window
    uint64_t l0, l1, l2, l3, l4, l5, l6, l7;
    uint64_t i0, i1, i2, i3, i4, i5;
};

// ================================================================
// Thread Control Block (TCB) extension for SPARC64
// ================================================================

struct ArchThreadData {
    uint64_t kernel_sp;
    SwitchContext* context;
    void* fpu_state;
    uint64_t context_id;
    uint64_t ctx_primary;    // Primary context (MMU)
    uint64_t ctx_secondary;  // Secondary context
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
    asm volatile ("mov %%sp, %0" : "=r"(sp));
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
} // namespace sparc64
} // namespace arch
} // namespace kernel
