/*
 * SPARC v8 Kernel Entry Point
 *
 * Entered by OpenBoot PROM (or QEMU -kernel).
 * Sets up a minimal environment and jumps to kernel_main().
 *
 * Copyright (c) 2024 guideX
 */

.section .boot, "ax"
.global _start

_start:
    /* Disable traps (clear ET bit 5 in PSR) and set PIL to max */
    rd %psr, %l0
    andn %l0, 0x20, %l0        /* clear ET (enable traps) */
    or   %l0, 0x0F00, %l0      /* set PIL = 15 (mask all interrupts) */
    wr   %l0, %psr
    nop
    nop
    nop

    /* Set up stack pointer (grows downward, 8-byte aligned) */
    set stack_top, %sp
    sub %sp, 96, %sp            /* SPARC ABI: 16 words for save area + args */

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
    st  %o2, [%o0]
    ba  clear_bss
    add %o0, 4, %o0

clear_bss_done:
    /* Call kernel_main(boot_environment=0, boot_magic=0)
     * SPARC calling convention: first arg in %o0, second in %o1
     * On bare SPARC there is no Multiboot or UEFI — pass zeros.
     */
    clr %o0
    clr %o1
    call kernel_main
    nop

    /* Should never reach here */
halt:
    ba halt
    nop

.section .bss
.align 16
stack_bottom:
    .skip 16384    /* 16 KB stack */
stack_top:
