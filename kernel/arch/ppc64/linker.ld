/*
 * PowerPC64 Kernel Linker Script
 *
 * Targets QEMU pseries machine.  The kernel is loaded as an
 * ELF64-powerpc-big image at 0x20000000 (512MB).
 *
 * QEMU pseries memory map (simplified):
 *   0x00000000 - 0x00007FFF : Exception vectors (copied at runtime)
 *   0x00008000 - 0x1FFFFFFF : Low memory (firmware, RTAS, device tree)
 *   0x20000000 - onwards    : Kernel load address
 *
 * PowerPC64 ELFv2 ABI requirements:
 *   - .toc section for Table of Contents
 *   - Global entry point at function+0
 *   - Local entry point at function+8 (after TOC setup)
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-powerpc", "elf64-powerpc", "elf64-powerpc")
OUTPUT_ARCH(powerpc:common64)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x20000000 (512MB - QEMU pseries convention) */
    . = 0x20000000;

    __kernel_start = .;

    /* Boot section (entry point and exception vectors) */
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

    /* Table of Contents (TOC) for ELFv2 ABI
     * .toc contains pointers to global data
     * .got contains global offset table entries
     */
    .toc ALIGN(8) :
    {
        /* Provide symbol for TOC base (r2 points here) */
        PROVIDE(.TOC. = . + 0x8000);
        *(.toc)
        *(.toc.*)
    }

    .got ALIGN(8) :
    {
        *(.got)
        *(.got.plt)
    }

    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    /* Uninitialized data */
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        *(.sbss)
        *(.sbss.*)
        __bss_end = .;
    }

    __kernel_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.plt)
        *(.interp)
        *(.gnu*)
    }
}
