//
// ARM (32-bit) Context Switch Implementation
//
// Provides context switching primitives for the scheduler on ARM.
//
// ARM context switch uses STMFD/LDMFD (push/pop multiple) for
// efficient register save/restore.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/arm.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm {
namespace context {

// ================================================================
// Thread entry wrapper
// ================================================================

static void (*s_pending_entry)(void*) = nullptr;
static void* s_pending_arg = nullptr;

extern "C" void thread_entry_wrapper()
{
    void (*entry)(void*) = s_pending_entry;
    void* arg = s_pending_arg;
    
    s_pending_entry = nullptr;
    s_pending_arg = nullptr;
    
    // Enable interrupts (CPSR.I = 0)
    enable_interrupts();
    
    if (entry) {
        entry(arg);
    }
    
    // Thread returned - halt
    // TODO: Call proper thread exit
    while (1) {
        wait_for_interrupt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint32_t stack_top, void (*entry_point)(void*), void* arg)
{
    // Align stack to 8 bytes (ARM AAPCS requirement)
    stack_top &= ~0x7U;
    
    // Reserve space for SwitchContext
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize callee-saved registers to zero
    ctx->r4 = 0;
    ctx->r5 = 0;
    ctx->r6 = 0;
    ctx->r7 = 0;
    ctx->r8 = 0;
    ctx->r9 = 0;
    ctx->r10 = 0;
    ctx->r11 = 0;  // Frame pointer
    
    // Set up return address to thread entry wrapper
    ctx->lr = reinterpret_cast<uint32_t>(&thread_entry_wrapper);
    
    // Stack pointer
    ctx->sp = stack_top;
    
    // Store entry point and argument
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
// ARM Context Switch Assembly
//
// switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   R0 = old_ctx (pointer to pointer)
//   R1 = new_ctx (pointer to new context)
//
// ARM calling convention: R0-R3 are arguments, R4-R11 are callee-saved
// ================================================================

asm(
    ".global switch_context\n"
#if defined(__ELF__)
    ".type switch_context, %function\n"
#endif
    "switch_context:\n"
    
    // ---- Save current context ----
    // Push callee-saved registers and LR onto stack
    // This creates a SwitchContext structure on the stack
    "    push    {r4-r11, lr}\n"      // Save R4-R11, LR (9 registers = 36 bytes)
    
    // Save SP to the context (SP is already pointing to saved regs)
    // We need to store the current SP value
    "    mov     r2, sp\n"
    "    push    {r2}\n"               // Push SP value (now 40 bytes)
    
    // Store context pointer to *old_ctx
    "    str     sp, [r0]\n"           // *old_ctx = current sp
    
    // ---- Restore new context ----
    // Load new stack pointer from new_ctx
    "    mov     sp, r1\n"             // sp = new_ctx
    
    // Pop SP value (we'll ignore it, we already set SP)
    "    add     sp, sp, #4\n"
    
    // Pop callee-saved registers and PC (from LR position)
    "    pop     {r4-r11, pc}\n"       // Restore R4-R11, branch to LR
#if defined(__ELF__)
    ".size switch_context, .-switch_context\n"
#endif
);

// ================================================================
// Full Context Save/Restore (for exception handling)
//
// TODO: Implement proper ARM exception context save/restore
// This requires handling different ARM modes (SVC, IRQ, etc.)
// ================================================================

asm(
    ".global save_full_context\n"
#if defined(__ELF__)
    ".type save_full_context, %function\n"
#endif
    "save_full_context:\n"
    // R0 = pointer to FullContext
    
    // Save R0-R12
    "    stmia   r0!, {r0-r12}\n"      // Save R0-R12, increment R0
    
    // Save SP, LR, PC
    "    mov     r1, sp\n"
    "    str     r1, [r0], #4\n"       // Save SP
    "    mov     r1, lr\n"
    "    str     r1, [r0], #4\n"       // Save LR
    "    str     r1, [r0], #4\n"       // Save PC (use LR as approximation)
    
    // Save CPSR
    "    mrs     r1, cpsr\n"
    "    str     r1, [r0], #4\n"
    
    // SPSR is only valid in exception modes
    // For now, just store CPSR again
    "    str     r1, [r0]\n"
    
    "    bx      lr\n"
#if defined(__ELF__)
    ".size save_full_context, .-save_full_context\n"
#endif
);

asm(
    ".global restore_full_context\n"
#if defined(__ELF__)
    ".type restore_full_context, %function\n"
#endif
    "restore_full_context:\n"
    // R0 = pointer to FullContext
    // This function does not return
    
    // Restore CPSR first (to SPSR for exception return)
    "    ldr     r1, [r0, #68]\n"      // Load CPSR
    "    msr     spsr_cxsf, r1\n"      // Store to SPSR
    
    // Restore R0-R12
    "    ldmia   r0, {r0-r12}\n"
    
    // TODO: Proper exception return with SPSR restoration
    // For now, just branch to saved LR
    // This needs proper implementation based on exception mode
    
    "    mov     pc, lr\n"             // Return (simplified)
#if defined(__ELF__)
    ".size restore_full_context, .-restore_full_context\n"
#endif
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
    data->tls_base = 0;
    data->ttbr0 = read_ttbr0();
    data->context_id = 0;
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
    
    // Switch page table if different
    if (current->ttbr0 != next->ttbr0 && next->ttbr0 != 0) {
        write_ttbr0(next->ttbr0);
        invalidate_tlb();
    }
    
    // Switch TLS base
    if (current->tls_base != next->tls_base) {
        set_tls_base(next->tls_base);
    }
    
    // Perform context switch
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // TODO: Clear timer interrupt (platform-specific)
    // On Versatile/RealView, write to timer clear register
    
    // TODO: Call scheduler
}

void arch_set_timer(uint32_t ticks)
{
    // TODO: Configure SP804 timer or ARM Generic Timer
    // Platform-specific timer setup
    (void)ticks;
}

} // namespace context
} // namespace arm
} // namespace arch
} // namespace kernel
