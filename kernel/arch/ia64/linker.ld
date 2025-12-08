/*
 * Itanium (IA-64) Kernel Linker Script
 *
 * Copyright (c) 2024 guideX
 */

OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB */
    . = 1M;
    
    __kernel_start = .;
    
    /* Boot section (must be first) */
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
        __bss_end = .;
    }
    
    /* IA-64 unwind tables */
    .IA_64.unwind_info :
    {
        *(.IA_64.unwind_info*)
    }
    
    .IA_64.unwind :
    {
        *(.IA_64.unwind*)
    }
    
    __kernel_end = .;
}
