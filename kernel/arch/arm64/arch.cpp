// ARM64 (AArch64) Architecture Implementation
//
// Runs in kernel mode (EL1) on ARMv8-A processors.
// Supports QEMU virt machine and physical ARM64 servers/boards.
//
// ARM64 uses system registers accessed via MSR/MRS instructions.
// Memory-mapped I/O is used for peripherals (GIC, UART, etc).
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/arm64.h"
#include "include/arch/serial_console.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm64 {

// ================================================================
// Internal State
// ================================================================

static uint64_t s_gicd_base = 0;
static uint64_t s_gicc_base = 0;

// ================================================================
// Memory-Mapped I/O Helpers
// ================================================================

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    asm volatile ("str %w0, [%1]" : : "r"(value), "r"(addr) : "memory");
#endif
}

static inline uint32_t mmio_read32(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return 0;
#else
    uint32_t value;
    asm volatile ("ldr %w0, [%1]" : "=r"(value) : "r"(addr) : "memory");
    return value;
#endif
}

// ================================================================
// CPU Control
// ================================================================

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    // Loop forever with WFI (Wait For Interrupt)
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
    // Clear DAIF.I bit to unmask IRQs
    asm volatile ("msr daifclr, #2" ::: "memory");
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    // Set DAIF.I bit to mask IRQs
    asm volatile ("msr daifset, #2" ::: "memory");
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

// ================================================================
// Memory Barriers
// ================================================================

void isb()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("isb" ::: "memory");
#endif
}

void dsb()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("dsb sy" ::: "memory");
#endif
}

void dmb()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("dmb sy" ::: "memory");
#endif
}

// ================================================================
// Current Exception Level
// ================================================================

uint64_t read_current_el()
{
#if GXOS_MSVC_STUB
    return EL1 << 2;  // Pretend we're at EL1
#else
    uint64_t value;
    asm volatile ("mrs %0, CurrentEL" : "=r"(value));
    return value;
#endif
}

// ================================================================
// SCTLR_EL1 - System Control Register
// ================================================================

uint64_t read_sctlr_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, sctlr_el1" : "=r"(value));
    return value;
#endif
}

void write_sctlr_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr sctlr_el1, %0" : : "r"(value));
    isb();
#endif
}

// ================================================================
// TCR_EL1 - Translation Control Register
// ================================================================

uint64_t read_tcr_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, tcr_el1" : "=r"(value));
    return value;
#endif
}

void write_tcr_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr tcr_el1, %0" : : "r"(value));
    isb();
#endif
}

// ================================================================
// TTBR0_EL1 - Translation Table Base Register 0
// ================================================================

uint64_t read_ttbr0_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, ttbr0_el1" : "=r"(value));
    return value;
#endif
}

void write_ttbr0_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr ttbr0_el1, %0" : : "r"(value));
    isb();
#endif
}

// ================================================================
// TTBR1_EL1 - Translation Table Base Register 1
// ================================================================

uint64_t read_ttbr1_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, ttbr1_el1" : "=r"(value));
    return value;
#endif
}

void write_ttbr1_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr ttbr1_el1, %0" : : "r"(value));
    isb();
#endif
}

// ================================================================
// MAIR_EL1 - Memory Attribute Indirection Register
// ================================================================

uint64_t read_mair_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, mair_el1" : "=r"(value));
    return value;
#endif
}

void write_mair_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr mair_el1, %0" : : "r"(value));
    isb();
#endif
}

// ================================================================
// VBAR_EL1 - Vector Base Address Register
// ================================================================

uint64_t read_vbar_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, vbar_el1" : "=r"(value));
    return value;
#endif
}

void write_vbar_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr vbar_el1, %0" : : "r"(value));
    isb();
#endif
}

// ================================================================
// ESR_EL1 - Exception Syndrome Register
// ================================================================

uint64_t read_esr_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, esr_el1" : "=r"(value));
    return value;
#endif
}

// ================================================================
// FAR_EL1 - Fault Address Register
// ================================================================

uint64_t read_far_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, far_el1" : "=r"(value));
    return value;
#endif
}

// ================================================================
// ELR_EL1 - Exception Link Register
// ================================================================

uint64_t read_elr_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, elr_el1" : "=r"(value));
    return value;
#endif
}

void write_elr_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr elr_el1, %0" : : "r"(value));
#endif
}

// ================================================================
// SPSR_EL1 - Saved Program Status Register
// ================================================================

uint64_t read_spsr_el1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, spsr_el1" : "=r"(value));
    return value;
#endif
}

void write_spsr_el1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr spsr_el1, %0" : : "r"(value));
#endif
}

// ================================================================
// SP_EL0 - Stack Pointer (EL0)
// ================================================================

uint64_t read_sp_el0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, sp_el0" : "=r"(value));
    return value;
#endif
}

void write_sp_el0(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr sp_el0, %0" : : "r"(value));
#endif
}

// ================================================================
// MIDR_EL1 - Main ID Register
// ================================================================

uint64_t read_midr_el1()
{
#if GXOS_MSVC_STUB
    // Return a fake Cortex-A53 ID
    return (IMPL_ARM << 24) | (0xF << 16) | (0xD03 << 4) | 0x4;
#else
    uint64_t value;
    asm volatile ("mrs %0, midr_el1" : "=r"(value));
    return value;
#endif
}

// ================================================================
// MPIDR_EL1 - Multiprocessor Affinity Register
// ================================================================

uint64_t read_mpidr_el1()
{
#if GXOS_MSVC_STUB
    return 0x80000000;  // Uniprocessor
#else
    uint64_t value;
    asm volatile ("mrs %0, mpidr_el1" : "=r"(value));
    return value;
#endif
}

// ================================================================
// Timer System Registers
// ================================================================

uint64_t read_cntfrq_el0()
{
#if GXOS_MSVC_STUB
    return 62500000;  // 62.5 MHz (typical QEMU)
#else
    uint64_t value;
    asm volatile ("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
#endif
}

uint64_t read_cntpct_el0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, cntpct_el0" : "=r"(value));
    return value;
#endif
}

uint64_t read_cntp_ctl_el0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, cntp_ctl_el0" : "=r"(value));
    return value;
#endif
}

void write_cntp_ctl_el0(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr cntp_ctl_el0, %0" : : "r"(value));
    isb();
#endif
}

uint64_t read_cntp_cval_el0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, cntp_cval_el0" : "=r"(value));
    return value;
#endif
}

void write_cntp_cval_el0(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr cntp_cval_el0, %0" : : "r"(value));
#endif
}

uint64_t read_cntp_tval_el0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("mrs %0, cntp_tval_el0" : "=r"(value));
    return value;
#endif
}

void write_cntp_tval_el0(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("msr cntp_tval_el0, %0" : : "r"(value));
#endif
}

// ================================================================
// Cache Operations
// ================================================================

void invalidate_icache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile (
        "ic iallu\n"    // Invalidate all instruction caches to PoU
        "dsb ish\n"     // Ensure completion
        "isb\n"         // Synchronize context
        ::: "memory"
    );
#endif
}

void invalidate_dcache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Invalidate entire data cache (dangerous, use clean_invalidate for safety)
    // This is a simplified version; full implementation requires set/way iteration
    asm volatile ("dsb sy" ::: "memory");
#endif
}

void clean_dcache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Clean entire data cache to PoC
    // Simplified; full implementation requires set/way iteration
    asm volatile ("dsb sy" ::: "memory");
#endif
}

void clean_invalidate_dcache()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Clean and invalidate data cache
    // Simplified; full implementation requires set/way iteration
    asm volatile ("dsb sy" ::: "memory");
#endif
}

void invalidate_tlb()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile (
        "tlbi vmalle1\n"   // Invalidate all TLB entries at EL1
        "dsb ish\n"        // Ensure completion
        "isb\n"            // Synchronize context
        ::: "memory"
    );
#endif
}

void invalidate_tlb_entry(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
#else
    asm volatile (
        "tlbi vaae1, %0\n" // Invalidate TLB entry by VA
        "dsb ish\n"
        "isb\n"
        : : "r"(addr >> 12) : "memory"
    );
#endif
}

// ================================================================
// GIC (Generic Interrupt Controller) Operations
// ================================================================

void gic_init(uint64_t gicd_base, uint64_t gicc_base)
{
    s_gicd_base = gicd_base;
    s_gicc_base = gicc_base;
    
#if !GXOS_MSVC_STUB
    // Disable distributor
    mmio_write32(gicd_base + GICD_CTLR, 0);
    
    // Get number of interrupt lines
    uint32_t typer = mmio_read32(gicd_base + GICD_TYPER);
    uint32_t numLines = ((typer & 0x1F) + 1) * 32;
    
    // Disable all interrupts
    for (uint32_t i = 0; i < numLines / 32; i++) {
        mmio_write32(gicd_base + GICD_ICENABLER + i * 4, 0xFFFFFFFF);
    }
    
    // Clear all pending interrupts
    for (uint32_t i = 0; i < numLines / 32; i++) {
        mmio_write32(gicd_base + GICD_ICPENDR + i * 4, 0xFFFFFFFF);
    }
    
    // Set all interrupts to group 0
    for (uint32_t i = 0; i < numLines / 32; i++) {
        mmio_write32(gicd_base + GICD_IGROUPR + i * 4, 0);
    }
    
    // Set default priority (lower = higher priority)
    for (uint32_t i = 0; i < numLines / 4; i++) {
        mmio_write32(gicd_base + GICD_IPRIORITYR + i * 4, 0xA0A0A0A0);
    }
    
    // Target all SPIs to CPU 0
    for (uint32_t i = 8; i < numLines / 4; i++) {
        mmio_write32(gicd_base + GICD_ITARGETSR + i * 4, 0x01010101);
    }
    
    // Configure all SPIs as level-triggered
    for (uint32_t i = 2; i < numLines / 16; i++) {
        mmio_write32(gicd_base + GICD_ICFGR + i * 4, 0);
    }
    
    // Enable distributor
    mmio_write32(gicd_base + GICD_CTLR, 1);
    
    // CPU Interface setup
    // Set priority mask (allow all priorities)
    mmio_write32(gicc_base + GICC_PMR, 0xFF);
    
    // Enable CPU interface
    mmio_write32(gicc_base + GICC_CTLR, 1);
#endif
}

void gic_enable_irq(uint32_t irq)
{
#if !GXOS_MSVC_STUB
    if (s_gicd_base == 0) return;
    
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    mmio_write32(s_gicd_base + GICD_ISENABLER + reg * 4, 1U << bit);
#else
    (void)irq;
#endif
}

void gic_disable_irq(uint32_t irq)
{
#if !GXOS_MSVC_STUB
    if (s_gicd_base == 0) return;
    
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    mmio_write32(s_gicd_base + GICD_ICENABLER + reg * 4, 1U << bit);
#else
    (void)irq;
#endif
}

void gic_set_priority(uint32_t irq, uint8_t priority)
{
#if !GXOS_MSVC_STUB
    if (s_gicd_base == 0) return;
    
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    
    uint32_t value = mmio_read32(s_gicd_base + GICD_IPRIORITYR + reg * 4);
    value &= ~(0xFF << shift);
    value |= (priority << shift);
    mmio_write32(s_gicd_base + GICD_IPRIORITYR + reg * 4, value);
#else
    (void)irq;
    (void)priority;
#endif
}

uint32_t gic_acknowledge_irq()
{
#if GXOS_MSVC_STUB
    return GIC_INTID_SPURIOUS;
#else
    if (s_gicc_base == 0) return GIC_INTID_SPURIOUS;
    return mmio_read32(s_gicc_base + GICC_IAR) & 0x3FF;
#endif
}

void gic_end_irq(uint32_t irq)
{
#if !GXOS_MSVC_STUB
    if (s_gicc_base == 0) return;
    mmio_write32(s_gicc_base + GICC_EOIR, irq);
#else
    (void)irq;
#endif
}

void gic_send_sgi(uint32_t sgi_id, uint32_t target_list)
{
#if !GXOS_MSVC_STUB
    if (s_gicd_base == 0) return;
    
    // SGI ID in bits [3:0], target list filter in bits [25:24], targets in bits [23:16]
    uint32_t value = (sgi_id & 0xF) | ((target_list & 0xFF) << 16);
    mmio_write32(s_gicd_base + GICD_SGIR, value);
#else
    (void)sgi_id;
    (void)target_list;
#endif
}

// ================================================================
// CPU Detection
// ================================================================

const char* get_implementer_name(uint8_t impl)
{
    switch (impl) {
        case IMPL_ARM:      return "ARM Limited";
        case IMPL_BROADCOM: return "Broadcom";
        case IMPL_CAVIUM:   return "Cavium";
        case IMPL_FUJITSU:  return "Fujitsu";
        case IMPL_INFINEON: return "Infineon";
        case IMPL_NVIDIA:   return "NVIDIA";
        case IMPL_APM:      return "Applied Micro";
        case IMPL_QUALCOMM: return "Qualcomm";
        case IMPL_SAMSUNG:  return "Samsung";
        case IMPL_MARVELL:  return "Marvell";
        case IMPL_APPLE:    return "Apple";
        case IMPL_INTEL:    return "Intel";
        case IMPL_AMPERE:   return "Ampere";
        default:            return "Unknown";
    }
}

void detect_cpu(CpuInfo* info)
{
    if (!info) return;
    
    uint64_t midr = read_midr_el1();
    uint64_t mpidr = read_mpidr_el1();
    
    info->midr = midr;
    info->implementer = static_cast<uint8_t>((midr & MIDR_IMPL_MASK) >> MIDR_IMPL_SHIFT);
    info->variant = static_cast<uint8_t>((midr & MIDR_VARIANT_MASK) >> MIDR_VARIANT_SHIFT);
    info->architecture = static_cast<uint8_t>((midr & MIDR_ARCH_MASK) >> MIDR_ARCH_SHIFT);
    info->partNumber = static_cast<uint16_t>((midr & MIDR_PARTNUM_MASK) >> MIDR_PARTNUM_SHIFT);
    info->revision = static_cast<uint8_t>(midr & MIDR_REV_MASK);
    
    info->affinity = static_cast<uint32_t>(mpidr & 0xFFFFFF);
    info->isMulticore = (mpidr & (1ULL << 30)) == 0;  // U bit = 0 means multiprocessor
}

// ================================================================
// MMU Initialization
// ================================================================

void init_mmu()
{
    // Build MAIR with memory attributes
    // Index 0: Device memory (nGnRnE)
    // Index 1: Normal non-cacheable
    // Index 2: Normal write-through
    // Index 3: Normal write-back
    uint64_t mair = 
        (MAIR_DEVICE_nGnRnE << (MAIR_IDX_DEVICE * 8)) |
        (MAIR_NORMAL_NC << (MAIR_IDX_NORMAL_NC * 8)) |
        (MAIR_NORMAL_WT << (MAIR_IDX_NORMAL_WT * 8)) |
        (MAIR_NORMAL_WB << (MAIR_IDX_NORMAL_WB * 8));
    
    write_mair_el1(mair);
    
    // Configure TCR for 48-bit addresses, 4KB granule
    // T0SZ = 16 (48-bit VA for TTBR0)
    // T1SZ = 16 (48-bit VA for TTBR1)
    // Inner/outer cacheable, inner shareable
    uint64_t tcr = 
        (16ULL << TCR_T0SZ_SHIFT) |           // 48-bit VA for TTBR0
        (16ULL << TCR_T1SZ_SHIFT) |           // 48-bit VA for TTBR1
        TCR_TG0_4K |                          // 4KB granule for TTBR0
        TCR_TG1_4K |                          // 4KB granule for TTBR1
        TCR_IRGN0_WBWA | TCR_ORGN0_WBWA |     // Cacheable for TTBR0
        TCR_IRGN1_WBWA | TCR_ORGN1_WBWA |     // Cacheable for TTBR1
        TCR_SH0_IS | TCR_SH1_IS |             // Inner shareable
        TCR_IPS_48BIT;                        // 48-bit PA
    
    write_tcr_el1(tcr);
    
    // Page tables should be set up by boot code before calling this
    // write_ttbr0_el1(page_table_base);
    // write_ttbr1_el1(kernel_page_table_base);
    
    // Enable MMU and caches
    uint64_t sctlr = read_sctlr_el1();
    sctlr |= SCTLR_M;   // MMU enable
    sctlr |= SCTLR_C;   // Data cache enable
    sctlr |= SCTLR_I;   // Instruction cache enable
    sctlr |= SCTLR_RES1; // Required reserved bits
    write_sctlr_el1(sctlr);
    
    // Ensure all TLB entries are invalidated
    invalidate_tlb();
}

// ================================================================
// Timer Initialization
// ================================================================

void init_timer()
{
    // Disable timer first
    write_cntp_ctl_el0(0);
    
    // Get frequency
    uint64_t freq = read_cntfrq_el0();
    
    // Set timer to fire in 10ms (100 Hz tick)
    uint64_t interval = freq / 100;
    
    // Set compare value
    uint64_t current = read_cntpct_el0();
    write_cntp_cval_el0(current + interval);
    
    // Enable timer
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);
}

// ================================================================
// GIC Initialization
// ================================================================

void init_gic()
{
    // QEMU virt machine GIC addresses
    // GICv2: GICD at 0x08000000, GICC at 0x08010000
    const uint64_t VIRT_GICD_BASE = 0x08000000;
    const uint64_t VIRT_GICC_BASE = 0x08010000;
    
    gic_init(VIRT_GICD_BASE, VIRT_GICC_BASE);
    
    // Enable timer interrupt (INTID 30 for physical timer on virt)
    gic_enable_irq(30);
}

// ================================================================
// Early Initialization (before MMU)
// ================================================================

void early_init()
{
    // Disable interrupts during early init
    disable_interrupts();
    
    // Initialize serial console for debug output
    serial_console::init();
}

// ================================================================
// Full Initialization
// ================================================================

void init()
{
    // Initialize GIC
    init_gic();
    
    // Initialize timer
    init_timer();
    
    // Enable interrupts
    enable_interrupts();
}

} // namespace arm64
} // namespace arch
} // namespace kernel
