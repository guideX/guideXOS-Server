//
// SPARC v9 (64-bit) Context Switch Implementation
//
// SPARC v9 context switching uses flushw instruction to flush
// all register windows instead of trap-based flushing.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/sparc64.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace sparc64 {
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

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg)
{
    // SPARC64 requires 16-byte stack alignment
    stack_top &= ~0xFULL;
    
    // SPARC64 also requires a register window save area (BIAS)
    // The stack pointer points 2047 bytes above the actual top
    // This is called the "stack bias"
    stack_top -= 2048;  // Bias adjustment
    
    // Reserve space for SwitchContext
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize registers
    ctx->sp = stack_top + 2047;  // Add bias back for actual SP
    ctx->fp = 0;
    ctx->i7 = 0;
    
    // PC for return - thread entry wrapper
    ctx->pc = reinterpret_cast<uint64_t>(&thread_entry_wrapper);
    
    // PSTATE - supervisor mode
    ctx->pstate = 0;
    ctx->cwp = 0;
    
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
// SPARC v9 Context Switch
//
// Uses flushw instruction to flush all register windows.
//
// TODO: Implement proper SPARC v9 assembly
// ================================================================

extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
{
    // Flush all register windows
    flush_windows();  // Uses flushw instruction in v9
    
    // TODO: Implement actual context switch assembly
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
    // TODO: Restore context and retry (return from trap)
    (void)ctx;
    while (1) {}
}

#endif // GXOS_MSVC_STUB

// ================================================================
// Scheduler hooks implementation
// ================================================================

void arch_thread_create(ArchThreadData* data, uint64_t stack,
                        void (*entry)(void*), void* arg)
{
    if (!data) return;
    
    data->context = init_context(stack, entry, arg);
    data->kernel_sp = stack;
    data->fpu_state = nullptr;
    data->context_id = 0;
    data->ctx_primary = 0;
    data->ctx_secondary = 0;
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
    // TODO: Update MMU context registers
    
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // Clear TICK compare interrupt
    // TODO: Clear tick interrupt
    
    // TODO: Call scheduler
}

void arch_set_timer(uint64_t ticks)
{
    // Set TICK_CMPR for next interrupt
    // TODO: Write to TICK_CMPR register
    (void)ticks;
}

} // namespace context
} // namespace sparc64
} // namespace arch
} // namespace kernel
