//
// PowerPC64 Architecture Implementation
//
// Runs on QEMU pseries machine in hypervisor mode.
// Implements CPU control, interrupt handling, and MMU initialization.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/ppc64.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace ppc64 {

// ================================================================
// CPU control
// ================================================================

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    while (1) {
        // PowerPC wait instruction - places CPU in low-power state
        // until an interrupt occurs
        asm volatile ("wait" ::: "memory");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    // Set MSR[EE] (External Interrupt Enable) bit
    // EE is bit 48 (counting from MSB=0) = bit 15 from LSB
    uint64_t msr = read_msr();
    msr |= (1ULL << 15);  // Set EE bit
    write_msr(msr);
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    // Clear MSR[EE] (External Interrupt Enable) bit
    uint64_t msr = read_msr();
    msr &= ~(1ULL << 15);  // Clear EE bit
    write_msr(msr);
#endif
}

void wait_for_interrupt()
{
#if GXOS_MSVC_STUB
    __nop();
#else
    // PowerPC wait instruction
    asm volatile ("wait" ::: "memory");
#endif
}

// ================================================================
// Machine State Register (MSR)
// ================================================================

uint64_t read_msr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfmsr %0" : "=r"(value));
    return value;
#endif
}

void write_msr(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtmsr %0" : : "r"(value) : "memory");
    asm volatile ("isync" ::: "memory");
#endif
}

// ================================================================
// Save/Restore Registers
// ================================================================

uint64_t read_srr0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfsrr0 %0" : "=r"(value));
    return value;
#endif
}

void write_srr0(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtsrr0 %0" : : "r"(value));
#endif
}

uint64_t read_srr1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfsrr1 %0" : "=r"(value));
    return value;
#endif
}

void write_srr1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtsrr1 %0" : : "r"(value));
#endif
}

// ================================================================
// Decrementer (Timer)
// ================================================================

uint32_t read_dec()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t value;
    asm volatile ("mfdec %0" : "=r"(value));
    return value;
#endif
}

void write_dec(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtdec %0" : : "r"(value));
#endif
}

// ================================================================
// Processor ID Register
// ================================================================

uint32_t read_pir()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t value;
    // PIR is SPR 1023
    asm volatile ("mfspr %0, 1023" : "=r"(value));
    return value;
#endif
}

// ================================================================
// SPRG Registers (software use)
// ================================================================

uint64_t read_sprg0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfsprg0 %0" : "=r"(value));
    return value;
#endif
}

void write_sprg0(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtsprg0 %0" : : "r"(value));
#endif
}

uint64_t read_sprg1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfsprg1 %0" : "=r"(value));
    return value;
#endif
}

void write_sprg1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtsprg1 %0" : : "r"(value));
#endif
}

uint64_t read_sprg2()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfsprg2 %0" : "=r"(value));
    return value;
#endif
}

void write_sprg2(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtsprg2 %0" : : "r"(value));
#endif
}

uint64_t read_sprg3()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfsprg3 %0" : "=r"(value));
    return value;
#endif
}

void write_sprg3(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtsprg3 %0" : : "r"(value));
#endif
}

// ================================================================
// Link Register and Count Register
// ================================================================

uint64_t read_lr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mflr %0" : "=r"(value));
    return value;
#endif
}

void write_lr(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtlr %0" : : "r"(value));
#endif
}

uint64_t read_ctr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfctr %0" : "=r"(value));
    return value;
#endif
}

void write_ctr(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtctr %0" : : "r"(value));
#endif
}

// ================================================================
// Condition Register and XER
// ================================================================

uint64_t read_cr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfcr %0" : "=r"(value));
    return value;
#endif
}

void write_cr(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtcr %0" : : "r"(value));
#endif
}

uint64_t read_xer()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfxer %0" : "=r"(value));
    return value;
#endif
}

void write_xer(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtxer %0" : : "r"(value));
#endif
}

// ================================================================
// Time Base Register
// ================================================================

uint64_t read_tb()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mftb %0" : "=r"(value));
    return value;
#endif
}

// ================================================================
// SDR1 (Page Table Base)
// ================================================================

uint64_t read_sdr1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mfsdr1 %0" : "=r"(value));
    return value;
#endif
}

void write_sdr1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("mtsdr1 %0" : : "r"(value));
    asm volatile ("isync" ::: "memory");
#endif
}

// ================================================================
// Synchronization / Barriers
// ================================================================

void sync()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("sync" ::: "memory");
#endif
}

void isync()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("isync" ::: "memory");
#endif
}

void eieio()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("eieio" ::: "memory");
#endif
}

// ================================================================
// Interrupt handling
// ================================================================

// Interrupt handler table
static interrupt_handler_t s_interrupt_handlers[MAX_INTERRUPT_HANDLERS] = { nullptr };

void interrupt_init()
{
    // Initialize interrupt handler table
    for (int i = 0; i < MAX_INTERRUPT_HANDLERS; ++i) {
        s_interrupt_handlers[i] = nullptr;
    }
    
    // Disable interrupts during setup
    disable_interrupts();
    
#if !GXOS_MSVC_STUB
    // Enable Machine Check (ME) in MSR
    uint64_t msr = read_msr();
    msr |= (1ULL << 12);  // ME bit
    write_msr(msr);
#endif
}

void interrupt_register_handler(int vector, interrupt_handler_t handler)
{
    if (vector >= 0 && vector < MAX_INTERRUPT_HANDLERS) {
        s_interrupt_handlers[vector] = handler;
    }
}

// Called from assembly interrupt handler
extern "C" void ppc64_interrupt_dispatch(int vector)
{
    if (vector >= 0 && vector < MAX_INTERRUPT_HANDLERS) {
        interrupt_handler_t handler = s_interrupt_handlers[vector];
        if (handler != nullptr) {
            handler();
        }
    }
}

// ================================================================
// MMU functions (stub implementation)
// ================================================================

// Simple page table structure for radix MMU
// PowerPC64 Book III-S uses either hashed page tables (HPT) or
// radix tree page tables. We use a simplified model here.

void mmu_init()
{
    // Stub: MMU initialization
    // For QEMU pseries, the MMU may already be configured by firmware
    // A full implementation would:
    // 1. Set up page table base in SDR1 (for HPT) or PTCR (for radix)
    // 2. Configure segment registers or partition table
    // 3. Enable IR and DR in MSR
    
#if !GXOS_MSVC_STUB
    // For now, leave MMU in whatever state firmware set up
    sync();
    isync();
#endif
}

void map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags)
{
    (void)virtual_addr;
    (void)physical_addr;
    (void)flags;
    
    // Stub: Map a virtual page to a physical page
    // A full implementation would:
    // 1. Find or allocate page table entry
    // 2. Set up PTE with physical address and flags
    // 3. Invalidate TLB for the virtual address
    
#if !GXOS_MSVC_STUB
    sync();
#endif
}

void unmap_page(uint64_t virtual_addr)
{
    (void)virtual_addr;
    
    // Stub: Unmap a virtual page
    // A full implementation would:
    // 1. Find and clear page table entry
    // 2. Invalidate TLB for the virtual address
    
#if !GXOS_MSVC_STUB
    sync();
#endif
}

// ================================================================
// CPU initialization
// ================================================================

void cpu_init()
{
    // Initialize CPU state
    
#if !GXOS_MSVC_STUB
    // Disable interrupts during initialization
    disable_interrupts();
    
    // Initialize SPRG registers for kernel use
    write_sprg0(0);  // Reserved for kernel stack pointer
    write_sprg1(0);  // Reserved for current thread
    write_sprg2(0);  // Reserved
    write_sprg3(0);  // Reserved
    
    // Initialize decrementer (disable for now)
    write_dec(0x7FFFFFFF);
    
    // Synchronize
    sync();
    isync();
#endif
}

uint64_t cpu_get_id()
{
    return static_cast<uint64_t>(read_pir());
}

void cpu_enable_interrupts()
{
    enable_interrupts();
}

void cpu_disable_interrupts()
{
    disable_interrupts();
}

// ================================================================
// Architecture initialization
// ================================================================

void init()
{
    // Initialize CPU
    cpu_init();
    
    // Initialize interrupt handling
    interrupt_init();
    
    // Initialize MMU
    mmu_init();
}

} // namespace ppc64
} // namespace arch
} // namespace kernel
