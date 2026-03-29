/*
 * Itanium (IA-64) Kernel Entry Point
 *
 * Entered by the HP ski simulator (or an EFI stub loader) with
 * the processor in physical mode, interrupts disabled.
 *
 * IA-64 boot differences from other architectures:
 *   - 128 general registers (r0-r127), r0 is hardwired to 0
 *   - Register Stack Engine (RSE) manages spill/fill automatically
 *   - Instruction bundles are 128-bit (3 instructions + template)
 *   - Global Pointer (gp / r1) used for data addressing
 *   - Branch registers (b0-b7), b0 = return link (rp)
 *   - Predicate registers (p0-p63), p0 always true
 *   - IVT (Interrupt Vector Table) must be 32 KB aligned
 *
 * ski SSC (Simulator System Call) interface:
 *   mov r15 = <ssc_code>
 *   break.i 0x80000
 *
 * Copyright (c) 2026 guideXOS Server
 */

/* ================================================================
 * SSC (Simulator System Call) codes for ski
 * ================================================================ */
#define SSC_STOP            0x00
#define SSC_CONSOLE_INIT    0x20
#define SSC_PUTCHAR         0x21
#define SSC_GETCHAR         0x22

/* ================================================================
 * Minimal Interrupt Vector Table (IVT)
 *
 * IA-64 IVT has 69 entry points, each 256 bytes (64 bundles).
 * The table must be 32 KB (0x8000) aligned.
 * We provide a catch-all that halts on any unexpected interrupt.
 * ================================================================ */

.section .ivt, "ax"
.align 32768
.global ia64_ivt

ia64_ivt:

/* Macro: generate one 256-byte IVT entry that just halts */
#define IVT_ENTRY_HALT(name)        \
    .align 256;                     \
name:                               \
    rsm psr.i | psr.ic ;;          \
    srlz.d ;;                       \
    hint @pause ;;                  \
    br.sptk name ;;

/* Entry 0x00: VHPT Translation (offset 0x0000) */
IVT_ENTRY_HALT(ivt_vhpt_translation)
/* Entry 0x01: Instruction TLB (offset 0x0400) */
IVT_ENTRY_HALT(ivt_itlb_miss)
/* Entry 0x02: Data TLB (offset 0x0800) */
IVT_ENTRY_HALT(ivt_dtlb_miss)
/* Entry 0x03: Alternate Instruction TLB (offset 0x0C00) */
IVT_ENTRY_HALT(ivt_alt_itlb)
/* Entry 0x04: Alternate Data TLB (offset 0x1000) */
IVT_ENTRY_HALT(ivt_alt_dtlb)
/* Entry 0x05: Data Nested TLB (offset 0x1400) */
IVT_ENTRY_HALT(ivt_nested_dtlb)
/* Entry 0x06: Instruction Key Miss (offset 0x1800) */
IVT_ENTRY_HALT(ivt_ikey_miss)
/* Entry 0x07: Data Key Miss (offset 0x1C00) */
IVT_ENTRY_HALT(ivt_dkey_miss)
/* Entry 0x08: Dirty Bit (offset 0x2000) */
IVT_ENTRY_HALT(ivt_dirty_bit)
/* Entry 0x09: Instruction Access Bit (offset 0x2400) */
IVT_ENTRY_HALT(ivt_iaccess_bit)
/* Entry 0x0A: Data Access Bit (offset 0x2800) */
IVT_ENTRY_HALT(ivt_daccess_bit)
/* Entry 0x0B: Break Instruction (offset 0x2C00) */
IVT_ENTRY_HALT(ivt_break)
/* Entry 0x0C: External Interrupt (offset 0x3000) */
.align 256
.global ivt_external_interrupt
ivt_external_interrupt:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
    /* Acknowledge and halt — full handler TODO */
    hint @pause ;;
    br.sptk ivt_external_interrupt ;;

/* Entries 0x0D–0x44: remaining vectors (catch-all halt) */
/* 0x0D: Reserved */
IVT_ENTRY_HALT(ivt_reserved_0d)
/* 0x0E: Reserved */
IVT_ENTRY_HALT(ivt_reserved_0e)
/* 0x0F: Reserved */
IVT_ENTRY_HALT(ivt_reserved_0f)
/* 0x10–0x13: Page Not Present, Key Permission, Instruction Access Rights, Data Access Rights */
IVT_ENTRY_HALT(ivt_page_not_present)
IVT_ENTRY_HALT(ivt_key_permission)
IVT_ENTRY_HALT(ivt_ia_rights)
IVT_ENTRY_HALT(ivt_da_rights)
/* 0x14: General Exception */
IVT_ENTRY_HALT(ivt_general_exception)
/* 0x15: Disabled FP Register */
IVT_ENTRY_HALT(ivt_disabled_fp)
/* 0x16: NaT Consumption */
IVT_ENTRY_HALT(ivt_nat_consumption)
/* 0x17: Speculation */
IVT_ENTRY_HALT(ivt_speculation)
/* 0x18: Reserved */
IVT_ENTRY_HALT(ivt_reserved_18)
/* 0x19: Debug */
IVT_ENTRY_HALT(ivt_debug)
/* 0x1A: Unaligned Reference */
IVT_ENTRY_HALT(ivt_unaligned_ref)
/* 0x1B: Unsupported Data Reference */
IVT_ENTRY_HALT(ivt_unsupported_data_ref)
/* 0x1C: Floating-Point Fault */
IVT_ENTRY_HALT(ivt_fp_fault)
/* 0x1D: Floating-Point Trap */
IVT_ENTRY_HALT(ivt_fp_trap)
/* 0x1E: Lower-Privilege Transfer Trap */
IVT_ENTRY_HALT(ivt_lp_transfer)
/* 0x1F: Taken Branch Trap */
IVT_ENTRY_HALT(ivt_taken_branch)
/* 0x20: Single Step Trap */
IVT_ENTRY_HALT(ivt_single_step)
/* 0x21–0x44: remaining reserved / impl-specific vectors */
IVT_ENTRY_HALT(ivt_reserved_21)
IVT_ENTRY_HALT(ivt_reserved_22)
IVT_ENTRY_HALT(ivt_reserved_23)
IVT_ENTRY_HALT(ivt_reserved_24)
IVT_ENTRY_HALT(ivt_reserved_25)
IVT_ENTRY_HALT(ivt_reserved_26)
IVT_ENTRY_HALT(ivt_reserved_27)
IVT_ENTRY_HALT(ivt_reserved_28)
IVT_ENTRY_HALT(ivt_reserved_29)
IVT_ENTRY_HALT(ivt_reserved_2a)
IVT_ENTRY_HALT(ivt_reserved_2b)
IVT_ENTRY_HALT(ivt_reserved_2c)
IVT_ENTRY_HALT(ivt_reserved_2d)
IVT_ENTRY_HALT(ivt_reserved_2e)
IVT_ENTRY_HALT(ivt_reserved_2f)
IVT_ENTRY_HALT(ivt_reserved_30)
IVT_ENTRY_HALT(ivt_reserved_31)
IVT_ENTRY_HALT(ivt_reserved_32)
IVT_ENTRY_HALT(ivt_reserved_33)
IVT_ENTRY_HALT(ivt_reserved_34)
IVT_ENTRY_HALT(ivt_reserved_35)
IVT_ENTRY_HALT(ivt_reserved_36)
IVT_ENTRY_HALT(ivt_reserved_37)
IVT_ENTRY_HALT(ivt_reserved_38)
IVT_ENTRY_HALT(ivt_reserved_39)
IVT_ENTRY_HALT(ivt_reserved_3a)
IVT_ENTRY_HALT(ivt_reserved_3b)
IVT_ENTRY_HALT(ivt_reserved_3c)
IVT_ENTRY_HALT(ivt_reserved_3d)
IVT_ENTRY_HALT(ivt_reserved_3e)
IVT_ENTRY_HALT(ivt_reserved_3f)
IVT_ENTRY_HALT(ivt_reserved_40)
IVT_ENTRY_HALT(ivt_reserved_41)
IVT_ENTRY_HALT(ivt_reserved_42)
IVT_ENTRY_HALT(ivt_reserved_43)
IVT_ENTRY_HALT(ivt_reserved_44)

/* ================================================================
 * Boot entry point
 * ================================================================ */

.section .boot, "ax"
.global _start
.proc _start

_start:
    /* ---- 1. Disable interrupts and interrupt collection ---- */
    rsm psr.i | psr.ic ;;
    srlz.d ;;

    /* ---- 2. Set up Global Pointer (gp / r1) ---- */
    movl gp = __gp ;;

    /* ---- 3. Set up memory stack (sp / r12) ----
     * IA-64 stack grows downward; sp must be 16-byte aligned.
     */
    movl sp = stack_top ;;

    /* ---- 4. Set up RSE backing store ----
     * ar.bspstore = base of backing store region.
     * Then invalidate all stacked registers.
     */
    movl r14 = rse_backing_store_base ;;
    mov ar.bspstore = r14 ;;
    loadrs ;;
    mov ar.rsc = 3 ;;       /* eager RSE mode, little-endian */

    /* ---- 5. Install IVT ----
     * Write the IVT base address to cr.iva, then serialize.
     */
    movl r14 = ia64_ivt ;;
    mov cr.iva = r14 ;;
    srlz.i ;;

    /* ---- 6. Initialize ski console via SSC ---- */
    mov r15 = SSC_CONSOLE_INIT ;;
    break.i 0x80000 ;;

    /* ---- 7. Clear BSS section ---- */
    movl r14 = __bss_start
    movl r15 = __bss_end
    mov r16 = 0 ;;

clear_bss:
    cmp.geu p6, p7 = r14, r15
(p7) st8 [r14] = r16, 8
(p7) br.cond.dptk clear_bss ;;

    /* ---- 8. Call kernel_main(0, 0) ----
     * IA-64 C calling convention: first two args in out0, out1.
     * Use alloc to create a register frame with 2 output registers.
     */
    alloc r14 = ar.pfs, 0, 0, 2, 0 ;;
    mov out0 = r0          /* boot_environment = NULL */
    mov out1 = r0 ;;       /* boot_magic = 0 */
    br.call.sptk.many rp = kernel_main ;;

    /* ---- Should never return ---- */
halt:
    hint @pause ;;
    br.sptk halt ;;

.endp _start

/* ================================================================
 * Memory stack — 64 KB (BSS)
 * ================================================================ */
.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

/* ================================================================
 * RSE (Register Stack Engine) backing store — 64 KB (BSS)
 *
 * The RSE spills stacked registers here automatically.
 * Must be separate from the memory stack.
 * ================================================================ */
.align 16
rse_backing_store_base:
    .skip 65536
rse_backing_store_top:
