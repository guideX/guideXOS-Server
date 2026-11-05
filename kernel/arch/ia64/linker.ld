/*
 * Itanium (IA-64) Kernel Linker Script
 *
 * Targets the HP ski simulator.  The kernel is loaded as a raw
 * ELF64-ia64-little image starting at 1 MB physical.
 *
 * The IVT (Interrupt Vector Table) must be 32 KB aligned.
 *
 * Copyright (c) 2026 guideXOS Server
 */

OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data   PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
    /* Kernel starts at 1 MB */
    . = 1M;

    __kernel_start = .;

    /* IVT: address alignment is enforced here. Its architectural layout and
     * extent are validated separately; linking alone does not qualify it. */
    .ivt ALIGN(32K) :
    {
        *(.ivt)
    } :text

    /* Boot entry point and executable code */
    .boot ALIGN(4K) :
    {
        *(.boot)
    } :text

    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    } :text

    .plt ALIGN(16) :
    {
        *(.plt)
        *(.plt.*)
    } :text

    /* Read-only data, including IA-64 function descriptors and unwind data */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
        *(.gcc_except_table*)
        *(.eh_frame*)
    } :rodata

    .opd ALIGN(16) (READONLY) :
    {
        *(.opd)
    } :rodata

    .IA_64.unwind_info :
    {
        *(.IA_64.unwind_info*)
    } :rodata

    .IA_64.unwind :
    {
        *(.IA_64.unwind*)
    } :rodata

    /* Short data (GP-relative addressing) */
    .sdata ALIGN(4K) :
    {
        __gp = . + 0x200000;   /* GP = start of sdata + 2 MB bias */
        *(.sdata)
        *(.sdata.*)
        *(.got)
        *(.got.*)
    } :data

    /* Keep zero-initialized GP-relative data beside .sdata. Placing .sbss at
     * the end of the large kernel BSS can exceed IA-64's short-data range. */
    .sbss ALIGN(16) (NOLOAD) :
    {
        *(.sbss)
        *(.sbss.*)
    } :data

    /* Initialized and uninitialized writable data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    } :data

    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    __kernel_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}

ASSERT(ADDR(.ivt) == ia64_ivt,
       "IA-64 IVT base symbol must identify the start of .ivt")
ASSERT((ADDR(.ivt) & 0x7fff) == 0,
       "IA-64 IVT must be aligned to 0x8000 bytes")
ASSERT(SIZEOF(.ivt) == 0x8000,
       "IA-64 IVT output section must be exactly 0x8000 bytes")
ASSERT((ia64_ivt_end - ia64_ivt) == 0x8000,
       "IA-64 IVT symbols must span exactly 0x8000 bytes")
ASSERT(ia64_ivt_end == (ADDR(.ivt) + SIZEOF(.ivt)),
       "IA-64 IVT end symbol must match the output section boundary")
