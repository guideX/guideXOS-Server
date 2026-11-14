//
// AMD64 (x86-64) Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the AMD64 architecture. Context switching is
// fundamental to multitasking - it saves the current thread's
// register state and restores another thread's state.
//
// This interface describes a voluntary function-call switch under the
// Microsoft x64 ABI. It does not represent the interrupt frame required for
// preemption or the CPU frame required for a CPL3 transition.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>
#include <stddef.h>

namespace kernel {
namespace arch {
namespace amd64 {
namespace context {

// Saved state for an ordinary Microsoft x64 ABI function-call switch.
// The freestanding build disables MMX/SSE in C/C++; FPU/SIMD task state and
// arbitrary interrupt frames are outside this context contract.
struct SwitchContext {
    uint64_t rbx;    // Microsoft x64 nonvolatile GPR
    uint64_t rbp;    // Microsoft x64 nonvolatile GPR
    uint64_t rdi;    // Microsoft x64 nonvolatile GPR
    uint64_t rsi;    // Microsoft x64 nonvolatile GPR
    uint64_t r12;    // Microsoft x64 nonvolatile GPR
    uint64_t r13;    // Microsoft x64 nonvolatile GPR
    uint64_t r14;    // Microsoft x64 nonvolatile GPR
    uint64_t r15;    // Microsoft x64 nonvolatile GPR
    uint64_t rsp;    // Stack pointer at the resumed call site
    uint64_t rip;    // Continuation address
};

static_assert(sizeof(SwitchContext) == 80, "AMD64 switch context layout must match context_switch.asm");
static_assert(offsetof(SwitchContext, rsp) == 64, "AMD64 context RSP offset mismatch");
static_assert(offsetof(SwitchContext, rip) == 72, "AMD64 context RIP offset mismatch");

// ================================================================
// Thread Control Block (TCB) extension for AMD64
//
// Architecture-specific data stored in each thread's TCB.
// ================================================================

struct ArchThreadData {
    // Caller-owned kernel stack used by this context.
    uint64_t kernel_stack_base;
    uint64_t kernel_stack_size;

    // Stack pointer used by this context.
    uint64_t kernel_sp;
    
    // Saved context (points to SwitchContext on stack)
    SwitchContext* context;
    
    // Reserved for future FPU state management. Must remain null while the
    // current no-SIMD context contract is active.
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

// Initialize a new kernel context within a caller-owned stack. The
// initial Microsoft x64 entry stack includes the required 32-byte shadow
// space. The caller must supply at least sizeof(SwitchContext)+40 bytes.
//
// Parameters:
//   stack_base - Start of the writable stack allocation
//   stack_size - Size in bytes of the writable stack allocation
//   entry_point - Function pointer where thread execution begins
//   arg        - Argument passed to entry_point
//
// Returns:
//   Pointer to the initialized SwitchContext on the stack
SwitchContext* init_context(uint64_t stack_base, uint64_t stack_size,
                            void (*entry_point)(void*), void* arg);

// Save and resume ordinary function-call contexts. This is not suitable for
// switching directly from an IRQ frame; callers use the Microsoft x64 ABI.
#if defined(__GNUC__) || defined(__clang__)
#define GXOS_AMD64_SWITCH_ABI __attribute__((ms_abi))
#else
#define GXOS_AMD64_SWITCH_ABI
#endif
extern "C" void GXOS_AMD64_SWITCH_ABI switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx);

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
void arch_thread_create(ArchThreadData* data, uint64_t stack_base, uint64_t stack_size,
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
