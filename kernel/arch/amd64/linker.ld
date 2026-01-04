/*
 * AMD64 Kernel Linker Script (ELF64)
 *
 * Copyright (c) 2024 guideX
 */

/* ELF64 output format */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    /* Start at a reasonable kernel virtual address - bootloader will load and relocate */
    . = 0x100000;
    
    __kernel_start = .;
    
    /* Boot section (must be first) */
    .boot ALIGN(4096) :
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
