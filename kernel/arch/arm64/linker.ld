/*
 * ARM64 Kernel Linker Script
 *
 * Targets QEMU virt machine. The kernel is loaded as an ELF64
 * image at 0x40000000 (RAM start on virt machine).
 *
 * QEMU virt machine memory map (typical):
 *   0x00000000 - 0x07FFFFFF : Flash memory (128 MB)
 *   0x08000000 - 0x0800FFFF : GIC Distributor
 *   0x08010000 - 0x0801FFFF : GIC CPU Interface
 *   0x09000000 - 0x09000FFF : UART (PL011)
 *   0x09010000 - 0x09010FFF : RTC (PL031)
 *   0x0A000000 - 0x0A0001FF : fw_cfg
 *   0x0C000000 - 0x0DFFFFFF : Platform bus
 *   0x10000000 - 0x3EFFFFFF : PCI MMIO
 *   0x3F000000 - 0x3FFFFFFF : PCI MMIO high
 *   0x40000000 - 0xFFFFFFFF : RAM (configurable size)
 *
 * Memory with -m 512M:
 *   0x40000000 - 0x5FFFFFFF : RAM (512 MB)
 *
 * ARM64 uses 4KB, 16KB, or 64KB pages.
 * We use 4KB pages for compatibility.
 *
 * Virtual address space (with 48-bit addresses):
 *   0x0000000000000000 - 0x0000FFFFFFFFFFFF : User space (TTBR0)
 *   0xFFFF000000000000 - 0xFFFFFFFFFFFFFFFF : Kernel space (TTBR1)
 *
 * This script places the kernel at the physical address,
 * using identity mapping initially.
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

/* Kernel load address (QEMU virt RAM start) */
KERNEL_PADDR = 0x40000000;

/* Kernel virtual address (higher-half mapping, optional) */
/* For now, use identity mapping */
KERNEL_VADDR = KERNEL_PADDR;

/* Page size */
PAGE_SIZE = 4096;

SECTIONS
{
    /* Start at physical/virtual address */
    . = KERNEL_VADDR;

    __kernel_start = .;

    /* Boot section (entry point, must be first) */
    .boot ALIGN(PAGE_SIZE) :
    {
        __boot_start = .;
        *(.boot)
        __boot_end = .;
    }

    /* Exception vectors (must be 2KB aligned) */
    .vectors ALIGN(2048) :
    {
        __vectors_start = .;
        *(.vectors)
        __vectors_end = .;
    }

    /* Text section (code) */
    .text ALIGN(PAGE_SIZE) :
    {
        __text_start = .;
        *(.text)
        *(.text.*)
        __text_end = .;
    }

    /* Read-only data */
    .rodata ALIGN(PAGE_SIZE) :
    {
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        __rodata_end = .;
    }

    /* Exception handling tables (C++) */
    .eh_frame ALIGN(8) :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
    }

    /* ARM-specific unwind information */
    .ARM.exidx ALIGN(8) :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    }

    /* Initialized data */
    .data ALIGN(PAGE_SIZE) :
    {
        __data_start = .;
        *(.data)
        *(.data.*)
        __data_end = .;
    }

    /* Global offset table (for PIC, if used) */
    .got ALIGN(8) :
    {
        *(.got)
        *(.got.plt)
    }

    /* Small data section */
    .sdata ALIGN(8) :
    {
        *(.sdata)
        *(.sdata.*)
    }

    /* Uninitialized data (BSS) */
    .bss ALIGN(PAGE_SIZE) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Kernel end marker */
    . = ALIGN(PAGE_SIZE);
    __kernel_end = .;

    /* Calculate sizes */
    __kernel_size = __kernel_end - __kernel_start;
    __bss_size = __bss_end - __bss_start;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.rel.*)
        *(.rela.*)
        *(.interp)
    }
}

/* Provide symbols for kernel code */
PROVIDE(__stack_size = 16384);
PROVIDE(__heap_size = 0x100000);  /* 1 MB initial heap */
