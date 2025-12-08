//
// Itanium (IA-64) Architecture Implementation
//
// Copyright (c) 2024 guideX
//

#include <arch/ia64.h>

namespace kernel {
namespace arch {
namespace ia64 {

void halt()
{
    while (1) {
        asm volatile ("hint @pause");
    }
}

void enable_interrupts()
{
    asm volatile (
        "ssm psr.i;;\n"
        "srlz.d;;\n"
        :
        :
        : "memory"
    );
}

void disable_interrupts()
{
    asm volatile (
        "rsm psr.i;;\n"
        "srlz.d;;\n"
        :
        :
        : "memory"
    );
}

void break_instruction(uint64_t imm)
{
    asm volatile ("break.i %0" : : "i"(imm));
}

uint64_t read_psr()
{
    uint64_t psr;
    asm volatile (
        "mov %0 = psr;;\n"
        : "=r"(psr)
    );
    return psr;
}

void write_psr(uint64_t value)
{
    asm volatile (
        "mov psr.l = %0;;\n"
        "srlz.d;;\n"
        :
        : "r"(value)
        : "memory"
    );
}

uint64_t read_ar(uint32_t ar_num)
{
    uint64_t value;
    asm volatile (
        "mov %0 = ar%1;;\n"
        : "=r"(value)
        : "i"(ar_num)
    );
    return value;
}

void write_ar(uint32_t ar_num, uint64_t value)
{
    asm volatile (
        "mov ar%1 = %0;;\n"
        :
        : "r"(value), "i"(ar_num)
    );
}

uint64_t read_cr(uint32_t cr_num)
{
    uint64_t value;
    asm volatile (
        "mov %0 = cr%1;;\n"
        : "=r"(value)
        : "i"(cr_num)
    );
    return value;
}

void write_cr(uint32_t cr_num, uint64_t value)
{
    asm volatile (
        "mov cr%1 = %0;;\n"
        :
        : "r"(value), "i"(cr_num)
    );
}

void flush_rse()
{
    asm volatile (
        "flushrs;;\n"
        :
        :
        : "memory"
    );
}

uint64_t read_bsp()
{
    uint64_t bsp;
    asm volatile (
        "mov %0 = ar.bsp;;\n"
        : "=r"(bsp)
    );
    return bsp;
}

uint64_t read_bspstore()
{
    uint64_t bspstore;
    asm volatile (
        "mov %0 = ar.bspstore;;\n"
        : "=r"(bspstore)
    );
    return bspstore;
}

void write_bspstore(uint64_t value)
{
    asm volatile (
        "mov ar.bspstore = %0;;\n"
        :
        : "r"(value)
    );
}

uint64_t read_iva()
{
    return read_cr(2);  // CR.IVA is CR2
}

void write_iva(uint64_t value)
{
    write_cr(2, value);  // CR.IVA is CR2
}

void flush_cache()
{
    asm volatile (
        "fc.i %0;;\n"
        "sync.i;;\n"
        "srlz.i;;\n"
        :
        : "r"(0)
        : "memory"
    );
}

void sync_instruction_cache()
{
    asm volatile (
        "sync.i;;\n"
        "srlz.i;;\n"
        :
        :
        : "memory"
    );
}

void init()
{
    // TODO: Initialize IA-64-specific features
    // - Set up interrupt vector table (IVA)
    // - Configure region registers
    // - Set up translation registers
    // - Initialize RSE
}

} // namespace ia64
} // namespace arch
} // namespace kernel
