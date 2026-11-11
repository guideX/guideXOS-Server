/*
 * Itanium (IA-64) Kernel Entry Point
 *
 * Entered through the HP ski simulator's IA-64 ELF loader with the
 * processor in physical mode, interrupts disabled. This is not a UEFI entry.
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
.equ SSC_STOP,            0
.equ SSC_CONSOLE_INIT,    20
.equ SSC_GETCHAR,         21
.equ SSC_PUTCHAR,         31
.equ SSC_EXIT,            66

/* ================================================================
 * Architectural Interrupt Vector Table (IVT)
 *
 * The Itanium IVT has exactly 68 slots and occupies 32 KiB. The first 20
 * slots are 0x400 bytes each; the remaining 48 are 0x100 bytes each.
 * Offsets follow Intel Itanium Architecture Software Developer's Manual,
 * Vol. 2, Table 5-7. These are fail-stop placeholders, not handlers.
 * ================================================================ */

.equ IVT_BYTES,             0x8000
.equ IVT_LONG_SLOT_BYTES,   0x0400
.equ IVT_SHORT_SLOT_BYTES,  0x0100

.section .ivt, "ax"
.align 32768
.global ia64_ivt

ia64_ivt:

/* Place each fail-stop handler at its architecture-defined slot offset. */
.macro IVT_ENTRY_HALT name, offset, slot_bytes
    .if (\offset + \slot_bytes) > IVT_BYTES
        .error "IA-64 IVT slot extends beyond 0x8000 bytes"
    .endif
    .org ia64_ivt + \offset
    .global \name
    .global \name\()_end
    .type \name, @function
    .proc \name
\name:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
    hint @pause ;;
    br.sptk \name ;;
\name\()_end:
    .endp \name
    .size \name, \name\()_end - \name
    .if (\name\()_end - \name) > \slot_bytes
        .error "IA-64 IVT handler exceeds its assigned slot"
    .endif
.endm

/* The one early-exec exception under qualification has a dedicated entry.
 * All other profiles retain the original fail-stop Break vector. */
.macro IVT_ENTRY_BREAK name, offset, slot_bytes
    .if (\offset + \slot_bytes) > IVT_BYTES
        .error "IA-64 IVT slot extends beyond 0x8000 bytes"
    .endif
    .org ia64_ivt + \offset
    .global \name
    .global \name\()_end
    .type \name, @function
    .proc \name
\name:
    br.sptk.many ia64_break_exception_entry ;;
\name\()_end:
    .endp \name
    .size \name, \name\()_end - \name
    .if (\name\()_end - \name) > \slot_bytes
        .error "IA-64 Break handler stub exceeds its assigned slot"
    .endif
.endm

/* Only the early-exec probe owns this single General Exception vector. */
.macro IVT_ENTRY_GENERAL_EXCEPTION name, offset, slot_bytes
    .if (\offset + \slot_bytes) > IVT_BYTES
        .error "IA-64 General Exception slot extends beyond 0x8000 bytes"
    .endif
    .org ia64_ivt + \offset
    .global \name
    .global \name\()_end
    .type \name, @function
    .proc \name
\name:
    br.sptk.many ia64_general_exception_entry ;;
\name\()_end:
    .endp \name
    .size \name, \name\()_end - \name
    .if (\name\()_end - \name) > \slot_bytes
        .error "IA-64 General Exception stub exceeds its assigned slot"
    .endif
.endm

/* Only the early-exec qualification owns one Unaligned Reference entry. */
.macro IVT_ENTRY_UNALIGNED_REFERENCE name, offset, slot_bytes
    .if (\offset + \slot_bytes) > IVT_BYTES
        .error "IA-64 Unaligned Reference slot extends beyond 0x8000 bytes"
    .endif
    .org ia64_ivt + \offset
    .global \name
    .global \name\()_end
    .type \name, @function
    .proc \name
\name:
    br.sptk.many ia64_unaligned_reference_entry ;;
\name\()_end:
    .endp \name
    .size \name, \name\()_end - \name
    .if (\name\()_end - \name) > \slot_bytes
        .error "IA-64 Unaligned Reference stub exceeds its assigned slot"
    .endif
.endm

/* First 20 long vectors: 0x0000 through 0x4c00 in 0x400-byte steps. */
IVT_ENTRY_HALT ivt_vhpt_translation,           0x0000, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_itlb_miss,                  0x0400, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_dtlb_miss,                  0x0800, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_alt_itlb,                   0x0c00, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_alt_dtlb,                   0x1000, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_nested_dtlb,                0x1400, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_ikey_miss,                  0x1800, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_dkey_miss,                  0x1c00, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_dirty_bit,                  0x2000, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_iaccess_bit,                0x2400, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_daccess_bit,                0x2800, IVT_LONG_SLOT_BYTES
.ifdef GXOS_IA64_EARLY_EXEC_PROFILE
IVT_ENTRY_BREAK ivt_break,                     0x2c00, IVT_LONG_SLOT_BYTES
.else
IVT_ENTRY_HALT ivt_break,                      0x2c00, IVT_LONG_SLOT_BYTES
.endif
IVT_ENTRY_HALT ivt_external_interrupt,         0x3000, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_virtual_external_interrupt, 0x3400, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_0e,                0x3800, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_0f,                0x3c00, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_10,                0x4000, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_11,                0x4400, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_12,                0x4800, IVT_LONG_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_13,                0x4c00, IVT_LONG_SLOT_BYTES

/* Remaining 48 short vectors: 0x5000 through 0x7f00 in 0x100-byte steps. */
IVT_ENTRY_HALT ivt_page_not_present,            0x5000, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_key_permission,              0x5100, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_ia_rights,                   0x5200, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_da_rights,                   0x5300, IVT_SHORT_SLOT_BYTES
.ifdef GXOS_IA64_EARLY_EXEC_PROFILE
IVT_ENTRY_GENERAL_EXCEPTION ivt_general_exception, 0x5400, IVT_SHORT_SLOT_BYTES
.else
IVT_ENTRY_HALT ivt_general_exception,           0x5400, IVT_SHORT_SLOT_BYTES
.endif
IVT_ENTRY_HALT ivt_disabled_fp,                 0x5500, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_nat_consumption,             0x5600, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_speculation,                 0x5700, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_1c,                 0x5800, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_debug,                       0x5900, IVT_SHORT_SLOT_BYTES
.ifdef GXOS_IA64_EARLY_EXEC_PROFILE
IVT_ENTRY_UNALIGNED_REFERENCE ivt_unaligned_ref, 0x5a00, IVT_SHORT_SLOT_BYTES
.else
IVT_ENTRY_HALT ivt_unaligned_ref,               0x5a00, IVT_SHORT_SLOT_BYTES
.endif
IVT_ENTRY_HALT ivt_unsupported_data_ref,         0x5b00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_fp_fault,                    0x5c00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_fp_trap,                     0x5d00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_lp_transfer,                 0x5e00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_taken_branch,                0x5f00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_single_step,                 0x6000, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_virtualization,              0x6100, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_26,                 0x6200, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_27,                 0x6300, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_28,                 0x6400, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_29,                 0x6500, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_2a,                 0x6600, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_2b,                 0x6700, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_2c,                 0x6800, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_ia32_exception,              0x6900, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_ia32_intercept,              0x6a00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_ia32_interrupt,              0x6b00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_30,                 0x6c00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_31,                 0x6d00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_32,                 0x6e00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_33,                 0x6f00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_34,                 0x7000, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_35,                 0x7100, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_36,                 0x7200, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_37,                 0x7300, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_38,                 0x7400, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_39,                 0x7500, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_3a,                 0x7600, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_3b,                 0x7700, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_3c,                 0x7800, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_3d,                 0x7900, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_3e,                 0x7a00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_3f,                 0x7b00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_40,                 0x7c00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_41,                 0x7d00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_42,                 0x7e00, IVT_SHORT_SLOT_BYTES
IVT_ENTRY_HALT ivt_reserved_43,                 0x7f00, IVT_SHORT_SLOT_BYTES

.org ia64_ivt + IVT_BYTES
.global ia64_ivt_end
ia64_ivt_end:
.size ia64_ivt, IVT_BYTES

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

    /* ---- Checkpoint A: inherited state after the interrupt-safe entry ----
     * BSP is readable in every RSE mode. Do not touch BSPSTORE or RNAT here:
     * their access requires enforced-lazy mode, which is not assumed yet.
     * The record is in initialized .data and survives the later BSS clear.
     */
    movl r14 = ia64_checkpoint_a ;;
    mov r15 = psr ;;
    add r16 = 16, r14 ;;
    st8 [r16] = r15 ;;
    mov r15 = ar.rsc ;;
    add r16 = 24, r14 ;;
    st8 [r16] = r15 ;;
    mov r15 = ar.bsp ;;
    add r16 = 32, r14 ;;
    st8 [r16] = r15 ;;
    mov r15 = ar.fpsr ;;
    add r16 = 56, r14 ;;
    st8 [r16] = r15 ;;
    movl r15 = 0x27 ;;
    add r16 = 8, r14 ;;
    st8 [r16] = r15 ;;
    movl r15 = 1 ;;
    st8 [r14] = r15 ;;
    mf ;;

    /* ---- 4. Set up RSE backing store ----
     * BSPSTORE and loadrs are only legal in enforced-lazy mode. Empty any
     * inherited dirty register-stack partition before moving BSPSTORE.
     */
    movl r14 = rse_backing_store_base ;;
    mov ar.rsc = 0 ;;
    loadrs ;;
    mov ar.bspstore = r14 ;;

    /* ---- Checkpoint B: enforced-lazy backing-store initialization ----
     * Capture legal RSE pointers before returning to eager mode. RNAT is
     * deliberately omitted: writing BSPSTORE makes RNAT's contents
     * architecturally undefined until later RSE activity establishes them.
     * No call, SSC, or extra RSE transition is introduced in this sequence.
     */
    movl r15 = ia64_checkpoint_b ;;
    mov r16 = psr ;;
    add r17 = 16, r15 ;;
    st8 [r17] = r16 ;;
    mov r16 = ar.rsc ;;
    add r17 = 24, r15 ;;
    st8 [r17] = r16 ;;
    mov r16 = ar.bsp ;;
    add r17 = 32, r15 ;;
    st8 [r17] = r16 ;;
    mov r16 = ar.bspstore ;;
    add r17 = 40, r15 ;;
    st8 [r17] = r16 ;;
    mov r16 = ar.fpsr ;;
    add r17 = 56, r15 ;;
    st8 [r17] = r16 ;;
    movl r16 = 0x2f ;;
    add r17 = 8, r15 ;;
    st8 [r17] = r16 ;;
    movl r16 = 2 ;;
    st8 [r15] = r16 ;;
    mf ;;
    mov ar.rsc = 3 ;;       /* eager RSE mode, privilege 0, little-endian */

    /* ---- 5. Establish the compiler-required FP environment ----
     * IA64-2's bounded profile permits ABI scratch FP registers, including
     * f32-f127. Do not inherit firmware/SKI disable bits or FPSR controls.
     */
    rsm psr.dfl | psr.dfh ;;
    srlz.d ;;
    movl r14 = 0x0009804c0270033f ;;
    mov.m ar.fpsr = r14 ;;

    /* ---- 6. Install IVT ----
     * Write the IVT base address to cr.iva, then serialize.
     */
    movl r14 = ia64_ivt ;;
    mov cr.iva = r14 ;;
    srlz.i ;;

    /* ---- 7. Initialize ski console via SSC ---- */
    mov r15 = SSC_CONSOLE_INIT ;;
    break.i 0x80000 ;;

    /* ---- 8. Clear BSS section ---- */
    movl r14 = __bss_start
    movl r15 = __bss_end
    mov r16 = 0 ;;

clear_bss:
    cmp.geu p6, p7 = r14, r15 ;;
(p7) st8 [r14] = r16, 8
(p7) br.cond.dptk clear_bss ;;

    /* ---- 9. Call kernel_main(0, 0) ----
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
.global rse_backing_store_base
rse_backing_store_base:
    .skip 65536
.global rse_backing_store_top
rse_backing_store_top:
