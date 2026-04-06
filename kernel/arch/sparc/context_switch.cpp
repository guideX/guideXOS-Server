//
// SPARC v8 (32-bit) Context Switch Implementation
//
// SPARC context switching is complex due to register windows.
// The SAVE and RESTORE instructions manage window switching,
// and flush_windows must be called before context switch to
// ensure all windows are written to the stack.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/sparc.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace sparc {
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
    
    enable_interrupts();
    
    if (entry) {
        entry(arg);
    }
    
    // Thread returned - halt
    // TODO: Call proper thread exit
    while (1) {
        halt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint32_t stack_top, void (*entry_point)(void*), void* arg)
{
    // SPARC requires 8-byte stack alignment
    stack_top &= ~0x7U;
    
    // Reserve space for SwitchContext
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize registers
    ctx->sp = stack_top;
    ctx->fp = 0;
    ctx->i7 = 0;
    
    // PC for return - thread entry wrapper
    ctx->pc = reinterpret_cast<uint32_t>(&thread_entry_wrapper);
    
    // PSR - supervisor mode, interrupts enabled (after entry)
    ctx->psr = 0;  // Will be set properly by switch_context
    
    // Clear local and in registers
    ctx->l0 = ctx->l1 = ctx->l2 = ctx->l3 = 0;
    ctx->l4 = ctx->l5 = ctx->l6 = ctx->l7 = 0;
    ctx->i0 = ctx->i1 = ctx->i2 = ctx->i3 = 0;
    ctx->i4 = ctx->i5 = 0;
    
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
// SPARC Context Switch
//
// switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   %o0 = old_ctx
//   %o1 = new_ctx
//
// SPARC context switch must:
// 1. Flush all register windows to stack (ta 0x03 or flush_windows)
// 2. Save current window's local/in registers
// 3. Save SP, FP, return address
// 4. Store context pointer to *old_ctx
// 5. Load new context
// 6. Restore registers
// 7. Return to new thread
//
// TODO: This needs proper SPARC assembly implementation
// ================================================================

extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
{
    // Placeholder - needs proper SPARC assembly
    // Key instruction: ta 0x03 (trap to flush windows)
    
    flush_windows();
    
    // TODO: Implement actual context switch
    (void)old_ctx;
    (void)new_ctx;
}

extern "C" void save_full_context(FullContext* ctx)
{
    // TODO: Save all registers for exception handling
    (void)ctx;
}

extern "C" void restore_full_context(FullContext* ctx)
{
    // TODO: Restore context and rett (return from trap)
    (void)ctx;
    while (1) {}
}

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
    data->context_id = 0;
    data->ctx_table = 0;
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
    
    // Switch MMU context if different
    // TODO: Update context table register
    
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // SPARC uses SLAVIO timer on Sun4m
    // TODO: Clear timer interrupt via SLAVIO
    slavio_eoi(10);  // Timer is typically IRQ 10
    
    // TODO: Call scheduler
}

void arch_set_timer(uint32_t ticks)
{
    // TODO: Program SLAVIO timer
    (void)ticks;
}

} // namespace context
} // namespace sparc
} // namespace arch
} // namespace kernel
