// ARM64 (AArch64) Architecture-Specific Code
//
// Targets ARMv8-A processors running in AArch64 state.
// Supports QEMU virt machine and physical ARM64 servers.
//
// ARM64 System Registers used:
//   - SCTLR_EL1   - System Control Register
//   - TCR_EL1     - Translation Control Register
//   - TTBR0_EL1   - Translation Table Base Register 0
//   - TTBR1_EL1   - Translation Table Base Register 1
//   - MAIR_EL1    - Memory Attribute Indirection Register
//   - VBAR_EL1    - Vector Base Address Register
//   - ESR_EL1     - Exception Syndrome Register
//   - FAR_EL1     - Fault Address Register
//   - SPSR_EL1    - Saved Program Status Register
//   - ELR_EL1     - Exception Link Register
//   - SP_EL0      - Stack Pointer (EL0)
//   - CurrentEL   - Current Exception Level
//   - DAIF        - Interrupt Mask Bits
//   - MPIDR_EL1   - Multiprocessor Affinity Register
//   - MIDR_EL1    - Main ID Register
//   - CNTFRQ_EL0  - Counter-timer Frequency
//   - CNTPCT_EL0  - Counter-timer Physical Count
//   - CNTP_CTL_EL0 - Counter-timer Physical Timer Control
//   - CNTP_CVAL_EL0 - Counter-timer Physical Timer Compare Value
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm64 {

// ================================================================
// Exception Levels
// ================================================================

static const uint64_t EL0 = 0;  // User/Application
static const uint64_t EL1 = 1;  // OS Kernel
static const uint64_t EL2 = 2;  // Hypervisor
static const uint64_t EL3 = 3;  // Secure Monitor

// CurrentEL register bits
static const uint64_t CURRENTEL_EL_MASK  = (3ULL << 2);
static const uint64_t CURRENTEL_EL_SHIFT = 2;

// ================================================================
// PSTATE / SPSR bits
// ================================================================

// Condition flags
static const uint64_t PSTATE_N  = (1ULL << 31);  // Negative
static const uint64_t PSTATE_Z  = (1ULL << 30);  // Zero
static const uint64_t PSTATE_C  = (1ULL << 29);  // Carry
static const uint64_t PSTATE_V  = (1ULL << 28);  // Overflow

// Exception masking bits (DAIF)
static const uint64_t PSTATE_D  = (1ULL << 9);   // Debug exception mask
static const uint64_t PSTATE_A  = (1ULL << 8);   // SError interrupt mask
static const uint64_t PSTATE_I  = (1ULL << 7);   // IRQ mask
static const uint64_t PSTATE_F  = (1ULL << 6);   // FIQ mask

// Execution state
static const uint64_t PSTATE_nRW = (1ULL << 4);  // 0 = AArch64, 1 = AArch32
static const uint64_t PSTATE_EL  = (3ULL << 2);  // Exception Level
static const uint64_t PSTATE_SP  = (1ULL << 0);  // Stack pointer select (EL0 or ELx)

// SPSR mode bits for returning to EL1
static const uint64_t SPSR_EL1H  = (5ULL << 0);  // EL1 with SP_EL1
static const uint64_t SPSR_EL1T  = (4ULL << 0);  // EL1 with SP_EL0
static const uint64_t SPSR_EL0T  = (0ULL << 0);  // EL0 with SP_EL0

// ================================================================
// SCTLR_EL1 - System Control Register
// ================================================================

static const uint64_t SCTLR_M   = (1ULL << 0);   // MMU enable
static const uint64_t SCTLR_A   = (1ULL << 1);   // Alignment check enable
static const uint64_t SCTLR_C   = (1ULL << 2);   // Data cache enable
static const uint64_t SCTLR_SA  = (1ULL << 3);   // Stack alignment check
static const uint64_t SCTLR_SA0 = (1ULL << 4);   // Stack alignment check for EL0
static const uint64_t SCTLR_I   = (1ULL << 12);  // Instruction cache enable
static const uint64_t SCTLR_WXN = (1ULL << 19);  // Write execute never
static const uint64_t SCTLR_EE  = (1ULL << 25);  // Exception endianness
static const uint64_t SCTLR_UCI = (1ULL << 26);  // User cache instructions
static const uint64_t SCTLR_nTWE = (1ULL << 18); // Don't trap WFE
static const uint64_t SCTLR_nTWI = (1ULL << 16); // Don't trap WFI

// Reserved bits that should be set to 1 (RES1)
static const uint64_t SCTLR_RES1 = (3ULL << 28) | (3ULL << 22) | (1ULL << 20) | (1ULL << 11);

// ================================================================
// TCR_EL1 - Translation Control Register
// ================================================================

// T0SZ - Size offset for TTBR0 (bits [5:0])
// T1SZ - Size offset for TTBR1 (bits [21:16])
// TG0  - Granule size for TTBR0 (bits [15:14])
// TG1  - Granule size for TTBR1 (bits [31:30])

static const uint64_t TCR_T0SZ_SHIFT = 0;
static const uint64_t TCR_T1SZ_SHIFT = 16;
static const uint64_t TCR_T0SZ_MASK  = (0x3FULL << TCR_T0SZ_SHIFT);
static const uint64_t TCR_T1SZ_MASK  = (0x3FULL << TCR_T1SZ_SHIFT);

// Granule sizes for TG0 (TTBR0)
static const uint64_t TCR_TG0_4K  = (0ULL << 14);
static const uint64_t TCR_TG0_64K = (1ULL << 14);
static const uint64_t TCR_TG0_16K = (2ULL << 14);

// Granule sizes for TG1 (TTBR1)
static const uint64_t TCR_TG1_16K = (1ULL << 30);
static const uint64_t TCR_TG1_4K  = (2ULL << 30);
static const uint64_t TCR_TG1_64K = (3ULL << 30);

// Inner cacheability for TTBR0
static const uint64_t TCR_IRGN0_NC   = (0ULL << 8);   // Non-cacheable
static const uint64_t TCR_IRGN0_WBWA = (1ULL << 8);   // Write-back, write-allocate
static const uint64_t TCR_IRGN0_WT   = (2ULL << 8);   // Write-through
static const uint64_t TCR_IRGN0_WB   = (3ULL << 8);   // Write-back, no write-allocate

// Outer cacheability for TTBR0
static const uint64_t TCR_ORGN0_NC   = (0ULL << 10);
static const uint64_t TCR_ORGN0_WBWA = (1ULL << 10);
static const uint64_t TCR_ORGN0_WT   = (2ULL << 10);
static const uint64_t TCR_ORGN0_WB   = (3ULL << 10);

// Shareability for TTBR0
static const uint64_t TCR_SH0_NS  = (0ULL << 12);  // Non-shareable
static const uint64_t TCR_SH0_OS  = (2ULL << 12);  // Outer shareable
static const uint64_t TCR_SH0_IS  = (3ULL << 12);  // Inner shareable

// Same for TTBR1 (shifted by 16)
static const uint64_t TCR_IRGN1_WBWA = (1ULL << 24);
static const uint64_t TCR_ORGN1_WBWA = (1ULL << 26);
static const uint64_t TCR_SH1_IS     = (3ULL << 28);

// IPS - Intermediate Physical Address Size
static const uint64_t TCR_IPS_32BIT  = (0ULL << 32);  // 4GB
static const uint64_t TCR_IPS_36BIT  = (1ULL << 32);  // 64GB
static const uint64_t TCR_IPS_40BIT  = (2ULL << 32);  // 1TB
static const uint64_t TCR_IPS_42BIT  = (3ULL << 32);  // 4TB
static const uint64_t TCR_IPS_44BIT  = (4ULL << 32);  // 16TB
static const uint64_t TCR_IPS_48BIT  = (5ULL << 32);  // 256TB
static const uint64_t TCR_IPS_52BIT  = (6ULL << 32);  // 4PB

// AS - ASID Size (16-bit if set)
static const uint64_t TCR_AS = (1ULL << 36);

// ================================================================
// Page Table Entry Bits (4KB granule)
// ================================================================

// Descriptor types (bits [1:0])
static const uint64_t PTE_VALID     = (1ULL << 0);   // Valid entry
static const uint64_t PTE_TABLE     = (1ULL << 1);   // Table descriptor (level 0-2)
static const uint64_t PTE_BLOCK     = (0ULL << 1);   // Block descriptor (level 1-2)
static const uint64_t PTE_PAGE      = (1ULL << 1);   // Page descriptor (level 3)

// Block/Page attributes (lower attributes)
static const uint64_t PTE_ATTR_IDX_SHIFT = 2;
static const uint64_t PTE_ATTR_IDX_MASK  = (7ULL << PTE_ATTR_IDX_SHIFT);
static const uint64_t PTE_NS       = (1ULL << 5);    // Non-secure
static const uint64_t PTE_AP_RW_EL1 = (0ULL << 6);   // Read/write EL1 only
static const uint64_t PTE_AP_RW_ALL = (1ULL << 6);   // Read/write all ELs
static const uint64_t PTE_AP_RO_EL1 = (2ULL << 6);   // Read-only EL1 only
static const uint64_t PTE_AP_RO_ALL = (3ULL << 6);   // Read-only all ELs
static const uint64_t PTE_SH_NS    = (0ULL << 8);    // Non-shareable
static const uint64_t PTE_SH_OS    = (2ULL << 8);    // Outer shareable
static const uint64_t PTE_SH_IS    = (3ULL << 8);    // Inner shareable
static const uint64_t PTE_AF       = (1ULL << 10);   // Access flag

// Upper attributes
static const uint64_t PTE_DBM      = (1ULL << 51);   // Dirty bit modifier
static const uint64_t PTE_CONTIG   = (1ULL << 52);   // Contiguous hint
static const uint64_t PTE_PXN      = (1ULL << 53);   // Privileged execute never
static const uint64_t PTE_UXN      = (1ULL << 54);   // User execute never
static const uint64_t PTE_XN       = PTE_UXN;        // Execute never (alias)

// Output address mask (bits [47:12] for 4KB pages)
static const uint64_t PTE_OA_MASK  = 0x0000FFFFFFFFF000ULL;

// ================================================================
// MAIR_EL1 - Memory Attribute Indirection Register
// ================================================================

// Common memory attribute encodings
static const uint64_t MAIR_DEVICE_nGnRnE = 0x00;  // Device, non-Gathering, non-Reordering, no Early Write Ack
static const uint64_t MAIR_DEVICE_nGnRE  = 0x04;  // Device, non-Gathering, non-Reordering, Early Write Ack
static const uint64_t MAIR_DEVICE_GRE    = 0x0C;  // Device, Gathering, Reordering, Early Write Ack
static const uint64_t MAIR_NORMAL_NC     = 0x44;  // Normal, non-cacheable
static const uint64_t MAIR_NORMAL_WT     = 0xBB;  // Normal, write-through
static const uint64_t MAIR_NORMAL_WB     = 0xFF;  // Normal, write-back

// Default MAIR indices
static const uint64_t MAIR_IDX_DEVICE    = 0;
static const uint64_t MAIR_IDX_NORMAL_NC = 1;
static const uint64_t MAIR_IDX_NORMAL_WT = 2;
static const uint64_t MAIR_IDX_NORMAL_WB = 3;

// ================================================================
// Page Sizes and Masks
// ================================================================

static const uint64_t PAGE_SIZE_4K   = 0x1000ULL;        // 4KB
static const uint64_t PAGE_SIZE_16K  = 0x4000ULL;        // 16KB
static const uint64_t PAGE_SIZE_64K  = 0x10000ULL;       // 64KB
static const uint64_t PAGE_SIZE_2M   = 0x200000ULL;      // 2MB (huge page level 2)
static const uint64_t PAGE_SIZE_1G   = 0x40000000ULL;    // 1GB (huge page level 1)

static const uint64_t PAGE_MASK_4K   = ~(PAGE_SIZE_4K - 1);
static const uint64_t PAGE_MASK_2M   = ~(PAGE_SIZE_2M - 1);
static const uint64_t PAGE_MASK_1G   = ~(PAGE_SIZE_1G - 1);

// ================================================================
// ESR_EL1 - Exception Syndrome Register
// ================================================================

static const uint64_t ESR_EC_SHIFT    = 26;
static const uint64_t ESR_EC_MASK     = (0x3FULL << ESR_EC_SHIFT);
static const uint64_t ESR_IL          = (1ULL << 25);  // Instruction length (32-bit)
static const uint64_t ESR_ISS_MASK    = 0x1FFFFFF;     // Instruction Specific Syndrome

// Exception classes (EC field)
static const uint64_t EC_UNKNOWN        = 0x00;
static const uint64_t EC_WFI_WFE        = 0x01;
static const uint64_t EC_MCR_MRC_CP15   = 0x03;
static const uint64_t EC_MCRR_MRRC_CP15 = 0x04;
static const uint64_t EC_MCR_MRC_CP14   = 0x05;
static const uint64_t EC_LDC_STC_CP14   = 0x06;
static const uint64_t EC_FP_SIMD        = 0x07;
static const uint64_t EC_PAUTH          = 0x09;
static const uint64_t EC_CP14_MRRC      = 0x0C;
static const uint64_t EC_ILL_EXEC       = 0x0E;
static const uint64_t EC_SVC_AARCH32    = 0x11;
static const uint64_t EC_HVC_AARCH32    = 0x12;
static const uint64_t EC_SMC_AARCH32    = 0x13;
static const uint64_t EC_SVC_AARCH64    = 0x15;
static const uint64_t EC_HVC_AARCH64    = 0x16;
static const uint64_t EC_SMC_AARCH64    = 0x17;
static const uint64_t EC_MSR_MRS        = 0x18;
static const uint64_t EC_SVE            = 0x19;
static const uint64_t EC_INST_ABORT_LOW = 0x20;
static const uint64_t EC_INST_ABORT     = 0x21;
static const uint64_t EC_PC_ALIGN       = 0x22;
static const uint64_t EC_DATA_ABORT_LOW = 0x24;
static const uint64_t EC_DATA_ABORT     = 0x25;
static const uint64_t EC_SP_ALIGN       = 0x26;
static const uint64_t EC_FP_AARCH32     = 0x28;
static const uint64_t EC_FP_AARCH64     = 0x2C;
static const uint64_t EC_SERROR         = 0x2F;
static const uint64_t EC_BREAKPOINT_LOW = 0x30;
static const uint64_t EC_BREAKPOINT     = 0x31;
static const uint64_t EC_STEP_LOW       = 0x32;
static const uint64_t EC_STEP           = 0x33;
static const uint64_t EC_WATCHPOINT_LOW = 0x34;
static const uint64_t EC_WATCHPOINT     = 0x35;
static const uint64_t EC_BRK            = 0x3C;

// Data/Instruction abort ISS codes
static const uint64_t ISS_DFSC_MASK     = 0x3F;
static const uint64_t DFSC_ADDR_L0      = 0x00;
static const uint64_t DFSC_ADDR_L1      = 0x01;
static const uint64_t DFSC_ADDR_L2      = 0x02;
static const uint64_t DFSC_ADDR_L3      = 0x03;
static const uint64_t DFSC_TRANS_L0     = 0x04;
static const uint64_t DFSC_TRANS_L1     = 0x05;
static const uint64_t DFSC_TRANS_L2     = 0x06;
static const uint64_t DFSC_TRANS_L3     = 0x07;
static const uint64_t DFSC_ACCESS_L1    = 0x09;
static const uint64_t DFSC_ACCESS_L2    = 0x0A;
static const uint64_t DFSC_ACCESS_L3    = 0x0B;
static const uint64_t DFSC_PERM_L1      = 0x0D;
static const uint64_t DFSC_PERM_L2      = 0x0E;
static const uint64_t DFSC_PERM_L3      = 0x0F;
static const uint64_t DFSC_ALIGN        = 0x21;
static const uint64_t DFSC_TLB_CONFLICT = 0x30;

// ================================================================
// GIC (Generic Interrupt Controller) Registers
// ================================================================

// GIC Distributor offsets (from GICD base)
static const uint32_t GICD_CTLR        = 0x000;
static const uint32_t GICD_TYPER       = 0x004;
static const uint32_t GICD_IIDR        = 0x008;
static const uint32_t GICD_IGROUPR     = 0x080;
static const uint32_t GICD_ISENABLER   = 0x100;
static const uint32_t GICD_ICENABLER   = 0x180;
static const uint32_t GICD_ISPENDR     = 0x200;
static const uint32_t GICD_ICPENDR     = 0x280;
static const uint32_t GICD_ISACTIVER   = 0x300;
static const uint32_t GICD_ICACTIVER   = 0x380;
static const uint32_t GICD_IPRIORITYR  = 0x400;
static const uint32_t GICD_ITARGETSR   = 0x800;
static const uint32_t GICD_ICFGR       = 0xC00;
static const uint32_t GICD_SGIR        = 0xF00;

// GIC CPU Interface offsets (from GICC base)
static const uint32_t GICC_CTLR        = 0x000;
static const uint32_t GICC_PMR         = 0x004;
static const uint32_t GICC_BPR         = 0x008;
static const uint32_t GICC_IAR         = 0x00C;
static const uint32_t GICC_EOIR        = 0x010;
static const uint32_t GICC_RPR         = 0x014;
static const uint32_t GICC_HPPIR       = 0x018;
static const uint32_t GICC_AIAR        = 0x020;
static const uint32_t GICC_AEOIR       = 0x024;
static const uint32_t GICC_AHPPIR      = 0x028;
static const uint32_t GICC_IIDR        = 0x0FC;
static const uint32_t GICC_DIR         = 0x1000;

// GIC interrupt IDs
static const uint32_t GIC_INTID_SPURIOUS = 1023;

// ================================================================
// CPU ID and Feature Registers
// ================================================================

// MIDR_EL1 fields
static const uint64_t MIDR_IMPL_SHIFT    = 24;
static const uint64_t MIDR_IMPL_MASK     = (0xFFULL << MIDR_IMPL_SHIFT);
static const uint64_t MIDR_VARIANT_SHIFT = 20;
static const uint64_t MIDR_VARIANT_MASK  = (0xFULL << MIDR_VARIANT_SHIFT);
static const uint64_t MIDR_ARCH_SHIFT    = 16;
static const uint64_t MIDR_ARCH_MASK     = (0xFULL << MIDR_ARCH_SHIFT);
static const uint64_t MIDR_PARTNUM_SHIFT = 4;
static const uint64_t MIDR_PARTNUM_MASK  = (0xFFFULL << MIDR_PARTNUM_SHIFT);
static const uint64_t MIDR_REV_MASK      = 0xF;

// Known implementer IDs
static const uint64_t IMPL_ARM       = 0x41;
static const uint64_t IMPL_BROADCOM  = 0x42;
static const uint64_t IMPL_CAVIUM    = 0x43;
static const uint64_t IMPL_FUJITSU   = 0x46;
static const uint64_t IMPL_INFINEON  = 0x49;
static const uint64_t IMPL_NVIDIA    = 0x4E;
static const uint64_t IMPL_APM       = 0x50;
static const uint64_t IMPL_QUALCOMM  = 0x51;
static const uint64_t IMPL_SAMSUNG   = 0x53;
static const uint64_t IMPL_MARVELL   = 0x56;
static const uint64_t IMPL_APPLE     = 0x61;
static const uint64_t IMPL_INTEL     = 0x69;
static const uint64_t IMPL_AMPERE    = 0xC0;

// ================================================================
// Timer Constants
// ================================================================

static const uint64_t CNTP_CTL_ENABLE   = (1ULL << 0);
static const uint64_t CNTP_CTL_IMASK    = (1ULL << 1);
static const uint64_t CNTP_CTL_ISTATUS  = (1ULL << 2);

// ================================================================
// CPU control functions
// ================================================================

void halt();
void enable_interrupts();
void disable_interrupts();
void wait_for_interrupt();

// ================================================================
// System Register Access Functions
// ================================================================

// Current Exception Level
uint64_t read_current_el();

// SCTLR_EL1 - System Control
uint64_t read_sctlr_el1();
void write_sctlr_el1(uint64_t value);

// TCR_EL1 - Translation Control
uint64_t read_tcr_el1();
void write_tcr_el1(uint64_t value);

// TTBR0_EL1 - Translation Table Base 0
uint64_t read_ttbr0_el1();
void write_ttbr0_el1(uint64_t value);

// TTBR1_EL1 - Translation Table Base 1
uint64_t read_ttbr1_el1();
void write_ttbr1_el1(uint64_t value);

// MAIR_EL1 - Memory Attribute Indirection
uint64_t read_mair_el1();
void write_mair_el1(uint64_t value);

// VBAR_EL1 - Vector Base Address
uint64_t read_vbar_el1();
void write_vbar_el1(uint64_t value);

// ESR_EL1 - Exception Syndrome
uint64_t read_esr_el1();

// FAR_EL1 - Fault Address
uint64_t read_far_el1();

// ELR_EL1 - Exception Link Register
uint64_t read_elr_el1();
void write_elr_el1(uint64_t value);

// SPSR_EL1 - Saved Program Status
uint64_t read_spsr_el1();
void write_spsr_el1(uint64_t value);

// SP_EL0 - Stack Pointer
uint64_t read_sp_el0();
void write_sp_el0(uint64_t value);

// MIDR_EL1 - Main ID Register
uint64_t read_midr_el1();

// MPIDR_EL1 - Multiprocessor Affinity
uint64_t read_mpidr_el1();

// ================================================================
// Timer System Register Functions
// ================================================================

// Counter frequency
uint64_t read_cntfrq_el0();

// Physical counter
uint64_t read_cntpct_el0();

// Physical timer control
uint64_t read_cntp_ctl_el0();
void write_cntp_ctl_el0(uint64_t value);

// Physical timer compare value
uint64_t read_cntp_cval_el0();
void write_cntp_cval_el0(uint64_t value);

// Timer value (countdown)
uint64_t read_cntp_tval_el0();
void write_cntp_tval_el0(uint64_t value);

// ================================================================
// Cache and TLB Operations
// ================================================================

void invalidate_icache();
void invalidate_dcache();
void clean_dcache();
void clean_invalidate_dcache();
void invalidate_tlb();
void invalidate_tlb_entry(uint64_t addr);

// Memory barriers
void isb();  // Instruction Synchronization Barrier
void dsb();  // Data Synchronization Barrier
void dmb();  // Data Memory Barrier

// ================================================================
// GIC Operations
// ================================================================

void gic_init(uint64_t gicd_base, uint64_t gicc_base);
void gic_enable_irq(uint32_t irq);
void gic_disable_irq(uint32_t irq);
void gic_set_priority(uint32_t irq, uint8_t priority);
uint32_t gic_acknowledge_irq();
void gic_end_irq(uint32_t irq);
void gic_send_sgi(uint32_t sgi_id, uint32_t target_list);

// ================================================================
// CPU Detection
// ================================================================

struct CpuInfo {
    uint64_t midr;           // Main ID register value
    uint8_t  implementer;    // Implementer code
    uint8_t  variant;        // Variant number
    uint8_t  architecture;   // Architecture code
    uint16_t partNumber;     // Part number
    uint8_t  revision;       // Revision number
    uint32_t affinity;       // Affinity from MPIDR
    bool     isMulticore;    // Multi-processor system
};

void detect_cpu(CpuInfo* info);
const char* get_implementer_name(uint8_t impl);

// ================================================================
// Initialization
// ================================================================

void early_init();
void init();
void init_mmu();
void init_gic();
void init_timer();

} // namespace arm64
} // namespace arch
} // namespace kernel
