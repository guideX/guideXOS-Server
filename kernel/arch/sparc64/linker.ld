/*
 * SPARC v9 (UltraSPARC) Kernel Linker Script
 *
 * 64-bit ELF for SPARC v9 / Sun4u.
 *
 * The kernel is loaded at 0x4000 (same low address as v8 for QEMU
 * -kernel compatibility).  The trap table requires 32-KB alignment.
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-sparc")
OUTPUT_ARCH(sparc:v9)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x4000 */
    . = 0x4000;

    __kernel_start = .;

    /* Trap vector table (must be 32-KB aligned for TBA) */
    .trapvec ALIGN(32K) :
    {
        *(.trapvec)
    }

    /* Boot section */
    .boot ALIGN(8K) :
    {
        *(.boot)
    }

    /* Text section (code) */
    .text ALIGN(8K) :
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata ALIGN(8K) :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialized data */
    .data ALIGN(8K) :
    {
        *(.data)
        *(.data.*)
    }

    /* Uninitialized data */
    .bss ALIGN(8K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note*)
        *(.debug*)
    }
}
