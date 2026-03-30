/*
 * RISC-V 64-bit (RV64) Kernel Entry Point
 *
 * Entered by OpenSBI in S-mode (supervisor) with:
 *   a0 = hart ID
 *   a1 = pointer to device tree blob (FDT)
 *
 * OpenSBI loads the kernel at 0x80200000 (default fw_jump payload
 * address for QEMU virt).  The processor is in S-mode with
 * virtual memory disabled (satp = 0).
 *
 * RISC-V boot differences from other architectures:
 *   - 32 general-purpose registers (x0-x31), x0 hardwired to 0
 *   - sp = x2 (stack pointer), gp = x3 (global pointer)
 *   - ra = x1 (return address)
 *   - No condition flags register; branches compare registers
 *   - SBI firmware handles M-mode; kernel runs in S-mode
 *   - Interrupts controlled via sstatus.SIE and sie CSR
 *
 * Copyright (c) 2026 guideXOS Server
 */

.section .boot, "ax"
.global _start

_start:
    /* ---- 1. Disable S-mode interrupts ---- */
    csrci   sstatus, 0x2        /* Clear SIE bit (bit 1) */

    /* ---- 2. Only hart 0 continues; park other harts ---- */
    bnez    a0, park_hart

    /* ---- 3. Set up Global Pointer (gp) ----
     * The linker provides __global_pointer$ for GP-relative
     * addressing of small data (.sdata / .sbss).
     */
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    /* ---- 4. Set up stack pointer ---- */
    la      sp, stack_top

    /* ---- 5. Clear BSS section ---- */
    la      t0, __bss_start
    la      t1, __bss_end
clear_bss:
    bgeu    t0, t1, clear_bss_done
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       clear_bss
clear_bss_done:

    /* ---- 6. Call kernel_main(0, 0) ----
     * a0 = boot_environment (NULL)
     * a1 = boot_magic (0)
     */
    li      a0, 0
    li      a1, 0
    call    kernel_main

    /* ---- Should never return ---- */
halt:
    wfi
    j       halt

/* ================================================================
 * Park non-boot harts (spin in WFI loop)
 * ================================================================ */
park_hart:
    wfi
    j       park_hart

/* ================================================================
 * Memory stack - 64 KB (BSS)
 * ================================================================ */
.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:
