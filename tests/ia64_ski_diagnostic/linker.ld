OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(_start)

PHDRS
{
  text   PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data   PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0x100000;
  .text ALIGN(16) : { *(.text.start) *(.text) *(.text.*) } :text
  . = ALIGN(0x8000);
  .ivt : { *(.ivt) } :text
  .rodata ALIGN(8) : { *(.rodata) *(.rodata.*) } :rodata
  .sdata ALIGN(8) :
  {
    __gp = . + 0x200000;
    *(.sdata) *(.sdata.*) *(.got) *(.got.*)
  } :data
  .sbss ALIGN(16) (NOLOAD) : { *(.sbss) *(.sbss.*) } :data
  .data ALIGN(16) : { *(.data) *(.data.*) } :data
  .bss ALIGN(16) (NOLOAD) : { *(.bss) *(.bss.*) *(COMMON) } :data
  /DISCARD/ : { *(.comment) *(.note*) }
}

ASSERT((ADDR(.ivt) & 0x7fff) == 0, "diagnostic IVA must be 32-KiB aligned")
ASSERT(SIZEOF(.ivt) == 0x8000, "diagnostic IVT span must be 32 KiB")
