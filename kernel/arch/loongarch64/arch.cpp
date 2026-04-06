//
// LoongArch 64-bit (LA64) Architecture Implementation
//
// Provides low-level CPU control functions for the LoongArch64
// architecture including CSR access, interrupt control, memory
// barriers, and TLB management.
//
// LoongArch uses dedicated instructions for CSR access:
//   csrrd  rd, csr_num    - Read CSR
//   csrwr  rd, csr_num    - Write CSR (returns old value)
//   csrxchg rd, rj, csr_num - Exchange with mask
//
// Platform: QEMU loongarch64-virt or Loongson 3A5000/3A6000 hardware.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/loongarch64.h"
#include "include/arch/loongarch_console.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace loongarch64 {

// ================================================================
// CPU control
// ================================================================

void halt()
{
#if GXOS_MSVC_STUB
    // MSVC host build - spin loop for simulation
    while (true) { __nop(); }
#else
    // LoongArch uses 'idle' instruction to enter low-power wait state
    // The CPU will wake on any enabled interrupt
    while (1) {
        // idle instruction - wait for interrupt
        // LoongArch encoding: 0x06488000
        asm volatile (".word 0x06488000" ::: "memory");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    // Set CRMD.IE (bit 2) to enable interrupts
    // csrxchg with mask to atomically set the bit
    uint64_t val = CRMD_IE;
    uint64_t mask = CRMD_IE;
    asm volatile (
        "csrxchg %0, %1, %2"
        : "+r"(val)
        : "r"(mask), "i"(CSR_CRMD)
        : "memory"
    );
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    // Clear CRMD.IE (bit 2) to disable interrupts
    uint64_t val = 0;
    uint64_t mask = CRMD_IE;
    asm volatile (
        "csrxchg %0, %1, %2"
        : "+r"(val)
        : "r"(mask), "i"(CSR_CRMD)
        : "memory"
    );
#endif
}

void wait_for_interrupt()
{
#if GXOS_MSVC_STUB
    __nop();
#else
    // idle instruction - enter low-power state until interrupt
    asm volatile (".word 0x06488000" ::: "memory");
#endif
}

// ================================================================
// CSR access functions
//
// LoongArch CSR instructions:
//   csrrd  rd, csr_num     - Read CSR into rd
//   csrwr  rd, csr_num     - Write rd to CSR, return old value
//   csrxchg rd, rj, csr_num - Exchange: CSR = (CSR & ~rj) | (rd & rj)
//
// Note: GCC inline assembly for LoongArch CSR access requires
// specific handling. For CSRs that need runtime index selection,
// we provide individual accessor functions for common CSRs.
// ================================================================

uint64_t read_csr(uint32_t csr)
{
#if GXOS_MSVC_STUB
    (void)csr;
    return 0;
#else
    // Generic CSR read - uses a switch for compile-time CSR selection
    // (LoongArch CSR instructions require immediate operands)
    // For runtime flexibility, add cases as needed
    uint64_t value = 0;
    switch (csr) {
        case CSR_CRMD:   asm volatile ("csrrd %0, 0x00" : "=r"(value)); break;
        case CSR_PRMD:   asm volatile ("csrrd %0, 0x01" : "=r"(value)); break;
        case CSR_ESTAT:  asm volatile ("csrrd %0, 0x05" : "=r"(value)); break;
        case CSR_ERA:    asm volatile ("csrrd %0, 0x06" : "=r"(value)); break;
        case CSR_BADV:   asm volatile ("csrrd %0, 0x07" : "=r"(value)); break;
        case CSR_EENTRY: asm volatile ("csrrd %0, 0x0C" : "=r"(value)); break;
        case CSR_CPUID:  asm volatile ("csrrd %0, 0x20" : "=r"(value)); break;
        case CSR_TID:    asm volatile ("csrrd %0, 0x40" : "=r"(value)); break;
        case CSR_TCFG:   asm volatile ("csrrd %0, 0x41" : "=r"(value)); break;
        case CSR_TVAL:   asm volatile ("csrrd %0, 0x42" : "=r"(value)); break;
        default: break;
    }
    return value;
#endif
}

void write_csr(uint32_t csr, uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)csr;
    (void)value;
#else
    switch (csr) {
        case CSR_CRMD:   asm volatile ("csrwr %0, 0x00" : "+r"(value)); break;
        case CSR_PRMD:   asm volatile ("csrwr %0, 0x01" : "+r"(value)); break;
        case CSR_ERA:    asm volatile ("csrwr %0, 0x06" : "+r"(value)); break;
        case CSR_EENTRY: asm volatile ("csrwr %0, 0x0C" : "+r"(value)); break;
        case CSR_TID:    asm volatile ("csrwr %0, 0x40" : "+r"(value)); break;
        case CSR_TCFG:   asm volatile ("csrwr %0, 0x41" : "+r"(value)); break;
        case CSR_TICLR:  asm volatile ("csrwr %0, 0x44" : "+r"(value)); break;
        default: break;
    }
#endif
}

uint64_t xchg_csr(uint32_t csr, uint64_t value, uint64_t mask)
{
#if GXOS_MSVC_STUB
    (void)csr;
    (void)value;
    (void)mask;
    return 0;
#else
    uint64_t old = value;
    switch (csr) {
        case CSR_CRMD:
            asm volatile ("csrxchg %0, %1, 0x00" : "+r"(old) : "r"(mask));
            break;
        case CSR_PRMD:
            asm volatile ("csrxchg %0, %1, 0x01" : "+r"(old) : "r"(mask));
            break;
        case CSR_ECFG:
            asm volatile ("csrxchg %0, %1, 0x04" : "+r"(old) : "r"(mask));
            break;
        default:
            old = 0;
            break;
    }
    return old;
#endif
}

// ================================================================
// Convenience functions for common CSRs
// ================================================================

uint64_t read_crmd()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x00" : "=r"(value));
    return value;
#endif
}

void write_crmd(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrwr %0, 0x00" : "+r"(value));
#endif
}

uint64_t read_prmd()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x01" : "=r"(value));
    return value;
#endif
}

void write_prmd(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrwr %0, 0x01" : "+r"(value));
#endif
}

uint64_t read_estat()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x05" : "=r"(value));
    return value;
#endif
}

uint64_t read_era()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x06" : "=r"(value));
    return value;
#endif
}

void write_era(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrwr %0, 0x06" : "+r"(value));
#endif
}

uint64_t read_badv()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x07" : "=r"(value));
    return value;
#endif
}

uint64_t read_eentry()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x0C" : "=r"(value));
    return value;
#endif
}

void write_eentry(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrwr %0, 0x0C" : "+r"(value));
#endif
}

uint64_t read_cpuid()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x20" : "=r"(value));
    return value;
#endif
}

uint64_t read_tid()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x40" : "=r"(value));
    return value;
#endif
}

void write_tid(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrwr %0, 0x40" : "+r"(value));
#endif
}

uint64_t read_tcfg()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x41" : "=r"(value));
    return value;
#endif
}

void write_tcfg(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("csrwr %0, 0x41" : "+r"(value));
#endif
}

uint64_t read_tval()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("csrrd %0, 0x42" : "=r"(value));
    return value;
#endif
}

void clear_timer_interrupt()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    uint64_t val = 1;  // Write 1 to clear timer interrupt
    asm volatile ("csrwr %0, 0x44" : "+r"(val));
#endif
}

// ================================================================
// Memory barrier / fence operations
// ================================================================

void dbar()
{
#if GXOS_MSVC_STUB
    _ReadWriteBarrier();
#else
    // dbar 0 - full memory barrier
    // LoongArch encoding: dbar hint (hint=0 for full barrier)
    asm volatile ("dbar 0" ::: "memory");
#endif
}

void ibar()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC
#else
    // ibar 0 - instruction barrier (sync instruction fetch)
    asm volatile ("ibar 0" ::: "memory");
#endif
}

// ================================================================
// TLB operations
// ================================================================

void invtlb_all()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC
#else
    // invtlb op, rj, rk - Invalidate TLB entries
    // op=0: Invalidate all entries
    asm volatile (
        "invtlb 0, $r0, $r0"
        ::: "memory"
    );
#endif
}

void invtlb_asid(uint32_t asid)
{
#if GXOS_MSVC_STUB
    (void)asid;
#else
    // op=4: Invalidate all entries matching ASID in rj
    register uint64_t r_asid asm("$r4") = asid;
    asm volatile (
        "invtlb 4, %0, $r0"
        :
        : "r"(r_asid)
        : "memory"
    );
#endif
}

void invtlb_va(uint64_t va, uint32_t asid)
{
#if GXOS_MSVC_STUB
    (void)va;
    (void)asid;
#else
    // op=5: Invalidate entry matching VA in rk and ASID in rj
    register uint64_t r_asid asm("$r4") = asid;
    register uint64_t r_va asm("$r5") = va;
    asm volatile (
        "invtlb 5, %0, %1"
        :
        : "r"(r_asid), "r"(r_va)
        : "memory"
    );
#endif
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    // Initialize LoongArch64 architecture-specific features
    //
    // This function is called early in kernel boot to set up:
    // 1. Exception entry point
    // 2. Timer configuration
    // 3. Interrupt controller (EXTIOI or legacy LIO)
    // 4. Memory management (TLB, page tables)
    //
    // Platform-specific initialization (QEMU virt vs real hardware)
    // should be done here based on device tree or ACPI tables.
    
    loongarch_console::init();
    
    // TODO: Set up exception entry point
    // write_eentry((uint64_t)exception_entry);
    
    // TODO: Configure timer for scheduling
    // write_tcfg(timer_config);
    
    // TODO: Initialize interrupt controller
    // On QEMU virt: Extended I/O Interrupt Controller (EXTIOI)
    // On real hardware: May use legacy LIO or EIOINTC
    
    // TODO: Set up direct mapping windows (DMW) for kernel memory
    // DMW provides direct translation without TLB for kernel space
    
    loongarch_console::puts("[LoongArch64] Architecture initialized\n");
}

} // namespace loongarch64
} // namespace arch
} // namespace kernel
