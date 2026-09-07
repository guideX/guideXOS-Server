OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

KERNEL_LOAD = 0x40000000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = KERNEL_LOAD;
    __kernel_start = .;

    .text : ALIGN(0x1000)
    {
        __text_start = .;
        KEEP(*(.text.entry))
        *(.text .text.*)
        __text_end = .;
    } :text

    .vectors ALIGN(0x800) :
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } :text

    . = ALIGN(0x1000);
    .rodata : ALIGN(0x1000)
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    } :rodata

    . = ALIGN(0x1000);
    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(0x1000)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    .translation_tables (NOLOAD) : ALIGN(0x1000)
    {
        __translation_tables_start = .;
        KEEP(*(.translation_tables))
        __translation_tables_end = .;
    } :data

    . = ALIGN(0x1000);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame)
        *(.ARM.exidx .ARM.exidx.*)
    }
}

