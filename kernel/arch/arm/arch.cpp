//
// ARM Architecture Implementation
//
// Copyright (c) 2024 guideX
//

#include <arch/arm.h>

namespace kernel {
namespace arch {
namespace arm {

void halt()
{
    while (1) {
        asm volatile ("wfi");  // Wait For Interrupt
    }
}

void enable_interrupts()
{
    asm volatile ("cpsie if");  // Clear CPSR I and F bits
}

void disable_interrupts()
{
    asm volatile ("cpsid if");  // Set CPSR I and F bits
}

void wait_for_interrupt()
{
    asm volatile ("wfi");
}

uint32_t read_sctlr()
{
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(value));
    return value;
}

void write_sctlr(uint32_t value)
{
    asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(value));
    asm volatile ("isb");
}

uint32_t read_ttbr0()
{
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(value));
    return value;
}

void write_ttbr0(uint32_t value)
{
    asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(value));
    asm volatile ("isb");
}

uint32_t read_ttbr1()
{
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(value));
    return value;
}

void write_ttbr1(uint32_t value)
{
    asm volatile ("mcr p15, 0, %0, c2, c0, 1" : : "r"(value));
    asm volatile ("isb");
}

uint32_t read_dacr()
{
    uint32_t value;
    asm volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(value));
    return value;
}

void write_dacr(uint32_t value)
{
    asm volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(value));
    asm volatile ("isb");
}

void invalidate_icache()
{
    asm volatile ("mcr p15, 0, %0, c7, c5, 0" : : "r"(0));
    asm volatile ("dsb");
    asm volatile ("isb");
}

void invalidate_dcache()
{
    asm volatile ("mcr p15, 0, %0, c7, c6, 0" : : "r"(0));
    asm volatile ("dsb");
}

void flush_dcache()
{
    asm volatile ("mcr p15, 0, %0, c7, c10, 0" : : "r"(0));
    asm volatile ("dsb");
}

void invalidate_tlb()
{
    asm volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
    asm volatile ("dsb");
    asm volatile ("isb");
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
