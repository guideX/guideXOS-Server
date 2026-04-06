//
// AMD64 (x86-64) Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the AMD64 architecture. Context switching is
// fundamental to multitasking - it saves the current thread's
// register state and restores another thread's state.
//
// AMD64 register set:
//   - 16 general-purpose registers (RAX, RBX, RCX, RDX, RSI, RDI,
//     RBP, RSP, R8-R15)
//   - Callee-saved: RBX, RBP, R12-R15
//   - Caller-saved: RAX, RCX, RDX, RSI, RDI, R8-R11
//   - RSP = stack pointer
//   - RIP = instruction pointer (implicitly saved via CALL/RET)
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
namespace amd64 {
namespace context {

// ================================================================
// Thread context structure
//
// Contains all state needed to resume a thread. This is saved on
// the kernel stack during context switch.
//
// Full context (for exception handling):
//   - All 16 GPRs
//   - RFLAGS
//   - RIP (from interrupt frame)
//   - Segment registers (CS, SS, DS, ES, FS, GS)
//
// Minimal context (for voluntary context switch):
//   - Callee-saved registers (RBX, RBP, R12-R15)
//   - Stack pointer (RSP)
//   - Return address (RIP, implicitly via stack)
// ================================================================

// Full thread context (used for exceptions/interrupts)
struct FullContext {
    // General-purpose registers
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    
    // Instruction pointer and flags
    uint64_t rip;
    uint64_t rflags;
    
    // Segment registers
    uint64_t cs;
    uint64_t ss;
    uint64_t ds;
    uint64_t es;
    uint64_t fs;
    uint64_t gs;
    
    // FS/GS base addresses (for TLS)
    uint64_t fs_base;
    uint64_t gs_base;
    
    // Interrupt/exception info
    uint64_t error_code;
    uint64_t vector;
};

// Minimal thread context (used for voluntary context switch)
// Only callee-saved registers need to be preserved across calls
struct SwitchContext {
    uint64_t rbx;    // Callee-saved
    uint64_t rbp;    // Base pointer / frame pointer
    uint64_t r12;    // Callee-saved
    uint64_t r13;    // Callee-saved
    uint64_t r14;    // Callee-saved
    uint64_t r15;    // Callee-saved
    uint64_t rsp;    // Stack pointer
    uint64_t rip;    // Return address (where to resume)
};

// ================================================================
// Thread Control Block (TCB) extension for AMD64
//
// Architecture-specific data stored in each thread's TCB.
// ================================================================

struct ArchThreadData {
    // Kernel stack pointer (used during context switch)
    uint64_t kernel_sp;
    
    // Saved context (points to SwitchContext on stack)
    SwitchContext* context;
    
    // Floating-point / SSE state pointer
    // Points to FXSAVE/XSAVE area (must be 16-byte aligned)
    void* fpu_state;
    
    // FS base (user TLS)
    uint64_t fs_base;
    
    // GS base (kernel per-CPU data)
    uint64_t gs_base;
    
    // Page table root (CR3 value)
    uint64_t cr3;
    
    // I/O permission bitmap base (for IOPB in TSS)
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
// This function does not return - it performs IRETQ.
extern "C" void restore_full_context(FullContext* ctx);

// Get the current stack pointer.
inline uint64_t get_sp()
{
    uint64_t sp;
#if defined(_MSC_VER)
    sp = 0;  // MSVC stub - use intrinsic in real implementation
#else
    asm volatile ("mov %%rsp, %0" : "=r"(sp));
#endif
    return sp;
}

// Get the current base pointer.
inline uint64_t get_bp()
{
    uint64_t bp;
#if defined(_MSC_VER)
    bp = 0;
#else
    asm volatile ("mov %%rbp, %0" : "=r"(bp));
#endif
    return bp;
}

// Get FS base (user TLS).
inline uint64_t get_fs_base()
{
    uint64_t base;
#if defined(_MSC_VER)
    base = 0;
#else
    // Read from MSR 0xC0000100 (FS_BASE)
    asm volatile (
        "mov $0xC0000100, %%ecx\n"
        "rdmsr\n"
        "shl $32, %%rdx\n"
        "or %%rdx, %%rax\n"
        "mov %%rax, %0"
        : "=r"(base)
        :
        : "rax", "rcx", "rdx"
    );
#endif
    return base;
}

// Set FS base (user TLS).
inline void set_fs_base(uint64_t base)
{
#if defined(_MSC_VER)
    (void)base;
#else
    // Write to MSR 0xC0000100 (FS_BASE)
    asm volatile (
        "mov %0, %%rax\n"
        "mov %%eax, %%edx\n"
        "shr $32, %%rdx\n"
        "mov $0xC0000100, %%ecx\n"
        "wrmsr"
        :
        : "r"(base)
        : "rax", "rcx", "rdx"
    );
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

// Set the timer for next preemption (via LAPIC timer or PIT).
void arch_set_timer(uint64_t ticks);

} // namespace context
} // namespace amd64
} // namespace arch
} // namespace kernel
