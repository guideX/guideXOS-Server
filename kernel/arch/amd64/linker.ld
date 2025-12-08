/*
 * AMD64 Kernel Linker Script
 *
 * Copyright (c) 2024 guideX
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
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
    
    __kernel_end = .;
}
