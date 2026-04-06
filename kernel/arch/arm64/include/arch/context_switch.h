// ARM64 Context Switch and Scheduler Integration
//
// This file defines the CPU context structure and context switching
// primitives for the ARM64 architecture.
//
// ARM64 (AArch64) register conventions (AAPCS64):
//   X0-X7   - Arguments / return values (caller-saved)
//   X8      - Indirect result location (caller-saved)
//   X9-X15  - Temporary (caller-saved)
//   X16-X17 - Intra-procedure call scratch (IP0/IP1, caller-saved)
//   X18     - Platform register (reserved, typically TLS)
//   X19-X28 - Callee-saved registers
//   X29     - Frame pointer (FP, callee-saved)
//   X30     - Link register (LR)
//   SP      - Stack pointer (must be 16-byte aligned)
//   XZR     - Zero register
//
// Callee-saved: X19-X28, X29 (FP), X30 (LR), SP
//
// SIMD/FP registers:
//   V0-V7   - Arguments / return values (caller-saved)
//   V8-V15  - Callee-saved (lower 64-bits only, D8-D15)
//   V16-V31 - Temporary (caller-saved)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm64 {
namespace context {

// ================================================================
// Thread context structure
// ================================================================

// Full thread context (for exceptions/interrupts)
// This structure matches the layout saved by boot.S exception handlers
struct FullContext {
    // General purpose registers (X0-X30)
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
    uint64_t x7;
    uint64_t x8;
    uint64_t x9;
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t x16;
    uint64_t x17;
    uint64_t x18;       // Platform register (TLS)
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;       // Frame pointer (FP)
    uint64_t x30;       // Link register (LR)
    
    // Exception state
    uint64_t sp;        // Stack pointer
    uint64_t pc;        // Program counter (from ELR_EL1)
    uint64_t pstate;    // Processor state (from SPSR_EL1)
    
    // Exception information
    uint64_t esr;       // Exception Syndrome Register
    uint64_t far;       // Fault Address Register
};

// Switch context (for voluntary context switch)
// Only callee-saved registers need to be preserved
struct SwitchContext {
    // Callee-saved general-purpose registers
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;       // Frame pointer
    uint64_t x30;       // Return address (LR)
    
    // Stack pointer
    uint64_t sp;
    
    // Thread local storage pointer
    uint64_t tpidr_el0;
    
    // Callee-saved FP/SIMD registers (lower 64-bits)
    // D8-D15 need to be preserved across calls
    uint64_t d8;
    uint64_t d9;
    uint64_t d10;
    uint64_t d11;
    uint64_t d12;
    uint64_t d13;
    uint64_t d14;
    uint64_t d15;
};

// FPU/SIMD context (for full FP state save)
struct FpuContext {
    // 32 SIMD registers (128-bit each)
    uint64_t v[32][2];  // V0-V31 (Q0-Q31)
    
    // FP status/control registers
    uint32_t fpsr;      // Floating-point Status Register
    uint32_t fpcr;      // Floating-point Control Register
};

// Thread Control Block extension for ARM64
struct ThreadContext {
    SwitchContext switchCtx;
    FpuContext    fpuCtx;
    bool          fpuUsed;       // Has this thread used FPU?
    uint64_t      kernelSp;      // Kernel stack pointer for user threads
};

// ================================================================
// Context switch functions
// ================================================================

// Initialize a new thread context
// Sets up the context so that when switched to, it will start
// executing at 'entryPoint' with 'argument' in X0
void init_context(SwitchContext* ctx, 
                  uint64_t stackTop,
                  void (*entryPoint)(void*),
                  void* argument);

// Switch from one thread to another
// Saves current callee-saved registers to 'from' and restores from 'to'
// This function returns when another thread switches back
void switch_context(SwitchContext* from, SwitchContext* to);

// Switch to a new thread without saving current context
// Used for initial switch or when current thread is terminating
void switch_to_context(SwitchContext* to);

// Save full context (for exception handlers)
void save_full_context(FullContext* ctx);

// Restore full context (for exception return)
void restore_full_context(FullContext* ctx);

// ================================================================
// FPU context functions
// ================================================================

// Save FPU/SIMD state
void save_fpu_context(FpuContext* ctx);

// Restore FPU/SIMD state
void restore_fpu_context(FpuContext* ctx);

// Enable FPU access for current thread
void enable_fpu();

// Disable FPU access (trap on first use)
void disable_fpu();

// ================================================================
// Stack helpers
// ================================================================

// Get current stack pointer
uint64_t get_sp();

// Set stack pointer (use with caution!)
void set_sp(uint64_t sp);

// Get frame pointer
uint64_t get_fp();

// Get link register (return address)
uint64_t get_lr();

// ================================================================
// TLS (Thread Local Storage) helpers
// ================================================================

// Get thread pointer (TPIDR_EL0)
uint64_t get_tls();

// Set thread pointer
void set_tls(uint64_t tls);

} // namespace context
} // namespace arm64
} // namespace arch
} // namespace kernel
