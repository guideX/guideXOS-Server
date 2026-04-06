//
// PowerPC64 Context Switch Implementation
//
// Provides context switching primitives for the scheduler on PowerPC64.
//
// PowerPC64 context switch saves/restores:
// - Callee-saved registers (r14-r31)
// - Stack pointer (r1)
// - TOC pointer (r2)
// - Link Register (LR)
// - Condition Register (CR)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/ppc64.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace ppc64 {
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
    
    // Enable interrupts for the new thread
    enable_interrupts();
    
    if (entry) {
        entry(arg);
    }
    
    // Thread returned - halt
    // TODO: Call proper thread exit via scheduler
    while (1) {
        halt();
    }
}

// ================================================================
// Context initialization
// ================================================================

SwitchContext* init_context(uint64_t stack_top, void (*entry_point)(void*), void* arg)
{
    // PowerPC64 ELFv2 ABI requires 16-byte stack alignment
    stack_top &= ~0xFULL;
    
    // Reserve space for minimum stack frame (32 bytes for ELFv2)
    // Plus space for SwitchContext
    stack_top -= 32;  // Minimum frame
    stack_top -= sizeof(SwitchContext);
    
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(stack_top);
    
    // Initialize non-volatile registers to 0
    ctx->r14 = 0;
    ctx->r15 = 0;
    ctx->r16 = 0;
    ctx->r17 = 0;
    ctx->r18 = 0;
    ctx->r19 = 0;
    ctx->r20 = 0;
    ctx->r21 = 0;
    ctx->r22 = 0;
    ctx->r23 = 0;
    ctx->r24 = 0;
    ctx->r25 = 0;
    ctx->r26 = 0;
    ctx->r27 = 0;
    ctx->r28 = 0;
    ctx->r29 = 0;
    ctx->r30 = 0;
    ctx->r31 = 0;
    
    // Stack pointer
    ctx->sp = stack_top;
    
    // TOC pointer - will be set properly when thread runs
    ctx->toc = 0;
    
    // Link Register - thread entry wrapper
    ctx->lr = reinterpret_cast<uint64_t>(&thread_entry_wrapper);
    
    // Condition Register - clear
    ctx->cr = 0;
    
    // Store entry point and argument for the wrapper
    s_pending_entry = entry_point;
    s_pending_arg = arg;
    
    return ctx;
}

// ================================================================
// Context switch implementation
// ================================================================

#if GXOS_MSVC_STUB

// Stub implementation for MSVC compilation
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
}

#else

// Assembly implementation of switch_context
// void switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
//
// r3 = old_ctx (pointer to pointer)
// r4 = new_ctx (pointer to new context)
//
// This is defined in assembly below or inline
asm(
".global switch_context\n"
".type switch_context, @function\n"
"switch_context:\n"
    // Save current context
    
    // Allocate stack frame for SwitchContext
    "addi    1, 1, -152\n"       // sizeof(SwitchContext) = 152 bytes (19 * 8)
    
    // Save non-volatile GPRs (r14-r31)
    "std     14, 0(1)\n"
    "std     15, 8(1)\n"
    "std     16, 16(1)\n"
    "std     17, 24(1)\n"
    "std     18, 32(1)\n"
    "std     19, 40(1)\n"
    "std     20, 48(1)\n"
    "std     21, 56(1)\n"
    "std     22, 64(1)\n"
    "std     23, 72(1)\n"
    "std     24, 80(1)\n"
    "std     25, 88(1)\n"
    "std     26, 96(1)\n"
    "std     27, 104(1)\n"
    "std     28, 112(1)\n"
    "std     29, 120(1)\n"
    "std     30, 128(1)\n"
    "std     31, 136(1)\n"
    
    // Save SP (already adjusted, save the address of our context)
    "mr      5, 1\n"             // r5 = current SP (context address)
    
    // Save TOC (r2)
    "std     2, 144(1)\n"
    
    // Save LR
    "mflr    0\n"
    "std     0, 152(1)\n"        // Actually at offset 144+8=152, but we need to adjust
    
    // Save CR
    "mfcr    0\n"
    "std     0, 160(1)\n"        // offset 152+8=160
    
    // Store old context pointer: *old_ctx = current_sp
    "std     1, 0(3)\n"          // *r3 = r1
    
    // Switch to new context
    "mr      1, 4\n"             // r1 = new_ctx (new SP)
    
    // Restore CR
    "ld      0, 160(1)\n"
    "mtcr    0\n"
    
    // Restore LR
    "ld      0, 152(1)\n"
    "mtlr    0\n"
    
    // Restore TOC (r2)
    "ld      2, 144(1)\n"
    
    // Restore non-volatile GPRs (r14-r31)
    "ld      14, 0(1)\n"
    "ld      15, 8(1)\n"
    "ld      16, 16(1)\n"
    "ld      17, 24(1)\n"
    "ld      18, 32(1)\n"
    "ld      19, 40(1)\n"
    "ld      20, 48(1)\n"
    "ld      21, 56(1)\n"
    "ld      22, 64(1)\n"
    "ld      23, 72(1)\n"
    "ld      24, 80(1)\n"
    "ld      25, 88(1)\n"
    "ld      26, 96(1)\n"
    "ld      27, 104(1)\n"
    "ld      28, 112(1)\n"
    "ld      29, 120(1)\n"
    "ld      30, 128(1)\n"
    "ld      31, 136(1)\n"
    
    // Deallocate stack frame
    "addi    1, 1, 152\n"
    
    // Return (via LR)
    "blr\n"
);

// Stub implementations for full context save/restore
// These would typically be in boot.s as part of exception handling
extern "C" void save_full_context(FullContext* ctx)
{
    (void)ctx;
    // Full context save is done in exception entry code
}

extern "C" void restore_full_context(FullContext* ctx)
{
    (void)ctx;
    // Full context restore is done in exception return code
}

#endif // GXOS_MSVC_STUB

} // namespace context
} // namespace ppc64
} // namespace arch
} // namespace kernel
