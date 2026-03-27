/*
 * SPARC v8 Kernel Entry Point & Trap Table
 *
 * Entered by OpenBoot PROM (or QEMU -kernel).
 * Provides a 256-entry trap table (required by SPARC v8 architecture),
 * window overflow/underflow handlers, IRQ dispatch, and startup code
 * that jumps to kernel_main().
 *
 * Copyright (c) 2024 guideX
 */

/* ================================================================
 * Trap table
 *
 * SPARC v8 requires the trap table to be aligned to 4096 bytes.
 * Each entry is exactly 4 instructions (16 bytes).
 * Entries 0x00-0x04: reset / instruction faults
 * Entry  0x05      : window overflow
 * Entry  0x06      : window underflow
 * Entries 0x11-0x1F: hardware interrupts (IRQ 1-15)
 * All other traps jump to a generic handler.
 * ================================================================ */

.section .trapvec, "ax"
.align 4096
.global sparc_trap_table

sparc_trap_table:

/* --- Trap 0x00: reset --- */
    ba _start; nop; nop; nop

/* --- Traps 0x01-0x04: instruction/data faults (generic) --- */
.rept 4
    ba trap_generic; nop; nop; nop
.endr

/* --- Trap 0x05: window overflow --- */
    ba window_overflow_handler; nop; nop; nop

/* --- Trap 0x06: window underflow --- */
    ba window_underflow_handler; nop; nop; nop

/* --- Traps 0x07-0x10: alignment, FP, etc (generic) --- */
.rept 10
    ba trap_generic; nop; nop; nop
.endr

/* --- Traps 0x11-0x1F: hardware interrupts (IRQ 1 through 15) --- */
/* Trap number = 0x10 + IRQ level.  The IRQ level is encoded in the
 * TBR bits [11:4], which equals (trap_type << 4).  We extract the
 * IRQ number in the shared irq_dispatch_asm routine. */
.rept 15
    ba irq_dispatch_asm; nop; nop; nop
.endr

/* --- Traps 0x20-0x7F: software traps and remaining hardware --- */
.rept 96
    ba trap_generic; nop; nop; nop
.endr

/* --- Traps 0x80-0xFF: software traps (ta instruction) --- */
.rept 128
    ba trap_generic; nop; nop; nop
.endr

/* ================================================================
 * Window overflow handler (trap 0x05)
 *
 * Saves the oldest register window to memory so a new SAVE can
 * proceed.  Uses the standard rotate-and-save algorithm.
 * ================================================================ */
.global window_overflow_handler
window_overflow_handler:
    /* %l0 = PSR, %l1 = PC, %l2 = nPC saved by hardware */

    /* Rotate WIM right by 1 (move invalid window mark) */
    mov %wim, %l3
    srl %l3, 1, %l4
    sll %l3, 7, %l5          /* NWINDOWS-1 = 7 for 8 windows */
    or  %l4, %l5, %l4
    mov %l4, %wim
    nop; nop; nop

    /* Save the window that just became valid */
    save                          /* enter the window we need to spill */
    std %l0, [%sp +  0]
    std %l2, [%sp +  8]
    std %l4, [%sp + 16]
    std %l6, [%sp + 24]
    std %i0, [%sp + 32]
    std %i2, [%sp + 40]
    std %i4, [%sp + 48]
    std %i6, [%sp + 56]
    restore                       /* back to trap window */

    jmp %l1                       /* return to trapped PC */
    rett %l2                      /* and nPC */

/* ================================================================
 * Window underflow handler (trap 0x06)
 *
 * Restores a register window from memory when a RESTORE would
 * move into the invalid window.
 * ================================================================ */
.global window_underflow_handler
window_underflow_handler:
    /* Rotate WIM left by 1 */
    mov %wim, %l3
    sll %l3, 1, %l4
    srl %l3, 7, %l5          /* NWINDOWS-1 = 7 */
    or  %l4, %l5, %l4
    mov %l4, %wim
    nop; nop; nop

    /* Two restores to get to the window being restored */
    restore
    restore
    ldd [%sp +  0], %l0
    ldd [%sp +  8], %l2
    ldd [%sp + 16], %l4
    ldd [%sp + 24], %l6
    ldd [%sp + 32], %i0
    ldd [%sp + 40], %i2
    ldd [%sp + 48], %i4
    ldd [%sp + 56], %i6
    save
    save

    jmp %l1
    rett %l2

/* ================================================================
 * IRQ dispatch (traps 0x11-0x1F)
 *
 * Extracts the IRQ number from TBR and calls the C function
 * sparc_irq_dispatch(uint32_t irq_number).
 * ================================================================ */
.global irq_dispatch_asm
irq_dispatch_asm:
    /* Save caller-saved registers on stack */
    save %sp, -96, %sp

    /* Read TBR to get trap type.  TBR[11:4] = trap number.
     * IRQ level = trap_number - 0x11 + 1 = trap_number - 0x10 */
    rd  %tbr, %l3
    srl %l3, 4, %l3
    and %l3, 0xFF, %l3        /* l3 = trap type (0x11..0x1F) */
    sub %l3, 0x11, %o0        /* o0 = IRQ index 0..14 (IRQ 1..15 mapped to 0..14) */

    call sparc_irq_dispatch
    nop

    restore

    jmp %l1
    rett %l2

/* ================================================================
 * Generic trap handler (catch-all for unhandled traps)
 * ================================================================ */
.global trap_generic
trap_generic:
    /* For now, loop forever.  A future version could call a C handler
     * with the trap type for diagnostics. */
    ba trap_generic
    nop

/* ================================================================
 * Boot entry point
 * ================================================================ */

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

    /* Install the trap table by writing its base address to TBR.
     * TBR bits [31:12] hold the trap base; bits [11:0] are read-only. */
    set sparc_trap_table, %l1
    wr  %l1, %tbr
    nop
    nop
    nop

    /* Initialize WIM: mark window 1 as invalid (CWP starts at 0) */
    mov 0x2, %l1
    wr  %l1, %wim
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

/* ================================================================
 * Stack (BSS)
 * ================================================================ */
.section .bss
.align 16
stack_bottom:
    .skip 32768    /* 32 KB stack */
stack_top:
