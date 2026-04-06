//
// x86 (32-bit) Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the x86 (IA-32) architecture. Context switching is
// fundamental to multitasking - it saves the current thread's
// register state and restores another thread's state.
//
// x86 register set:
//   - 8 general-purpose registers (EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP)
//   - Callee-saved: EBX, ESI, EDI, EBP
//   - Caller-saved: EAX, ECX, EDX
//   - ESP = stack pointer
//   - EIP = instruction pointer (implicitly saved via CALL/RET)
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
namespace x86 {
namespace context {

// ================================================================
// Thread context structure
//
// Contains all state needed to resume a thread. This is saved on
// the kernel stack during context switch.
//
// Full context (for exception handling):
//   - All 8 GPRs
//   - EFLAGS
//   - EIP (from interrupt frame)
//   - Segment registers (CS, SS, DS, ES, FS, GS)
//
// Minimal context (for voluntary context switch):
//   - Callee-saved registers (EBX, ESI, EDI, EBP)
//   - Stack pointer (ESP)
//   - Return address (EIP, implicitly via stack)
// ================================================================

// Full thread context (used for exceptions/interrupts)
struct FullContext {
    // General-purpose registers
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    
    // Instruction pointer and flags
    uint32_t eip;
    uint32_t eflags;
    
    // Segment registers
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    
    // Interrupt/exception info
    uint32_t error_code;
    uint32_t vector;
};

// Minimal thread context (used for voluntary context switch)
// Only callee-saved registers need to be preserved across calls
struct SwitchContext {
    uint32_t ebx;    // Callee-saved
    uint32_t esi;    // Callee-saved
    uint32_t edi;    // Callee-saved
    uint32_t ebp;    // Base pointer / frame pointer
    uint32_t esp;    // Stack pointer
    uint32_t eip;    // Return address (where to resume)
};

// ================================================================
// Thread Control Block (TCB) extension for x86
//
// Architecture-specific data stored in each thread's TCB.
// ================================================================

struct ArchThreadData {
    // Kernel stack pointer (used during context switch)
    uint32_t kernel_sp;
    
    // Saved context (points to SwitchContext on stack)
    SwitchContext* context;
    
    // Floating-point / SSE state pointer
    // Points to FXSAVE area (must be 16-byte aligned)
    void* fpu_state;
    
    // FS segment base (for TLS)
    uint32_t fs_base;
    
    // GS segment base
    uint32_t gs_base;
    
    // Page directory physical address (CR3 value)
    uint32_t cr3;
    
    // I/O permission bitmap offset in TSS
    uint16_t iopb_offset;
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
SwitchContext* init_context(uint32_t stack_top, void (*entry_point)(void*), void* arg);

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
// This function does not return - it performs IRET.
extern "C" void restore_full_context(FullContext* ctx);

// Get the current stack pointer.
inline uint32_t get_sp()
{
    uint32_t sp;
#if defined(_MSC_VER)
    sp = 0;  // MSVC stub
#else
    asm volatile ("mov %%esp, %0" : "=r"(sp));
#endif
    return sp;
}

// Get the current base pointer.
inline uint32_t get_bp()
{
    uint32_t bp;
#if defined(_MSC_VER)
    bp = 0;
#else
    asm volatile ("mov %%ebp, %0" : "=r"(bp));
#endif
    return bp;
}

// ================================================================
// Scheduler hooks
//
// These are called by the generic scheduler at key points.
// ================================================================

// Called when a new thread is created.
// Allocates and initializes architecture-specific thread data.
void arch_thread_create(ArchThreadData* data, uint32_t stack, 
                        void (*entry)(void*), void* arg);

// Called when a thread is destroyed.
// Cleans up architecture-specific resources.
void arch_thread_destroy(ArchThreadData* data);

// Called to switch to a different thread.
// Saves current thread state and restores target thread.
void arch_switch_to(ArchThreadData* current, ArchThreadData* next);

// Called on timer interrupt for preemptive scheduling.
void arch_timer_tick();

// Set the timer for next preemption (via PIT or LAPIC timer).
void arch_set_timer(uint32_t ticks);

} // namespace context
} // namespace x86
} // namespace arch
} // namespace kernel
