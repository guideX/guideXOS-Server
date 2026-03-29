/*
 * Itanium (IA-64) Kernel Linker Script
 *
 * Targets the HP ski simulator.  The kernel is loaded as a raw
 * ELF64-ia64-little image starting at 1 MB physical.
 *
 * The IVT (Interrupt Vector Table) must be 32 KB aligned.
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1 MB */
    . = 1M;

    __kernel_start = .;

    /* IVT — 32 KB aligned, must be before .text so cr.iva can
     * point to a fixed address early in the image. */
    .ivt ALIGN(32K) :
    {
        *(.ivt)
    }

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

    /* Short data (GP-relative addressing) */
    .sdata ALIGN(4K) :
    {
        __gp = . + 0x200000;   /* GP = start of sdata + 2 MB bias */
        *(.sdata)
        *(.sdata.*)
    }

    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
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

    /* IA-64 unwind tables (needed by GCC for stack unwinding) */
    .IA_64.unwind_info :
    {
        *(.IA_64.unwind_info*)
    }

    .IA_64.unwind :
    {
        *(.IA_64.unwind*)
    }

    __kernel_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
