//
// SPARC Architecture Implementation
//
// Copyright (c) 2024 guideX
//

#include <arch/sparc.h>

namespace kernel {
namespace arch {
namespace sparc {

void halt()
{
    while (1) {
        asm volatile ("nop");
    }
}

void enable_interrupts()
{
    uint32_t psr = read_psr();
    psr &= ~0x20;  // Clear PIL field
    write_psr(psr);
}

void disable_interrupts()
{
    uint32_t psr = read_psr();
    psr |= 0x20;   // Set PIL to 15
    write_psr(psr);
}

uint32_t read_asi(uint32_t asi, uint32_t address)
{
    uint32_t value;
    asm volatile (
        "lda [%1] %2, %0"
        : "=r"(value)
        : "r"(address), "i"(asi)
    );
    return value;
}

void write_asi(uint32_t asi, uint32_t address, uint32_t value)
{
    asm volatile (
        "sta %0, [%1] %2"
        :
        : "r"(value), "r"(address), "i"(asi)
    );
}

void flush_windows()
{
    asm volatile (
        "save %%sp, -64, %%sp\n"
        "restore\n"
        :
        :
        : "memory"
    );
}

uint32_t read_psr()
{
    uint32_t psr;
    asm volatile (
        "rd %%psr, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(psr)
    );
    return psr;
}

void write_psr(uint32_t value)
{
    asm volatile (
        "wr %0, %%psr\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
}

uint32_t read_tbr()
{
    uint32_t tbr;
    asm volatile (
        "rd %%tbr, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(tbr)
    );
    return tbr;
}

void write_tbr(uint32_t value)
{
    asm volatile (
        "wr %0, %%tbr\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
}

uint32_t read_wim()
{
    uint32_t wim;
    asm volatile (
        "rd %%wim, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(wim)
    );
    return wim;
}

void write_wim(uint32_t value)
{
    asm volatile (
        "wr %0, %%wim\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
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
