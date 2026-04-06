//
// RISC-V 64-bit Context Switch Implementation
//
// Provides context switching primitives for the scheduler on RISC-V 64.
//
// RISC-V context switch is straightforward:
// - Save callee-saved registers (ra, sp, s0-s11)
// - Switch stack pointer
// - Restore callee-saved registers
// - Return via ret (jalr x0, ra)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/riscv64.h"
#include "include/arch/sbi_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace riscv64 {
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
    
    sbi_console::puts("[Thread] Entry returned, halting\n");
    while (1) {
        wait_for_interrupt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg)
{
    // RISC-V requires 16-byte stack alignment
    stack_top &= ~0xFULL;
    
    // Reserve space for SwitchContext
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize callee-saved registers
    ctx->s0 = 0;
    ctx->s1 = 0;
    ctx->s2 = 0;
    ctx->s3 = 0;
    ctx->s4 = 0;
    ctx->s5 = 0;
    ctx->s6 = 0;
    ctx->s7 = 0;
    ctx->s8 = 0;
    ctx->s9 = 0;
    ctx->s10 = 0;
    ctx->s11 = 0;
    
    // Return address - thread entry wrapper
    ctx->ra = reinterpret_cast<uint64_t>(&thread_entry_wrapper);
    
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
// RISC-V 64 Context Switch Assembly
//
// switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   a0 = old_ctx (pointer to pointer)
//   a1 = new_ctx (pointer to new context)
// ================================================================

asm(
    ".global switch_context\n"
    ".type switch_context, @function\n"
    "switch_context:\n"
    
    // ---- Save current context ----
    // Allocate space on stack for SwitchContext (14 registers * 8 bytes = 112)
    "    addi    sp, sp, -112\n"
    
    // Save callee-saved registers
    "    sd      ra,  0(sp)\n"
    "    sd      sp,  8(sp)\n"       // Will be updated
    "    sd      s0,  16(sp)\n"
    "    sd      s1,  24(sp)\n"
    "    sd      s2,  32(sp)\n"
    "    sd      s3,  40(sp)\n"
    "    sd      s4,  48(sp)\n"
    "    sd      s5,  56(sp)\n"
    "    sd      s6,  64(sp)\n"
    "    sd      s7,  72(sp)\n"
    "    sd      s8,  80(sp)\n"
    "    sd      s9,  88(sp)\n"
    "    sd      s10, 96(sp)\n"
    "    sd      s11, 104(sp)\n"
    
    // Store context pointer to *old_ctx
    "    sd      sp, (a0)\n"
    
    // ---- Restore new context ----
    // Load new stack pointer
    "    mv      sp, a1\n"
    
    // Restore callee-saved registers
    "    ld      ra,  0(sp)\n"
    // Skip sp (we just set it)
    "    ld      s0,  16(sp)\n"
    "    ld      s1,  24(sp)\n"
    "    ld      s2,  32(sp)\n"
    "    ld      s3,  40(sp)\n"
    "    ld      s4,  48(sp)\n"
    "    ld      s5,  56(sp)\n"
    "    ld      s6,  64(sp)\n"
    "    ld      s7,  72(sp)\n"
    "    ld      s8,  80(sp)\n"
    "    ld      s9,  88(sp)\n"
    "    ld      s10, 96(sp)\n"
    "    ld      s11, 104(sp)\n"
    
    // Deallocate context frame
    "    addi    sp, sp, 112\n"
    
    // Return to new thread
    "    ret\n"
    
    ".size switch_context, .-switch_context\n"
);

// ================================================================
// Full Context Save/Restore
// ================================================================

asm(
    ".global save_full_context\n"
    ".type save_full_context, @function\n"
    "save_full_context:\n"
    // a0 = pointer to FullContext
    
    // Save all GPRs (x1-x31)
    "    sd      ra,  0(a0)\n"
    "    sd      sp,  8(a0)\n"
    "    sd      gp,  16(a0)\n"
    "    sd      tp,  24(a0)\n"
    "    sd      t0,  32(a0)\n"
    "    sd      t1,  40(a0)\n"
    "    sd      t2,  48(a0)\n"
    "    sd      s0,  56(a0)\n"
    "    sd      s1,  64(a0)\n"
    "    sd      a0,  72(a0)\n"      // Note: saving current a0
    "    sd      a1,  80(a0)\n"
    "    sd      a2,  88(a0)\n"
    "    sd      a3,  96(a0)\n"
    "    sd      a4,  104(a0)\n"
    "    sd      a5,  112(a0)\n"
    "    sd      a6,  120(a0)\n"
    "    sd      a7,  128(a0)\n"
    "    sd      s2,  136(a0)\n"
    "    sd      s3,  144(a0)\n"
    "    sd      s4,  152(a0)\n"
    "    sd      s5,  160(a0)\n"
    "    sd      s6,  168(a0)\n"
    "    sd      s7,  176(a0)\n"
    "    sd      s8,  184(a0)\n"
    "    sd      s9,  192(a0)\n"
    "    sd      s10, 200(a0)\n"
    "    sd      s11, 208(a0)\n"
    "    sd      t3,  216(a0)\n"
    "    sd      t4,  224(a0)\n"
    "    sd      t5,  232(a0)\n"
    "    sd      t6,  240(a0)\n"
    
    // Save CSRs
    "    csrr    t0, sepc\n"
    "    sd      t0, 248(a0)\n"
    "    csrr    t0, sstatus\n"
    "    sd      t0, 256(a0)\n"
    "    csrr    t0, scause\n"
    "    sd      t0, 264(a0)\n"
    "    csrr    t0, stval\n"
    "    sd      t0, 272(a0)\n"
    
    "    ret\n"
    ".size save_full_context, .-save_full_context\n"
);

asm(
    ".global restore_full_context\n"
    ".type restore_full_context, @function\n"
    "restore_full_context:\n"
    // a0 = pointer to FullContext
    // This function does not return - it uses sret
    
    // Restore CSRs first
    "    ld      t0, 248(a0)\n"
    "    csrw    sepc, t0\n"
    "    ld      t0, 256(a0)\n"
    "    csrw    sstatus, t0\n"
    
    // Restore GPRs (in reverse order to free a0 last)
    "    ld      t6,  240(a0)\n"
    "    ld      t5,  232(a0)\n"
    "    ld      t4,  224(a0)\n"
    "    ld      t3,  216(a0)\n"
    "    ld      s11, 208(a0)\n"
    "    ld      s10, 200(a0)\n"
    "    ld      s9,  192(a0)\n"
    "    ld      s8,  184(a0)\n"
    "    ld      s7,  176(a0)\n"
    "    ld      s6,  168(a0)\n"
    "    ld      s5,  160(a0)\n"
    "    ld      s4,  152(a0)\n"
    "    ld      s3,  144(a0)\n"
    "    ld      s2,  136(a0)\n"
    "    ld      a7,  128(a0)\n"
    "    ld      a6,  120(a0)\n"
    "    ld      a5,  112(a0)\n"
    "    ld      a4,  104(a0)\n"
    "    ld      a3,  96(a0)\n"
    "    ld      a2,  88(a0)\n"
    "    ld      a1,  80(a0)\n"
    "    ld      s1,  64(a0)\n"
    "    ld      s0,  56(a0)\n"
    "    ld      t2,  48(a0)\n"
    "    ld      t1,  40(a0)\n"
    "    ld      t0,  32(a0)\n"
    "    ld      tp,  24(a0)\n"
    "    ld      gp,  16(a0)\n"
    "    ld      sp,  8(a0)\n"
    "    ld      ra,  0(a0)\n"
    "    ld      a0,  72(a0)\n"      // Restore a0 last
    
    // Return from supervisor exception
    "    sret\n"
    ".size restore_full_context, .-restore_full_context\n"
);

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
    data->satp = read_satp();
    data->hartid = 0;  // TODO: Get actual hart ID
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
    if (current->satp != next->satp && next->satp != 0) {
        write_satp(next->satp);
        sfence_vma();
    }
    
    // Switch thread pointer
    if (current->tp != next->tp) {
        set_tp(next->tp);
    }
    
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // Clear timer interrupt via SBI
    // TODO: Call SBI to set next timer
    
    // TODO: Call scheduler
}

void arch_set_timer(uint64_t ticks)
{
    // Set timer via SBI
    // sbi_set_timer(read_time() + ticks);
    (void)ticks;
}

} // namespace context
} // namespace riscv64
} // namespace arch
} // namespace kernel
