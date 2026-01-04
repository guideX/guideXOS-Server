//
// ARM Architecture Implementation
//
// Copyright (c) 2024 guideX
//

#include "include/arch/arm.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm {

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    while (1) {
        asm volatile ("wfi");  // Wait For Interrupt
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    asm volatile ("cpsie if");  // Clear CPSR I and F bits
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    asm volatile ("cpsid if");  // Set CPSR I and F bits
#endif
}

void wait_for_interrupt()
{
#if GXOS_MSVC_STUB
    __nop();
#else
    asm volatile ("wfi");
#endif
}

uint32_t read_sctlr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(value));
    return value;
#endif
}

void write_sctlr(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(value));
    asm volatile ("isb");
#endif
}

uint32_t read_ttbr0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(value));
    return value;
#endif
}

void write_ttbr0(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(value));
    asm volatile ("isb");
#endif
}

uint32_t read_ttbr1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(value));
    return value;
#endif
}

void write_ttbr1(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mcr p15, 0, %0, c2, c0, 1" : : "r"(value));
    asm volatile ("isb");
#endif
}

uint32_t read_dacr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(value));
    return value;
#endif
}

void write_dacr(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(value));
    asm volatile ("isb");
#endif
}

void invalidate_icache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("mcr p15, 0, %0, c7, c5, 0" : : "r"(0));
    asm volatile ("dsb");
    asm volatile ("isb");
#endif
}

void invalidate_dcache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("mcr p15, 0, %0, c7, c6, 0" : : "r"(0));
    asm volatile ("dsb");
#endif
}

void flush_dcache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("mcr p15, 0, %0, c7, c10, 0" : : "r"(0));
    asm volatile ("dsb");
#endif
}

void invalidate_tlb()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
    asm volatile ("dsb");
    asm volatile ("isb");
#endif
}

void init()
{
    // TODO: Initialize ARM-specific features
    // - Set up page tables
    // - Enable MMU
    // - Configure caches
    // - Set up interrupt vectors
}

} // namespace arm
} // namespace arch
} // namespace kernel
