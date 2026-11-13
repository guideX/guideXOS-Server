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
    . = 1M;
    __kernel_start = .;
    .ivt ALIGN(32K) : { *(.ivt) } :text
    .text ALIGN(4K) :
    {
        __text_start = .;
        *(.text.start) *(.text) *(.text.*)
        __text_end = .;
    } :text
    .rodata ALIGN(4K) : { *(.rodata) *(.rodata.*) *(.IA_64.unwind_info*) *(.IA_64.unwind*) } :rodata
    .sdata ALIGN(4K) :
    {
        __gp = . + 0x200000;
        *(.sdata) *(.sdata.*) *(.got) *(.got.*)
    } :data
    .sbss ALIGN(16) (NOLOAD) : { *(.sbss) *(.sbss.*) } :data
    .data ALIGN(4K) : { *(.data) *(.data.*) } :data
    .bss ALIGN(4K) (NOLOAD) :
    {
        __bss_start = .;
        *(.bss) *(.bss.*) *(COMMON)
        __bss_end = .;
    } :data
    __kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) }
}

ASSERT((ADDR(.ivt) & 0x7fff) == 0, "probe IVT alignment")
ASSERT(SIZEOF(.ivt) == 0x8000, "probe IVT size")
ASSERT((ia64_ivt_end - ia64_ivt) == 0x8000, "probe IVT symbol span")
ASSERT((ia64_coop_context_a & 0x1ff) == 0, "context A 512-byte alignment")
ASSERT((ia64_coop_context_b & 0x1ff) == 0, "context B 512-byte alignment")
ASSERT((ia64_coop_stack_a_bottom & 0xf) == 0, "stack A alignment")
ASSERT((ia64_coop_stack_b_bottom & 0xf) == 0, "stack B alignment")
ASSERT((ia64_coop_rbs_a_base & 0x1ff) == 0, "RBS A collection alignment")
ASSERT((ia64_coop_rbs_b_base & 0x1ff) == 0, "RBS B collection alignment")
ASSERT((ia64_coop_stack_a_top - ia64_coop_stack_a_bottom) == 65536, "stack A size")
ASSERT((ia64_coop_stack_b_top - ia64_coop_stack_b_bottom) == 65536, "stack B size")
ASSERT((ia64_coop_rbs_a_limit - ia64_coop_rbs_a_base) == 65536, "RBS A size")
ASSERT((ia64_coop_rbs_b_limit - ia64_coop_rbs_b_base) == 65536, "RBS B size")
