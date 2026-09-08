#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace native_elf {
namespace NativeElfDebugTrap {

// Register image supplied by the temporary vector-1/vector-3 gates. The
// layout is intentionally bounded and matches the save/restore sequence in
// both gates.
struct BreakpointContext {
    // The first field is a copied pre-trap target RSP. The assembly gate
    // stores it in the padding slot so the saved general-purpose registers
    // retain their natural offsets below.
    uint64_t rsp;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};

static_assert(offsetof(BreakpointContext, rsp) == 0, "debug trap rsp offset");
static_assert(offsetof(BreakpointContext, rax) == 8, "debug trap rax offset");
static_assert(offsetof(BreakpointContext, r15) == 120, "debug trap r15 offset");
static_assert(offsetof(BreakpointContext, rip) == 128, "debug trap rip offset");
static_assert(offsetof(BreakpointContext, cs) == 136, "debug trap cs offset");
static_assert(offsetof(BreakpointContext, rflags) == 144, "debug trap rflags offset");
static_assert(sizeof(BreakpointContext) == 152, "debug trap frame size");

typedef bool (*BreakpointHandler)(BreakpointContext* context);

// Temporarily replaces IDT vectors 1 and 3 and restores the exact previous
// gates on teardown. This is used only for the one active NativeElf debug
// generation.
bool install(BreakpointHandler breakpointHandler,
             BreakpointHandler singleStepHandler);
void uninstall();

} // namespace NativeElfDebugTrap
} // namespace native_elf
} // namespace kernel
