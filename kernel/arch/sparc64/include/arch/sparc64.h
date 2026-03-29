//
// SPARC v9 (UltraSPARC) Architecture-Specific Code
//
// Targets SPARC v9 (64-bit, e.g. UltraSPARC / Sun4u).
// No port I/O — all device access is memory-mapped.
//
// Key differences from SPARC v8:
//   - 64-bit registers and addresses
//   - PSTATE register replaces PSR (no ET bit; uses PSTATE.IE)
//   - TBA replaces TBR (Trap Base Address, bits [63:15])
//   - Trap table entries are 32 bytes (8 instructions) not 16
//   - Register windows managed via CLEANWIN/OTHERWIN/CANRESTORE/CANSAVE
//   - TICK register for cycle counting
//   - flushw instruction replaces manual window flush
//   - ASI space expanded to 8 bits with many new ASIs
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc64 {

// ---------------------------------------------------------------
// CPU control
// ---------------------------------------------------------------

void halt();
void enable_interrupts();
void disable_interrupts();

// ---------------------------------------------------------------
// PSTATE register (replaces PSR in v9)
//
// Bit layout (key fields):
//   Bit 1  : IE  (Interrupt Enable)
//   Bit 2  : PRIV (Privileged mode)
//   Bit 3  : AM  (Address Mask — 32-bit compat when set)
//   Bit 4  : PEF (Enable FPU)
//   Bit 8  : TLE (Trap Little-Endian)
//   Bit 9  : CLE (Current Little-Endian)
//   Bit 10 : MG  (MMU Global)
//   Bit 11 : IG  (Interrupt Global)
//   Bit 12 : AG  (Alternate Global)
// ---------------------------------------------------------------

uint64_t read_pstate();
void     write_pstate(uint64_t value);

// ---------------------------------------------------------------
// TBA — Trap Base Address (replaces TBR in v9)
// Bits [63:15] hold the base; bits [14:0] are reserved/zero.
// ---------------------------------------------------------------

uint64_t read_tba();
void     write_tba(uint64_t value);

// ---------------------------------------------------------------
// TL — Trap Level (0 = normal, 1+ = nested trap)
// ---------------------------------------------------------------

uint64_t read_tl();
void     write_tl(uint64_t value);

// ---------------------------------------------------------------
// PIL — Processor Interrupt Level (same concept as v8, 0-15)
// In v9 PIL is a separate register, not part of PSTATE.
// ---------------------------------------------------------------

uint64_t read_pil();
void     write_pil(uint64_t value);

// ---------------------------------------------------------------
// TICK — Tick counter (cycle counter)
// ---------------------------------------------------------------

uint64_t read_tick();

// ---------------------------------------------------------------
// TPC / TNPC / TSTATE / TT — per-trap-level state
// (read at current TL)
// ---------------------------------------------------------------

uint64_t read_tpc();
uint64_t read_tnpc();
uint64_t read_tstate();
uint64_t read_tt();

// ---------------------------------------------------------------
// VER — Version register (read-only, identifies CPU)
// ---------------------------------------------------------------

uint64_t read_ver();

// ---------------------------------------------------------------
// Register window operations
// ---------------------------------------------------------------

void flush_windows();     // uses flushw instruction (v9)

// ---------------------------------------------------------------
// ASI (Address Space Identifier) operations — 64-bit
// ---------------------------------------------------------------

uint64_t read_asi64(uint8_t asi, uint64_t address);
void     write_asi64(uint8_t asi, uint64_t address, uint64_t value);
uint32_t read_asi32(uint8_t asi, uint64_t address);
void     write_asi32(uint8_t asi, uint64_t address, uint32_t value);

// ---------------------------------------------------------------
// Memory-mapped I/O helpers (64-bit addresses)
// ---------------------------------------------------------------

uint64_t mmio_read64(uint64_t address);
void     mmio_write64(uint64_t address, uint64_t value);
uint32_t mmio_read32(uint64_t address);
void     mmio_write32(uint64_t address, uint32_t value);
uint16_t mmio_read16(uint64_t address);
void     mmio_write16(uint64_t address, uint16_t value);
uint8_t  mmio_read8(uint64_t address);
void     mmio_write8(uint64_t address, uint8_t value);

// ---------------------------------------------------------------
// Sun4u psycho / sabre PCI interrupt controller
//
// On UltraSPARC Sun4u machines (and QEMU -machine sun4u) the
// interrupt controller is part of the PCI host bridge
// (psycho / sabre).  It uses interrupt mapping registers
// (IMR) and interrupt clear registers (ICR).
//
// QEMU sun4u PCI bridge MMIO base: 0x1FE00000000
//   Interrupt mapping regs: base + 0x0C00 .. base + 0x0C38
//   Interrupt clear  regs: base + 0x1400 .. base + 0x1438
// ---------------------------------------------------------------

void pci_intctrl_init();
void pci_irq_enable(uint32_t irq);
void pci_irq_disable(uint32_t irq);
void pci_eoi(uint32_t irq);

// ---------------------------------------------------------------
// Sun4u framebuffer helpers
//
// QEMU sun4u provides an ATI PGX framebuffer or the default VGA
// at a PCI BAR address.  For simplicity we use the OpenBoot-
// provided linear framebuffer at a well-known address.
// ---------------------------------------------------------------

bool probe_sun4u_framebuffer();

// ---------------------------------------------------------------
// Initialize architecture-specific features
// ---------------------------------------------------------------

void init();

} // namespace sparc64
} // namespace arch
} // namespace kernel
