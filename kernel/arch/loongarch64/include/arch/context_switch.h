//
// LoongArch 64-bit Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the LoongArch64 architecture. Context switching
// is fundamental to multitasking - it saves the current thread's
// register state and restores another thread's state.
//
// LoongArch64 register set:
//   - 32 general-purpose registers ($r0-$r31)
//   - $r0 = zero (hardwired, not saved)
//   - $r1 = ra (return address)
//   - $r2 = tp (thread pointer)
//   - $r3 = sp (stack pointer)
//   - $r4-$r11 = a0-a7 (arguments, caller-saved)
//   - $r12-$r20 = t0-t8 (temporaries, caller-saved)
//   - $r21 = reserved
//   - $r22 = fp/s9 (frame pointer, callee-saved)
//   - $r23-$r31 = s0-s8 (callee-saved)
//
// For context switch, we only need to save callee-saved registers
// plus the stack pointer and return address, since the C calling
// convention handles caller-saved registers.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace context {

// ================================================================
// Thread context structure
//
// Contains all state needed to resume a thread. This is saved on
// the kernel stack during context switch.
//
// Full context (for exception handling):
//   - All 31 GPRs (excluding $r0)
//   - ERA (exception return address)
//   - PRMD (previous mode)
//   - CRMD (current mode, for reference)
//
// Minimal context (for voluntary context switch):
//   - Callee-saved registers ($r22-$r31 = s0-s8, fp)
//   - Stack pointer ($r3)
//   - Return address ($r1)
//   - Thread pointer ($r2, if used)
// ================================================================

// Full thread context (used for exceptions/interrupts)
struct FullContext {
    // General-purpose registers ($r1-$r31, $r0 is always zero)
    uint64_t ra;     // $r1 - return address
    uint64_t tp;     // $r2 - thread pointer
    uint64_t sp;     // $r3 - stack pointer
    uint64_t a0;     // $r4 - argument/return value 0
    uint64_t a1;     // $r5 - argument 1
    uint64_t a2;     // $r6 - argument 2
    uint64_t a3;     // $r7 - argument 3
    uint64_t a4;     // $r8 - argument 4
    uint64_t a5;     // $r9 - argument 5
    uint64_t a6;     // $r10 - argument 6
    uint64_t a7;     // $r11 - argument 7 (also syscall number)
    uint64_t t0;     // $r12 - temporary 0
    uint64_t t1;     // $r13 - temporary 1
    uint64_t t2;     // $r14 - temporary 2
    uint64_t t3;     // $r15 - temporary 3
    uint64_t t4;     // $r16 - temporary 4
    uint64_t t5;     // $r17 - temporary 5
    uint64_t t6;     // $r18 - temporary 6
    uint64_t t7;     // $r19 - temporary 7
    uint64_t t8;     // $r20 - temporary 8
    uint64_t r21;    // $r21 - reserved (kernel scratch)
    uint64_t fp;     // $r22 - frame pointer / s9
    uint64_t s0;     // $r23 - callee-saved 0
    uint64_t s1;     // $r24 - callee-saved 1
    uint64_t s2;     // $r25 - callee-saved 2
    uint64_t s3;     // $r26 - callee-saved 3
    uint64_t s4;     // $r27 - callee-saved 4
    uint64_t s5;     // $r28 - callee-saved 5
    uint64_t s6;     // $r29 - callee-saved 6
    uint64_t s7;     // $r30 - callee-saved 7
    uint64_t s8;     // $r31 - callee-saved 8
    
    // CSR state
    uint64_t era;    // Exception Return Address
    uint64_t prmd;   // Previous Mode (PLV, IE state before exception)
    uint64_t crmd;   // Current Mode (for reference)
    uint64_t estat;  // Exception Status (cause code)
    uint64_t badv;   // Bad Virtual Address (for faults)
};

// Minimal thread context (used for voluntary context switch)
// Only callee-saved registers need to be preserved across calls
struct SwitchContext {
    uint64_t ra;     // $r1 - return address (where to resume)
    uint64_t sp;     // $r3 - stack pointer
    uint64_t tp;     // $r2 - thread pointer (TLS)
    uint64_t fp;     // $r22 / s9 - frame pointer
    uint64_t s0;     // $r23 - callee-saved
    uint64_t s1;     // $r24 - callee-saved
    uint64_t s2;     // $r25 - callee-saved
    uint64_t s3;     // $r26 - callee-saved
    uint64_t s4;     // $r27 - callee-saved
    uint64_t s5;     // $r28 - callee-saved
    uint64_t s6;     // $r29 - callee-saved
    uint64_t s7;     // $r30 - callee-saved
    uint64_t s8;     // $r31 - callee-saved
};

// ================================================================
// Thread Control Block (TCB) extension for LoongArch
//
// Architecture-specific data stored in each thread's TCB.
// ================================================================

struct ArchThreadData {
    // Kernel stack pointer (used during context switch)
    uint64_t kernel_sp;
    
    // Saved context (points to SwitchContext on stack)
    SwitchContext* context;
    
    // Floating-point state (if FPU enabled)
    // TODO: Add FP register save area when FPU support is added
    
    // Thread-local storage base (for tp register)
    uint64_t tls_base;
    
    // Address space ID (for TLB tagging)
    uint32_t asid;
    
    // Page table root (physical address for CSR_PGD)
    uint64_t pgd_phys;
};

// ================================================================
// Context Switch API
//
// These functions integrate with the scheduler to perform context
// switches. The actual register save/restore is done in assembly.
// ================================================================

// Initialize a new thread's context.
// Sets up the initial stack frame so the thread starts executing
// at 'entry_point' with 'arg' as its argument.
//
// Parameters:
//   stack_top  - Top of the thread's kernel stack
//   entry_point - Function pointer where thread execution begins
//   arg        - Argument passed to entry_point
//
// Returns:
//   Pointer to the initialized SwitchContext on the stack
SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg);

// Perform a context switch from 'old_ctx' to 'new_ctx'.
// Saves current callee-saved registers to old_ctx and restores
// from new_ctx, then returns to the new thread.
//
// This function is called like a normal function but "returns"
// in a different thread context.
//
// Assembly implementation required.
extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx);

// Save full context to the provided structure.
// Used by exception handlers to capture complete thread state.
extern "C" void save_full_context(FullContext* ctx);

// Restore full context and return from exception.
// This function does not return - it jumps to ctx->era.
extern "C" void restore_full_context(FullContext* ctx);

// Get the current stack pointer.
inline uint64_t get_sp()
{
    uint64_t sp;
#if defined(_MSC_VER)
    sp = 0;  // MSVC stub
#else
    asm volatile ("move %0, $sp" : "=r"(sp));
#endif
    return sp;
}

// Get the current thread pointer.
inline uint64_t get_tp()
{
    uint64_t tp;
#if defined(_MSC_VER)
    tp = 0;  // MSVC stub
#else
    asm volatile ("move %0, $tp" : "=r"(tp));
#endif
    return tp;
}

// Set the thread pointer.
inline void set_tp(uint64_t tp)
{
#if !defined(_MSC_VER)
    asm volatile ("move $tp, %0" : : "r"(tp));
#else
    (void)tp;
#endif
}

// ================================================================
// Scheduler hooks
//
// These are called by the generic scheduler at key points.
// ================================================================

// Called when a new thread is created.
// Allocates and initializes architecture-specific thread data.
void arch_thread_create(ArchThreadData* data, uint64_t stack, 
                        void (*entry)(void*), void* arg);

// Called when a thread is destroyed.
// Cleans up architecture-specific resources.
void arch_thread_destroy(ArchThreadData* data);

// Called to switch to a different thread.
// Saves current thread state and restores target thread.
void arch_switch_to(ArchThreadData* current, ArchThreadData* next);

// Called on timer interrupt for preemptive scheduling.
void arch_timer_tick();

// Set the timer for next preemption.
void arch_set_timer(uint64_t ticks);

} // namespace context
} // namespace loongarch64
} // namespace arch
} // namespace kernel
