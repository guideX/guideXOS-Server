//
// RISC-V 64-bit (RV64) Architecture Implementation
//
// Runs in S-mode on top of OpenSBI.  CSR access uses the
// supervisor-level registers (sstatus, sie, stvec, etc.).
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/riscv64.h"
#include "include/arch/sbi_console.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace riscv64 {

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    while (1) {
        asm volatile ("wfi");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    asm volatile ("csrsi sstatus, 0x2");  // Set SIE bit (bit 1)
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    asm volatile ("csrci sstatus, 0x2");  // Clear SIE bit (bit 1)
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

uint64_t read_sstatus()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, sstatus" : "=r"(value));
    return value;
#endif
}

void write_sstatus(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrw sstatus, %0" : : "r"(value));
#endif
}

uint64_t read_sie()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, sie" : "=r"(value));
    return value;
#endif
}

void write_sie(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrw sie, %0" : : "r"(value));
#endif
}

uint64_t read_sip()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, sip" : "=r"(value));
    return value;
#endif
}

uint64_t read_stvec()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, stvec" : "=r"(value));
    return value;
#endif
}

void write_stvec(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrw stvec, %0" : : "r"(value));
#endif
}

uint64_t read_sepc()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, sepc" : "=r"(value));
    return value;
#endif
}

void write_sepc(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrw sepc, %0" : : "r"(value));
#endif
}

uint64_t read_scause()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, scause" : "=r"(value));
    return value;
#endif
}

uint64_t read_stval()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, stval" : "=r"(value));
    return value;
#endif
}

uint64_t read_satp()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, satp" : "=r"(value));
    return value;
#endif
}

void write_satp(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrw satp, %0" : : "r"(value));
    sfence_vma();
#endif
}

uint64_t read_time()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrr %0, time" : "=r"(value));
    return value;
#endif
}

void fence()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("fence" ::: "memory");
#endif
}

void fence_i()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("fence.i" ::: "memory");
#endif
}

void sfence_vma()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("sfence.vma" ::: "memory");
#endif
}

void init()
{
    // Initialize SBI console for early output
    sbi_console::init();

    // Print boot banner via SBI console
    sbi_console::puts("guideXOS RISC-V 64 kernel init\r\n");

    // Read and display stvec
    uint64_t stvec = read_stvec();
    sbi_console::puts("  stvec = ");
    sbi_console::put_hex(stvec);
    sbi_console::puts("\r\n");

    // Read and display sstatus
    uint64_t sstatus = read_sstatus();
    sbi_console::puts("  sstatus = ");
    sbi_console::put_hex(sstatus);
    sbi_console::puts("\r\n");

    sbi_console::puts("RISC-V 64 arch init complete\r\n");
}

} // namespace riscv64
} // namespace arch
} // namespace kernel
