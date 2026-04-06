/*
 * PowerPC64 Kernel Entry Point & Exception Vectors
 *
 * Entered by QEMU pseries machine (or SLOF firmware) in 64-bit
 * big-endian mode with the processor in hypervisor state.
 *
 * PowerPC64 boot differences from other architectures:
 *   - 32 general-purpose 64-bit registers (r0-r31)
 *   - r1 = stack pointer (by convention)
 *   - r2 = TOC (Table of Contents) pointer for PIC code
 *   - r3-r10 = argument/return value registers
 *   - r13 = small data area pointer (thread-local in Linux)
 *   - Link Register (LR) holds return address
 *   - Count Register (CTR) for loop/branch
 *   - Condition Register (CR) for comparisons
 *   - MSR controls interrupts, privilege, MMU
 *   - Exception vectors at fixed offsets from 0x0
 *   - No delay slots (unlike MIPS)
 *
 * QEMU pseries boot:
 *   - Kernel loaded at 0x20000000 (512MB) by default
 *   - r3 = pointer to device tree blob (FDT)
 *   - r4 = kernel base address
 *   - MSR[SF]=1 (64-bit mode), MSR[HV]=1 (hypervisor)
 *   - MMU may or may not be enabled depending on SLOF config
 *
 * Copyright (c) 2026 guideXOS Server
 */

/* ================================================================
 * PowerPC64 ELF ABI conventions:
 *
 * Function calls:
 *   - r1 = stack pointer (16-byte aligned)
 *   - r2 = TOC pointer
 *   - r3-r10 = arguments
 *   - r3 = return value
 *   - r0, r3-r12 = caller-saved (volatile)
 *   - r14-r31 = callee-saved (non-volatile)
 *   - LR = return address (caller-saved)
 *   - CR fields 0,1,5,6,7 = caller-saved
 *   - CR fields 2,3,4 = callee-saved
 *
 * Stack frame (ELFv2 ABI):
 *   - SP always points to back chain
 *   - Minimum frame size = 32 bytes
 *   - Parameter save area at SP+32
 * ================================================================ */

.section .boot, "ax"
.global _start
.global __exception_vectors

/* ================================================================
 * Exception vector table
 *
 * PowerPC64 Book III-S exception vectors must be placed at
 * specific fixed addresses starting from 0x0 (real mode).
 * Each vector has up to 256 bytes (64 instructions).
 *
 * For a kernel loaded at 0x20000000, we need to copy these
 * vectors to physical address 0x0, or use the hypervisor
 * to relocate them.
 *
 * For initial QEMU testing, we place them here and the
 * kernel will run in real mode.
 * ================================================================ */

.align 8

/* ---- Vector 0x0100: System Reset ----
 * Entered on system reset or wake from sleep.
 * This is also the initial entry point on some systems.
 */
.org 0x100
exception_system_reset:
    b       _start              /* Jump to main entry point */

/* ---- Vector 0x0200: Machine Check ----
 * Entered on hardware error (memory parity, bus error, etc.)
 */
.org 0x200
exception_machine_check:
    /* Save registers and call handler */
    mtsprg0 r3                  /* Save r3 in SPRG0 */
    li      r3, 2               /* Vector number for machine check */
    b       exception_common

/* ---- Vector 0x0300: Data Storage Interrupt (DSI) ----
 * Entered on data access fault (page fault, protection, etc.)
 */
.org 0x300
exception_data_storage:
    mtsprg0 r3
    li      r3, 3
    b       exception_common

/* ---- Vector 0x0380: Data Segment Interrupt ----
 * Entered on data segment fault (segment not present)
 */
.org 0x380
exception_data_segment:
    mtsprg0 r3
    li      r3, 4
    b       exception_common

/* ---- Vector 0x0400: Instruction Storage Interrupt (ISI) ----
 * Entered on instruction fetch fault
 */
.org 0x0400
exception_instruction_storage:
    mtsprg0 r3
    li      r3, 5
    b       exception_common

/* ---- Vector 0x0480: Instruction Segment Interrupt ----
 * Entered on instruction segment fault
 */
.org 0x0480
exception_instruction_segment:
    mtsprg0 r3
    li      r3, 6
    b       exception_common

/* ---- Vector 0x0500: External Interrupt ----
 * Entered on external hardware interrupt (IRQ)
 */
.org 0x0500
exception_external:
    mtsprg0 r3
    li      r3, 7
    b       exception_common

/* ---- Vector 0x0600: Alignment Interrupt ----
 * Entered on misaligned memory access
 */
.org 0x0600
exception_alignment:
    mtsprg0 r3
    li      r3, 8
    b       exception_common

/* ---- Vector 0x0700: Program Interrupt ----
 * Entered on illegal instruction, privilege violation, trap
 */
.org 0x0700
exception_program:
    mtsprg0 r3
    li      r3, 9
    b       exception_common

/* ---- Vector 0x0800: Floating-Point Unavailable ----
 */
.org 0x0800
exception_fp_unavailable:
    mtsprg0 r3
    li      r3, 10
    b       exception_common

/* ---- Vector 0x0900: Decrementer Interrupt ----
 * Entered when DEC register counts down past zero (timer tick)
 */
.org 0x0900
exception_decrementer:
    mtsprg0 r3
    li      r3, 11              /* Vector number for decrementer */
    b       exception_common

/* ---- Vector 0x0C00: System Call ----
 * Entered by the 'sc' instruction (syscall)
 */
.org 0x0C00
exception_syscall:
    mtsprg0 r3
    li      r3, 12
    b       exception_common

/* ================================================================
 * Common exception handler
 *
 * On entry:
 *   - r3 = vector number (saved by specific handler)
 *   - SPRG0 = original r3 value
 *   - SRR0 = return address (PC at time of exception)
 *   - SRR1 = saved MSR
 *
 * We save state and call the C++ interrupt dispatcher.
 * ================================================================ */

.align 8
exception_common:
    /* Save additional registers on stack */
    /* For a full implementation, save all GPRs, LR, CTR, CR, XER */
    
    /* Simple stub: just acknowledge and return */
    /* In a full kernel, we would:
     * 1. Save all registers to exception frame
     * 2. Switch to kernel stack
     * 3. Call ppc64_interrupt_dispatch(vector)
     * 4. Restore registers
     * 5. Return via rfid
     */
    
    mfsprg0 r3                  /* Restore r3 */
    rfid                        /* Return from interrupt */

/* ================================================================
 * Main entry point: _start
 *
 * Entered by SLOF/QEMU with:
 *   r3 = pointer to device tree blob (FDT)
 *   r4 = kernel base address
 *   r5 = 0 (reserved)
 *
 * We perform minimal initialization:
 * 1. Disable interrupts
 * 2. Set up stack
 * 3. Clear BSS
 * 4. Jump to kernel_main
 * ================================================================ */

.org 0x2000                     /* Start main code after vectors */
_start:
    /* ---- 1. Disable all interrupts ----
     * Clear MSR[EE] (external interrupts) by reading MSR,
     * clearing the bit, and writing it back.
     * MSR[EE] is bit 48 (from MSB=0) = bit 15 (from LSB=0)
     */
    mfmsr   r0                  /* Read MSR into r0 */
    li      r11, 0x8000         /* Load 0x8000 (bit 15) */
    andc    r0, r0, r11         /* Clear EE bit: r0 = r0 & ~r11 */
    mtmsr   r0                  /* Write back to MSR */
    isync                       /* Synchronize instruction stream */

    /* ---- 2. Only CPU 0 continues; park other CPUs ----
     * Read Processor ID Register (PIR) - SPR 1023
     * If not CPU 0, go to park loop
     */
    mfspr   r0, 1023            /* r0 = PIR (processor ID) */
    cmpdi   r0, 0               /* Compare with 0 */
    bne     park_cpu            /* If not CPU 0, park */

    /* ---- 3. Set up TOC (Table of Contents) pointer ----
     * For ELFv2 ABI, r2 points to .TOC. section
     * The linker provides this symbol
     */
    lis     r2, .TOC.@ha        /* Load high part of TOC address */
    addi    r2, r2, .TOC.@l     /* Add low part */

    /* ---- 4. Set up stack pointer ----
     * Stack grows downward, must be 16-byte aligned
     * Load address of stack_top from linker script
     */
    lis     r1, stack_top@ha    /* Load high part of stack address */
    addi    r1, r1, stack_top@l /* Add low part */

    /* ---- 5. Clear BSS section ----
     * Zero all bytes from __bss_start to __bss_end
     */
    lis     r3, __bss_start@ha
    addi    r3, r3, __bss_start@l   /* r3 = __bss_start */
    lis     r4, __bss_end@ha
    addi    r4, r4, __bss_end@l     /* r4 = __bss_end */
    li      r5, 0                   /* r5 = 0 (value to store) */

clear_bss:
    cmpd    r3, r4              /* Compare r3 with r4 */
    bge     clear_bss_done      /* If r3 >= r4, done */
    std     r5, 0(r3)           /* Store doubleword (8 bytes) of zeros */
    addi    r3, r3, 8           /* Advance pointer by 8 bytes */
    b       clear_bss           /* Loop */
clear_bss_done:

    /* ---- 6. Call kernel_main(boot_env, magic) ----
     * Arguments:
     *   r3 = boot_environment (NULL for now)
     *   r4 = boot_magic (0)
     *
     * PowerPC64 ELFv2 ABI: first 8 args in r3-r10
     */
    li      r3, 0               /* boot_environment = NULL */
    li      r4, 0               /* boot_magic = 0 */
    bl      kernel_main         /* Call kernel_main */
    nop                         /* Linker may need this for TOC restore */

    /* ---- Should never return ---- */
halt_loop:
    /* Wait for interrupt (low power state) */
    wait
    b       halt_loop

/* ================================================================
 * Park non-boot CPUs (spin in wait loop)
 *
 * Secondary CPUs wait here until the primary CPU wakes them
 * via inter-processor interrupt (IPI).
 * ================================================================ */

park_cpu:
    wait                        /* Enter low-power wait state */
    b       park_cpu            /* Loop forever */

/* ================================================================
 * Stack space - 64 KB (BSS)
 *
 * Stack must be 16-byte aligned per ELFv2 ABI.
 * ================================================================ */

.section .bss
.align 4                        /* 16-byte alignment (2^4) */
stack_bottom:
    .skip 65536                 /* 64 KB stack */
stack_top:
