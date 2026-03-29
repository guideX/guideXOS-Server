//
// Itanium (IA-64) Architecture Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/ia64.h"
#include "include/arch/ski_console.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace ia64 {

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    while (1) {
        asm volatile ("hint @pause");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    asm volatile (
        "ssm psr.i;;\n"
        "srlz.d;;\n"
        :
        :
        : "memory"
    );
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    asm volatile (
        "rsm psr.i;;\n"
        "srlz.d;;\n"
        :
        :
        : "memory"
    );
#endif
}

void break_instruction(uint64_t imm)
{
#if GXOS_MSVC_STUB
    (void)imm;
    __debugbreak();
#else
    asm volatile ("break.i %0" : : "i"(imm));
#endif
}

uint64_t read_psr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t psr;
    asm volatile (
        "mov %0 = psr;;\n"
        : "=r"(psr)
    );
    return psr;
#endif
}

void write_psr(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "mov psr.l = %0;;\n"
        "srlz.d;;\n"
        :
        : "r"(value)
        : "memory"
    );
#endif
}

uint64_t read_ar(uint32_t ar_num)
{
#if GXOS_MSVC_STUB
    (void)ar_num; return 0;
#else
    uint64_t value;
    asm volatile (
        "mov %0 = ar%1;;\n"
        : "=r"(value)
        : "i"(ar_num)
    );
    return value;
#endif
}

void write_ar(uint32_t ar_num, uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)ar_num; (void)value;
#else
    asm volatile (
        "mov ar%1 = %0;;\n"
        :
        : "r"(value), "i"(ar_num)
    );
#endif
}

uint64_t read_cr(uint32_t cr_num)
{
#if GXOS_MSVC_STUB
    (void)cr_num; return 0;
#else
    uint64_t value;
    asm volatile (
        "mov %0 = cr%1;;\n"
        : "=r"(value)
        : "i"(cr_num)
    );
    return value;
#endif
}

void write_cr(uint32_t cr_num, uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)cr_num; (void)value;
#else
    asm volatile (
        "mov cr%1 = %0;;\n"
        :
        : "r"(value), "i"(cr_num)
    );
#endif
}

void flush_rse()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile (
        "flushrs;;\n"
        :
        :
        : "memory"
    );
#endif
}

uint64_t read_bsp()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t bsp;
    asm volatile (
        "mov %0 = ar.bsp;;\n"
        : "=r"(bsp)
    );
    return bsp;
#endif
}

uint64_t read_bspstore()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t bspstore;
    asm volatile (
        "mov %0 = ar.bspstore;;\n"
        : "=r"(bspstore)
    );
    return bspstore;
#endif
}

void write_bspstore(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "mov ar.bspstore = %0;;\n"
        :
        : "r"(value)
    );
#endif
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
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile (
        "fc.i %0;;\n"
        "sync.i;;\n"
        "srlz.i;;\n"
        :
        : "r"(0)
        : "memory"
    );
#endif
}

void sync_instruction_cache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile (
        "sync.i;;\n"
        "srlz.i;;\n"
        :
        :
        : "memory"
    );
#endif
}

void init()
{
    // IVT is already installed by boot.s (cr.iva set before kernel_main).
    // RSE backing store is also configured in boot.s.

    // Initialize ski simulator console for early output
    ski_console::init();

    // Print boot banner via firmware console
    ski_console::puts("guideXOS IA-64 kernel init\r\n");

    // Verify IVA is set (diagnostic)
    uint64_t iva = read_iva();
    ski_console::puts("  IVA = ");
    ski_console::put_hex(iva);
    ski_console::puts("\r\n");

    // Verify RSE backing store pointer (diagnostic)
    uint64_t bsp = read_bsp();
    ski_console::puts("  BSP = ");
    ski_console::put_hex(bsp);
    ski_console::puts("\r\n");

    // Set DCR (Default Control Register, CR0) to safe defaults:
    // bit 0: defer data-TLB misses to VHPT walker (0 = use IVT handler)
    write_cr(0, 0);

    // Enable interrupt collection (psr.ic) so the IVT is active,
    // but keep external interrupts masked (psr.i = 0) until the
    // kernel explicitly calls enable_interrupts().
#if !GXOS_MSVC_STUB
    asm volatile (
        "ssm psr.ic;;\n"
        "srlz.d;;\n"
        :
        :
        : "memory"
    );
#endif

    ski_console::puts("  Interrupt collection enabled\r\n");
    ski_console::puts("IA-64 arch init complete\r\n");
}

} // namespace ia64
} // namespace arch
} // namespace kernel
