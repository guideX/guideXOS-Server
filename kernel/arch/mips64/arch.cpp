//
// MIPS64 Architecture Implementation
//
// Runs in kernel mode on QEMU malta or virt machine types.
// CP0 access uses privileged coprocessor 0 instructions.
//
// MIPS64 uses Coprocessor 0 (CP0) for system control:
//   - Status register controls interrupt enable, operating mode
//   - Cause register contains exception code, interrupt pending
//   - EPC holds exception return address
//   - Count/Compare provide timer functionality
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/mips64.h"
#include "include/arch/serial_console.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace mips64 {

// ================================================================
// CPU control
// ================================================================

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    // MIPS64 uses 'wait' instruction to halt until interrupt
    while (1) {
        asm volatile ("wait");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    // Set IE (Interrupt Enable) bit in Status register
    // Clear EXL (Exception Level) to allow interrupts
    uint64_t status = read_status();
    status |= STATUS_IE;       // Enable interrupts
    status &= ~STATUS_EXL;     // Clear exception level
    write_status(status);
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    // Clear IE (Interrupt Enable) bit in Status register
    uint64_t status = read_status();
    status &= ~STATUS_IE;
    write_status(status);
#endif
}

void wait_for_interrupt()
{
#if GXOS_MSVC_STUB
    __nop();
#else
    // MIPS64 'wait' instruction - low power until interrupt
    asm volatile ("wait");
#endif
}

// ================================================================
// CP0 Register Access - Status Register (CP0 $12, Select 0)
// ================================================================

uint64_t read_status()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    // mfc0 for 32-bit, dmfc0 for 64-bit
    asm volatile ("dmfc0 %0, $12" : "=r"(value));
    return value;
#endif
}

void write_status(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $12" : : "r"(value));
    ehb();  // Execution hazard barrier after CP0 write
#endif
}

// ================================================================
// CP0 Register Access - Cause Register (CP0 $13, Select 0)
// ================================================================

uint64_t read_cause()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $13" : "=r"(value));
    return value;
#endif
}

void write_cause(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    // Only IP[1:0] (software interrupt) bits are writable
    asm volatile ("dmtc0 %0, $13" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - EPC (CP0 $14, Select 0)
// ================================================================

uint64_t read_epc()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $14" : "=r"(value));
    return value;
#endif
}

void write_epc(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $14" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - PRId (CP0 $15, Select 0)
// ================================================================

uint64_t read_prid()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $15" : "=r"(value));
    return value;
#endif
}

// ================================================================
// CP0 Register Access - Config (CP0 $16, Select 0)
// ================================================================

uint64_t read_config()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $16" : "=r"(value));
    return value;
#endif
}

// ================================================================
// CP0 Register Access - Count (CP0 $9, Select 0)
// ================================================================

uint64_t read_count()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $9" : "=r"(value));
    return value;
#endif
}

void write_count(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $9" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - Compare (CP0 $11, Select 0)
// ================================================================

uint64_t read_compare()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $11" : "=r"(value));
    return value;
#endif
}

void write_compare(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    // Writing to Compare clears the timer interrupt
    asm volatile ("dmtc0 %0, $11" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - BadVAddr (CP0 $8, Select 0)
// ================================================================

uint64_t read_badvaddr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $8" : "=r"(value));
    return value;
#endif
}

// ================================================================
// CP0 Register Access - Context (CP0 $4, Select 0)
// ================================================================

uint64_t read_context()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $4" : "=r"(value));
    return value;
#endif
}

void write_context(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $4" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - EntryHi (CP0 $10, Select 0)
// ================================================================

uint64_t read_entryhi()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $10" : "=r"(value));
    return value;
#endif
}

void write_entryhi(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $10" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - EntryLo0 (CP0 $2, Select 0)
// ================================================================

uint64_t read_entrylo0()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $2" : "=r"(value));
    return value;
#endif
}

void write_entrylo0(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $2" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - EntryLo1 (CP0 $3, Select 0)
// ================================================================

uint64_t read_entrylo1()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $3" : "=r"(value));
    return value;
#endif
}

void write_entrylo1(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $3" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - PageMask (CP0 $5, Select 0)
// ================================================================

uint64_t read_pagemask()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $5" : "=r"(value));
    return value;
#endif
}

void write_pagemask(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $5" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - Index (CP0 $0, Select 0)
// ================================================================

uint64_t read_index()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $0" : "=r"(value));
    return value;
#endif
}

void write_index(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $0" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// CP0 Register Access - Wired (CP0 $6, Select 0)
// ================================================================

uint64_t read_wired()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t value;
    asm volatile ("dmfc0 %0, $6" : "=r"(value));
    return value;
#endif
}

void write_wired(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("dmtc0 %0, $6" : : "r"(value));
    ehb();
#endif
}

// ================================================================
// TLB Operations
// ================================================================

void tlbwi()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Write TLB entry at Index register
    asm volatile ("tlbwi" ::: "memory");
    ehb();
#endif
}

void tlbwr()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Write TLB entry at Random register
    asm volatile ("tlbwr" ::: "memory");
    ehb();
#endif
}

void tlbr()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Read TLB entry at Index register into EntryHi/EntryLo0/EntryLo1
    asm volatile ("tlbr" ::: "memory");
    ehb();
#endif
}

void tlbp()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Probe TLB for entry matching EntryHi, result in Index
    asm volatile ("tlbp" ::: "memory");
    ehb();
#endif
}

// ================================================================
// Barrier/Sync Operations
// ================================================================

void sync()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Full memory barrier
    asm volatile ("sync" ::: "memory");
#endif
}

void ehb()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    // Execution hazard barrier - ensures CP0 writes take effect
    // sll $0, $0, 3 is the standard EHB encoding
    asm volatile ("sll $0, $0, 3" ::: "memory");
#endif
}

// ================================================================
// CPU Identification
// ================================================================

uint64_t cpu_get_id()
{
    // PRId register contains processor identification
    // Implementation field is in bits [15:8]
    // Revision field is in bits [7:0]
    return read_prid();
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    serial_console::init();
    serial_console::puts("[MIPS64] Architecture initialization\n");
    
    // Read and display CPU ID
    uint64_t prid = read_prid();
    serial_console::puts("[MIPS64] PRId: ");
    serial_console::put_hex(prid);
    serial_console::puts("\n");
    
    // Configure Status register for kernel mode operation
    // - Enable 64-bit kernel addressing (KX)
    // - Keep interrupts disabled initially
    // - Use normal exception vectors (BEV=0 after boot)
    // - Enable CP0 access
    uint64_t status = read_status();
    status |= STATUS_KX | STATUS_CU0;      // 64-bit kernel, CP0 access
    status &= ~STATUS_BEV;                  // Normal exception vectors
    status &= ~STATUS_IE;                   // Interrupts disabled
    status &= ~(STATUS_EXL | STATUS_ERL);  // Clear exception levels
    status |= (0xFFULL << 8);              // Enable all interrupt mask bits
    write_status(status);
    
    serial_console::puts("[MIPS64] Status configured: ");
    serial_console::put_hex(read_status());
    serial_console::puts("\n");
    
    // Initialize timer
    write_count(0);
    write_compare(0xFFFFFFFFULL);  // Far future to avoid immediate interrupt
    
    serial_console::puts("[MIPS64] Architecture init complete\n");
}

} // namespace mips64
} // namespace arch
} // namespace kernel
