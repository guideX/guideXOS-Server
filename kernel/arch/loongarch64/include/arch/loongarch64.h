//
// LoongArch 64-bit (LA64) Architecture-Specific Code
//
// LoongArch is a RISC-style architecture developed by Loongson.
// It uses a flat 64-bit address space with memory-mapped I/O.
// The architecture has 32 general-purpose registers ($r0-$r31),
// where $r0 is hardwired to zero.
//
// Key features:
//   - 32 GPRs: $r0 (zero), $r1 (ra), $r3 (sp), $r22 (fp/s9)
//   - CSR (Control Status Register) access via csrrd/csrwr/csrxchg
//   - Privilege levels: PLV0 (kernel) to PLV3 (user)
//   - Page-based virtual memory with configurable page sizes
//   - Hardware TLB with software-managed refill option
//
// This implementation targets the LA64 (64-bit) variant running
// in PLV0 (kernel mode) on QEMU loongarch64-virt or real hardware.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace loongarch64 {

// ================================================================
// LoongArch64 CSR (Control Status Register) definitions
//
// CSRs are accessed via csrrd/csrwr/csrxchg instructions.
// Key kernel-mode CSRs (PLV0):
// ================================================================

// CSR indices (used with csrrd/csrwr)
static const uint32_t CSR_CRMD     = 0x00;   // Current mode info
static const uint32_t CSR_PRMD     = 0x01;   // Previous mode info (exception return)
static const uint32_t CSR_EUEN     = 0x02;   // Extended unit enable (FPU, etc.)
static const uint32_t CSR_MISC     = 0x03;   // Miscellaneous control
static const uint32_t CSR_ECFG     = 0x04;   // Exception configuration
static const uint32_t CSR_ESTAT    = 0x05;   // Exception status
static const uint32_t CSR_ERA      = 0x06;   // Exception return address
static const uint32_t CSR_BADV     = 0x07;   // Bad virtual address
static const uint32_t CSR_BADI     = 0x08;   // Bad instruction
static const uint32_t CSR_EENTRY   = 0x0C;   // Exception entry address
static const uint32_t CSR_TLBIDX   = 0x10;   // TLB index
static const uint32_t CSR_TLBEHI   = 0x11;   // TLB entry high
static const uint32_t CSR_TLBELO0  = 0x12;   // TLB entry low 0
static const uint32_t CSR_TLBELO1  = 0x13;   // TLB entry low 1
static const uint32_t CSR_ASID     = 0x18;   // Address space ID
static const uint32_t CSR_PGDL     = 0x19;   // Page directory base (low)
static const uint32_t CSR_PGDH     = 0x1A;   // Page directory base (high)
static const uint32_t CSR_PGD      = 0x1B;   // Page directory base (selected)
static const uint32_t CSR_PWCL     = 0x1C;   // Page walk controller (low)
static const uint32_t CSR_PWCH     = 0x1D;   // Page walk controller (high)
static const uint32_t CSR_STLBPS   = 0x1E;   // STLB page size
static const uint32_t CSR_RVACFG   = 0x1F;   // Reduced virtual address config
static const uint32_t CSR_CPUID    = 0x20;   // CPU ID
static const uint32_t CSR_PRCFG1   = 0x21;   // Processor config 1
static const uint32_t CSR_PRCFG2   = 0x22;   // Processor config 2
static const uint32_t CSR_PRCFG3   = 0x23;   // Processor config 3
static const uint32_t CSR_SAVE0    = 0x30;   // Scratch register 0 (for exception handlers)
static const uint32_t CSR_SAVE1    = 0x31;   // Scratch register 1
static const uint32_t CSR_SAVE2    = 0x32;   // Scratch register 2
static const uint32_t CSR_SAVE3    = 0x33;   // Scratch register 3
static const uint32_t CSR_TID      = 0x40;   // Timer ID
static const uint32_t CSR_TCFG     = 0x41;   // Timer configuration
static const uint32_t CSR_TVAL     = 0x42;   // Timer value
static const uint32_t CSR_CNTC     = 0x43;   // Timer offset (compensation)
static const uint32_t CSR_TICLR    = 0x44;   // Timer interrupt clear
static const uint32_t CSR_LLBCTL   = 0x60;   // LLBit control
static const uint32_t CSR_IMPCTL1  = 0x80;   // Implementation-specific control 1
static const uint32_t CSR_IMPCTL2  = 0x81;   // Implementation-specific control 2
static const uint32_t CSR_TLBRENTRY = 0x88;  // TLB refill exception entry
static const uint32_t CSR_TLBRBADV  = 0x89;  // TLB refill bad virtual address
static const uint32_t CSR_TLBRERA   = 0x8A;  // TLB refill exception return address
static const uint32_t CSR_TLBRSAVE  = 0x8B;  // TLB refill scratch
static const uint32_t CSR_TLBRELO0  = 0x8C;  // TLB refill entry low 0
static const uint32_t CSR_TLBRELO1  = 0x8D;  // TLB refill entry low 1
static const uint32_t CSR_TLBREHI   = 0x8E;  // TLB refill entry high
static const uint32_t CSR_TLBRPRMD  = 0x8F;  // TLB refill previous mode
static const uint32_t CSR_DMW0     = 0x180;  // Direct mapping window 0
static const uint32_t CSR_DMW1     = 0x181;  // Direct mapping window 1
static const uint32_t CSR_DMW2     = 0x182;  // Direct mapping window 2
static const uint32_t CSR_DMW3     = 0x183;  // Direct mapping window 3

// CRMD (Current Mode) register bits
static const uint64_t CRMD_PLV_MASK = 0x3;   // Privilege level (0=kernel, 3=user)
static const uint64_t CRMD_IE       = (1ULL << 2);  // Interrupt enable
static const uint64_t CRMD_DA       = (1ULL << 3);  // Direct address (no translation)
static const uint64_t CRMD_PG       = (1ULL << 4);  // Paging enable
static const uint64_t CRMD_DATF     = (0x3ULL << 5);  // Direct address translation flag
static const uint64_t CRMD_DATM     = (0x3ULL << 7);  // Direct address translation mode

// ESTAT (Exception Status) register bits
static const uint64_t ESTAT_IS_MASK = 0x1FFF;        // Interrupt status bits
static const uint64_t ESTAT_ECODE_MASK = (0x3FULL << 16);  // Exception code

// Exception codes
static const uint32_t ECODE_INT  = 0x00;    // Interrupt
static const uint32_t ECODE_PIL  = 0x01;    // Page invalid (load)
static const uint32_t ECODE_PIS  = 0x02;    // Page invalid (store)
static const uint32_t ECODE_PIF  = 0x03;    // Page invalid (fetch)
static const uint32_t ECODE_PME  = 0x04;    // Page modification exception
static const uint32_t ECODE_PNR  = 0x05;    // Page not readable
static const uint32_t ECODE_PNX  = 0x06;    // Page not executable
static const uint32_t ECODE_PPI  = 0x07;    // Page privilege invalid
static const uint32_t ECODE_ADE  = 0x08;    // Address error (data)
static const uint32_t ECODE_ALE  = 0x09;    // Address alignment error
static const uint32_t ECODE_BCE  = 0x0A;    // Bound check error
static const uint32_t ECODE_SYS  = 0x0B;    // System call
static const uint32_t ECODE_BRK  = 0x0C;    // Breakpoint
static const uint32_t ECODE_INE  = 0x0D;    // Instruction not exist
static const uint32_t ECODE_IPE  = 0x0E;    // Instruction privilege error
static const uint32_t ECODE_FPD  = 0x0F;    // FPU disabled
static const uint32_t ECODE_SXD  = 0x10;    // LSX disabled
static const uint32_t ECODE_ASXD = 0x11;    // LASX disabled
static const uint32_t ECODE_FPE  = 0x12;    // FPU exception
static const uint32_t ECODE_TLB  = 0x3F;    // TLB refill

// ================================================================
// CPU control functions
// ================================================================

// Halt the CPU (wait for interrupt)
void halt();

// Enable interrupts (set CRMD.IE)
void enable_interrupts();

// Disable interrupts (clear CRMD.IE)
void disable_interrupts();

// Wait for interrupt (low-power idle)
void wait_for_interrupt();

// ================================================================
// CSR access functions
// ================================================================

// Read a CSR register
uint64_t read_csr(uint32_t csr);

// Write a CSR register
void write_csr(uint32_t csr, uint64_t value);

// Exchange CSR value (atomically swap with mask)
uint64_t xchg_csr(uint32_t csr, uint64_t value, uint64_t mask);

// Convenience functions for common CSRs
uint64_t read_crmd();
void write_crmd(uint64_t value);
uint64_t read_prmd();
void write_prmd(uint64_t value);
uint64_t read_estat();
uint64_t read_era();
void write_era(uint64_t value);
uint64_t read_badv();
uint64_t read_eentry();
void write_eentry(uint64_t value);
uint64_t read_cpuid();
uint64_t read_tid();
void write_tid(uint64_t value);
uint64_t read_tcfg();
void write_tcfg(uint64_t value);
uint64_t read_tval();
void clear_timer_interrupt();

// ================================================================
// Memory barrier / fence operations
// ================================================================

// General memory barrier (dbar instruction)
void dbar();

// Instruction barrier (ibar instruction)
void ibar();

// ================================================================
// TLB operations
// ================================================================

// Invalidate all TLB entries
void invtlb_all();

// Invalidate TLB entry by ASID
void invtlb_asid(uint32_t asid);

// Invalidate TLB entry by virtual address and ASID
void invtlb_va(uint64_t va, uint32_t asid);

// ================================================================
// Initialization
// ================================================================

// Initialize LoongArch64-specific features
void init();

} // namespace loongarch64
} // namespace arch
} // namespace kernel
