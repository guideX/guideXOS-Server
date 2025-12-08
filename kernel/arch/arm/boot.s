/*
 * ARM Bootloader Entry Point
 *
 * Copyright (c) 2024 guideX
 */

.section .boot, "ax"
.global _start

_start:
    /* Disable interrupts */
    cpsid if
    
    /* Read CPU ID from MPIDR */
    mrc p15, 0, r5, c0, c0, 5
    and r5, r5, #3
    cmp r5, #0
    bne halt_cpu        /* Only CPU 0 continues */
    
    /* Set up stack pointer */
    ldr sp, =stack_top
    
    /* Clear BSS section */
    ldr r4, =__bss_start
    ldr r9, =__bss_end
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0

clear_bss:
    cmp r4, r9
    bge clear_bss_done
    stmia r4!, {r5-r8}
    b clear_bss

clear_bss_done:
    /* Jump to kernel main */
    bl kernel_main
    
halt_cpu:
    /* Halt the CPU */
    wfi
    b halt_cpu

.section .bss
.align 16
stack_bottom:
    .space 16384    /* 16 KB stack */
stack_top:
