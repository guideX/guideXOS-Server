OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(_start)
SECTIONS
{
  . = 0x100000;
  .text ALIGN(0x8000) : { *(.text.start) *(.text .text.*) }
  .rodata ALIGN(8) : { *(.rodata .rodata.*) }
  /DISCARD/ : { *(.comment) *(.note*) }
}
