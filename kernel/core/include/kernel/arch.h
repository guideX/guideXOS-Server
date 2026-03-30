//
// Architecture Abstraction Layer
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "types.h"

// Include architecture-specific headers based on target
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_AMD64
    #if defined(_MSC_VER)
        #include "../../../arch/amd64/include/arch/amd64.h"
    #else
        #include <arch/amd64.h>
    #endif
    namespace kernel { namespace arch { using namespace amd64; } }
#elif defined(__i386__) || defined(_M_IX86)
    #define ARCH_X86
    #if defined(_MSC_VER)
        #include "../../../arch/x86/include/arch/x86.h"
    #else
        #include <arch/x86.h>
    #endif
    namespace kernel { namespace arch { using namespace x86; } }
#elif defined(__arm__) || defined(__aarch32__) || defined(_M_ARM)
    #define ARCH_ARM
    #if defined(_MSC_VER)
        #include "../../../arch/arm/include/arch/arm.h"
    #else
        #include <arch/arm.h>
    #endif
    namespace kernel { namespace arch { using namespace arm; } }
#elif defined(__ia64__) || defined(_M_IA64)
    #define ARCH_IA64
    #if defined(_MSC_VER)
        #include "../../../arch/ia64/include/arch/ia64.h"
    #else
        #include <arch/ia64.h>
    #endif
    namespace kernel { namespace arch { using namespace ia64; } }
#elif defined(__sparc__) && (defined(__arch64__) || defined(__sparcv9))
    #define ARCH_SPARC64
    #if defined(_MSC_VER)
        #include "../../../arch/sparc64/include/arch/sparc64.h"
    #else
        #include <arch/sparc64.h>
    #endif
    namespace kernel { namespace arch { using namespace sparc64; } }
#elif defined(__sparc__)
    #define ARCH_SPARC
    #if defined(_MSC_VER)
        #include "../../../arch/sparc/include/arch/sparc.h"
    #else
        #include <arch/sparc.h>
    #endif
    namespace kernel { namespace arch { using namespace sparc; } }
#elif defined(__riscv) && (__riscv_xlen == 64)
    #define ARCH_RISCV64
    #if defined(_MSC_VER)
        #include "../../../arch/riscv64/include/arch/riscv64.h"
    #else
        #include <arch/riscv64.h>
    #endif
    namespace kernel { namespace arch { using namespace riscv64; } }
#else
    #error "Unsupported architecture"
#endif

// ================================================================
// Platform feature macros
//
// Each arch sets the features it supports.  Core code uses these
// guards so that x86-specific hardware (PIC, PS/2, VGA text,
// port I/O) is never compiled on architectures that lack it.
// ================================================================

#if defined(ARCH_X86) || defined(ARCH_AMD64)
    #define ARCH_HAS_PORT_IO    1
    #define ARCH_HAS_VGA_TEXT   1
    #define ARCH_HAS_PIC_8259   1
    #define ARCH_HAS_PS2        1
#else
    #define ARCH_HAS_PORT_IO    0
    #define ARCH_HAS_VGA_TEXT   0
    #define ARCH_HAS_PIC_8259   0
    #define ARCH_HAS_PS2        0
#endif

// USB host controller availability
// All current architectures have a USB HCI implementation:
//   x86/amd64 : UHCI (PCI, port I/O)
//   ARM       : DWC OTG (MMIO)
//   SPARC     : OHCI (SBus MMIO)
//   SPARC64   : OHCI (PCI MMIO)
//   IA-64     : OHCI (PCI MMIO)
//   RISC-V 64 : OHCI (PCI ECAM MMIO)
#define ARCH_HAS_USB  1

// ================================================================
// PCI Audio controller availability
//
// HDA / AC'97 : PCI MMIO — x86/amd64/ia64/sparc64/riscv64.
// CS4231 SBus : SPARC v8 (Sun4m) — SBus MMIO, no PCI.
// PL041 AACI  : ARM Versatile/RealView — MMIO AC'97 controller.
// ================================================================

#if defined(ARCH_X86) || defined(ARCH_AMD64)
    #define ARCH_HAS_PCI_AUDIO   1
    #define ARCH_HAS_SBUS_AUDIO  0
    #define ARCH_HAS_PL041_AUDIO 0
#elif defined(ARCH_IA64) || defined(ARCH_SPARC64) || defined(ARCH_RISCV64)
    #define ARCH_HAS_PCI_AUDIO   1
    #define ARCH_HAS_SBUS_AUDIO  0
    #define ARCH_HAS_PL041_AUDIO 0
#elif defined(ARCH_SPARC)
    #define ARCH_HAS_PCI_AUDIO   0
    #define ARCH_HAS_SBUS_AUDIO  1
    #define ARCH_HAS_PL041_AUDIO 0
#elif defined(ARCH_ARM)
    #define ARCH_HAS_PCI_AUDIO   0
    #define ARCH_HAS_SBUS_AUDIO  0
    #define ARCH_HAS_PL041_AUDIO 1
#else
    #define ARCH_HAS_PCI_AUDIO   0
    #define ARCH_HAS_SBUS_AUDIO  0
    #define ARCH_HAS_PL041_AUDIO 0
#endif

// ================================================================
// Storage controller availability
//
// ATA PIO : uses port I/O registers — x86/amd64 only.
// AHCI    : SATA over PCI MMIO — x86/amd64/ia64/sparc64/riscv64.
// NVMe    : PCIe MMIO — x86/amd64/ia64/sparc64/riscv64.
// ================================================================

#if defined(ARCH_X86) || defined(ARCH_AMD64)
    #define ARCH_HAS_ATA_PIO   1
    #define ARCH_HAS_AHCI      1
    #define ARCH_HAS_NVME      1
#elif defined(ARCH_IA64) || defined(ARCH_SPARC64) || defined(ARCH_RISCV64)
    #define ARCH_HAS_ATA_PIO   0
    #define ARCH_HAS_AHCI      1
    #define ARCH_HAS_NVME      1
#else
    #define ARCH_HAS_ATA_PIO   0
    #define ARCH_HAS_AHCI      0
    #define ARCH_HAS_NVME      0
#endif

// ================================================================
// Filesystem support
//
// FAT32/exFAT : universal — works on all architectures.
// ext4        : universal — native Linux FS, useful everywhere.
// UFS         : primarily for SPARC (Solaris/SunOS), also FreeBSD.
//               Compiled on all arches but most relevant for SPARC.
// ================================================================

#define ARCH_HAS_FS_FAT   1
#define ARCH_HAS_FS_EXT4  1
#define ARCH_HAS_FS_UFS   1

// ================================================================
// Graphics / framebuffer support
//
// VESA_BGA  : Bochs VBE (port I/O 0x01CE/0x01CF) — x86/amd64 only.
//             Used by QEMU -vga std, Bochs, VirtualBox.
// VGA_REGS  : Standard VGA register access (DAC, CRTC, sequencer,
//             attribute controller) via port I/O — x86/amd64 only.
// PCI_VGA   : PCI VGA BAR0 linear framebuffer discovery.
//             x86/amd64 via port-I/O PCI config; IA-64/SPARC64 via MMIO.
// EFI_GOP   : EFI Graphics Output Protocol framebuffer — IA-64 (and
//             any EFI-booted platform).
// SUN_FB    : Sun-specific framebuffers (TCX, CG3, CG6 for SPARC v8;
//             PCI VGA for SPARC v9).
// PL111_FB  : ARM PL111 CLCD controller (Versatile/RealView).
//             QEMU versatilepb maps at 0x10120000.
// RAMFB     : QEMU ramfb (RAM framebuffer via fw_cfg) — RISC-V 64.
// FB_CONSOLE: Software text console rendered onto the framebuffer.
//             Available on all architectures that have a framebuffer.
// ================================================================

#if defined(ARCH_X86) || defined(ARCH_AMD64)
#define ARCH_HAS_VESA_BGA   1
#define ARCH_HAS_VGA_REGS   1
#define ARCH_HAS_PCI_VGA    1
#define ARCH_HAS_EFI_GOP    0
#define ARCH_HAS_SUN_FB     0
#define ARCH_HAS_PL111_FB   0
#define ARCH_HAS_RAMFB      0
#define ARCH_HAS_FB_CONSOLE 1
#elif defined(ARCH_IA64)
#define ARCH_HAS_VESA_BGA   0
#define ARCH_HAS_VGA_REGS   0
#define ARCH_HAS_PCI_VGA    1
#define ARCH_HAS_EFI_GOP    1
#define ARCH_HAS_SUN_FB     0
#define ARCH_HAS_PL111_FB   0
#define ARCH_HAS_RAMFB      0
#define ARCH_HAS_FB_CONSOLE 1
#elif defined(ARCH_SPARC64)
#define ARCH_HAS_VESA_BGA   0
#define ARCH_HAS_VGA_REGS   0
#define ARCH_HAS_PCI_VGA    1
#define ARCH_HAS_EFI_GOP    0
#define ARCH_HAS_SUN_FB     1
#define ARCH_HAS_PL111_FB   0
#define ARCH_HAS_RAMFB      0
#define ARCH_HAS_FB_CONSOLE 1
#elif defined(ARCH_SPARC)
#define ARCH_HAS_VESA_BGA   0
#define ARCH_HAS_VGA_REGS   0
#define ARCH_HAS_PCI_VGA    0
#define ARCH_HAS_EFI_GOP    0
#define ARCH_HAS_SUN_FB     1
#define ARCH_HAS_PL111_FB   0
#define ARCH_HAS_RAMFB      0
#define ARCH_HAS_FB_CONSOLE 1
#elif defined(ARCH_ARM)
    #define ARCH_HAS_VESA_BGA   0
    #define ARCH_HAS_VGA_REGS   0
    #define ARCH_HAS_PCI_VGA    0
    #define ARCH_HAS_EFI_GOP    0
    #define ARCH_HAS_SUN_FB     0
    #define ARCH_HAS_PL111_FB   1
    #define ARCH_HAS_RAMFB      0
    #define ARCH_HAS_FB_CONSOLE 1
#elif defined(ARCH_RISCV64)
    #define ARCH_HAS_VESA_BGA   0
    #define ARCH_HAS_VGA_REGS   0
    #define ARCH_HAS_PCI_VGA    1
    #define ARCH_HAS_EFI_GOP    0
    #define ARCH_HAS_SUN_FB     0
    #define ARCH_HAS_PL111_FB   0
    #define ARCH_HAS_RAMFB      1
    #define ARCH_HAS_FB_CONSOLE 1
#else
    #define ARCH_HAS_VESA_BGA   0
    #define ARCH_HAS_VGA_REGS   0
    #define ARCH_HAS_PCI_VGA    0
    #define ARCH_HAS_EFI_GOP    0
    #define ARCH_HAS_SUN_FB     0
    #define ARCH_HAS_PL111_FB   0
    #define ARCH_HAS_RAMFB      0
    #define ARCH_HAS_FB_CONSOLE 0
#endif

namespace kernel {
namespace arch {

// Get architecture name
inline const char* get_arch_name()
{
#if defined(ARCH_AMD64)
    return "AMD64 (x86-64)";
#elif defined(ARCH_X86)
    return "x86 (32-bit)";
#elif defined(ARCH_ARM)
    return "ARM";
#elif defined(ARCH_IA64)
    return "Itanium (IA-64)";
#elif defined(ARCH_SPARC64)
    return "SPARC v9 (UltraSPARC)";
#elif defined(ARCH_SPARC)
    return "SPARC v8";
#elif defined(ARCH_RISCV64)
    return "RISC-V 64 (RV64IMA)";
#else
    return "Unknown";
#endif
}

// Get architecture bitness
inline uint32_t get_arch_bits()
{
#if defined(ARCH_AMD64) || defined(ARCH_IA64) || defined(ARCH_SPARC64) || defined(ARCH_RISCV64)
    return 64;
#elif defined(ARCH_X86) || defined(ARCH_ARM) || defined(ARCH_SPARC)
    return 32;
#else
    return 0;
#endif
}

} // namespace arch
} // namespace kernel
