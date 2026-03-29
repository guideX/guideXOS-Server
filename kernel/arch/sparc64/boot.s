/*
 * SPARC v9 (UltraSPARC) Kernel Entry Point & Trap Table
 *
 * Entered by OpenBoot PROM (or QEMU -kernel) in 64-bit mode.
 *
 * SPARC v9 trap table differences from v8:
 *   - Each entry is 8 instructions (32 bytes), not 4 (16 bytes)
 *   - Table must be aligned to 32 KB (0x8000) — TBA bits [63:15]
 *   - There are 512 entries total (256 hardware + 256 software)
 *   - Hardware saves TPC, TNPC, TSTATE, TT automatically
 *   - Return via retry (re-execute) or done (skip) instead of rett
 *   - Register windows use flushw; no WIM register
 *
 * Copyright (c) 2026 guideXOS Server
 */

/* ================================================================
 * Trap table
 *
 * SPARC v9 requires 32-KB alignment.
 * 512 entries x 32 bytes = 16 KB per trap level.
 * We provide TL=0 (normal) traps only.
 * ================================================================ */

.section .trapvec, "ax"
.align 32768
.global sparc64_trap_table

sparc64_trap_table:

/* ---- Trap 0x000: Power-on reset ---- */
    ba,a _start
    nop; nop; nop; nop; nop; nop; nop

/* ---- Traps 0x001-0x007: Faults (instruction_access_exception etc.) ---- */
.rept 7
    ba,a trap_generic
    nop; nop; nop; nop; nop; nop; nop
.endr

/* ---- Trap 0x008: spill_normal (window spill to memory) ---- */
    ba,a spill_handler
    nop; nop; nop; nop; nop; nop; nop

/* ---- Trap 0x009-0x00F: More faults ---- */
.rept 7
    ba,a trap_generic
    nop; nop; nop; nop; nop; nop; nop
.endr

/* ---- Trap 0x010: fill_normal (window fill from memory) ---- */
    ba,a fill_handler
    nop; nop; nop; nop; nop; nop; nop

/* ---- Traps 0x011-0x040: Various faults / reserved ---- */
.rept 48
    ba,a trap_generic
    nop; nop; nop; nop; nop; nop; nop
.endr

/* ---- Traps 0x041-0x04F: interrupt_vector (hardware IRQs) ----
 * SPARC v9 maps hardware interrupts to trap types 0x041-0x04F
 * (interrupt_level_1 through interrupt_level_15).
 * interrupt_level_n = trap type 0x040 + n                     */
.rept 15
    ba,a irq_dispatch_asm
    nop; nop; nop; nop; nop; nop; nop
.endr

/* ---- Traps 0x050-0x07F: Reserved / implementation-dependent ---- */
.rept 48
    ba,a trap_generic
    nop; nop; nop; nop; nop; nop; nop
.endr

/* ---- Traps 0x080-0x0FF: spill/fill variants + software traps ---- */
.rept 128
    ba,a trap_generic
    nop; nop; nop; nop; nop; nop; nop
.endr

/* ---- Traps 0x100-0x1FF: Software traps (ta instruction) ---- */
.rept 256
    ba,a trap_generic
    nop; nop; nop; nop; nop; nop; nop
.endr

/* ================================================================
 * Window spill handler (trap 0x008 — spill_normal)
 *
 * SPARC v9 window spill: save the register window that is about
 * to be overwritten.  The hardware has already decremented CWP.
 * We save 16 registers (locals + ins) = 128 bytes to %sp.
 *
 * In v9 the stack pointer (%sp / %o6) holds a 64-bit address
 * with a mandatory +BIAS of 2047 for 64-bit stack frames.
 * Effective address = %sp + BIAS.
 * ================================================================ */

#define BIAS 2047
#define STACK_FRAME_SZ 176  /* 128 (16 regs x 8) + 48 (ABI area) */

.global spill_handler
spill_handler:
    /* Save locals and ins of the window being spilled */
    stx  %l0, [%sp + BIAS +  0]
    stx  %l1, [%sp + BIAS +  8]
    stx  %l2, [%sp + BIAS + 16]
    stx  %l3, [%sp + BIAS + 24]
    stx  %l4, [%sp + BIAS + 32]
    stx  %l5, [%sp + BIAS + 40]
    stx  %l6, [%sp + BIAS + 48]
    stx  %l7, [%sp + BIAS + 56]
    stx  %i0, [%sp + BIAS + 64]
    stx  %i1, [%sp + BIAS + 72]
    stx  %i2, [%sp + BIAS + 80]
    stx  %i3, [%sp + BIAS + 88]
    stx  %i4, [%sp + BIAS + 96]
    stx  %i5, [%sp + BIAS + 104]
    stx  %i6, [%sp + BIAS + 112]
    stx  %i7, [%sp + BIAS + 120]
    saved                           /* tell hardware window is saved */
    retry                           /* return to trapped instruction */

/* ================================================================
 * Window fill handler (trap 0x010 — fill_normal)
 *
 * Restores a register window from the stack.
 * ================================================================ */

.global fill_handler
fill_handler:
    ldx  [%sp + BIAS +  0], %l0
    ldx  [%sp + BIAS +  8], %l1
    ldx  [%sp + BIAS + 16], %l2
    ldx  [%sp + BIAS + 24], %l3
    ldx  [%sp + BIAS + 32], %l4
    ldx  [%sp + BIAS + 40], %l5
    ldx  [%sp + BIAS + 48], %l6
    ldx  [%sp + BIAS + 56], %l7
    ldx  [%sp + BIAS + 64], %i0
    ldx  [%sp + BIAS + 72], %i1
    ldx  [%sp + BIAS + 80], %i2
    ldx  [%sp + BIAS + 88], %i3
    ldx  [%sp + BIAS + 96], %i4
    ldx  [%sp + BIAS + 104], %i5
    ldx  [%sp + BIAS + 112], %i6
    ldx  [%sp + BIAS + 120], %i7
    restored                        /* tell hardware window is restored */
    retry

/* ================================================================
 * IRQ dispatch (traps 0x041–0x04F)
 *
 * Reads the TT (trap type) register to determine which IRQ fired.
 * interrupt_level_n = TT 0x41..0x4F  ?  IRQ 1..15
 * Calls sparc64_irq_dispatch(tt_value) in C++.
 * ================================================================ */

.global irq_dispatch_asm
irq_dispatch_asm:
    /* Open a new register window for the handler */
    save %sp, -(STACK_FRAME_SZ), %sp

    /* Read the trap type from %tt privileged register */
    rdpr %tt, %o0

    call sparc64_irq_dispatch
    nop

    restore

    retry                           /* return to interrupted code */

/* ================================================================
 * Generic trap handler (catch-all)
 * ================================================================ */

.global trap_generic
trap_generic:
    ba,a trap_generic
    nop

/* ================================================================
 * Boot entry point
 * ================================================================ */

.section .boot, "ax"
.global _start

_start:
    /* ---- 1. Set up PSTATE: privileged, interrupts disabled ---- */
    wrpr  %g0, 0x04, %pstate       /* PRIV=1, IE=0, AM=0 */

    /* ---- 2. Set trap level to 0 (normal operation) ---- */
    wrpr  %g0, 0, %tl

    /* ---- 3. Set PIL to max (mask all interrupts) ---- */
    wrpr  %g0, 15, %pil

    /* ---- 4. Install trap table ---- */
    sethi %hi(sparc64_trap_table), %g1
    or    %g1, %lo(sparc64_trap_table), %g1
    wrpr  %g1, %tba

    /* ---- 5. Set up 64-bit stack ----
     * SPARC v9 ABI requires %sp to be 16-byte aligned and uses
     * a +2047 BIAS.  %sp stores (real_address - 2047).
     */
    sethi %hi(stack_top), %g1
    or    %g1, %lo(stack_top), %g1
    sub   %g1, BIAS, %sp
    sub   %sp, STACK_FRAME_SZ, %sp

    /* Clear frame pointer */
    clr   %fp

    /* ---- 6. Clear BSS ---- */
    sethi %hi(__bss_start), %o0
    or    %o0, %lo(__bss_start), %o0
    sethi %hi(__bss_end), %o1
    or    %o1, %lo(__bss_end), %o1
    clr   %o2

clear_bss:
    cmp   %o0, %o1
    bge,a  clear_bss_done
    nop
    stx   %o2, [%o0]
    ba    clear_bss
    add   %o0, 8, %o0

clear_bss_done:
    /* ---- 7. Call kernel_main(0, 0) ---- */
    clr   %o0
    clr   %o1
    call  kernel_main
    nop

    /* Should never return */
halt:
    ba,a halt
    nop

/* ================================================================
 * Stack (BSS) — 64 KB for 64-bit deep call chains
 * ================================================================ */
.section .bss
.align 32
stack_bottom:
    .skip 65536
stack_top:
