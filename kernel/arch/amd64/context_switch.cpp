//
// AMD64 Context Switch Implementation
//
// Provides context switching primitives for the scheduler on AMD64.
// The low-level register save/restore is done with inline assembly
// (or separate .asm files for complex operations).
//
// Context switch sequence:
// 1. Save callee-saved registers to old context
// 2. Switch stack pointer
// 3. Restore callee-saved registers from new context
// 4. Return (which now returns in new thread's context)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/amd64.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace amd64 {
namespace context {

// ================================================================
// Thread entry wrapper
//
// New threads don't have a valid return address, so we use this
// wrapper to call the actual thread function and handle cleanup
// when it returns.
// ================================================================

// Thread entry trampoline - called by new threads
extern "C" void thread_entry_trampoline();

// The actual thread function and argument are stored here
// during thread creation. The trampoline reads them and calls.
// (In a real implementation, these would be in the thread's context)
static void (*s_pending_entry)(void*) = nullptr;
static void* s_pending_arg = nullptr;

extern "C" void thread_entry_wrapper()
{
    // Get the entry point and argument
    void (*entry)(void*) = s_pending_entry;
    void* arg = s_pending_arg;
    
    // Clear the pending values
    s_pending_entry = nullptr;
    s_pending_arg = nullptr;
    
    // Enable interrupts for this thread
    enable_interrupts();
    
    // Call the thread function
    if (entry) {
        entry(arg);
    }
    
    // Thread has returned - should call scheduler to exit
    // For now, just halt
    // TODO: Call proper thread exit handler
    while (1) {
        halt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg)
{
    // Align stack to 16 bytes (AMD64 ABI requirement)
    stack_top &= ~0xFULL;
    
    // Reserve space for SwitchContext on the stack
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize all callee-saved registers to zero
    ctx->rbx = 0;
    ctx->rbp = 0;
    ctx->r12 = 0;
    ctx->r13 = 0;
    ctx->r14 = 0;
    ctx->r15 = 0;
    
    // Set up for first context switch
    // The "return address" is the thread entry wrapper
    ctx->rip = reinterpret_cast<uint64_t>(&thread_entry_wrapper);
    
    // Stack pointer points to the context (will be restored)
    ctx->rsp = stack_top;
    
    // Store entry point and argument for the wrapper to use
    // TODO: In a proper implementation, store these in the thread's
    // private data area, not static variables
    s_pending_entry = entry_point;
    s_pending_arg = arg;
    
    return ctx;
}

// ================================================================
// Context switch implementation
//
// This is the core context switch routine. It must:
// 1. Save all callee-saved registers to *old_ctx
// 2. Update *old_ctx to point to the saved context
// 3. Load all callee-saved registers from new_ctx
// 4. Return (which continues in the new thread)
//
// The assembly implementation is required for correctness.
// ================================================================

#if GXOS_MSVC_STUB

// MSVC stub - no actual context switch
extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
{
    (void)old_ctx;
    (void)new_ctx;
    // Cannot perform real context switch on MSVC host build
}

extern "C" void save_full_context(FullContext* ctx)
{
    (void)ctx;
}

extern "C" void restore_full_context(FullContext* ctx)
{
    (void)ctx;
    while (1) {}  // Never returns
}

#else

// ================================================================
// AMD64 Context Switch Assembly
//
// switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   RDI = old_ctx (pointer to pointer) - System V ABI
//   RSI = new_ctx (pointer to new context)
//
// This function saves the current context, stores its address in
// *old_ctx, then restores new_ctx and returns there.
// ================================================================

asm(
    ".global switch_context\n"
#if defined(__ELF__)
    ".type switch_context, @function\n"
#endif
    "switch_context:\n"
    
    // ---- Save current context ----
    // Allocate space on stack for SwitchContext (64 bytes)
    "    sub     $64, %rsp\n"
    
    // Save callee-saved registers
    "    mov     %rbx, 0(%rsp)\n"     // rbx
    "    mov     %rbp, 8(%rsp)\n"     // rbp
    "    mov     %r12, 16(%rsp)\n"    // r12
    "    mov     %r13, 24(%rsp)\n"    // r13
    "    mov     %r14, 32(%rsp)\n"    // r14
    "    mov     %r15, 40(%rsp)\n"    // r15
    
    // Save stack pointer (after adjustment)
    "    mov     %rsp, 48(%rsp)\n"    // rsp
    
    // Save return address (from caller's stack frame)
    // The return address is at the original RSP position
    "    mov     64(%rsp), %rax\n"    // Get return address
    "    mov     %rax, 56(%rsp)\n"    // Store as rip
    
    // Store context pointer to *old_ctx (RDI)
    "    mov     %rsp, (%rdi)\n"      // *old_ctx = current rsp (context)
    
    // ---- Restore new context ----
    // Load new stack pointer from new_ctx->rsp
    "    mov     48(%rsi), %rsp\n"    // rsp = new_ctx->rsp
    
    // Restore callee-saved registers
    "    mov     0(%rsp), %rbx\n"
    "    mov     8(%rsp), %rbp\n"
    "    mov     16(%rsp), %r12\n"
    "    mov     24(%rsp), %r13\n"
    "    mov     32(%rsp), %r14\n"
    "    mov     40(%rsp), %r15\n"
    
    // Get return address
    "    mov     56(%rsp), %rax\n"
    
    // Deallocate context frame
    "    add     $64, %rsp\n"
    
    // Jump to new thread's return address
    "    jmp     *%rax\n"
#if defined(__ELF__)
    ".size switch_context, .-switch_context\n"
#endif
);

// ================================================================
// Full Context Save/Restore (for exception handling)
//
// TODO: These need proper implementation with all GPRs and
// segment registers for interrupt/exception handling.
// ================================================================

asm(
    ".global save_full_context\n"
#if defined(__ELF__)
    ".type save_full_context, @function\n"
#endif
    "save_full_context:\n"
    // RDI = pointer to FullContext
    
    // Save all GPRs
    "    mov     %rax, 0(%rdi)\n"
    "    mov     %rbx, 8(%rdi)\n"
    "    mov     %rcx, 16(%rdi)\n"
    "    mov     %rdx, 24(%rdi)\n"
    "    mov     %rsi, 32(%rdi)\n"
    "    mov     %rdi, 40(%rdi)\n"    // Note: RDI is overwritten
    "    mov     %rbp, 48(%rdi)\n"
    "    mov     %rsp, 56(%rdi)\n"
    "    mov     %r8,  64(%rdi)\n"
    "    mov     %r9,  72(%rdi)\n"
    "    mov     %r10, 80(%rdi)\n"
    "    mov     %r11, 88(%rdi)\n"
    "    mov     %r12, 96(%rdi)\n"
    "    mov     %r13, 104(%rdi)\n"
    "    mov     %r14, 112(%rdi)\n"
    "    mov     %r15, 120(%rdi)\n"
    
    // Save RIP (return address)
    "    mov     (%rsp), %rax\n"
    "    mov     %rax, 128(%rdi)\n"
    
    // Save RFLAGS
    "    pushfq\n"
    "    pop     %rax\n"
    "    mov     %rax, 136(%rdi)\n"
    
    // Save segment registers
    "    mov     %cs, %ax\n"
    "    movzx   %ax, %rax\n"
    "    mov     %rax, 144(%rdi)\n"
    "    mov     %ss, %ax\n"
    "    movzx   %ax, %rax\n"
    "    mov     %rax, 152(%rdi)\n"
    "    mov     %ds, %ax\n"
    "    movzx   %ax, %rax\n"
    "    mov     %rax, 160(%rdi)\n"
    "    mov     %es, %ax\n"
    "    movzx   %ax, %rax\n"
    "    mov     %rax, 168(%rdi)\n"
    "    mov     %fs, %ax\n"
    "    movzx   %ax, %rax\n"
    "    mov     %rax, 176(%rdi)\n"
    "    mov     %gs, %ax\n"
    "    movzx   %ax, %rax\n"
    "    mov     %rax, 184(%rdi)\n"
    
    // TODO: Save FS_BASE and GS_BASE from MSRs
    
    "    ret\n"
#if defined(__ELF__)
    ".size save_full_context, .-save_full_context\n"
#endif
);

asm(
    ".global restore_full_context\n"
#if defined(__ELF__)
    ".type restore_full_context, @function\n"
#endif
    "restore_full_context:\n"
    // RDI = pointer to FullContext
    // This function does not return - it performs IRETQ
    
    // TODO: Restore segment registers
    // TODO: Set up IRETQ frame on stack
    
    // Restore RFLAGS
    "    mov     136(%rdi), %rax\n"
    "    push    %rax\n"
    "    popfq\n"
    
    // Restore GPRs (except RDI which we need, and RSP/RIP)
    "    mov     0(%rdi), %rax\n"
    "    mov     8(%rdi), %rbx\n"
    "    mov     16(%rdi), %rcx\n"
    "    mov     24(%rdi), %rdx\n"
    "    mov     32(%rdi), %rsi\n"
    "    mov     48(%rdi), %rbp\n"
    "    mov     64(%rdi), %r8\n"
    "    mov     72(%rdi), %r9\n"
    "    mov     80(%rdi), %r10\n"
    "    mov     88(%rdi), %r11\n"
    "    mov     96(%rdi), %r12\n"
    "    mov     104(%rdi), %r13\n"
    "    mov     112(%rdi), %r14\n"
    "    mov     120(%rdi), %r15\n"
    
    // Set up IRETQ frame: SS, RSP, RFLAGS, CS, RIP
    "    mov     56(%rdi), %rsp\n"     // Temporary RSP for IRETQ setup
    "    sub     $40, %rsp\n"          // Make room for IRETQ frame
    
    // TODO: Proper IRETQ frame setup
    // For now, just jump to RIP
    "    mov     128(%rdi), %rax\n"    // RIP
    "    mov     40(%rdi), %rdi\n"     // Restore RDI last
    "    jmp     *%rax\n"
#if defined(__ELF__)
    ".size restore_full_context, .-restore_full_context\n"
#endif
);

#endif // GXOS_MSVC_STUB

// ================================================================
// Scheduler hooks implementation
// ================================================================

void arch_thread_create(ArchThreadData* data, uint64_t stack,
                        void (*entry)(void*), void* arg)
{
    if (!data) return;
    
    // Initialize the context
    data->context = init_context(stack, entry, arg);
    data->kernel_sp = stack;
    data->fpu_state = nullptr;  // TODO: Allocate FXSAVE area
    data->fs_base = 0;
    data->gs_base = 0;
    data->cr3 = read_cr3();     // Inherit current address space
    data->iopb_offset = 0;
}

void arch_thread_destroy(ArchThreadData* data)
{
    if (!data) return;
    
    // Clean up architecture-specific resources
    // TODO: Free FPU state memory
    data->context = nullptr;
    data->kernel_sp = 0;
    data->fpu_state = nullptr;
}

void arch_switch_to(ArchThreadData* current, ArchThreadData* next)
{
    if (!current || !next || !next->context) {
        return;
    }
    
    // Switch address space if different
    if (current->cr3 != next->cr3 && next->cr3 != 0) {
        write_cr3(next->cr3);
    }
    
    // Switch FS base (user TLS)
    if (current->fs_base != next->fs_base) {
        set_fs_base(next->fs_base);
    }
    
    // TODO: Switch FPU state if either thread uses FPU
    // TODO: Update TSS.RSP0 for kernel stack on syscall/interrupt
    
    // Perform the context switch
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // Acknowledge LAPIC timer or PIT interrupt
    // TODO: Send EOI to LAPIC: write 0 to LAPIC EOI register
    
    // TODO: Call scheduler to check for preemption
    // scheduler::tick();
}

void arch_set_timer(uint64_t ticks)
{
    // Configure LAPIC timer for next interrupt
    // TODO: Write to LAPIC timer registers
    //   - Initial Count Register: ticks
    //   - Timer Mode: One-shot or periodic
    //   - Timer Vector: interrupt vector number
    (void)ticks;
}

} // namespace context
} // namespace amd64
} // namespace arch
} // namespace kernel
