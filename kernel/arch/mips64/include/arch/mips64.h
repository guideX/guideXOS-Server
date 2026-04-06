//
// MIPS64 Architecture-Specific Code
//
// Targets MIPS64 Release 2 running in kernel mode on QEMU malta
// or virt machine types.
//
// MIPS64 Coprocessor 0 (CP0) registers used:
//   - Status (CP0 Register 12, Select 0) - processor status
//   - Cause (CP0 Register 13, Select 0) - exception cause
//   - EPC (CP0 Register 14, Select 0) - exception program counter
//   - PRId (CP0 Register 15, Select 0) - processor ID
//   - Config (CP0 Register 16, Select 0) - processor configuration
//   - Count (CP0 Register 9, Select 0) - timer count
//   - Compare (CP0 Register 11, Select 0) - timer compare
//   - Context (CP0 Register 4, Select 0) - TLB context
//   - EntryHi (CP0 Register 10, Select 0) - TLB entry high
//   - EntryLo0/1 (CP0 Register 2/3, Select 0) - TLB entry low
//   - PageMask (CP0 Register 5, Select 0) - TLB page mask
//   - Index (CP0 Register 0, Select 0) - TLB index
//   - Random (CP0 Register 1, Select 0) - TLB random
//   - Wired (CP0 Register 6, Select 0) - TLB wired
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace mips64 {

// ================================================================
// CP0 Status Register Bits
// ================================================================

// Status register bits
static const uint64_t STATUS_IE    = (1ULL << 0);   // Interrupt Enable
static const uint64_t STATUS_EXL   = (1ULL << 1);   // Exception Level
static const uint64_t STATUS_ERL   = (1ULL << 2);   // Error Level
static const uint64_t STATUS_KSU   = (3ULL << 3);   // Kernel/Supervisor/User mode
static const uint64_t STATUS_UX    = (1ULL << 5);   // User extended addressing
static const uint64_t STATUS_SX    = (1ULL << 6);   // Supervisor extended addressing
static const uint64_t STATUS_KX    = (1ULL << 7);   // Kernel extended addressing
static const uint64_t STATUS_IM    = (0xFFULL << 8); // Interrupt Mask
static const uint64_t STATUS_BEV   = (1ULL << 22);  // Boot Exception Vectors
static const uint64_t STATUS_FR    = (1ULL << 26);  // Floating Point Register mode
static const uint64_t STATUS_CU0   = (1ULL << 28);  // Coprocessor 0 usable
static const uint64_t STATUS_CU1   = (1ULL << 29);  // Coprocessor 1 (FPU) usable

// Kernel mode = KSU bits 0
static const uint64_t STATUS_KSU_KERNEL = (0ULL << 3);
static const uint64_t STATUS_KSU_SUPER  = (1ULL << 3);
static const uint64_t STATUS_KSU_USER   = (2ULL << 3);

// ================================================================
// CP0 Cause Register Bits
// ================================================================

// Cause register exception codes (bits [6:2])
static const uint64_t CAUSE_EXCCODE_MASK   = (0x1FULL << 2);
static const uint64_t CAUSE_EXCCODE_SHIFT  = 2;

// Exception codes
static const uint64_t EXC_INT    = 0;   // Interrupt
static const uint64_t EXC_MOD    = 1;   // TLB modification
static const uint64_t EXC_TLBL   = 2;   // TLB load
static const uint64_t EXC_TLBS   = 3;   // TLB store
static const uint64_t EXC_ADEL   = 4;   // Address error (load/fetch)
static const uint64_t EXC_ADES   = 5;   // Address error (store)
static const uint64_t EXC_IBE    = 6;   // Bus error (instruction)
static const uint64_t EXC_DBE    = 7;   // Bus error (data)
static const uint64_t EXC_SYS    = 8;   // Syscall
static const uint64_t EXC_BP     = 9;   // Breakpoint
static const uint64_t EXC_RI     = 10;  // Reserved instruction
static const uint64_t EXC_CPU    = 11;  // Coprocessor unusable
static const uint64_t EXC_OV     = 12;  // Overflow
static const uint64_t EXC_TR     = 13;  // Trap
static const uint64_t EXC_MSAFPE = 14;  // MSA floating-point exception
static const uint64_t EXC_FPE    = 15;  // Floating-point exception
static const uint64_t EXC_WATCH  = 23;  // Watch
static const uint64_t EXC_MCHECK = 24;  // Machine check

// Cause register interrupt pending bits (IP[7:0])
static const uint64_t CAUSE_IP_MASK  = (0xFFULL << 8);
static const uint64_t CAUSE_IP_SHIFT = 8;

// Branch delay slot
static const uint64_t CAUSE_BD = (1ULL << 31);

// ================================================================
// MIPS64 TLB Constants
// ================================================================

static const uint64_t PAGE_SIZE_4K   = 4096;
static const uint64_t PAGE_MASK_4K   = 0x0000000000001FFFULL;
static const uint64_t TLB_VALID      = (1ULL << 1);  // V bit
static const uint64_t TLB_DIRTY      = (1ULL << 2);  // D bit
static const uint64_t TLB_GLOBAL     = (1ULL << 0);  // G bit

// ================================================================
// CPU control functions
// ================================================================

void halt();
void enable_interrupts();
void disable_interrupts();
void wait_for_interrupt();

// ================================================================
// CP0 Register Access Functions
// ================================================================

// Status register (CP0 $12)
uint64_t read_status();
void write_status(uint64_t value);

// Cause register (CP0 $13)
uint64_t read_cause();
void write_cause(uint64_t value);

// EPC - Exception Program Counter (CP0 $14)
uint64_t read_epc();
void write_epc(uint64_t value);

// PRId - Processor ID (CP0 $15)
uint64_t read_prid();

// Config register (CP0 $16)
uint64_t read_config();

// Count register (CP0 $9) - timer count
uint64_t read_count();
void write_count(uint64_t value);

// Compare register (CP0 $11) - timer compare
uint64_t read_compare();
void write_compare(uint64_t value);

// BadVAddr register (CP0 $8) - bad virtual address
uint64_t read_badvaddr();

// Context register (CP0 $4) - TLB context
uint64_t read_context();
void write_context(uint64_t value);

// EntryHi register (CP0 $10) - TLB entry high
uint64_t read_entryhi();
void write_entryhi(uint64_t value);

// EntryLo0 register (CP0 $2) - TLB entry low 0
uint64_t read_entrylo0();
void write_entrylo0(uint64_t value);

// EntryLo1 register (CP0 $3) - TLB entry low 1
uint64_t read_entrylo1();
void write_entrylo1(uint64_t value);

// PageMask register (CP0 $5) - TLB page mask
uint64_t read_pagemask();
void write_pagemask(uint64_t value);

// Index register (CP0 $0) - TLB index
uint64_t read_index();
void write_index(uint64_t value);

// Wired register (CP0 $6) - TLB wired entries
uint64_t read_wired();
void write_wired(uint64_t value);

// ================================================================
// TLB Operations
// ================================================================

void tlbwi();   // Write TLB entry at Index
void tlbwr();   // Write TLB entry at Random
void tlbr();    // Read TLB entry at Index
void tlbp();    // Probe TLB for matching entry

// ================================================================
// Barrier/Sync Operations
// ================================================================

void sync();    // Memory barrier
void ehb();     // Execution hazard barrier

// ================================================================
// CPU Identification
// ================================================================

uint64_t cpu_get_id();

// ================================================================
// Initialization
// ================================================================

void init();

} // namespace mips64
} // namespace arch
} // namespace kernel
