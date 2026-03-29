//
// SPARC v9 (UltraSPARC) Architecture Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/sparc64.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

// Trap table defined in boot.s
extern "C" { extern char sparc64_trap_table[]; }

namespace kernel {
namespace arch {
namespace sparc64 {

// ================================================================
// CPU control
//
// SPARC v9 PSTATE register:
//   Bit 1 : IE  (Interrupt Enable — must be 1 for interrupts)
//
// PIL register:
//   Bits [3:0] : Processor Interrupt Level (0–15)
//
// enable_interrupts  : set PSTATE.IE, clear PIL to 0
// disable_interrupts : clear PSTATE.IE, set PIL to 15
// ================================================================

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    // UltraSPARC doesn't have a true halt; spin with a memory barrier
    while (1) {
        asm volatile ("rd %%ccr, %%g0" ::: "memory");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    uint64_t pstate = read_pstate();
    pstate |= 0x02ULL;       // set IE (bit 1)
    write_pstate(pstate);
    write_pil(0);             // allow all interrupt levels
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    write_pil(15);            // mask all interrupt levels
    uint64_t pstate = read_pstate();
    pstate &= ~0x02ULL;      // clear IE (bit 1)
    write_pstate(pstate);
#endif
}

// ================================================================
// PSTATE register access
// ================================================================

uint64_t read_pstate()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%pstate, %0" : "=r"(val));
    return val;
#endif
}

void write_pstate(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("wrpr %0, %%pstate" : : "r"(value));
#endif
}

// ================================================================
// TBA — Trap Base Address
// ================================================================

uint64_t read_tba()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%tba, %0" : "=r"(val));
    return val;
#endif
}

void write_tba(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("wrpr %0, %%tba" : : "r"(value));
#endif
}

// ================================================================
// TL — Trap Level
// ================================================================

uint64_t read_tl()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%tl, %0" : "=r"(val));
    return val;
#endif
}

void write_tl(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("wrpr %0, %%tl" : : "r"(value));
#endif
}

// ================================================================
// PIL — Processor Interrupt Level
// ================================================================

uint64_t read_pil()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%pil, %0" : "=r"(val));
    return val;
#endif
}

void write_pil(uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile ("wrpr %0, %%pil" : : "r"(value));
#endif
}

// ================================================================
// TICK — Cycle counter
// ================================================================

uint64_t read_tick()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rd %%tick, %0" : "=r"(val));
    return val;
#endif
}

// ================================================================
// Per-trap-level state registers
// ================================================================

uint64_t read_tpc()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%tpc, %0" : "=r"(val));
    return val;
#endif
}

uint64_t read_tnpc()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%tnpc, %0" : "=r"(val));
    return val;
#endif
}

uint64_t read_tstate()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%tstate, %0" : "=r"(val));
    return val;
#endif
}

uint64_t read_tt()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%tt, %0" : "=r"(val));
    return val;
#endif
}

// ================================================================
// VER — Version register (read-only)
// ================================================================

uint64_t read_ver()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint64_t val;
    asm volatile ("rdpr %%ver, %0" : "=r"(val));
    return val;
#endif
}

// ================================================================
// Register window flush
//
// SPARC v9 provides the flushw instruction which flushes all
// register windows to the stack in one operation — much simpler
// than the v8 manual save/restore loop.
// ================================================================

void flush_windows()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile ("flushw" ::: "memory");
#endif
}

// ================================================================
// ASI read / write  (64-bit addresses, 64-bit and 32-bit data)
//
// SPARC v9 extends ASI to a full 8-bit field.  The ldxa/stxa
// instructions operate on 64-bit data; lduwa/stwa on 32-bit.
// ================================================================

uint64_t read_asi64(uint8_t asi, uint64_t address)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; return 0;
#else
    uint64_t value;
    asm volatile (
        "ldxa [%1] %2, %0"
        : "=r"(value)
        : "r"(address), "i"(asi)
    );
    return value;
#endif
}

void write_asi64(uint8_t asi, uint64_t address, uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; (void)value;
#else
    asm volatile (
        "stxa %0, [%1] %2"
        :
        : "r"(value), "r"(address), "i"(asi)
        : "memory"
    );
#endif
}

uint32_t read_asi32(uint8_t asi, uint64_t address)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; return 0;
#else
    uint32_t value;
    asm volatile (
        "lduwa [%1] %2, %0"
        : "=r"(value)
        : "r"(address), "i"(asi)
    );
    return value;
#endif
}

void write_asi32(uint8_t asi, uint64_t address, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; (void)value;
#else
    asm volatile (
        "stwa %0, [%1] %2"
        :
        : "r"(value), "r"(address), "i"(asi)
        : "memory"
    );
#endif
}

// ================================================================
// Memory-mapped I/O helpers  (64-bit addresses)
// ================================================================

uint64_t mmio_read64(uint64_t address)
{
#if GXOS_MSVC_STUB
    (void)address; return 0;
#else
    return *reinterpret_cast<volatile uint64_t*>(address);
#endif
}

void mmio_write64(uint64_t address, uint64_t value)
{
#if GXOS_MSVC_STUB
    (void)address; (void)value;
#else
    *reinterpret_cast<volatile uint64_t*>(address) = value;
#endif
}

uint32_t mmio_read32(uint64_t address)
{
#if GXOS_MSVC_STUB
    (void)address; return 0;
#else
    return *reinterpret_cast<volatile uint32_t*>(address);
#endif
}

void mmio_write32(uint64_t address, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)address; (void)value;
#else
    *reinterpret_cast<volatile uint32_t*>(address) = value;
#endif
}

uint16_t mmio_read16(uint64_t address)
{
#if GXOS_MSVC_STUB
    (void)address; return 0;
#else
    return *reinterpret_cast<volatile uint16_t*>(address);
#endif
}

void mmio_write16(uint64_t address, uint16_t value)
{
#if GXOS_MSVC_STUB
    (void)address; (void)value;
#else
    *reinterpret_cast<volatile uint16_t*>(address) = value;
#endif
}

uint8_t mmio_read8(uint64_t address)
{
#if GXOS_MSVC_STUB
    (void)address; return 0;
#else
    return *reinterpret_cast<volatile uint8_t*>(address);
#endif
}

void mmio_write8(uint64_t address, uint8_t value)
{
#if GXOS_MSVC_STUB
    (void)address; (void)value;
#else
    *reinterpret_cast<volatile uint8_t*>(address) = value;
#endif
}

// ================================================================
// Sun4u PCI interrupt controller (psycho / sabre)
//
// QEMU sun4u uses a simulated sabre PCI host bridge.
// The interrupt mapping/clear registers are at fixed offsets
// from the PCI controller's MMIO base.
//
// QEMU sun4u PCI bridge base: 0x1FE00000000
//   Interrupt Mapping Registers : base + 0x0C00 (8 bytes each, 8 slots)
//   Interrupt Clear Registers   : base + 0x1400 (8 bytes each, 8 slots)
//
// Each IMR has:
//   Bit 31 : Valid (1 = enabled)
//   Bits [4:0] : Target CPU (0 for uniprocessor)
//
// Each ICR: write 0 to clear the interrupt (send EOI).
// ================================================================

static const uint64_t SABRE_BASE         = 0x1FE00000000ULL;
static const uint64_t SABRE_IMR_BASE     = SABRE_BASE + 0x0C00ULL;
static const uint64_t SABRE_ICR_BASE     = SABRE_BASE + 0x1400ULL;
static const uint32_t SABRE_IMR_VALID    = (1u << 31);
static const int      SABRE_IRQ_SLOTS    = 8;

void pci_intctrl_init()
{
    // Disable all interrupt mapping registers
    for (int i = 0; i < SABRE_IRQ_SLOTS; ++i) {
        mmio_write64(SABRE_IMR_BASE + (uint64_t)i * 8, 0);
    }
    // Clear all pending interrupts
    for (int i = 0; i < SABRE_IRQ_SLOTS; ++i) {
        mmio_write64(SABRE_ICR_BASE + (uint64_t)i * 8, 0);
    }
}

void pci_irq_enable(uint32_t irq)
{
    if (irq >= (uint32_t)SABRE_IRQ_SLOTS) return;
    uint64_t val = mmio_read64(SABRE_IMR_BASE + (uint64_t)irq * 8);
    val |= SABRE_IMR_VALID;  // set Valid bit
    val &= ~0x1FULL;         // target CPU 0
    mmio_write64(SABRE_IMR_BASE + (uint64_t)irq * 8, val);
}

void pci_irq_disable(uint32_t irq)
{
    if (irq >= (uint32_t)SABRE_IRQ_SLOTS) return;
    uint64_t val = mmio_read64(SABRE_IMR_BASE + (uint64_t)irq * 8);
    val &= ~(uint64_t)SABRE_IMR_VALID;  // clear Valid bit
    mmio_write64(SABRE_IMR_BASE + (uint64_t)irq * 8, val);
}

void pci_eoi(uint32_t irq)
{
    if (irq >= (uint32_t)SABRE_IRQ_SLOTS) return;
    // Write 0 to the corresponding Interrupt Clear Register
    mmio_write64(SABRE_ICR_BASE + (uint64_t)irq * 8, 0);
}

// ================================================================
// Sun4u framebuffer probe
//
// QEMU sun4u provides a PCI VGA-compatible framebuffer.
// With -vga std (the default) the linear framebuffer is at
// PCI BAR0, typically mapped at 0x0000000080000000 on QEMU.
// Default resolution: 1024x768, 32-bit XRGB.
// ================================================================

static const uint64_t SUN4U_FB_BASE    = 0x80000000ULL;
static const uint32_t SUN4U_FB_WIDTH   = 1024;
static const uint32_t SUN4U_FB_HEIGHT  = 768;

bool probe_sun4u_framebuffer()
{
    // The actual buffer pointer is set via init_sun4u() in
    // framebuffer.cpp.  This just confirms the hw is expected.
    return true;
}

// ================================================================
// Architecture initialisation
// ================================================================

void init()
{
    // Trap table and initial PSTATE are already configured in
    // boot.s before kernel_main() is called.

    // Initialise the PCI interrupt controller
    pci_intctrl_init();
}

} // namespace sparc64
} // namespace arch
} // namespace kernel
