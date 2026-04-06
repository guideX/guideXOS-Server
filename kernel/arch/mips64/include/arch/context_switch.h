//
// MIPS64 Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the MIPS64 architecture.
//
// MIPS64 register set (n64 ABI):
//   $0  (zero)  - Always zero (hardwired)
//   $1  (at)    - Assembler temporary
//   $2-$3 (v0-v1) - Return values
//   $4-$11 (a0-a7) - Arguments
//   $12-$15 (t4-t7) - Temporaries (caller-saved)
//   $16-$23 (s0-s7) - Saved registers (callee-saved)
//   $24-$25 (t8-t9) - Temporaries (caller-saved)
//   $26-$27 (k0-k1) - Kernel reserved
//   $28 (gp)   - Global pointer
//   $29 (sp)   - Stack pointer
//   $30 (fp/s8) - Frame pointer / saved register (callee-saved)
//   $31 (ra)   - Return address
//
// Callee-saved: $s0-$s7 ($16-$23), $fp ($30), $ra ($31), $gp ($28)
//
// Special registers:
//   HI, LO     - Multiply/divide result registers
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace mips64 {
namespace context {

// ================================================================
// Thread context structure
// ================================================================

// Full thread context (for exceptions/interrupts)
// This structure matches the layout saved by boot.s exception handlers
struct FullContext {
    // General purpose registers (32 GPRs, $0 is always zero)
    uint64_t at;     // $1  - Assembler temporary
    uint64_t v0;     // $2  - Return value 0
    uint64_t v1;     // $3  - Return value 1
    uint64_t a0;     // $4  - Argument 0
    uint64_t a1;     // $5  - Argument 1
    uint64_t a2;     // $6  - Argument 2
    uint64_t a3;     // $7  - Argument 3
    uint64_t a4;     // $8  - Argument 4 (t0 in o32)
    uint64_t a5;     // $9  - Argument 5 (t1 in o32)
    uint64_t a6;     // $10 - Argument 6 (t2 in o32)
    uint64_t a7;     // $11 - Argument 7 (t3 in o32)
    uint64_t t4;     // $12 - Temporary 4
    uint64_t t5;     // $13 - Temporary 5
    uint64_t t6;     // $14 - Temporary 6
    uint64_t t7;     // $15 - Temporary 7
    uint64_t s0;     // $16 - Saved 0
    uint64_t s1;     // $17 - Saved 1
    uint64_t s2;     // $18 - Saved 2
    uint64_t s3;     // $19 - Saved 3
    uint64_t s4;     // $20 - Saved 4
    uint64_t s5;     // $21 - Saved 5
    uint64_t s6;     // $22 - Saved 6
    uint64_t s7;     // $23 - Saved 7
    uint64_t t8;     // $24 - Temporary 8
    uint64_t t9;     // $25 - Temporary 9
    // k0, k1 ($26, $27) are kernel reserved, not saved
    uint64_t gp;     // $28 - Global pointer
    uint64_t sp;     // $29 - Stack pointer
    uint64_t fp;     // $30 - Frame pointer / s8
    uint64_t ra;     // $31 - Return address
    
    // Special registers
    uint64_t hi;     // HI register (multiply/divide high)
    uint64_t lo;     // LO register (multiply/divide low)
    
    // CP0 state
    uint64_t epc;    // Exception Program Counter (CP0 $14)
    uint64_t status; // Status register (CP0 $12)
    uint64_t cause;  // Cause register (CP0 $13)
    uint64_t badvaddr; // Bad Virtual Address (CP0 $8)
};

// Switch context (for voluntary context switch)
// Only callee-saved registers need to be preserved
struct SwitchContext {
    // Callee-saved registers
    uint64_t s0;     // $16
    uint64_t s1;     // $17
    uint64_t s2;     // $18
    uint64_t s3;     // $19
    uint64_t s4;     // $20
    uint64_t s5;     // $21
    uint64_t s6;     // $22
    uint64_t s7;     // $23
    uint64_t fp;     // $30 / s8
    uint64_t ra;     // $31 - Return address (used as resume point)
    uint64_t sp;     // $29 - Stack pointer
    uint64_t gp;     // $28 - Global pointer
    
    // We also save HI/LO since they might be in use
    uint64_t hi;
    uint64_t lo;
};

// ================================================================
// Context switch functions
// ================================================================

// Initialize a new thread context
// stack_top: Top of the thread's stack
// entry_point: Function to call when thread starts
// arg: Argument to pass to entry_point
// Returns: Pointer to initialized SwitchContext
SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg);

// Switch from current thread to new thread
// old_ctx: Pointer to store current thread's context
// new_ctx: Pointer to new thread's context to restore
extern "C" void context_switch(SwitchContext** old_ctx, SwitchContext* new_ctx);

// Entry wrapper for new threads
extern "C" void thread_entry_wrapper();

// ================================================================
// Full context functions (for exceptions)
// ================================================================

// Save full context to structure
void save_full_context(FullContext* ctx);

// Restore full context from structure
void restore_full_context(const FullContext* ctx);

// Get pointer to current exception context
FullContext* get_exception_context();

} // namespace context
} // namespace mips64
} // namespace arch
} // namespace kernel
