//
// LoongArch 64-bit Context Switch Implementation
//
// Provides context switching primitives for the scheduler.
// The low-level register save/restore is done with inline assembly
// (or separate .s files for complex operations).
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
#include "include/arch/loongarch64.h"
#include "include/arch/loongarch_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace loongarch64 {
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
    loongarch_console::puts("[Thread] Entry function returned, halting\n");
    while (1) {
        wait_for_interrupt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg)
{
    // Align stack to 16 bytes (LoongArch ABI requirement)
    stack_top &= ~0xFULL;
    
    // Reserve space for SwitchContext on the stack
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize all callee-saved registers to zero
    ctx->fp = 0;
    ctx->s0 = 0;
    ctx->s1 = 0;
    ctx->s2 = 0;
    ctx->s3 = 0;
    ctx->s4 = 0;
    ctx->s5 = 0;
    ctx->s6 = 0;
    ctx->s7 = 0;
    ctx->s8 = 0;
    
    // Set up for first context switch
    // The "return address" is the thread entry wrapper
    ctx->ra = reinterpret_cast<uint64_t>(&thread_entry_wrapper);
    
    // Stack pointer points just above the context
    ctx->sp = stack_top;
    
    // Thread pointer (TLS) - set to zero initially
    ctx->tp = 0;
    
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
    // Cannot perform real context switch on MSVC
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
// LoongArch Context Switch Assembly
//
// switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   $a0 = old_ctx (pointer to pointer)
//   $a1 = new_ctx (pointer to new context)
//
// This function saves the current context, stores its address in
// *old_ctx, then restores new_ctx and returns there.
// ================================================================

asm(
    ".global switch_context\n"
    ".type switch_context, @function\n"
    "switch_context:\n"
    
    // ---- Save current context ----
    // Allocate space on stack for SwitchContext
    "    addi.d  $sp, $sp, -104\n"    // 13 registers * 8 bytes = 104
    
    // Save callee-saved registers
    "    st.d    $ra,  $sp, 0\n"      // Return address
    "    st.d    $sp,  $sp, 8\n"      // Stack pointer (will be updated)
    "    st.d    $tp,  $sp, 16\n"     // Thread pointer
    "    st.d    $fp,  $sp, 24\n"     // Frame pointer / s9
    "    st.d    $s0,  $sp, 32\n"     // $r23
    "    st.d    $s1,  $sp, 40\n"     // $r24
    "    st.d    $s2,  $sp, 48\n"     // $r25
    "    st.d    $s3,  $sp, 56\n"     // $r26
    "    st.d    $s4,  $sp, 64\n"     // $r27
    "    st.d    $s5,  $sp, 72\n"     // $r28
    "    st.d    $s6,  $sp, 80\n"     // $r29
    "    st.d    $s7,  $sp, 88\n"     // $r30
    "    st.d    $s8,  $sp, 96\n"     // $r31
    
    // Store context pointer to *old_ctx
    "    st.d    $sp, $a0, 0\n"       // *old_ctx = current sp (context)
    
    // ---- Restore new context ----
    // Load new stack pointer
    "    move    $sp, $a1\n"          // sp = new_ctx
    
    // Restore callee-saved registers
    "    ld.d    $ra,  $sp, 0\n"
    // Skip loading sp (we already set it)
    "    ld.d    $tp,  $sp, 16\n"
    "    ld.d    $fp,  $sp, 24\n"
    "    ld.d    $s0,  $sp, 32\n"
    "    ld.d    $s1,  $sp, 40\n"
    "    ld.d    $s2,  $sp, 48\n"
    "    ld.d    $s3,  $sp, 56\n"
    "    ld.d    $s4,  $sp, 64\n"
    "    ld.d    $s5,  $sp, 72\n"
    "    ld.d    $s6,  $sp, 80\n"
    "    ld.d    $s7,  $sp, 88\n"
    "    ld.d    $s8,  $sp, 96\n"
    
    // Deallocate context frame
    "    addi.d  $sp, $sp, 104\n"
    
    // Return to new thread
    "    jr      $ra\n"
    
    ".size switch_context, .-switch_context\n"
);

// ================================================================
// Full Context Save/Restore (for exception handling)
// ================================================================

asm(
    ".global save_full_context\n"
    ".type save_full_context, @function\n"
    "save_full_context:\n"
    // $a0 = pointer to FullContext
    
    // Save all GPRs (except $r0 which is always zero)
    "    st.d    $ra,   $a0, 0\n"     // ra ($r1)
    "    st.d    $tp,   $a0, 8\n"     // tp ($r2)
    "    st.d    $sp,   $a0, 16\n"    // sp ($r3)
    "    st.d    $a0,   $a0, 24\n"    // a0 ($r4)
    "    st.d    $a1,   $a0, 32\n"    // a1 ($r5)
    "    st.d    $a2,   $a0, 40\n"    // a2 ($r6)
    "    st.d    $a3,   $a0, 48\n"    // a3 ($r7)
    "    st.d    $a4,   $a0, 56\n"    // a4 ($r8)
    "    st.d    $a5,   $a0, 64\n"    // a5 ($r9)
    "    st.d    $a6,   $a0, 72\n"    // a6 ($r10)
    "    st.d    $a7,   $a0, 80\n"    // a7 ($r11)
    "    st.d    $t0,   $a0, 88\n"    // t0 ($r12)
    "    st.d    $t1,   $a0, 96\n"    // t1 ($r13)
    "    st.d    $t2,   $a0, 104\n"   // t2 ($r14)
    "    st.d    $t3,   $a0, 112\n"   // t3 ($r15)
    "    st.d    $t4,   $a0, 120\n"   // t4 ($r16)
    "    st.d    $t5,   $a0, 128\n"   // t5 ($r17)
    "    st.d    $t6,   $a0, 136\n"   // t6 ($r18)
    "    st.d    $t7,   $a0, 144\n"   // t7 ($r19)
    "    st.d    $t8,   $a0, 152\n"   // t8 ($r20)
    "    st.d    $r21,  $a0, 160\n"   // $r21 (reserved)
    "    st.d    $fp,   $a0, 168\n"   // fp ($r22)
    "    st.d    $s0,   $a0, 176\n"   // s0 ($r23)
    "    st.d    $s1,   $a0, 184\n"   // s1 ($r24)
    "    st.d    $s2,   $a0, 192\n"   // s2 ($r25)
    "    st.d    $s3,   $a0, 200\n"   // s3 ($r26)
    "    st.d    $s4,   $a0, 208\n"   // s4 ($r27)
    "    st.d    $s5,   $a0, 216\n"   // s5 ($r28)
    "    st.d    $s6,   $a0, 224\n"   // s6 ($r29)
    "    st.d    $s7,   $a0, 232\n"   // s7 ($r30)
    "    st.d    $s8,   $a0, 240\n"   // s8 ($r31)
    
    // Save CSRs
    "    csrrd   $t0, 0x06\n"         // ERA
    "    st.d    $t0, $a0, 248\n"
    "    csrrd   $t0, 0x01\n"         // PRMD
    "    st.d    $t0, $a0, 256\n"
    "    csrrd   $t0, 0x00\n"         // CRMD
    "    st.d    $t0, $a0, 264\n"
    "    csrrd   $t0, 0x05\n"         // ESTAT
    "    st.d    $t0, $a0, 272\n"
    "    csrrd   $t0, 0x07\n"         // BADV
    "    st.d    $t0, $a0, 280\n"
    
    "    jr      $ra\n"
    ".size save_full_context, .-save_full_context\n"
);

asm(
    ".global restore_full_context\n"
    ".type restore_full_context, @function\n"
    "restore_full_context:\n"
    // $a0 = pointer to FullContext
    // This function does not return - it jumps to ERA
    
    // Restore CSRs first
    "    ld.d    $t0, $a0, 248\n"     // ERA
    "    csrwr   $t0, 0x06\n"
    "    ld.d    $t0, $a0, 256\n"     // PRMD
    "    csrwr   $t0, 0x01\n"
    
    // Restore GPRs (in reverse order to free $a0 last)
    "    ld.d    $s8,   $a0, 240\n"
    "    ld.d    $s7,   $a0, 232\n"
    "    ld.d    $s6,   $a0, 224\n"
    "    ld.d    $s5,   $a0, 216\n"
    "    ld.d    $s4,   $a0, 208\n"
    "    ld.d    $s3,   $a0, 200\n"
    "    ld.d    $s2,   $a0, 192\n"
    "    ld.d    $s1,   $a0, 184\n"
    "    ld.d    $s0,   $a0, 176\n"
    "    ld.d    $fp,   $a0, 168\n"
    "    ld.d    $r21,  $a0, 160\n"
    "    ld.d    $t8,   $a0, 152\n"
    "    ld.d    $t7,   $a0, 144\n"
    "    ld.d    $t6,   $a0, 136\n"
    "    ld.d    $t5,   $a0, 128\n"
    "    ld.d    $t4,   $a0, 120\n"
    "    ld.d    $t3,   $a0, 112\n"
    "    ld.d    $t2,   $a0, 104\n"
    "    ld.d    $t1,   $a0, 96\n"
    "    ld.d    $t0,   $a0, 88\n"
    "    ld.d    $a7,   $a0, 80\n"
    "    ld.d    $a6,   $a0, 72\n"
    "    ld.d    $a5,   $a0, 64\n"
    "    ld.d    $a4,   $a0, 56\n"
    "    ld.d    $a3,   $a0, 48\n"
    "    ld.d    $a2,   $a0, 40\n"
    "    ld.d    $a1,   $a0, 32\n"
    "    ld.d    $ra,   $a0, 0\n"
    "    ld.d    $tp,   $a0, 8\n"
    "    ld.d    $sp,   $a0, 16\n"
    "    ld.d    $a0,   $a0, 24\n"    // Restore a0 last
    
    // Return from exception
    "    ertn\n"
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
    
    // Initialize the context
    data->context = init_context(stack, entry, arg);
    data->kernel_sp = stack;
    data->tls_base = 0;
    data->asid = 0;
    data->pgd_phys = 0;
}

void arch_thread_destroy(ArchThreadData* data)
{
    if (!data) return;
    
    // Clean up architecture-specific resources
    // TODO: Free any allocated memory, invalidate TLB entries, etc.
    data->context = nullptr;
    data->kernel_sp = 0;
}

void arch_switch_to(ArchThreadData* current, ArchThreadData* next)
{
    if (!current || !next || !next->context) {
        return;
    }
    
    // Switch address space if different
    if (current->asid != next->asid || current->pgd_phys != next->pgd_phys) {
        // Write new page table root
        if (next->pgd_phys != 0) {
            write_csr(CSR_PGD, next->pgd_phys);
        }
        
        // Flush TLB for old ASID (optional, depends on strategy)
        // invtlb_asid(current->asid);
    }
    
    // Perform the context switch
    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // Clear the timer interrupt
    clear_timer_interrupt();
    
    // TODO: Call scheduler to check for preemption
    // scheduler::tick();
}

void arch_set_timer(uint64_t ticks)
{
    // Configure the timer for next interrupt
    // TCFG format:
    //   [47:2] = InitVal (timer countdown value)
    //   [1]    = Periodic (1 = auto-reload, 0 = one-shot)
    //   [0]    = En (enable timer)
    
    uint64_t tcfg = (ticks << 2) | 0x1;  // Enable, one-shot
    write_tcfg(tcfg);
}

} // namespace context
} // namespace loongarch64
} // namespace arch
} // namespace kernel
