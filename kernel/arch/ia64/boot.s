/*
 * Itanium (IA-64) Bootloader Entry Point
 *
 * Copyright (c) 2024 guideX
 */

.section .boot, "ax"
.global _start
.proc _start

_start:
    /* Disable interrupts */
    rsm psr.i | psr.ic
    srlz.d
    
    /* Set up global pointer */
    movl gp = __gp
    
    /* Set up stack pointer */
    movl sp = stack_top
    
    /* Clear BSS section */
    movl r14 = __bss_start
    movl r15 = __bss_end
    mov r16 = 0
    
clear_bss:
    cmp.geu p6, p7 = r14, r15
(p7) st8 [r14] = r16, 8
(p7) br.cond.dptk clear_bss
    
    /* Jump to kernel main */
    br.call.sptk.many rp = kernel_main
    
halt:
    /* Halt the CPU */
    hint @pause
    br halt
    
.endp _start

.section .bss
.align 16
stack_bottom:
    .space 16384    /* 16 KB stack */
stack_top:
