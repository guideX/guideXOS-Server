/*
 * LoongArch 64-bit (LA64) Kernel Entry Point
 *
 * Entered by firmware/bootloader in PLV0 (kernel privilege) with:
 *   $a0 = boot parameter pointer (FDT or boot info structure)
 *   $a1 = boot magic or secondary parameter
 *
 * On QEMU loongarch64-virt, the kernel is loaded at 0x9000000000200000
 * (direct-mapped kernel space). The processor is in PLV0 with
 * virtual memory potentially already configured by firmware.
 *
 * LoongArch register conventions:
 *   $r0  = zero (hardwired to 0)
 *   $r1  = ra (return address)
 *   $r2  = tp (thread pointer, TLS)
 *   $r3  = sp (stack pointer)
 *   $r4-$r11 = a0-a7 (arguments, caller-saved)
 *   $r12-$r20 = t0-t8 (temporaries, caller-saved)
 *   $r21 = reserved (kernel scratch)
 *   $r22 = fp/s9 (frame pointer, callee-saved)
 *   $r23-$r31 = s0-s8 (callee-saved)
 *
 * LoongArch boot differences from other architectures:
 *   - No separate M/S modes like RISC-V; uses PLV0-3 privilege levels
 *   - CSR access via csrrd/csrwr/csrxchg instructions
 *   - Direct Mapping Windows (DMW) for kernel address translation
 *   - IOCSR for I/O configuration space access
 *   - Timer interrupt via CSR_TCFG configuration
 *
 * Copyright (c) 2026 guideXOS Server
 */

/*
 * ================================================================
 * LoongArch Assembly Instruction Reference (for developers)
 *
 * Data movement:
 *   move rd, rj         - Copy register (pseudo for or rd, rj, $r0)
 *   li.d rd, imm        - Load 64-bit immediate
 *   lu12i.w rd, imm20   - Load upper 20 bits (sign-extend to 64-bit)
 *   lu32i.d rd, imm20   - Load bits [51:32]
 *   lu52i.d rd, rj, imm - Load bits [63:52]
 *   ld.d rd, rj, imm    - Load 64-bit doubleword
 *   st.d rd, rj, imm    - Store 64-bit doubleword
 *
 * Arithmetic:
 *   add.d rd, rj, rk    - 64-bit addition
 *   addi.d rd, rj, imm  - Add immediate
 *   sub.d rd, rj, rk    - 64-bit subtraction
 *
 * Logical:
 *   and rd, rj, rk      - Bitwise AND
 *   or rd, rj, rk       - Bitwise OR
 *   xor rd, rj, rk      - Bitwise XOR
 *
 * Control flow:
 *   b offset            - Unconditional branch
 *   bl offset           - Branch and link (call)
 *   beq rj, rd, offset  - Branch if equal
 *   bne rj, rd, offset  - Branch if not equal
 *   blt rj, rd, offset  - Branch if less than (signed)
 *   bge rj, rd, offset  - Branch if greater or equal (signed)
 *   bltu rj, rd, offset - Branch if less than (unsigned)
 *   bgeu rj, rd, offset - Branch if greater or equal (unsigned)
 *   jirl rd, rj, offset - Jump indirect and link
 *   ertn                - Exception return
 *
 * CSR access:
 *   csrrd rd, csr       - Read CSR to rd
 *   csrwr rd, csr       - Write rd to CSR
 *   csrxchg rd, rj, csr - Exchange: CSR = (CSR & ~rj) | (rd & rj)
 *
 * System:
 *   syscall code        - System call
 *   break code          - Breakpoint
 *   idle level          - Enter idle/sleep state
 *   dbar hint           - Data barrier
 *   ibar hint           - Instruction barrier
 *
 * ================================================================
 */

.section .boot, "ax"
.global _start
.type _start, @function

_start:
    /* ---- 1. Disable interrupts ----
     * Clear CRMD.IE (bit 2) using csrxchg
     * csrxchg: CSR = (CSR & ~mask) | (value & mask)
     * To clear: value=0, mask=0x4
     */
    li.d    $t0, 0          /* Value to set (0 to clear) */
    li.d    $t1, 0x4        /* Mask for IE bit */
    csrxchg $t0, $t1, 0x0   /* CRMD = CSR 0x00 */

    /* ---- 2. Determine core ID, park secondary cores ----
     * Read CPUID from CSR to get core number
     * Only core 0 continues; others spin in idle
     */
    csrrd   $t0, 0x20       /* CSR_CPUID = 0x20 */
    andi    $t0, $t0, 0x3FF /* Extract core ID (lower 10 bits) */
    bnez    $t0, park_core

    /* ---- 3. Set up stack pointer ----
     * Stack grows downward; point to top of stack area
     */
    la.global $sp, stack_top

    /* ---- 4. Clear BSS section ----
     * Zero-initialize uninitialized data
     */
    la.global $t0, __bss_start
    la.global $t1, __bss_end
clear_bss:
    bgeu    $t0, $t1, clear_bss_done
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    b       clear_bss
clear_bss_done:

    /* ---- 5. Set up Direct Mapping Window (DMW) ----
     * DMW provides direct address translation for kernel space
     * without going through TLB. This is essential for early boot.
     *
     * DMW0 configuration for uncached access (I/O):
     *   VSEG = 0x8 (addresses 0x8xxx_xxxx_xxxx_xxxx)
     *   PSEG = 0x0 (maps to physical 0x0xxx_xxxx_xxxx_xxxx)
     *   MAT  = 0 (strongly-ordered uncached)
     *   PLV0 = 1 (kernel access only)
     *
     * DMW1 configuration for cached access (memory):
     *   VSEG = 0x9 (addresses 0x9xxx_xxxx_xxxx_xxxx)
     *   PSEG = 0x0 (maps to physical 0x0xxx_xxxx_xxxx_xxxx)
     *   MAT  = 1 (cacheable coherent)
     *   PLV0 = 1 (kernel access only)
     *
     * TODO: Configure actual DMW values based on memory map
     */

    /* ---- 6. Set up exception entry point ----
     * EENTRY CSR (0x0C) holds the exception handler address
     * All exceptions (including syscalls) vector here
     */
    la.global $t0, exception_entry
    csrwr   $t0, 0x0C       /* CSR_EENTRY = 0x0C */

    /* ---- 7. Set up TLB refill exception entry ----
     * TLBRENTRY CSR (0x88) for TLB miss handling
     */
    la.global $t0, tlb_refill_entry
    csrwr   $t0, 0x88       /* CSR_TLBRENTRY = 0x88 */

    /* ---- 8. Call kernel_main ----
     * Arguments preserved from bootloader:
     *   $a0 = boot parameter pointer (FDT/boot info)
     *   $a1 = boot magic or secondary parameter
     *
     * If no valid boot info, pass zeros
     */
    /* Preserve original $a0, $a1 if valid, otherwise clear */
    /* For now, pass through whatever firmware provided */
    bl      kernel_main

    /* ---- Should never return ---- */
halt_loop:
    /* idle instruction - wait for interrupt (low power) */
    /* LoongArch encoding for 'idle 0': 0x06488000 */
    .word   0x06488000
    b       halt_loop

/* ================================================================
 * Park secondary cores (spin in idle loop)
 * ================================================================ */
park_core:
    .word   0x06488000      /* idle 0 */
    b       park_core

/* ================================================================
 * Exception Entry Point
 *
 * All exceptions (interrupts, syscalls, faults) enter here.
 * The hardware saves state to CSRs:
 *   ERA   (0x06) = return address
 *   PRMD  (0x01) = previous privilege mode and IE state
 *   ESTAT (0x05) = exception status and cause code
 *   BADV  (0x07) = bad virtual address (for memory faults)
 *
 * This stub saves registers and calls the C++ exception dispatcher.
 * ================================================================ */
.global exception_entry
.type exception_entry, @function
.align 12                   /* LoongArch requires 4KB alignment for EENTRY */
exception_entry:
    /* Save scratch register to SAVE0 CSR for use during save */
    csrwr   $t0, 0x30       /* CSR_SAVE0 = 0x30 */

    /* TODO: Save all general-purpose registers to kernel stack or
     * per-CPU exception save area.
     *
     * LoongArch exception handling sequence:
     * 1. Save $sp to SAVE1 CSR
     * 2. Load kernel exception stack from per-CPU area
     * 3. Save all GPRs ($r1-$r31, skip $r0=zero) to stack frame
     * 4. Read exception cause from ESTAT
     * 5. Dispatch to appropriate handler based on ECODE
     * 6. Restore registers
     * 7. Execute ertn to return from exception
     *
     * For syscalls (ECODE=0x0B):
     * - $a7 contains syscall number
     * - $a0-$a6 contain arguments
     * - Return value goes in $a0
     *
     * This is a stub - full implementation needed for:
     * - Register save/restore frame
     * - Interrupt dispatcher
     * - Syscall handler
     * - Page fault handler
     * - Other exception handlers
     */

    /* Placeholder: restore $t0 and spin (TODO: implement properly) */
    csrrd   $t0, 0x30
    b       exception_entry

/* ================================================================
 * TLB Refill Exception Entry
 *
 * Separate entry point for TLB miss exceptions for performance.
 * TLB refill uses separate CSRs (TLBR*) to avoid corrupting
 * regular exception state during nested exceptions.
 *
 * This is called when a memory access misses in the TLB.
 * The handler must:
 * 1. Read TLBRBADV for the faulting virtual address
 * 2. Walk the page tables to find the mapping
 * 3. Write TLBELO0/TLBELO1 with the page table entries
 * 4. Execute tlbfill to load the TLB entry
 * 5. Execute ertn to return
 * ================================================================ */
.global tlb_refill_entry
.type tlb_refill_entry, @function
.align 12                   /* 4KB alignment required */
tlb_refill_entry:
    /* TODO: Implement TLB refill handler
     *
     * Quick path TLB refill sequence:
     * 1. Read TLBRBADV (0x89) for faulting VA
     * 2. Calculate page table entry addresses
     * 3. Load PTE values
     * 4. Write to TLBELO0 (0x8C) and TLBELO1 (0x8D)
     * 5. Execute 'tlbfill' instruction
     * 6. Execute 'ertn' to return
     *
     * For now, spin (will cause hang on TLB miss - early boot
     * should use direct mapping windows to avoid TLB misses)
     */
    b       tlb_refill_entry

/* ================================================================
 * Stack space (16KB, BSS section)
 * ================================================================ */
.section .bss
.align 16
stack_bottom:
    .space  16384           /* 16 KB stack */
stack_top:

/* ================================================================
 * End of boot.s
 * ================================================================ */
