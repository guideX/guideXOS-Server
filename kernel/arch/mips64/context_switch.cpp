//
// MIPS64 Context Switch Implementation
//
// Provides context switching primitives for the scheduler on MIPS64.
//
// MIPS64 context switch saves/restores callee-saved registers:
// - $s0-$s7 ($16-$23)
// - $fp ($30)
// - $ra ($31)
// - $sp ($29)
// - $gp ($28)
// - HI, LO special registers
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/mips64.h"
#include "include/arch/serial_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace mips64 {
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
    
    serial_console::puts("[Thread] Entry returned, halting\n");
    while (1) {
        wait_for_interrupt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg)
{
    // MIPS64 n64 ABI requires 16-byte stack alignment
    stack_top &= ~0xFULL;
    
    // Reserve space for SwitchContext
    stack_top -= sizeof(SwitchContext);
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize callee-saved registers to zero
    ctx->s0 = 0;
    ctx->s1 = 0;
    ctx->s2 = 0;
    ctx->s3 = 0;
    ctx->s4 = 0;
    ctx->s5 = 0;
    ctx->s6 = 0;
    ctx->s7 = 0;
    ctx->fp = 0;
    
    // Return address - thread entry wrapper
    // When context_switch returns, it will jump to this address
    ctx->ra = reinterpret_cast<uint64_t>(&thread_entry_wrapper);
    
    // Stack pointer
    ctx->sp = stack_top;
    
    // Global pointer (will be set properly by entry wrapper)
    ctx->gp = 0;
    
    // Clear HI/LO
    ctx->hi = 0;
    ctx->lo = 0;
    
    // Store entry point and argument for the entry wrapper
    s_pending_entry = entry_point;
    s_pending_arg = arg;
    
    return ctx;
}

// ================================================================
// Context switch implementation (assembly)
// ================================================================

#if !GXOS_MSVC_STUB

// Assembly implementation of context_switch
// void context_switch(SwitchContext** old_ctx, SwitchContext* new_ctx)
//   $a0 = pointer to pointer to store old context
//   $a1 = pointer to new context to restore
__asm__ (
    ".section .text\n"
    ".align 4\n"
    ".global context_switch\n"
    "context_switch:\n"
    ".set noreorder\n"
    ".set noat\n"
    
    // Allocate space on stack for SwitchContext (14 * 8 = 112 bytes)
    // Round up to 128 for alignment
    "daddiu $sp, $sp, -128\n"
    
    // Save callee-saved registers to stack
    "sd     $s0, 0($sp)\n"      // s0
    "sd     $s1, 8($sp)\n"      // s1
    "sd     $s2, 16($sp)\n"     // s2
    "sd     $s3, 24($sp)\n"     // s3
    "sd     $s4, 32($sp)\n"     // s4
    "sd     $s5, 40($sp)\n"     // s5
    "sd     $s6, 48($sp)\n"     // s6
    "sd     $s7, 56($sp)\n"     // s7
    "sd     $fp, 64($sp)\n"     // fp
    "sd     $ra, 72($sp)\n"     // ra (return address)
    "daddiu $t0, $sp, 128\n"    // Original sp
    "sd     $t0, 80($sp)\n"     // sp (before we allocated)
    "sd     $gp, 88($sp)\n"     // gp
    
    // Save HI/LO
    "mfhi   $t0\n"
    "mflo   $t1\n"
    "sd     $t0, 96($sp)\n"     // hi
    "sd     $t1, 104($sp)\n"    // lo
    
    // Store current context pointer to *old_ctx
    "sd     $sp, 0($a0)\n"
    
    // Load new context
    "move   $sp, $a1\n"
    
    // Restore callee-saved registers from new context
    "ld     $s0, 0($sp)\n"
    "ld     $s1, 8($sp)\n"
    "ld     $s2, 16($sp)\n"
    "ld     $s3, 24($sp)\n"
    "ld     $s4, 32($sp)\n"
    "ld     $s5, 40($sp)\n"
    "ld     $s6, 48($sp)\n"
    "ld     $s7, 56($sp)\n"
    "ld     $fp, 64($sp)\n"
    "ld     $ra, 72($sp)\n"
    "ld     $gp, 88($sp)\n"
    
    // Restore HI/LO
    "ld     $t0, 96($sp)\n"
    "ld     $t1, 104($sp)\n"
    "mthi   $t0\n"
    "mtlo   $t1\n"
    
    // Restore stack pointer
    "ld     $sp, 80($sp)\n"
    
    // Return to new thread
    // The 'jr $ra' will jump to the return address of the new context
    "jr     $ra\n"
    "nop\n"                     // Delay slot
    
    ".set at\n"
    ".set reorder\n"
);

#else

// MSVC stub for host builds
extern "C" void context_switch(SwitchContext** old_ctx, SwitchContext* new_ctx)
{
    (void)old_ctx;
    (void)new_ctx;
}

#endif

// ================================================================
// Full context functions
// ================================================================

static FullContext* s_current_exception_context = nullptr;

FullContext* get_exception_context()
{
    return s_current_exception_context;
}

void save_full_context(FullContext* ctx)
{
#if GXOS_MSVC_STUB
    (void)ctx;
#else
    // This would typically be called from assembly in the exception handler
    // For now, just record the pointer
    s_current_exception_context = ctx;
#endif
}

void restore_full_context(const FullContext* ctx)
{
#if GXOS_MSVC_STUB
    (void)ctx;
#else
    // This would typically jump back via eret after restoring
    // For now, just clear the exception context
    s_current_exception_context = nullptr;
#endif
}

} // namespace context
} // namespace mips64
} // namespace arch
} // namespace kernel
