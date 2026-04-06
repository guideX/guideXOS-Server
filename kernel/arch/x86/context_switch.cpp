//
// x86 (32-bit) Context Switch Implementation
//
// Provides context switching primitives for the scheduler on x86.
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
#include "include/arch/x86.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace x86 {
namespace context {

// ================================================================
// Thread entry wrapper
//
// New threads don't have a valid return address, so we use this
// wrapper to call the actual thread function and handle cleanup
// when it returns.
// ================================================================

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

SwitchContext* init_context(uint32_t stack_top, void (*entry_point)(void*), void* arg)
{
    // Align stack to 16 bytes (for SSE compatibility)
    stack_top &= ~0xFU;
    
    // Reserve space for SwitchContext on the stack
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize all callee-saved registers to zero
    ctx->ebx = 0;
    ctx->esi = 0;
    ctx->edi = 0;
    ctx->ebp = 0;
    
    // Set up for first context switch
    // The "return address" is the thread entry wrapper
    ctx->eip = reinterpret_cast<uint32_t>(&thread_entry_wrapper);
    
    // Stack pointer points to the context
    ctx->esp = stack_top;
    
    // Store entry point and argument for the wrapper to use
    // TODO: Store in thread's private data, not static variables
    s_pending_entry = entry_point;
    s_pending_arg = arg;
    
    return ctx;
}

// ================================================================
// Context switch implementation
// ================================================================

#if GXOS_MSVC_STUB

extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
{
    (void)old_ctx;
    (void)new_ctx;
}

extern "C" void save_full_context(FullContext* ctx)
{
    (void)ctx;
}

extern "C" void restore_full_context(FullContext* ctx)
{
    (void)ctx;
    while (1) {}
}

#else

// ================================================================
// x86 Context Switch Assembly (cdecl calling convention)
//
// switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   [esp+4] = old_ctx (pointer to pointer)
//   [esp+8] = new_ctx (pointer to new context)
// ================================================================

asm(
    ".global switch_context\n"
    ".type switch_context, @function\n"
    "switch_context:\n"
    
    // Get parameters from stack
    "    mov     4(%esp), %eax\n"     // eax = old_ctx
    "    mov     8(%esp), %edx\n"     // edx = new_ctx
    
    // ---- Save current context ----
    // Allocate space on stack for SwitchContext (24 bytes)
    "    sub     $24, %esp\n"
    
    // Save callee-saved registers
    "    mov     %ebx, 0(%esp)\n"     // ebx
    "    mov     %esi, 4(%esp)\n"     // esi
    "    mov     %edi, 8(%esp)\n"     // edi
    "    mov     %ebp, 12(%esp)\n"    // ebp
    
    // Save stack pointer
    "    mov     %esp, 16(%esp)\n"    // esp
    
    // Save return address
    "    mov     24(%esp), %ecx\n"    // Get return address
    "    mov     %ecx, 20(%esp)\n"    // Store as eip
    
    // Store context pointer to *old_ctx
    "    mov     %esp, (%eax)\n"      // *old_ctx = esp
    
    // ---- Restore new context ----
    // Load new stack pointer from new_ctx->esp
    "    mov     16(%edx), %esp\n"
    
    // Restore callee-saved registers
    "    mov     0(%esp), %ebx\n"
    "    mov     4(%esp), %esi\n"
    "    mov     8(%esp), %edi\n"
    "    mov     12(%esp), %ebp\n"
    
    // Get return address
    "    mov     20(%esp), %ecx\n"
    
    // Deallocate context frame
    "    add     $24, %esp\n"
    
    // Jump to new thread's return address
    "    jmp     *%ecx\n"
    
    ".size switch_context, .-switch_context\n"
);

// ================================================================
// Full Context Save/Restore (for exception handling)
// ================================================================

asm(
    ".global save_full_context\n"
    ".type save_full_context, @function\n"
    "save_full_context:\n"
    // [esp+4] = pointer to FullContext
    "    mov     4(%esp), %ecx\n"
    
    // Save all GPRs
    "    mov     %eax, 0(%ecx)\n"
    "    mov     %ebx, 4(%ecx)\n"
    // ecx is already in use, save it from the copy we made
    "    mov     4(%esp), %eax\n"
    "    mov     %eax, 8(%ecx)\n"
    "    mov     %edx, 12(%ecx)\n"
    "    mov     %esi, 16(%ecx)\n"
    "    mov     %edi, 20(%ecx)\n"
    "    mov     %ebp, 24(%ecx)\n"
    "    mov     %esp, 28(%ecx)\n"
    
    // Save EIP (return address)
    "    mov     (%esp), %eax\n"
    "    mov     %eax, 32(%ecx)\n"
    
    // Save EFLAGS
    "    pushfl\n"
    "    pop     %eax\n"
    "    mov     %eax, 36(%ecx)\n"
    
    // Save segment registers
    "    mov     %cs, %ax\n"
    "    movzx   %ax, %eax\n"
    "    mov     %eax, 40(%ecx)\n"
    "    mov     %ss, %ax\n"
    "    movzx   %ax, %eax\n"
    "    mov     %eax, 44(%ecx)\n"
    "    mov     %ds, %ax\n"
    "    movzx   %ax, %eax\n"
    "    mov     %eax, 48(%ecx)\n"
    "    mov     %es, %ax\n"
    "    movzx   %ax, %eax\n"
    "    mov     %eax, 52(%ecx)\n"
    "    mov     %fs, %ax\n"
    "    movzx   %ax, %eax\n"
    "    mov     %eax, 56(%ecx)\n"
    "    mov     %gs, %ax\n"
    "    movzx   %ax, %eax\n"
    "    mov     %eax, 60(%ecx)\n"
    
    "    ret\n"
    ".size save_full_context, .-save_full_context\n"
);

asm(
    ".global restore_full_context\n"
    ".type restore_full_context, @function\n"
    "restore_full_context:\n"
    // [esp+4] = pointer to FullContext
    // This function does not return - it performs IRET
    
    "    mov     4(%esp), %ecx\n"
    
    // Restore EFLAGS
    "    mov     36(%ecx), %eax\n"
    "    push    %eax\n"
    "    popfl\n"
    
    // Restore GPRs (except ESP and ECX which we need)
    "    mov     0(%ecx), %eax\n"
    "    mov     4(%ecx), %ebx\n"
    "    mov     12(%ecx), %edx\n"
    "    mov     16(%ecx), %esi\n"
    "    mov     20(%ecx), %edi\n"
    "    mov     24(%ecx), %ebp\n"
    
    // Get return address and set up simple return
    "    mov     28(%ecx), %esp\n"
    "    mov     32(%ecx), %ecx\n"
    
    // Jump to saved EIP
    // TODO: Proper IRET with segment restoration
    "    jmp     *%ecx\n"
    
    ".size restore_full_context, .-restore_full_context\n"
);

#endif // GXOS_MSVC_STUB

// ================================================================
// Scheduler hooks implementation
// ================================================================

void arch_thread_create(ArchThreadData* data, uint32_t stack,
                        void (*entry)(void*), void* arg)
{
    if (!data) return;
    
    data->context = init_context(stack, entry, arg);
    data->kernel_sp = stack;
    data->fpu_state = nullptr;
    data->fs_base = 0;
    data->gs_base = 0;
    data->cr3 = read_cr3();
    data->iopb_offset = 0;
}

void arch_thread_destroy(ArchThreadData* data)
{
    if (!data) return;
    
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
    
    // Perform the context switch
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // TODO: Call scheduler
}

void arch_set_timer(uint32_t ticks)
{
    // TODO: Configure PIT or LAPIC timer
    (void)ticks;
}

} // namespace context
} // namespace x86
} // namespace arch
} // namespace kernel
