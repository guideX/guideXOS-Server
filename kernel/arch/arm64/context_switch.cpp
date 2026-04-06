// ARM64 Context Switch Implementation
//
// Provides thread context switching for the ARM64 architecture.
// Handles callee-saved registers according to AAPCS64 calling convention.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/context_switch.h"
#include "include/arch/arm64.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm64 {
namespace context {

// ================================================================
// Internal helpers
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

// ================================================================
// Context initialization
// ================================================================

void init_context(SwitchContext* ctx,
                  uint64_t stackTop,
                  void (*entryPoint)(void*),
                  void* argument)
{
    if (!ctx) return;
    
    // Clear the context
    memzero(ctx, sizeof(SwitchContext));
    
    // Stack must be 16-byte aligned on ARM64
    stackTop &= ~0xFULL;
    
    // Set up stack pointer
    ctx->sp = stackTop;
    
    // X30 (LR) holds the entry point - when we "return" we'll jump there
    ctx->x30 = reinterpret_cast<uint64_t>(entryPoint);
    
    // X19 will hold the argument (first callee-saved register)
    // The thread entry trampoline will move this to X0
    ctx->x19 = reinterpret_cast<uint64_t>(argument);
    
    // Frame pointer starts at stack top
    ctx->x29 = stackTop;
}

// ================================================================
// Context switch (assembly implementation)
// ================================================================

#if GXOS_MSVC_STUB

// Stub implementations for MSVC builds (no actual context switch)
void switch_context(SwitchContext* from, SwitchContext* to)
{
    (void)from;
    (void)to;
}

void switch_to_context(SwitchContext* to)
{
    (void)to;
}

#else

// Implemented in assembly for actual ARM64 builds
extern "C" void asm_switch_context(SwitchContext* from, SwitchContext* to);
extern "C" void asm_switch_to_context(SwitchContext* to);

void switch_context(SwitchContext* from, SwitchContext* to)
{
    asm_switch_context(from, to);
}

void switch_to_context(SwitchContext* to)
{
    asm_switch_to_context(to);
}

// Assembly implementations
__asm__(
    ".global asm_switch_context\n"
    ".type asm_switch_context, %function\n"
    "asm_switch_context:\n"
    "    // Save callee-saved registers to 'from' (X0)\n"
    "    stp     x19, x20, [x0, #(0 * 8)]\n"
    "    stp     x21, x22, [x0, #(2 * 8)]\n"
    "    stp     x23, x24, [x0, #(4 * 8)]\n"
    "    stp     x25, x26, [x0, #(6 * 8)]\n"
    "    stp     x27, x28, [x0, #(8 * 8)]\n"
    "    stp     x29, x30, [x0, #(10 * 8)]\n"
    "    mov     x9, sp\n"
    "    str     x9, [x0, #(12 * 8)]\n"      // Save SP
    "    mrs     x9, tpidr_el0\n"
    "    str     x9, [x0, #(13 * 8)]\n"      // Save TPIDR_EL0
    "    \n"
    "    // Save FP callee-saved registers (D8-D15)\n"
    "    stp     d8, d9, [x0, #(14 * 8)]\n"
    "    stp     d10, d11, [x0, #(16 * 8)]\n"
    "    stp     d12, d13, [x0, #(18 * 8)]\n"
    "    stp     d14, d15, [x0, #(20 * 8)]\n"
    "    \n"
    "    // Restore callee-saved registers from 'to' (X1)\n"
    "    ldp     x19, x20, [x1, #(0 * 8)]\n"
    "    ldp     x21, x22, [x1, #(2 * 8)]\n"
    "    ldp     x23, x24, [x1, #(4 * 8)]\n"
    "    ldp     x25, x26, [x1, #(6 * 8)]\n"
    "    ldp     x27, x28, [x1, #(8 * 8)]\n"
    "    ldp     x29, x30, [x1, #(10 * 8)]\n"
    "    ldr     x9, [x1, #(12 * 8)]\n"
    "    mov     sp, x9\n"                   // Restore SP
    "    ldr     x9, [x1, #(13 * 8)]\n"
    "    msr     tpidr_el0, x9\n"            // Restore TPIDR_EL0
    "    \n"
    "    // Restore FP callee-saved registers\n"
    "    ldp     d8, d9, [x1, #(14 * 8)]\n"
    "    ldp     d10, d11, [x1, #(16 * 8)]\n"
    "    ldp     d12, d13, [x1, #(18 * 8)]\n"
    "    ldp     d14, d15, [x1, #(20 * 8)]\n"
    "    \n"
    "    // Return to new thread (X30 was restored with return address)\n"
    "    ret\n"
);

__asm__(
    ".global asm_switch_to_context\n"
    ".type asm_switch_to_context, %function\n"
    "asm_switch_to_context:\n"
    "    // Restore callee-saved registers from 'to' (X0)\n"
    "    ldp     x19, x20, [x0, #(0 * 8)]\n"
    "    ldp     x21, x22, [x0, #(2 * 8)]\n"
    "    ldp     x23, x24, [x0, #(4 * 8)]\n"
    "    ldp     x25, x26, [x0, #(6 * 8)]\n"
    "    ldp     x27, x28, [x0, #(8 * 8)]\n"
    "    ldp     x29, x30, [x0, #(10 * 8)]\n"
    "    ldr     x9, [x0, #(12 * 8)]\n"
    "    mov     sp, x9\n"
    "    ldr     x9, [x0, #(13 * 8)]\n"
    "    msr     tpidr_el0, x9\n"
    "    \n"
    "    // Restore FP callee-saved registers\n"
    "    ldp     d8, d9, [x0, #(14 * 8)]\n"
    "    ldp     d10, d11, [x0, #(16 * 8)]\n"
    "    ldp     d12, d13, [x0, #(18 * 8)]\n"
    "    ldp     d14, d15, [x0, #(20 * 8)]\n"
    "    \n"
    "    // For new threads, X19 contains the argument\n"
    "    // Move to X0 as first argument for entry function\n"
    "    mov     x0, x19\n"
    "    \n"
    "    // Jump to entry point (in X30/LR)\n"
    "    ret\n"
);

#endif // GXOS_MSVC_STUB

// ================================================================
// Full context save/restore (for exception handlers)
// ================================================================

void save_full_context(FullContext* ctx)
{
    if (!ctx) return;
    
#if GXOS_MSVC_STUB
    memzero(ctx, sizeof(FullContext));
#else
    // This is typically done by the exception handler in boot.S
    // This function is for manual saves if needed
    asm volatile (
        "stp     x0, x1, [%0, #(0 * 8)]\n"
        "stp     x2, x3, [%0, #(2 * 8)]\n"
        "stp     x4, x5, [%0, #(4 * 8)]\n"
        "stp     x6, x7, [%0, #(6 * 8)]\n"
        "stp     x8, x9, [%0, #(8 * 8)]\n"
        "stp     x10, x11, [%0, #(10 * 8)]\n"
        "stp     x12, x13, [%0, #(12 * 8)]\n"
        "stp     x14, x15, [%0, #(14 * 8)]\n"
        "stp     x16, x17, [%0, #(16 * 8)]\n"
        "stp     x18, x19, [%0, #(18 * 8)]\n"
        "stp     x20, x21, [%0, #(20 * 8)]\n"
        "stp     x22, x23, [%0, #(22 * 8)]\n"
        "stp     x24, x25, [%0, #(24 * 8)]\n"
        "stp     x26, x27, [%0, #(26 * 8)]\n"
        "stp     x28, x29, [%0, #(28 * 8)]\n"
        "str     x30, [%0, #(30 * 8)]\n"
        : : "r"(ctx) : "memory"
    );
    
    // Save SP
    uint64_t sp;
    asm volatile ("mov %0, sp" : "=r"(sp));
    ctx->sp = sp;
    
    // Save exception state
    ctx->esr = read_esr_el1();
    ctx->far = read_far_el1();
#endif
}

void restore_full_context(FullContext* ctx)
{
    if (!ctx) return;
    
#if !GXOS_MSVC_STUB
    // Restore general purpose registers and return via eret
    // This is typically done by exception return in boot.S
    asm volatile (
        "ldp     x0, x1, [%0, #(0 * 8)]\n"
        "ldp     x2, x3, [%0, #(2 * 8)]\n"
        "ldp     x4, x5, [%0, #(4 * 8)]\n"
        "ldp     x6, x7, [%0, #(6 * 8)]\n"
        "ldp     x8, x9, [%0, #(8 * 8)]\n"
        "ldp     x10, x11, [%0, #(10 * 8)]\n"
        "ldp     x12, x13, [%0, #(12 * 8)]\n"
        "ldp     x14, x15, [%0, #(14 * 8)]\n"
        "ldp     x16, x17, [%0, #(16 * 8)]\n"
        "ldp     x18, x19, [%0, #(18 * 8)]\n"
        "ldp     x20, x21, [%0, #(20 * 8)]\n"
        "ldp     x22, x23, [%0, #(22 * 8)]\n"
        "ldp     x24, x25, [%0, #(24 * 8)]\n"
        "ldp     x26, x27, [%0, #(26 * 8)]\n"
        "ldp     x28, x29, [%0, #(28 * 8)]\n"
        "ldr     x30, [%0, #(30 * 8)]\n"
        : : "r"(ctx) : "memory"
    );
#else
    (void)ctx;
#endif
}

// ================================================================
// FPU context management
// ================================================================

void save_fpu_context(FpuContext* ctx)
{
    if (!ctx) return;
    
#if GXOS_MSVC_STUB
    memzero(ctx, sizeof(FpuContext));
#else
    // Save all 32 SIMD registers
    uint64_t* v = reinterpret_cast<uint64_t*>(ctx->v);
    asm volatile (
        "stp     q0, q1, [%0, #(0 * 32)]\n"
        "stp     q2, q3, [%0, #(2 * 32)]\n"
        "stp     q4, q5, [%0, #(4 * 32)]\n"
        "stp     q6, q7, [%0, #(6 * 32)]\n"
        "stp     q8, q9, [%0, #(8 * 32)]\n"
        "stp     q10, q11, [%0, #(10 * 32)]\n"
        "stp     q12, q13, [%0, #(12 * 32)]\n"
        "stp     q14, q15, [%0, #(14 * 32)]\n"
        "stp     q16, q17, [%0, #(16 * 32)]\n"
        "stp     q18, q19, [%0, #(18 * 32)]\n"
        "stp     q20, q21, [%0, #(20 * 32)]\n"
        "stp     q22, q23, [%0, #(22 * 32)]\n"
        "stp     q24, q25, [%0, #(24 * 32)]\n"
        "stp     q26, q27, [%0, #(26 * 32)]\n"
        "stp     q28, q29, [%0, #(28 * 32)]\n"
        "stp     q30, q31, [%0, #(30 * 32)]\n"
        : : "r"(v) : "memory"
    );
    
    // Save FPSR and FPCR
    uint64_t fpsr, fpcr;
    asm volatile ("mrs %0, fpsr" : "=r"(fpsr));
    asm volatile ("mrs %0, fpcr" : "=r"(fpcr));
    ctx->fpsr = static_cast<uint32_t>(fpsr);
    ctx->fpcr = static_cast<uint32_t>(fpcr);
#endif
}

void restore_fpu_context(FpuContext* ctx)
{
    if (!ctx) return;
    
#if !GXOS_MSVC_STUB
    // Restore FPSR and FPCR first
    asm volatile ("msr fpsr, %0" : : "r"(static_cast<uint64_t>(ctx->fpsr)));
    asm volatile ("msr fpcr, %0" : : "r"(static_cast<uint64_t>(ctx->fpcr)));
    
    // Restore all 32 SIMD registers
    uint64_t* v = reinterpret_cast<uint64_t*>(ctx->v);
    asm volatile (
        "ldp     q0, q1, [%0, #(0 * 32)]\n"
        "ldp     q2, q3, [%0, #(2 * 32)]\n"
        "ldp     q4, q5, [%0, #(4 * 32)]\n"
        "ldp     q6, q7, [%0, #(6 * 32)]\n"
        "ldp     q8, q9, [%0, #(8 * 32)]\n"
        "ldp     q10, q11, [%0, #(10 * 32)]\n"
        "ldp     q12, q13, [%0, #(12 * 32)]\n"
        "ldp     q14, q15, [%0, #(14 * 32)]\n"
        "ldp     q16, q17, [%0, #(16 * 32)]\n"
        "ldp     q18, q19, [%0, #(18 * 32)]\n"
        "ldp     q20, q21, [%0, #(20 * 32)]\n"
        "ldp     q22, q23, [%0, #(22 * 32)]\n"
        "ldp     q24, q25, [%0, #(24 * 32)]\n"
        "ldp     q26, q27, [%0, #(26 * 32)]\n"
        "ldp     q28, q29, [%0, #(28 * 32)]\n"
        "ldp     q30, q31, [%0, #(30 * 32)]\n"
        : : "r"(v) : "memory"
    );
#else
    (void)ctx;
#endif
}

void enable_fpu()
{
#if !GXOS_MSVC_STUB
    // Clear CPACR_EL1 FPEN bits to allow FP/SIMD access
    uint64_t cpacr;
    asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);  // FPEN = 0b11 (no trap)
    asm volatile ("msr cpacr_el1, %0" : : "r"(cpacr));
    asm volatile ("isb");
#endif
}

void disable_fpu()
{
#if !GXOS_MSVC_STUB
    // Set CPACR_EL1 FPEN bits to trap FP/SIMD access
    uint64_t cpacr;
    asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr &= ~(3ULL << 20);  // FPEN = 0b00 (trap)
    asm volatile ("msr cpacr_el1, %0" : : "r"(cpacr));
    asm volatile ("isb");
#endif
}

// ================================================================
// Stack and register helpers
// ================================================================

uint64_t get_sp()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t sp;
    asm volatile ("mov %0, sp" : "=r"(sp));
    return sp;
#endif
}

void set_sp(uint64_t sp)
{
#if !GXOS_MSVC_STUB
    asm volatile ("mov sp, %0" : : "r"(sp));
#else
    (void)sp;
#endif
}

uint64_t get_fp()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t fp;
    asm volatile ("mov %0, x29" : "=r"(fp));
    return fp;
#endif
}

uint64_t get_lr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t lr;
    asm volatile ("mov %0, x30" : "=r"(lr));
    return lr;
#endif
}

// ================================================================
// Thread Local Storage
// ================================================================

uint64_t get_tls()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t tls;
    asm volatile ("mrs %0, tpidr_el0" : "=r"(tls));
    return tls;
#endif
}

void set_tls(uint64_t tls)
{
#if !GXOS_MSVC_STUB
    asm volatile ("msr tpidr_el0, %0" : : "r"(tls));
#else
    (void)tls;
#endif
}

} // namespace context
} // namespace arm64
} // namespace arch
} // namespace kernel
