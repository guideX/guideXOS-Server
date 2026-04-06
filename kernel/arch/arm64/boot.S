/*
 * ARM64 Kernel Entry Point & Exception Vectors
 *
 * Entry from bootloader or QEMU virt machine at EL1 or EL2.
 * Handles EL2->EL1 transition if needed.
 *
 * QEMU virt machine loads kernel as ELF64 or flat binary at
 * configurable address (default 0x40000000).
 *
 * ARM64 (AArch64) Register Conventions (AAPCS64):
 *   X0-X7   - Arguments / return values (caller-saved)
 *   X8      - Indirect result location (caller-saved)
 *   X9-X15  - Temporary (caller-saved)
 *   X16-X17 - Intra-procedure call (IP0/IP1, caller-saved)
 *   X18     - Platform register (reserved)
 *   X19-X28 - Callee-saved
 *   X29     - Frame pointer (FP, callee-saved)
 *   X30     - Link register (LR)
 *   SP      - Stack pointer
 *   PC      - Program counter
 *   XZR     - Zero register (always reads as 0)
 *
 * Special Purpose Registers:
 *   ELR_ELx - Exception Link Register
 *   SPSR_ELx - Saved Program Status Register
 *   SP_ELx  - Stack Pointer for exception level x
 *   VBAR_ELx - Vector Base Address Register
 *
 * Exception Vector Table (VBAR_EL1):
 *   Offset 0x000: Synchronous, Current EL, SP_EL0
 *   Offset 0x080: IRQ, Current EL, SP_EL0
 *   Offset 0x100: FIQ, Current EL, SP_EL0
 *   Offset 0x180: SError, Current EL, SP_EL0
 *   Offset 0x200: Synchronous, Current EL, SP_ELx
 *   Offset 0x280: IRQ, Current EL, SP_ELx
 *   Offset 0x300: FIQ, Current EL, SP_ELx
 *   Offset 0x380: SError, Current EL, SP_ELx
 *   Offset 0x400: Synchronous, Lower EL, AArch64
 *   Offset 0x480: IRQ, Lower EL, AArch64
 *   Offset 0x500: FIQ, Lower EL, AArch64
 *   Offset 0x580: SError, Lower EL, AArch64
 *   Offset 0x600: Synchronous, Lower EL, AArch32
 *   Offset 0x680: IRQ, Lower EL, AArch32
 *   Offset 0x700: FIQ, Lower EL, AArch32
 *   Offset 0x780: SError, Lower EL, AArch32
 *
 * Copyright (c) 2026 guideXOS Server
 */

/* ================================================================
 * Macros
 * ================================================================ */

/* Exception vector entry - each entry is 0x80 (128) bytes */
.macro VECTOR_ENTRY label
    .align 7
    b \label
.endm

/* Save all general-purpose registers to stack */
.macro SAVE_CONTEXT
    sub     sp, sp, #(34 * 8)           /* Allocate stack space */
    stp     x0, x1, [sp, #(0 * 8)]
    stp     x2, x3, [sp, #(2 * 8)]
    stp     x4, x5, [sp, #(4 * 8)]
    stp     x6, x7, [sp, #(6 * 8)]
    stp     x8, x9, [sp, #(8 * 8)]
    stp     x10, x11, [sp, #(10 * 8)]
    stp     x12, x13, [sp, #(12 * 8)]
    stp     x14, x15, [sp, #(14 * 8)]
    stp     x16, x17, [sp, #(16 * 8)]
    stp     x18, x19, [sp, #(18 * 8)]
    stp     x20, x21, [sp, #(20 * 8)]
    stp     x22, x23, [sp, #(22 * 8)]
    stp     x24, x25, [sp, #(24 * 8)]
    stp     x26, x27, [sp, #(26 * 8)]
    stp     x28, x29, [sp, #(28 * 8)]
    
    mrs     x0, elr_el1                 /* Save exception return address */
    mrs     x1, spsr_el1                /* Save saved program status */
    stp     x30, x0, [sp, #(30 * 8)]    /* LR and ELR */
    str     x1, [sp, #(32 * 8)]         /* SPSR */
.endm

/* Restore all general-purpose registers from stack */
.macro RESTORE_CONTEXT
    ldr     x1, [sp, #(32 * 8)]         /* Restore SPSR */
    ldp     x30, x0, [sp, #(30 * 8)]    /* Restore LR and ELR */
    msr     spsr_el1, x1
    msr     elr_el1, x0
    
    ldp     x0, x1, [sp, #(0 * 8)]
    ldp     x2, x3, [sp, #(2 * 8)]
    ldp     x4, x5, [sp, #(4 * 8)]
    ldp     x6, x7, [sp, #(6 * 8)]
    ldp     x8, x9, [sp, #(8 * 8)]
    ldp     x10, x11, [sp, #(10 * 8)]
    ldp     x12, x13, [sp, #(12 * 8)]
    ldp     x14, x15, [sp, #(14 * 8)]
    ldp     x16, x17, [sp, #(16 * 8)]
    ldp     x18, x19, [sp, #(18 * 8)]
    ldp     x20, x21, [sp, #(20 * 8)]
    ldp     x22, x23, [sp, #(22 * 8)]
    ldp     x24, x25, [sp, #(24 * 8)]
    ldp     x26, x27, [sp, #(26 * 8)]
    ldp     x28, x29, [sp, #(28 * 8)]
    
    add     sp, sp, #(34 * 8)           /* Deallocate stack space */
.endm

/* ================================================================
 * Entry Point
 * ================================================================ */

.section .boot, "ax"
.global _start
_start:
    /*
     * Entry from bootloader or QEMU.
     * We may be at EL1, EL2, or EL3 depending on environment.
     * x0 may contain DTB address on some platforms.
     */
    
    /* Save DTB pointer if provided */
    mov     x20, x0
    
    /* Disable interrupts */
    msr     daifset, #0xF
    
    /* Check current exception level */
    mrs     x0, CurrentEL
    and     x0, x0, #0xC
    cmp     x0, #(3 << 2)               /* EL3? */
    beq     _el3_entry
    cmp     x0, #(2 << 2)               /* EL2? */
    beq     _el2_entry
    b       _el1_entry                  /* Already at EL1 */

/* ================================================================
 * EL3 Entry (Secure Monitor level)
 * Drop to EL2 non-secure
 * ================================================================ */
_el3_entry:
    /* Configure SCR_EL3 */
    mov     x0, #(1 << 10)              /* RW: EL2 is AArch64 */
    orr     x0, x0, #(1 << 0)           /* NS: Non-secure */
    orr     x0, x0, #(1 << 8)           /* HCE: Hypervisor call enable */
    orr     x0, x0, #(1 << 7)           /* SMD: Secure monitor call disable */
    msr     scr_el3, x0
    
    /* Set up SPSR for EL2 return */
    mov     x0, #0x3C9                  /* DAIF masked, EL2h */
    msr     spsr_el3, x0
    
    /* Set return address to EL2 entry */
    adr     x0, _el2_entry
    msr     elr_el3, x0
    
    eret

/* ================================================================
 * EL2 Entry (Hypervisor level)
 * Configure for EL1 and drop down
 * ================================================================ */
_el2_entry:
    /* Configure HCR_EL2 */
    mov     x0, #(1 << 31)              /* RW: EL1 is AArch64 */
    msr     hcr_el2, x0
    
    /* Disable coprocessor traps */
    mov     x0, xzr
    msr     cptr_el2, x0
    msr     hstr_el2, x0
    
    /* Configure SCTLR_EL1 to safe defaults */
    mov     x0, #0x0800                 /* Reserved bits */
    movk    x0, #0x30C5, lsl #16        /* Set reserved bits, disable MMU/caches */
    msr     sctlr_el1, x0
    
    /* Set vector table for EL1 (will be overwritten later) */
    adr     x0, _vectors
    msr     vbar_el1, x0
    
    /* Configure SPSR for EL1h return */
    mov     x0, #0x3C5                  /* DAIF masked, EL1h */
    msr     spsr_el2, x0
    
    /* Set return address to EL1 entry */
    adr     x0, _el1_entry
    msr     elr_el2, x0
    
    /* Enable physical counter/timer access from EL1 */
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3                  /* EL1PCTEN, EL1PCEN */
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr            /* No virtual offset */
    
    eret

/* ================================================================
 * EL1 Entry (Kernel level)
 * This is where we do main kernel initialization
 * ================================================================ */
_el1_entry:
    /* Set up kernel stack */
    adrp    x0, __stack_top
    add     x0, x0, :lo12:__stack_top
    mov     sp, x0
    
    /* Clear BSS section */
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:  cmp     x0, x1
    bge     2f
    str     xzr, [x0], #8
    b       1b
2:

    /* Set up exception vector table */
    adr     x0, _vectors
    msr     vbar_el1, x0
    isb
    
    /* Initialize serial console for early debug output */
    bl      _Z11early_printPKc
    
    /* Restore DTB pointer as first argument */
    mov     x0, x20
    
    /* Jump to C++ kernel entry point */
    bl      kernel_main
    
    /* If kernel returns, halt */
_halt:
    wfi
    b       _halt

/* ================================================================
 * Exception Vector Table
 * Must be 2KB aligned (0x800 bytes)
 * Each entry is 128 bytes (32 instructions)
 * ================================================================ */

.section .vectors, "ax"
.align 11                               /* 2KB alignment */

.global _vectors
_vectors:

/* ---- Current EL with SP_EL0 ---- */
VECTOR_ENTRY    _sync_sp0_handler       /* Synchronous */
VECTOR_ENTRY    _irq_sp0_handler        /* IRQ */
VECTOR_ENTRY    _fiq_sp0_handler        /* FIQ */
VECTOR_ENTRY    _serror_sp0_handler     /* SError */

/* ---- Current EL with SP_ELx ---- */
VECTOR_ENTRY    _sync_spx_handler       /* Synchronous */
VECTOR_ENTRY    _irq_spx_handler        /* IRQ */
VECTOR_ENTRY    _fiq_spx_handler        /* FIQ */
VECTOR_ENTRY    _serror_spx_handler     /* SError */

/* ---- Lower EL using AArch64 ---- */
VECTOR_ENTRY    _sync_lower64_handler   /* Synchronous (syscall from user) */
VECTOR_ENTRY    _irq_lower64_handler    /* IRQ */
VECTOR_ENTRY    _fiq_lower64_handler    /* FIQ */
VECTOR_ENTRY    _serror_lower64_handler /* SError */

/* ---- Lower EL using AArch32 ---- */
VECTOR_ENTRY    _sync_lower32_handler   /* Synchronous */
VECTOR_ENTRY    _irq_lower32_handler    /* IRQ */
VECTOR_ENTRY    _fiq_lower32_handler    /* FIQ */
VECTOR_ENTRY    _serror_lower32_handler /* SError */

/* ================================================================
 * Exception Handlers
 * ================================================================ */

.section .text

/* --- Current EL, SP_EL0 handlers --- */

_sync_sp0_handler:
    SAVE_CONTEXT
    mov     x0, sp                      /* Pass context pointer */
    mov     x1, #0                      /* Exception type */
    bl      exception_handler
    RESTORE_CONTEXT
    eret

_irq_sp0_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #1
    bl      irq_handler
    RESTORE_CONTEXT
    eret

_fiq_sp0_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #2
    bl      fiq_handler
    RESTORE_CONTEXT
    eret

_serror_sp0_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #3
    bl      serror_handler
    RESTORE_CONTEXT
    eret

/* --- Current EL, SP_ELx handlers --- */

_sync_spx_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #4
    bl      exception_handler
    RESTORE_CONTEXT
    eret

_irq_spx_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #5
    bl      irq_handler
    RESTORE_CONTEXT
    eret

_fiq_spx_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #6
    bl      fiq_handler
    RESTORE_CONTEXT
    eret

_serror_spx_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #7
    bl      serror_handler
    RESTORE_CONTEXT
    eret

/* --- Lower EL, AArch64 handlers --- */

_sync_lower64_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #8
    bl      syscall_handler             /* May be syscall from user space */
    RESTORE_CONTEXT
    eret

_irq_lower64_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #9
    bl      irq_handler
    RESTORE_CONTEXT
    eret

_fiq_lower64_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #10
    bl      fiq_handler
    RESTORE_CONTEXT
    eret

_serror_lower64_handler:
    SAVE_CONTEXT
    mov     x0, sp
    mov     x1, #11
    bl      serror_handler
    RESTORE_CONTEXT
    eret

/* --- Lower EL, AArch32 handlers (not supported, just halt) --- */

_sync_lower32_handler:
_irq_lower32_handler:
_fiq_lower32_handler:
_serror_lower32_handler:
    b       _halt

/* ================================================================
 * C Handler Stubs (weak symbols, can be overridden)
 * ================================================================ */

.weak exception_handler
.weak irq_handler
.weak fiq_handler
.weak serror_handler
.weak syscall_handler
.weak _Z11early_printPKc

exception_handler:
irq_handler:
fiq_handler:
serror_handler:
syscall_handler:
_Z11early_printPKc:
    ret

/* ================================================================
 * Data Section
 * ================================================================ */

.section .data

/* Debug message for early boot */
.global boot_msg
boot_msg:
    .asciz "guideXOS ARM64 booting...\n"

/* ================================================================
 * BSS Section
 * ================================================================ */

.section .bss

/* Kernel stack (16KB, 16-byte aligned) */
.align 4
__stack_bottom:
    .space 16384
.global __stack_top
__stack_top:
