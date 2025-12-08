/*
 * SPARC Bootloader Entry Point
 *
 * Copyright (c) 2024 guideX
 */

.section .boot, "ax"
.global _start

_start:
    /* Disable interrupts */
    rd %psr, %l0
    or %l0, 0x20, %l0       /* Set PIL to 15 (disable interrupts) */
    wr %l0, %psr
    nop
    nop
    nop
    
    /* Set up stack pointer */
    set stack_top, %sp
    sub %sp, 64, %sp        /* SPARC window save area */
    
    /* Clear frame pointer */
    clr %fp
    
    /* Clear BSS section */
    set __bss_start, %o0
    set __bss_end, %o1
    clr %o2

clear_bss:
    cmp %o0, %o1
    bge clear_bss_done
    nop
    st %o2, [%o0]
    ba clear_bss
    add %o0, 4, %o0

clear_bss_done:
    /* Jump to kernel main */
    call kernel_main
    nop
    
halt:
    /* Halt the CPU */
    ba halt
    nop

.section .bss
.align 16
stack_bottom:
    .skip 16384    /* 16 KB stack */
stack_top:
