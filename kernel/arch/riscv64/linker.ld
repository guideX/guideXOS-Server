/*
 * RISC-V 64-bit (RV64) Kernel Linker Script
 *
 * Targets QEMU virt machine with OpenSBI.  The kernel is loaded
 * as an ELF64-littleriscv image at 0x80200000 (the default
 * OpenSBI fw_jump payload address).
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x80200000 (OpenSBI fw_jump convention) */
    . = 0x80200000;

    __kernel_start = .;

    /* Boot section (entry point, must be first) */
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

    /* Small data (GP-relative addressing) */
    .sdata ALIGN(4K) :
    {
        __global_pointer$ = . + 0x800;
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

    __kernel_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.riscv.attributes)
    }
}
