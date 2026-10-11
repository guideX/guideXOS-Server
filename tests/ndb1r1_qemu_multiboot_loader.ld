ENTRY(_start)
PHDRS
{
    loader PT_LOAD FLAGS(7);
}
SECTIONS
{
    . = 0x30000000;
    .boot : ALIGN(0x1000) { *(.boot) } :loader
    .text : ALIGN(0x1000) { *(.text) *(.text.*) } :loader
    .rodata : ALIGN(0x1000) { *(.rodata) *(.rodata.*) } :loader
    .data : ALIGN(0x1000) { *(.data) *(.data.*) } :loader
    .bss (NOLOAD) : ALIGN(0x1000) { *(.bss) *(.bss.*) *(COMMON) } :loader
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
