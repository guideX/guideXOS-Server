/*
 * MIPS64 Kernel Linker Script
 *
 * Targets QEMU malta machine.  The kernel is loaded as an ELF64
 * image at 0xFFFFFFFF80100000 (kseg0 address).
 *
 * MIPS64 memory map (64-bit kernel mode):
 *   0x0000000000000000 - 0x3FFFFFFFFFFFFFFF : User space (useg/xuseg)
 *   0x4000000000000000 - 0x7FFFFFFFFFFFFFFF : User space (xuseg)
 *   0x8000000000000000 - 0xBFFFFFFFFFFFFFFF : Kernel (xkphys - physical map)
 *   0xC000000000000000 - 0xDFFFFFFFFFFFFFFF : Kernel (xkseg - supervisor)
 *   0xE000000000000000 - 0xFFFFFFFFFFFFFFFF : Kernel (xkseg - kernel)
 *
 * 32-bit compatibility (using 64-bit sign-extended addresses):
 *   0xFFFFFFFF80000000 - 0xFFFFFFFF9FFFFFFF : kseg0 (cached, unmapped)
 *   0xFFFFFFFFA0000000 - 0xFFFFFFFFBFFFFFFF : kseg1 (uncached, unmapped)
 *   0xFFFFFFFFC0000000 - 0xFFFFFFFFDFFFFFFF : kseg2 (cached, mapped)
 *   0xFFFFFFFFE0000000 - 0xFFFFFFFFFFFFFFFF : kseg3 (cached, mapped)
 *
 * Physical addresses:
 *   0x00000000 - 0x0FFFFFFF : RAM (256 MB on malta)
 *   0x10000000 - 0x17FFFFFF : PCI memory space
 *   0x18000000 - 0x1BFFFFFF : PCI I/O space  
 *   0x1F000000 - 0x1FFFFFFF : ISA I/O space (UART at 0x1F000900)
 *   0x1FC00000 - 0x1FCFFFFF : Boot ROM
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-tradlittlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)

/* Kernel load address in kseg0 (cached, unmapped) */
KERNEL_VADDR = 0xFFFFFFFF80100000;
KERNEL_PADDR = 0x00100000;

SECTIONS
{
    /* Start at kseg0 virtual address */
    . = KERNEL_VADDR;

    __kernel_start = .;
    __vectors_start = .;

    /* Exception vectors must be at specific offsets from EBase
     * We place them at the start of the kernel image
     */
    .vectors ALIGN(4K) :
    {
        *(.vectors)
    }

    __vectors_end = .;

    /* Boot section (entry point) */
    .boot ALIGN(4K) :
    {
        *(.boot)
    }

    /* Text section (code) */
    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* MIPS-specific sections */
    .MIPS.options :
    {
        *(.MIPS.options)
    }

    .MIPS.abiflags :
    {
        *(.MIPS.abiflags)
    }

    /* Global pointer base (for GP-relative addressing)
     * GP-relative addressing is disabled (-G 0) but we define
     * _gp anyway for compatibility.
     */
    .sdata ALIGN(4K) :
    {
        _gp = . + 0x7FF0;
        *(.sdata)
        *(.sdata.*)
        *(.lit8)
        *(.lit4)
    }

    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }

    /* GOT (Global Offset Table) - for PIC code, but we don't use PIC */
    .got ALIGN(4K) :
    {
        *(.got)
        *(.got.plt)
    }

    /* Uninitialized data */
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;

    /* Debug sections (optional, for debugging with GDB) */
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_loc      0 : { *(.debug_loc) }
    .debug_ranges   0 : { *(.debug_ranges) }

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu.attributes)
        *(.reginfo)
        *(.pdr)
    }
}
