//
// SPARC v8 Architecture Implementation
//
// Copyright (c) 2024 guideX
//

#include "include/arch/sparc.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace sparc {

// PSR bit layout (SPARC v8):
//   Bit  5    : ET  (Enable Traps — must be 1 for interrupts to fire)
//   Bits 11:8 : PIL (Processor Interrupt Level, 0–15)
//
// enable_interrupts  : set ET, clear PIL to 0
// disable_interrupts : clear ET, set PIL to 15

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    while (1) {
        asm volatile ("nop");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    uint32_t psr = read_psr();
    psr |= 0x20;          // set ET
    psr &= ~0x0F00u;      // clear PIL (allow all levels)
    write_psr(psr);
    asm volatile ("nop; nop; nop");
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    uint32_t psr = read_psr();
    psr &= ~0x20u;        // clear ET
    psr |= 0x0F00u;       // set PIL to 15
    write_psr(psr);
    asm volatile ("nop; nop; nop");
#endif
}

uint32_t read_asi(uint32_t asi, uint32_t address)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; return 0;
#else
    uint32_t value;
    asm volatile (
        "lda [%1] %2, %0"
        : "=r"(value)
        : "r"(address), "i"(asi)
    );
    return value;
#endif
}

void write_asi(uint32_t asi, uint32_t address, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; (void)value;
#else
    asm volatile (
        "sta %0, [%1] %2"
        :
        : "r"(value), "r"(address), "i"(asi)
    );
#endif
}

void flush_windows()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile (
        "save %%sp, -64, %%sp\n"
        "restore\n"
        :
        :
        : "memory"
    );
#endif
}

uint32_t read_psr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t psr;
    asm volatile (
        "rd %%psr, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(psr)
    );
    return psr;
#endif
}

void write_psr(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "wr %0, %%psr\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
#endif
}

uint32_t read_tbr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t tbr;
    asm volatile (
        "rd %%tbr, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(tbr)
    );
    return tbr;
#endif
}

void write_tbr(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "wr %0, %%tbr\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
#endif
}

uint32_t read_wim()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t wim;
    asm volatile (
        "rd %%wim, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(wim)
    );
    return wim;
#endif
}

void write_wim(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "wr %0, %%wim\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
#endif
}

void init()
{
    // TODO: Initialize SPARC-specific features
    // - Set up trap table
    // - Configure MMU
    // - Initialize register windows
    // - Set up interrupt handlers
}

} // namespace sparc
} // namespace arch
} // namespace kernel
