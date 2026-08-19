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
    __guidexos_native_image_base = __kernel_start;
    
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

    __guidexos_native_executable_start = ADDR(.boot);
    __guidexos_native_executable_end = .;
    
    /* Read-only data */
    .rodata ALIGN(4096) :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /*
     * Win64 AMD64 unwind records are emitted in ordinary COFF .pdata/.xdata
     * input sections, including COMDAT/group variants such as .pdata$foo.
     * Keep the complete input families in loadable read-only output sections.
     * The native provider deliberately performs a bounded linear lookup, so
     * this layout does not claim that linker input order sorts records by
     * BeginAddress.
     */
    .pdata ALIGN(4) :
    {
        __guidexos_native_pdata_start = .;
        KEEP(*(.pdata*))
        __guidexos_native_pdata_end = .;
    }

    .xdata ALIGN(4) :
    {
        __guidexos_native_xdata_start = .;
        KEEP(*(.xdata*))
        __guidexos_native_xdata_end = .;
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
        *(.debug*)
    }
}
