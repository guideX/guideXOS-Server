/*
 * LoongArch 64-bit (LA64) Kernel Linker Script
 *
 * Targets QEMU loongarch64-virt machine. The kernel is loaded as
 * an ELF64-loongarch64 image.
 *
 * Memory layout on QEMU loongarch64-virt:
 *   0x00000000_00000000 - 0x0FFFFFFF_FFFFFFFF : User space
 *   0x80000000_00000000 - 0x8FFFFFFF_FFFFFFFF : Direct Mapping Window (uncached)
 *   0x90000000_00000000 - 0x9FFFFFFF_FFFFFFFF : Direct Mapping Window (cached)
 *
 * The kernel runs in the cached direct mapping region (0x9...).
 * Physical memory starts at 0x0 and the kernel is typically loaded
 * at physical 0x200000 (2MB offset).
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-loongarch")
OUTPUT_ARCH(loongarch)
ENTRY(_start)

/* Kernel virtual base address (direct mapped, cached) */
KERNEL_VMA = 0x9000000000200000;

/* Corresponding physical address */
KERNEL_LMA = 0x0000000000200000;

SECTIONS
{
    /* Kernel starts at 2MB offset (0x200000) in the cached DMW region */
    . = KERNEL_VMA;

    __kernel_start = .;
    __kernel_phys_start = KERNEL_LMA;

    /* ================================================================
     * Boot section (entry point, must be first)
     * Contains the _start symbol and early boot code.
     * ================================================================ */
    .boot ALIGN(4K) : AT(KERNEL_LMA)
    {
        __boot_start = .;
        *(.boot)
        *(.boot.*)
        __boot_end = .;
    }

    /* ================================================================
     * Text section (code)
     * Contains all executable code.
     * ================================================================ */
    .text ALIGN(4K) : AT(KERNEL_LMA + SIZEOF(.boot))
    {
        __text_start = .;
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        __text_end = .;
    }

    /* ================================================================
     * Read-only data
     * Constants, string literals, etc.
     * ================================================================ */
    .rodata ALIGN(4K) : AT(KERNEL_LMA + SIZEOF(.boot) + SIZEOF(.text))
    {
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
        __rodata_end = .;
    }

    /* ================================================================
     * Exception table (for C++ exception handling if needed)
     * ================================================================ */
    .eh_frame ALIGN(8) : AT(KERNEL_LMA + SIZEOF(.boot) + SIZEOF(.text) + SIZEOF(.rodata))
    {
        __eh_frame_start = .;
        *(.eh_frame)
        *(.eh_frame.*)
        __eh_frame_end = .;
    }

    /* ================================================================
     * Global Offset Table (for position-independent code)
     * ================================================================ */
    .got ALIGN(8) :
    {
        __got_start = .;
        *(.got)
        *(.got.plt)
        __got_end = .;
    }

    /* ================================================================
     * Initialized data
     * Global variables with initial values.
     * ================================================================ */
    .data ALIGN(4K) :
    {
        __data_start = .;
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        __data_end = .;
    }

    /* ================================================================
     * Constructors and destructors (for C++ static init)
     * ================================================================ */
    .init_array ALIGN(8) :
    {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        __init_array_end = .;
    }

    .fini_array ALIGN(8) :
    {
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(.fini_array.*))
        __fini_array_end = .;
    }

    /* ================================================================
     * Uninitialized data (BSS)
     * Global variables without initial values (zero-initialized).
     * ================================================================ */
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        __bss_end = .;
    }

    /* ================================================================
     * Kernel end markers
     * ================================================================ */
    . = ALIGN(4K);
    __kernel_end = .;
    __kernel_size = __kernel_end - __kernel_start;

    /* Physical end address */
    __kernel_phys_end = KERNEL_LMA + __kernel_size;

    /* ================================================================
     * Debug sections (optional, stripped in release)
     * ================================================================ */
    .debug_info     0 : { *(.debug_info)     }
    .debug_abbrev   0 : { *(.debug_abbrev)   }
    .debug_line     0 : { *(.debug_line)     }
    .debug_frame    0 : { *(.debug_frame)    }
    .debug_str      0 : { *(.debug_str)      }
    .debug_loc      0 : { *(.debug_loc)      }
    .debug_ranges   0 : { *(.debug_ranges)   }
    .debug_aranges  0 : { *(.debug_aranges)  }

    /* ================================================================
     * Discard unwanted sections
     * ================================================================ */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.interp)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
    }
}

/* ================================================================
 * Linker script assertions
 * ================================================================ */

/* Ensure boot section is at expected location */
ASSERT(__boot_start == KERNEL_VMA, "Boot section not at kernel base!")

/* Ensure BSS is properly aligned */
ASSERT(__bss_start == ALIGN(__bss_start, 4K), "BSS not page aligned!")
