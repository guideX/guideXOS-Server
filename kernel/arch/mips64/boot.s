/*
 * MIPS64 Kernel Entry Point & Exception Vectors
 *
 * Entered by bootloader/QEMU at reset vector in kernel mode (64-bit).
 *
 * QEMU malta/virt machine loads kernel at 0xFFFFFFFF80100000 (kseg0 cached)
 * or physical address 0x00100000.  The reset vector is at 0xBFC00000.
 *
 * MIPS64 register conventions (n64 ABI):
 *   $0  (zero)  - Always zero (hardwired)
 *   $1  (at)    - Assembler temporary
 *   $2-$3 (v0-v1) - Return values
 *   $4-$11 (a0-a7) - Arguments
 *   $12-$15 (t4-t7) - Temporaries (caller-saved)
 *   $16-$23 (s0-s7) - Saved registers (callee-saved)
 *   $24-$25 (t8-t9) - Temporaries (caller-saved)
 *   $26-$27 (k0-k1) - Kernel reserved
 *   $28 (gp)   - Global pointer
 *   $29 (sp)   - Stack pointer
 *   $30 (fp/s8) - Frame pointer / saved register
 *   $31 (ra)   - Return address
 *
 * Special registers:
 *   HI, LO     - Multiply/divide result registers
 *
 * MIPS64 exception vectors (with BEV=0):
 *   0x80000000 - TLB Refill (32-bit compatibility)
 *   0x80000080 - XTLB Refill (64-bit TLB miss)
 *   0x80000100 - Cache Error
 *   0x80000180 - General Exception
 *   0x80000200 - Interrupt (vectored interrupt mode, if enabled)
 *
 * Copyright (c) 2026 guideXOS Server
 */

.set noreorder   /* Do not reorder instructions - we handle delay slots */
.set noat        /* Do not use $at automatically */

/* ================================================================
 * Macros for 64-bit address loading
 * ================================================================ */

/* Load 64-bit address into register (handles any address) */
.macro dla reg, symbol
    lui     \reg, %highest(\symbol)
    daddiu  \reg, \reg, %higher(\symbol)
    dsll    \reg, \reg, 16
    daddiu  \reg, \reg, %hi(\symbol)
    dsll    \reg, \reg, 16
    daddiu  \reg, \reg, %lo(\symbol)
.endm

/* Load 32-bit address into register (for kseg0/kseg1 addresses) */
.macro la32 reg, symbol
    lui     \reg, %hi(\symbol)
    addiu   \reg, \reg, %lo(\symbol)
.endm

/* ================================================================
 * Exception Vectors
 *
 * These must be placed at fixed addresses when BEV=0.
 * We use a trampoline approach - each vector jumps to a handler.
 * ================================================================ */

.section .vectors, "ax"
.align 12    /* 4KB alignment for exception vector base */

/* ---- TLB Refill Vector (offset 0x000) ----
 * 
 * This vector is taken when a TLB miss occurs for a 32-bit address.
 * On MIPS64, this is rarely used; XTLB Refill is more common.
 */
.org 0x000
.global vec_tlb_refill
vec_tlb_refill:
    j       tlb_refill_handler    /* Jump to TLB refill handler */
    nop                           /* Delay slot - executed before jump */

/* ---- XTLB Refill Vector (offset 0x080) ----
 *
 * This vector is taken when a TLB miss occurs for a 64-bit address.
 * This is the primary TLB miss handler on MIPS64.
 */
.org 0x080
.global vec_xtlb_refill
vec_xtlb_refill:
    j       xtlb_refill_handler   /* Jump to XTLB refill handler */
    nop                           /* Delay slot */

/* ---- Cache Error Vector (offset 0x100) ----
 *
 * This vector is taken when a cache error occurs.
 * Usually fatal - we just halt.
 */
.org 0x100
.global vec_cache_error
vec_cache_error:
    j       cache_error_handler   /* Jump to cache error handler */
    nop                           /* Delay slot */

/* ---- General Exception Vector (offset 0x180) ----
 *
 * This vector handles all exceptions except TLB refill and cache error:
 * - Interrupts (when not using vectored interrupt mode)
 * - System calls (syscall instruction)
 * - Breakpoints (break instruction)
 * - Address errors
 * - Bus errors
 * - Arithmetic overflow
 * - Trap instructions
 * - FPU exceptions
 * - Coprocessor unusable
 */
.org 0x180
.global vec_general_exception
vec_general_exception:
    j       general_exception_handler  /* Jump to general exception handler */
    nop                                /* Delay slot */

/* ---- Interrupt Vector (offset 0x200) ----
 *
 * This vector is taken when using vectored interrupt mode (VI).
 * Not all MIPS implementations support this.
 */
.org 0x200
.global vec_interrupt
vec_interrupt:
    j       interrupt_handler     /* Jump to interrupt handler */
    nop                           /* Delay slot */

/* ================================================================
 * Boot Section - Entry Point
 *
 * This is where execution begins after reset.
 * On QEMU malta, the bootloader jumps here after loading the kernel.
 * ================================================================ */

.section .boot, "ax"
.align 4
.global _start

_start:
    /* ---- 1. Disable interrupts and clear exception flags ----
     *
     * Set Status register to known state:
     * - Disable interrupts (IE=0)
     * - Clear exception level (EXL=0)
     * - Clear error level (ERL=0)
     * - Enable 64-bit kernel addressing (KX=1)
     * - Use boot exception vectors initially (BEV=1)
     * - Enable CP0 access (CU0=1)
     */
    lui     $t0, 0x1040          /* Status: KX=1 (bit 7), CU0=1 (bit 28) */
    ori     $t0, $t0, 0x0080     /* Complete the KX bit */
    mtc0    $t0, $12             /* Write to Status register (CP0 $12) */
    ehb                          /* Execution hazard barrier */

    /* ---- 2. Clear Cause register ----
     *
     * Clear software interrupt pending bits.
     */
    mtc0    $zero, $13           /* Clear Cause register (CP0 $13) */
    ehb

    /* ---- 3. Set up exception vector base ----
     *
     * For MIPS64r2+, EBase register (CP0 $15, select 1) sets the
     * exception vector base. We set it to __vectors_start.
     */
    dla     $t0, __vectors_start
    mtc0    $t0, $15, 1          /* Write to EBase (CP0 $15, select 1) */
    ehb

    /* ---- 4. Set up Global Pointer (gp) ----
     *
     * The global pointer is used for GP-relative addressing of
     * small data sections (.sdata, .sbss). The linker defines
     * _gp as the base + 0x7FF0.
     */
    dla     $gp, _gp

    /* ---- 5. Set up Stack Pointer (sp) ----
     *
     * Stack grows downward. We place it at the top of the stack area.
     * MIPS64 n64 ABI requires 16-byte stack alignment.
     */
    dla     $sp, stack_top
    daddiu  $sp, $sp, -16        /* Reserve space for initial frame */
    and     $sp, $sp, -16        /* Ensure 16-byte alignment */

    /* ---- 6. Clear BSS section ----
     *
     * BSS contains uninitialized global/static variables.
     * We must zero it before jumping to C++ code.
     */
    dla     $t0, __bss_start     /* Start of BSS */
    dla     $t1, __bss_end       /* End of BSS */
clear_bss:
    beq     $t0, $t1, clear_bss_done  /* If start == end, done */
    nop                          /* Delay slot */
    sd      $zero, 0($t0)        /* Store doubleword of zeros */
    daddiu  $t0, $t0, 8          /* Advance pointer by 8 bytes */
    j       clear_bss            /* Continue loop */
    nop                          /* Delay slot */
clear_bss_done:

    /* ---- 7. Switch to normal exception vectors ----
     *
     * Now that EBase is set up, clear BEV to use normal vectors.
     */
    mfc0    $t0, $12             /* Read Status */
    lui     $t1, 0xFFBF          /* Mask to clear BEV (bit 22) */
    ori     $t1, $t1, 0xFFFF
    and     $t0, $t0, $t1        /* Clear BEV */
    mtc0    $t0, $12             /* Write Status */
    ehb

    /* ---- 8. Call kernel_main ----
     *
     * Pass boot environment and magic as arguments:
     *   $a0 = boot_environment pointer (NULL)
     *   $a1 = boot_magic value (0)
     */
    move    $a0, $zero           /* boot_environment = NULL */
    move    $a1, $zero           /* boot_magic = 0 */
    
    dla     $t0, kernel_main     /* Load address of kernel_main */
    jalr    $t0                  /* Jump and link to kernel_main */
    nop                          /* Delay slot */

    /* ---- 9. Halt if kernel_main returns ----
     *
     * kernel_main should never return. If it does, loop forever.
     */
halt:
    wait                         /* Low-power wait for interrupt */
    j       halt                 /* Loop forever */
    nop                          /* Delay slot */

/* ================================================================
 * Exception Handlers
 *
 * These are the actual handlers jumped to from the exception vectors.
 * They save context and call C++ handlers.
 * ================================================================ */

.section .text, "ax"
.align 4

/* ---- TLB Refill Handler (32-bit mode) ---- */
.global tlb_refill_handler
tlb_refill_handler:
    /* Save $k0, $k1 are reserved for kernel - we can use them freely */
    
    /* For now, just call the C++ handler with minimal save */
    move    $k0, $ra             /* Save return address in k0 */
    dla     $t0, trap_tlb_refill /* Load handler address */
    jalr    $t0                  /* Call C++ handler */
    nop                          /* Delay slot */
    move    $ra, $k0             /* Restore return address */
    
    eret                         /* Return from exception */
    nop                          /* Delay slot (not actually used after eret) */

/* ---- XTLB Refill Handler (64-bit mode) ---- */
.global xtlb_refill_handler
xtlb_refill_handler:
    move    $k0, $ra
    dla     $t0, trap_xtlb_refill
    jalr    $t0
    nop
    move    $ra, $k0
    
    eret
    nop

/* ---- Cache Error Handler ----
 *
 * Cache errors are usually fatal. Just halt.
 */
.global cache_error_handler
cache_error_handler:
    dla     $t0, trap_cache_error
    jalr    $t0
    nop
    
    /* Should not return, but if it does, halt */
cache_halt:
    wait
    j       cache_halt
    nop

/* ---- General Exception Handler ----
 *
 * This handles all exceptions except TLB refill and cache error.
 * We need to save full context since we don't know what caused it.
 */
.global general_exception_handler
general_exception_handler:
    /* Save context on kernel stack */
    /* $k0 and $k1 are kernel reserved - safe to use without saving */
    
    /* Use $k0 to hold original SP while we set up exception frame */
    move    $k0, $sp
    
    /* Allocate exception frame on stack (40 registers * 8 bytes = 320 bytes)
     * Plus alignment = 336 bytes (rounded to 16-byte alignment)
     */
    daddiu  $sp, $sp, -336
    
    /* Save general purpose registers */
    sd      $at, 0($sp)          /* $1 - assembler temporary */
    sd      $v0, 8($sp)          /* $2 - return value 0 */
    sd      $v1, 16($sp)         /* $3 - return value 1 */
    sd      $a0, 24($sp)         /* $4 - argument 0 */
    sd      $a1, 32($sp)         /* $5 - argument 1 */
    sd      $a2, 40($sp)         /* $6 - argument 2 */
    sd      $a3, 48($sp)         /* $7 - argument 3 */
    sd      $a4, 56($sp)         /* $8 - argument 4 (t0 in o32) */
    sd      $a5, 64($sp)         /* $9 - argument 5 (t1 in o32) */
    sd      $a6, 72($sp)         /* $10 - argument 6 (t2 in o32) */
    sd      $a7, 80($sp)         /* $11 - argument 7 (t3 in o32) */
    sd      $t4, 88($sp)         /* $12 - temporary 4 */
    sd      $t5, 96($sp)         /* $13 - temporary 5 */
    sd      $t6, 104($sp)        /* $14 - temporary 6 */
    sd      $t7, 112($sp)        /* $15 - temporary 7 */
    sd      $s0, 120($sp)        /* $16 - saved 0 */
    sd      $s1, 128($sp)        /* $17 - saved 1 */
    sd      $s2, 136($sp)        /* $18 - saved 2 */
    sd      $s3, 144($sp)        /* $19 - saved 3 */
    sd      $s4, 152($sp)        /* $20 - saved 4 */
    sd      $s5, 160($sp)        /* $21 - saved 5 */
    sd      $s6, 168($sp)        /* $22 - saved 6 */
    sd      $s7, 176($sp)        /* $23 - saved 7 */
    sd      $t8, 184($sp)        /* $24 - temporary 8 */
    sd      $t9, 192($sp)        /* $25 - temporary 9 */
    /* $26 (k0), $27 (k1) are not saved - kernel reserved */
    sd      $gp, 200($sp)        /* $28 - global pointer */
    sd      $k0, 208($sp)        /* Original SP (saved in k0) */
    sd      $fp, 216($sp)        /* $30 - frame pointer */
    sd      $ra, 224($sp)        /* $31 - return address */
    
    /* Save HI and LO registers */
    mfhi    $t0
    mflo    $t1
    sd      $t0, 232($sp)        /* HI register */
    sd      $t1, 240($sp)        /* LO register */
    
    /* Save CP0 registers */
    dmfc0   $t0, $14             /* EPC - Exception Program Counter */
    dmfc0   $t1, $12             /* Status register */
    dmfc0   $t2, $13             /* Cause register */
    dmfc0   $t3, $8              /* BadVAddr - bad virtual address */
    sd      $t0, 248($sp)        /* EPC */
    sd      $t1, 256($sp)        /* Status */
    sd      $t2, 264($sp)        /* Cause */
    sd      $t3, 272($sp)        /* BadVAddr */
    
    /* Call C++ exception dispatcher
     * Pass pointer to saved context in $a0
     */
    move    $a0, $sp
    dla     $t0, trap_dispatch
    jalr    $t0
    nop
    
    /* Restore CP0 registers */
    ld      $t0, 248($sp)        /* EPC */
    ld      $t1, 256($sp)        /* Status */
    dmtc0   $t0, $14             /* Restore EPC */
    dmtc0   $t1, $12             /* Restore Status */
    ehb
    
    /* Restore HI and LO */
    ld      $t0, 232($sp)
    ld      $t1, 240($sp)
    mthi    $t0
    mtlo    $t1
    
    /* Restore general purpose registers */
    ld      $at, 0($sp)
    ld      $v0, 8($sp)
    ld      $v1, 16($sp)
    ld      $a0, 24($sp)
    ld      $a1, 32($sp)
    ld      $a2, 40($sp)
    ld      $a3, 48($sp)
    ld      $a4, 56($sp)
    ld      $a5, 64($sp)
    ld      $a6, 72($sp)
    ld      $a7, 80($sp)
    ld      $t4, 88($sp)
    ld      $t5, 96($sp)
    ld      $t6, 104($sp)
    ld      $t7, 112($sp)
    ld      $s0, 120($sp)
    ld      $s1, 128($sp)
    ld      $s2, 136($sp)
    ld      $s3, 144($sp)
    ld      $s4, 152($sp)
    ld      $s5, 160($sp)
    ld      $s6, 168($sp)
    ld      $s7, 176($sp)
    ld      $t8, 184($sp)
    ld      $t9, 192($sp)
    ld      $gp, 200($sp)
    ld      $fp, 216($sp)
    ld      $ra, 224($sp)
    
    /* Restore SP last */
    ld      $sp, 208($sp)
    
    /* Return from exception */
    eret
    nop

/* ---- Interrupt Handler (Vectored Interrupt Mode) ---- */
.global interrupt_handler
interrupt_handler:
    /* Same as general exception for now */
    j       general_exception_handler
    nop

/* ================================================================
 * Stack Area (BSS section)
 *
 * 64 KB kernel stack, 16-byte aligned.
 * ================================================================ */

.section .bss
.align 4
stack_bottom:
    .skip 65536                  /* 64 KB stack */
stack_top:
