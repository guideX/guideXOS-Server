//
// SPARC v8 Architecture Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/sparc.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

// Trap table defined in boot.s
extern "C" { extern char sparc_trap_table[]; }

namespace kernel {
namespace arch {
namespace sparc {

// ================================================================
// PSR bit layout (SPARC v8):
//   Bit  5    : ET  (Enable Traps — must be 1 for interrupts to fire)
//   Bits 11:8 : PIL (Processor Interrupt Level, 0–15)
//
// enable_interrupts  : set ET, clear PIL to 0
// disable_interrupts : clear ET, set PIL to 15
// ================================================================

void halt()
{
#if GXOS_MSVC_STUB
    while (true) { __nop(); }
#else
    while (1) {
        asm volatile ("nop");
    }
#endif
}

void enable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    uint32_t psr = read_psr();
    psr |= 0x20;          // set ET
    psr &= ~0x0F00u;      // clear PIL (allow all levels)
    write_psr(psr);
    asm volatile ("nop; nop; nop");
#endif
}

void disable_interrupts()
{
#if GXOS_MSVC_STUB
    // No-op on MSVC host build
#else
    uint32_t psr = read_psr();
    psr &= ~0x20u;        // clear ET
    psr |= 0x0F00u;       // set PIL to 15
    write_psr(psr);
    asm volatile ("nop; nop; nop");
#endif
}

// ================================================================
// ASI read / write
// ================================================================

uint32_t read_asi(uint32_t asi, uint32_t address)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; return 0;
#else
    uint32_t value;
    asm volatile (
        "lda [%1] %2, %0"
        : "=r"(value)
        : "r"(address), "i"(asi)
    );
    return value;
#endif
}

void write_asi(uint32_t asi, uint32_t address, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)asi; (void)address; (void)value;
#else
    asm volatile (
        "sta %0, [%1] %2"
        :
        : "r"(value), "r"(address), "i"(asi)
    );
#endif
}

// ================================================================
// Register window flush
// ================================================================

void flush_windows()
{
#if GXOS_MSVC_STUB
    // No-op
#else
    asm volatile (
        "save %%sp, -64, %%sp\n"
        "restore\n"
        :
        :
        : "memory"
    );
#endif
}

// ================================================================
// Special register access  (PSR, TBR, WIM)
// ================================================================

uint32_t read_psr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t psr;
    asm volatile (
        "rd %%psr, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(psr)
    );
    return psr;
#endif
}

void write_psr(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "wr %0, %%psr\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
#endif
}

uint32_t read_tbr()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t tbr;
    asm volatile (
        "rd %%tbr, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(tbr)
    );
    return tbr;
#endif
}

void write_tbr(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "wr %0, %%tbr\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
#endif
}

uint32_t read_wim()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    uint32_t wim;
    asm volatile (
        "rd %%wim, %0\n"
        "nop\n"
        "nop\n"
        "nop\n"
        : "=r"(wim)
    );
    return wim;
#endif
}

void write_wim(uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)value;
#else
    asm volatile (
        "wr %0, %%wim\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r"(value)
    );
#endif
}

// ================================================================
// Memory-mapped I/O helpers
//
// SPARC has no port I/O; all device access is memory-mapped.
// These use volatile pointers to ensure the compiler does not
// reorder or optimise away the accesses.
// ================================================================

uint32_t mmio_read32(uint32_t address)
{
#if GXOS_MSVC_STUB
    (void)address; return 0;
#else
    return *reinterpret_cast<volatile uint32_t*>(address);
#endif
}

void mmio_write32(uint32_t address, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)address; (void)value;
#else
    *reinterpret_cast<volatile uint32_t*>(address) = value;
#endif
}

uint8_t mmio_read8(uint32_t address)
{
#if GXOS_MSVC_STUB
    (void)address; return 0;
#else
    return *reinterpret_cast<volatile uint8_t*>(address);
#endif
}

void mmio_write8(uint32_t address, uint8_t value)
{
#if GXOS_MSVC_STUB
    (void)address; (void)value;
#else
    *reinterpret_cast<volatile uint8_t*>(address) = value;
#endif
}

// ================================================================
// Sun4m SLAVIO interrupt controller
//
// On the SPARCstation 5 / Sun4m (and QEMU -machine SS-5):
//   System interrupt controller base: 0x10000000
//   Per-CPU interrupt controller base: 0x10004000
//
// System registers (offsets from 0x10000000):
//   +0x00  Pending  (read-only)
//   +0x04  Mask/Set (write: set mask bits)
//   +0x08  Mask/Clr (write: clear mask bits -> enable IRQ)
//   +0x0C  Target   (which CPU receives the interrupt)
//
// Per-CPU registers (offsets from 0x10004000, CPU 0):
//   +0x00  Pending  (read-only)
//   +0x04  Clear/EOI (write: clear pending / acknowledge)
//   +0x08  Set      (write: set soft interrupt)
// ================================================================

static const uint32_t SLAVIO_SYS_BASE    = 0x10000000u;
static const uint32_t SLAVIO_CPU0_BASE   = 0x10004000u;

// System offsets
static const uint32_t SLAVIO_SYS_PENDING = 0x00u;
static const uint32_t SLAVIO_SYS_MASK_SET = 0x04u;
static const uint32_t SLAVIO_SYS_MASK_CLR = 0x08u;

// Per-CPU offsets
static const uint32_t SLAVIO_CPU_PENDING = 0x00u;
static const uint32_t SLAVIO_CPU_CLR     = 0x04u;

void slavio_init()
{
    // Mask all system-level interrupts
    mmio_write32(SLAVIO_SYS_BASE + SLAVIO_SYS_MASK_SET, 0xFFFFFFFEu);
}

void slavio_irq_enable(uint32_t irq)
{
    if (irq == 0 || irq > 15) return;
    // Clear the mask bit to enable
    mmio_write32(SLAVIO_SYS_BASE + SLAVIO_SYS_MASK_CLR, 1u << irq);
}

void slavio_irq_disable(uint32_t irq)
{
    if (irq == 0 || irq > 15) return;
    // Set the mask bit to disable
    mmio_write32(SLAVIO_SYS_BASE + SLAVIO_SYS_MASK_SET, 1u << irq);
}

void slavio_eoi(uint32_t irq)
{
    if (irq == 0 || irq > 15) return;
    // Clear the pending bit on CPU 0 controller
    mmio_write32(SLAVIO_CPU0_BASE + SLAVIO_CPU_CLR, 1u << irq);
}

// ================================================================
// Sun4m TCX framebuffer probe
//
// On QEMU SS-5 the TCX 8-bit framebuffer lives at 0x50000000
// with a 24-bit "direct" plane at 0x50800000.  The QEMU
// -machine SS-5 default resolution is 1024x768.
//
// For simplicity we use the 8-bit indexed framebuffer (which
// QEMU converts to 32-bit behind the scenes when using -vga tcx).
// When QEMU's tcx is in 24-bit mode the DFLT plane is 32-bit
// XRGB at 0x50800000.  We attempt the 24-bit plane first.
// ================================================================

// Forward-declare framebuffer internal helpers (defined in framebuffer.cpp)
// We call them through a SPARC-specific init wrapper.

static const uint32_t TCX_FB_BASE_24  = 0x50800000u;  // 24-bit direct plane
static const uint32_t TCX_FB_BASE_8   = 0x50000000u;  // 8-bit indexed plane
static const uint32_t TCX_DEFAULT_W   = 1024u;
static const uint32_t TCX_DEFAULT_H   = 768u;

bool probe_tcx_framebuffer()
{
    // Will be called from framebuffer init path; see framebuffer.cpp
    // The actual buffer pointer and dimensions are set through the
    // framebuffer module's init_sun4m() function.
    // This just returns true to indicate we know the hw exists.
    return true;
}

// ================================================================
// Architecture initialisation
// ================================================================

void init()
{
    // The trap table is already installed and WIM configured in
    // boot.s before kernel_main() is called.  Here we initialise
    // the interrupt controller and any remaining hardware.

    // Initialise the SLAVIO interrupt controller (mask everything)
    slavio_init();
}

} // namespace sparc
} // namespace arch
} // namespace kernel

// ================================================================
// C-linkage IRQ dispatch called from boot.s irq_dispatch_asm
// ================================================================

// Forward-declare the C++ handler table in interrupts.cpp
namespace kernel { namespace interrupts {
    extern "C" void sparc_irq_dispatch(uint32_t irq_index);
} }
