/*
 * AMD64 Kernel Linker Script for MinGW
 *
 * Copyright (c) 2024 guideX
 */

/* MinGW produces PE/COFF format - we'll convert to flat binary */
ENTRY(_start)

SECTIONS
{
    /* Start at 0 for PE format - bootloader will relocate */
    . = 0x0;
    
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
    
    /* Discard unnecessary PE sections */
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
