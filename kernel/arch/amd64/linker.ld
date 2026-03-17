/*
 * AMD64 Kernel Linker Script for MinGW (PE/COFF)
 *
 * Copyright (c) 2024 guideX
 */

/* PE/COFF output - will be converted to ELF64 */
ENTRY(_start)

SECTIONS
{
    /* Start at 1MB - standard kernel load address */
    . = 0x100000;
    
    __kernel_start = .;
    
    /* Boot section at 1MB (must be first, contains _start entry point) */
    .boot : ALIGN(4096)
    {
        *(.boot)
    }
    
    /* Text section (code) */
    .text ALIGN(4096) :
    {
        *(.text)
        *(.text.*)
    }
    
    /* Read-only data */
    .rodata ALIGN(4096) :
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    /* Initialized data */
    .data ALIGN(4096) :
    {
        *(.data)
        *(.data.*)
    }
    
    /* Uninitialized data */
    .bss ALIGN(4096) :
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
        *(.pdata)
        *(.xdata)
        *(.debug*)
    }
}
