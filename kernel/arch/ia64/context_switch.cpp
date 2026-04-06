//
// IA-64 (Itanium) Context Switch Implementation
//
// Provides context switching primitives for the scheduler on IA-64.
//
// IA-64 context switch is more complex than other architectures due to:
// - Register Stack Engine (RSE) managing stacked registers
// - Large number of preserved registers
// - Predicate registers affecting control flow
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/ia64.h"
#include "include/arch/ski_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace ia64 {
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
    ski_console::puts("[Thread] Entry returned, halting\n");
    while (1) {
        halt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg)
{
    // IA-64 requires 16-byte stack alignment
    stack_top &= ~0xFULL;
    
    // Reserve space for SwitchContext
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize preserved registers
    ctx->gr4 = 0;
    ctx->gr5 = 0;
    ctx->gr6 = 0;
    ctx->gr7 = 0;
    
    // Stack pointer
    ctx->sp = stack_top;
    
    // Global pointer - needs to be set from kernel's GP
    ctx->gp = 0;  // TODO: Get kernel GP
    
    // Return pointer - thread entry wrapper
    ctx->rp = reinterpret_cast<uint64_t>(&thread_entry_wrapper);
    
    // Previous function state (empty frame)
    ctx->pfs = 0;
    
    // RSE state - needs proper initialization
    ctx->bsp = 0;
    ctx->bspstore = 0;
    ctx->rnat = 0;
    
    // Predicate registers
    ctx->pr = 0;
    
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
// IA-64 Context Switch Assembly
//
// switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   r32 = old_ctx (in0)
//   r33 = new_ctx (in1)
//
// IA-64 uses a register-based calling convention with stacked registers.
// TODO: This needs proper IA-64 assembly syntax and RSE handling
// ================================================================

// Placeholder - needs proper IA-64 assembly implementation
extern "C" void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
{
    // TODO: Implement IA-64 context switch
    // Key steps:
    // 1. Flush RSE (flushrs instruction)
    // 2. Save preserved GRs (r4-r7)
    // 3. Save branch registers
    // 4. Save AR.PFS, AR.LC, AR.EC
    // 5. Save RSE state (AR.BSP, AR.BSPSTORE, AR.RNAT)
    // 6. Save predicate registers
    // 7. Store context pointer to *old_ctx
    // 8. Load new context
    // 9. Restore RSE state
    // 10. Restore registers
    // 11. Return via br.ret
    
    (void)old_ctx;
    (void)new_ctx;
}

extern "C" void save_full_context(FullContext* ctx)
{
    // TODO: Save all IA-64 context for exception handling
    (void)ctx;
}

extern "C" void restore_full_context(FullContext* ctx)
{
    // TODO: Restore IA-64 context and return from interruption
    // Uses rfi (return from interruption) instruction
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
    data->tp = 0;
    data->region_id = 0;
    data->pt_base = 0;
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
    
    // Switch page table if needed
    // TODO: Update VHPT and region registers
    
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // TODO: Clear ITC match interrupt
    // TODO: Call scheduler
}

void arch_set_timer(uint64_t ticks)
{
    // TODO: Set ITC match register for next interrupt
    // ITM = ITC + ticks
    (void)ticks;
}

} // namespace context
} // namespace ia64
} // namespace arch
} // namespace kernel
