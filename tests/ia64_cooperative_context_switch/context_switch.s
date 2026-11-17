/*
 * Bounded synchronous RSE switch used only by the IA-64 Ski qualification.
 * The interface is a normal procedure call: in0=outgoing Context,
 * in1=incoming Context.  No production scheduler code references this file.
 */

.equ C_GR4, 0
.equ C_GR8, 32
.equ C_SP, 40
.equ C_GP, 48
.equ C_TP, 56
.equ C_RP, 64
.equ C_PFS, 72
.equ C_RSC, 80
.equ C_BSP, 88
.equ C_BSPSTORE, 96
.equ C_RNAT, 104
.equ C_CALLER_UNAT, 112
.equ C_SPILL_UNAT, 120
.equ C_LC, 128
.equ C_EC, 136
.equ C_CCV, 144
.equ C_PR, 152
.equ C_BR1, 160
.equ C_BR2, 168
.equ C_BR3, 176
.equ C_BR4, 184
.equ C_BR5, 192
.equ C_FPSR, 200
.equ C_FP2, 208
.equ C_STACK_BASE, 528
.equ C_STACK_LIMIT, 536
.equ C_RBS_BASE, 544
.equ C_RBS_LIMIT, 552
.equ C_FIRST_ENTRY, 560
.equ C_CONTEXT_ID, 568
.equ C_SWITCH_COUNT, 592
.equ C_PROFILE_MAGIC, 600
.equ C_RNAT_VALID_MASK, 640
.equ C_RNAT_DEFINED_BITS, 648
.equ C_OBSERVED_SP, 656
.equ C_OBSERVED_BSP, 664
.equ C_OBSERVED_BSPSTORE, 672
.equ C_OBSERVED_PSR, 680
.equ C_CODE_BASE, 688
.equ C_CODE_LIMIT, 696

.equ CTX_PROFILE_MAGIC, 0x3541434f4f505631
.equ CTX_A_ID, 0xA
.equ CTX_B_ID, 0xB

.macro STORE64 offset, value
    adds r28 = \offset, r14 ;;
    st8 [r28] = \value ;;
.endm

.macro LOAD64 offset, target, value
    adds \target = \offset, r15 ;;
    ld8 \value = [\target] ;;
.endm

.macro SAVE_FP offset, freg
    adds r28 = \offset, r14 ;;
    stf.spill [r28] = \freg ;;
.endm

.macro RESTORE_FP offset, freg
    adds r28 = \offset, r15 ;;
    ldf.fill \freg = [r28] ;;
.endm

/* Fail if [a0,a1) overlaps [b0,b1). */
.macro ASSERT_DISJOINT a0, a1, b0, b1
    cmp.ltu p6, p7 = \a0, \b1 ;;
(p7) br.cond.sptk .Lrange_disjoint_\@ ;;
    cmp.ltu p6, p7 = \b0, \a1 ;;
(p6) br.cond.sptk ia64_coop_switch_fail ;;
.Lrange_disjoint_\@:
.endm

.section .text.ia64_coop_switch, "ax"
.align 16
.global ia64_coop_context_switch
.type ia64_coop_context_switch, @function
.proc ia64_coop_context_switch
ia64_coop_context_switch:
    /* Save the caller's RSE frame marker as the return context. */
    alloc r16 = ar.pfs, 2, 0, 0, 0 ;;
    mov r14 = in0 ;;
    mov r15 = in1 ;;

    /* Context bases share the 512-byte RSE collection alignment. */
    movl r29 = 511 ;;
    and r22 = r14, r29 ;;
    cmp.eq p6, p7 = r22, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    and r22 = r15, r29 ;;
    cmp.eq p6, p7 = r22, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;

    /* Reject malformed identity, bounds, geometry, and unsupported profile. */
    adds r28 = C_PROFILE_MAGIC, r14 ;;
    ld8 r20 = [r28] ;;
    adds r28 = C_PROFILE_MAGIC, r15 ;;
    ld8 r21 = [r28] ;;
    movl r22 = CTX_PROFILE_MAGIC ;;
    cmp.eq p6, p7 = r20, r22 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.eq p6, p7 = r21, r22 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_CONTEXT_ID, r14 ;;
    ld8 r22 = [r28] ;;
    adds r28 = C_CONTEXT_ID, r15 ;;
    ld8 r23 = [r28] ;;
    cmp.eq p6, p7 = r22, r23 ;;
(p6) br.cond.sptk ia64_coop_switch_fail ;;
    mov r24 = CTX_A_ID ;;
    cmp.eq p6, p7 = r22, r24 ;;
(p6) br.cond.sptk .Lswitch_from_a ;;
    mov r24 = CTX_B_ID ;;
    cmp.eq p6, p7 = r22, r24 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    mov r24 = CTX_A_ID ;;
    cmp.eq p6, p7 = r23, r24 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    br.cond.sptk .Lswitch_ids_ok ;;
.Lswitch_from_a:
    mov r24 = CTX_B_ID ;;
    cmp.eq p6, p7 = r23, r24 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
.Lswitch_ids_ok:
    adds r28 = C_STACK_BASE, r14 ;;
    ld8 r20 = [r28] ;;
    adds r28 = C_STACK_LIMIT, r14 ;;
    ld8 r21 = [r28] ;;
    adds r28 = C_RBS_BASE, r14 ;;
    ld8 r22 = [r28] ;;
    adds r28 = C_RBS_LIMIT, r14 ;;
    ld8 r23 = [r28] ;;
    adds r28 = C_STACK_BASE, r15 ;;
    ld8 r24 = [r28] ;;
    adds r28 = C_STACK_LIMIT, r15 ;;
    ld8 r25 = [r28] ;;
    adds r28 = C_RBS_BASE, r15 ;;
    ld8 r26 = [r28] ;;
    adds r28 = C_RBS_LIMIT, r15 ;;
    ld8 r27 = [r28] ;;
    cmp.ltu p6, p7 = r20, r21 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.ltu p6, p7 = r22, r23 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.ltu p6, p7 = r24, r25 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.ltu p6, p7 = r26, r27 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    ASSERT_DISJOINT r20, r21, r22, r23
    ASSERT_DISJOINT r20, r21, r24, r25
    ASSERT_DISJOINT r20, r21, r26, r27
    ASSERT_DISJOINT r22, r23, r24, r25
    ASSERT_DISJOINT r22, r23, r26, r27
    ASSERT_DISJOINT r24, r25, r26, r27

    /* Both stacks are 16-byte aligned and the saved SP may equal its limit. */
    adds r28 = C_SP, r14 ;;
    ld8 r20 = [r28] ;;
    mov r22 = 15 ;;
    and r21 = r20, r22 ;;
    cmp.eq p6, p7 = r21, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_STACK_BASE, r14 ;;
    ld8 r21 = [r28] ;;
    cmp.leu p6, p7 = r21, r20 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_STACK_LIMIT, r14 ;;
    ld8 r21 = [r28] ;;
    cmp.leu p6, p7 = r20, r21 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_SP, r15 ;;
    ld8 r20 = [r28] ;;
    mov r22 = 15 ;;
    and r21 = r20, r22 ;;
    cmp.eq p6, p7 = r21, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_STACK_BASE, r15 ;;
    ld8 r21 = [r28] ;;
    cmp.leu p6, p7 = r21, r20 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_STACK_LIMIT, r15 ;;
    ld8 r21 = [r28] ;;
    cmp.leu p6, p7 = r20, r21 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;

    /* Backing-store pointers and bounds use 512-byte collection geometry. */
    adds r28 = C_RBS_BASE, r14 ;;
    ld8 r20 = [r28] ;;
    movl r22 = 511 ;;
    and r21 = r20, r22 ;;
    cmp.eq p6, p7 = r21, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_RBS_BASE, r15 ;;
    ld8 r20 = [r28] ;;
    movl r22 = 511 ;;
    and r21 = r20, r22 ;;
    cmp.eq p6, p7 = r21, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_RBS_LIMIT, r15 ;;
    ld8 r21 = [r28] ;;
    adds r28 = C_BSP, r15 ;;
    ld8 r20 = [r28] ;;
    mov r22 = r20 ;;
    cmp.leu p6, p7 = r20, r21 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_RBS_BASE, r15 ;;
    ld8 r21 = [r28] ;;
    cmp.leu p6, p7 = r21, r20 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_RBS_LIMIT, r15 ;;
    ld8 r21 = [r28] ;;
    adds r28 = C_BSPSTORE, r15 ;;
    ld8 r20 = [r28] ;;
    cmp.eq p6, p7 = r20, r22 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.leu p6, p7 = r20, r21 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_RBS_BASE, r15 ;;
    ld8 r21 = [r28] ;;
    cmp.leu p6, p7 = r21, r20 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;

    /* The test uses a fixed RSC=3 profile and a valid continuation per path. */
    adds r28 = C_RSC, r15 ;;
    ld8 r20 = [r28] ;;
    mov r21 = 3 ;;
    cmp.eq p6, p7 = r20, r21 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_FIRST_ENTRY, r15 ;;
    ld8 r20 = [r28] ;;
    mov r21 = 1 ;;
    cmp.leu p6, p7 = r20, r21 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.eq p6, p7 = r20, r0 ;;
(p6) br.cond.sptk .Lsaved_context ;;
.Lfirst_entry_context:
    adds r28 = C_PFS, r15 ;;
    ld8 r20 = [r28] ;;
    cmp.eq p6, p7 = r20, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;; /* bootstrap PFS must be zero */
    br.cond.sptk .Ltarget_checked ;;
.Lsaved_context:
    adds r28 = C_PFS, r15 ;;
    ld8 r20 = [r28] ;;
    cmp.eq p6, p7 = r20, r0 ;;
(p6) br.cond.sptk ia64_coop_switch_fail ;; /* resumed PFS must be nonzero */
.Ltarget_checked:
.Ltarget_saved:
    adds r28 = C_RP, r15 ;;
    ld8 r20 = [r28] ;;
    mov r22 = 15 ;;
    and r21 = r20, r22 ;;
    cmp.eq p6, p7 = r21, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.ne p6, p7 = r20, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_CODE_BASE, r15 ;;
    ld8 r22 = [r28] ;;
    movl r23 = __text_start ;;
    cmp.eq p6, p7 = r22, r23 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_CODE_LIMIT, r15 ;;
    ld8 r22 = [r28] ;;
    movl r23 = __text_end ;;
    cmp.eq p6, p7 = r22, r23 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    cmp.ltu p6, p7 = r20, r23 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    movl r22 = __text_start ;;
    cmp.leu p6, p7 = r22, r20 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;

    /* Save the synchronous caller context. */
    mov r20 = ar.rsc ;;
    mov r22 = 3 ;;
    cmp.eq p6, p7 = r20, r22 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    mov r21 = psr ;;
    movl r22 = 0x4000 ;;
    and r23 = r21, r22 ;;
    cmp.eq p6, p7 = r23, r0 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;
    mov r22 = ar.bsp ;;
    mov r23 = b0 ;;
    mov r24 = r12 ;
    mov r25 = r1 ;
    mov r26 = r13 ;
    mov r27 = ar.lc ;;
    STORE64 C_SP, r24
    STORE64 C_GP, r25
    STORE64 C_TP, r26
    STORE64 C_RP, r23
    STORE64 C_PFS, r16
    STORE64 C_RSC, r20
    STORE64 C_BSP, r22
    STORE64 C_LC, r27
    mov r27 = ar.ec ;;
    STORE64 136, r27
    mov r27 = ar.ccv ;;
    STORE64 144, r27
    STORE64 C_OBSERVED_SP, r24
    STORE64 C_OBSERVED_BSP, r22
    STORE64 C_OBSERVED_PSR, r21

    /* Preserve the caller's UNAT separately from spill-slot NaT bits. */
    mov r27 = ar.unat ;;
    STORE64 C_CALLER_UNAT, r27
    mov r28 = r14 ;;
    st8.spill [r28] = r4, 8 ;;
    st8.spill [r28] = r5, 8 ;;
    st8.spill [r28] = r6, 8 ;;
    st8.spill [r28] = r7 ;;
    mov r27 = ar.unat ;;
    STORE64 C_SPILL_UNAT, r27

    mov r27 = pr ;;
    STORE64 C_PR, r27
    mov r27 = b1 ;;
    STORE64 C_BR1, r27
    mov r27 = b2 ;;
    STORE64 C_BR2, r27
    mov r27 = b3 ;;
    STORE64 C_BR3, r27
    mov r27 = b4 ;;
    STORE64 C_BR4, r27
    mov r27 = b5 ;;
    STORE64 C_BR5, r27
    mov r27 = ar.fpsr ;;
    STORE64 C_FPSR, r27

    SAVE_FP 208, f2
    SAVE_FP 224, f3
    SAVE_FP 240, f4
    SAVE_FP 256, f5
    SAVE_FP 272, f16
    SAVE_FP 288, f17
    SAVE_FP 304, f18
    SAVE_FP 320, f19
    SAVE_FP 336, f20
    SAVE_FP 352, f21
    SAVE_FP 368, f22
    SAVE_FP 384, f23
    SAVE_FP 400, f24
    SAVE_FP 416, f25
    SAVE_FP 432, f26
    SAVE_FP 448, f27
    SAVE_FP 464, f28
    SAVE_FP 480, f29
    SAVE_FP 496, f30
    SAVE_FP 512, f31
    adds r28 = C_SWITCH_COUNT, r14 ;;
    ld8 r27 = [r28] ;;
    adds r27 = 1, r27 ;;
    st8 [r28] = r27 ;;
    adds r28 = C_FIRST_ENTRY, r14 ;;
    st8 [r28] = r0 ;;

    /* Synchronous RSE switch order: save BSP/PFS/RSC, flush, freeze, RNAT. */
    flushrs ;;
    mf ;;
    mov.m ar.rsc = r0 ;;
    srlz.d ;;
    mov r24 = ar.rnat ;;
    mov r23 = ar.bspstore ;;
    adds r28 = C_BSP, r14 ;;
    ld8 r22 = [r28] ;;
    cmp.eq p6, p7 = r22, r23 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_RBS_BASE, r14 ;;
    ld8 r26 = [r28] ;;
    cmp.leu p6, p7 = r26, r23 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    adds r28 = C_RBS_LIMIT, r14 ;;
    ld8 r27 = [r28] ;;
    cmp.leu p6, p7 = r23, r27 ;;
(p7) br.cond.sptk ia64_coop_switch_fail ;;
    extr.u r21 = r23, 3, 6 ;;
    mov r22 = 63 ;;
    cmp.geu p6, p7 = r21, r22 ;;
(p6) br.cond.sptk ia64_coop_switch_fail ;;
    adds r21 = 1, r21 ;;
    mov r22 = 1 ;;
    shl r22 = r22, r21 ;;
    adds r22 = -1, r22 ;;
    and r25 = r24, r22 ;;
    STORE64 C_RNAT, r24
    STORE64 C_BSPSTORE, r23
    STORE64 C_RNAT_VALID_MASK, r22
    STORE64 C_RNAT_DEFINED_BITS, r25
    STORE64 C_OBSERVED_BSPSTORE, r23
    mf ;;

    /* Install incoming RBS. BSPSTORE invalidates RNAT, so RNAT follows it. */
    invala ;;
    mov.m ar.rsc = r0 ;;
    srlz.d ;;
    LOAD64 C_BSPSTORE, r28, r20
    mov ar.bspstore = r20 ;;
    srlz.d ;;
    LOAD64 C_RNAT, r28, r21
    mov ar.rnat = r21 ;;
    LOAD64 C_PFS, r28, r22
    mov ar.pfs = r22 ;;
    LOAD64 C_FIRST_ENTRY, r28, r29
    LOAD64 C_SPILL_UNAT, r28, r26
    mov ar.unat = r26 ;;

    mov r28 = r15 ;;
    ld8.fill r4 = [r28], 8 ;;
    ld8.fill r5 = [r28], 8 ;;
    ld8.fill r6 = [r28], 8 ;;
    ld8.fill r7 = [r28] ;;
    mov r22 = 1 ;;
    cmp.eq p6, p7 = r29, r22 ;;
    adds r28 = C_GR8, r15 ;;
(p6) ld8.fill r8 = [r28] ;;
    LOAD64 C_CALLER_UNAT, r28, r26
    mov ar.unat = r26 ;;

    LOAD64 C_LC, r28, r20
    mov ar.lc = r20 ;;
    LOAD64 136, r28, r20
    mov ar.ec = r20 ;;
    LOAD64 144, r28, r20
    mov ar.ccv = r20 ;;

    LOAD64 C_BR1, r28, r20
    mov b1 = r20 ;;
    LOAD64 C_BR2, r28, r20
    mov b2 = r20 ;;
    LOAD64 C_BR3, r28, r20
    mov b3 = r20 ;;
    LOAD64 C_BR4, r28, r20
    mov b4 = r20 ;;
    LOAD64 C_BR5, r28, r20
    mov b5 = r20 ;;
    LOAD64 C_PR, r28, r20
    mov pr = r20, -1 ;;

    RESTORE_FP 208, f2
    RESTORE_FP 224, f3
    RESTORE_FP 240, f4
    RESTORE_FP 256, f5
    RESTORE_FP 272, f16
    RESTORE_FP 288, f17
    RESTORE_FP 304, f18
    RESTORE_FP 320, f19
    RESTORE_FP 336, f20
    RESTORE_FP 352, f21
    RESTORE_FP 368, f22
    RESTORE_FP 384, f23
    RESTORE_FP 400, f24
    RESTORE_FP 416, f25
    RESTORE_FP 432, f26
    RESTORE_FP 448, f27
    RESTORE_FP 464, f28
    RESTORE_FP 480, f29
    RESTORE_FP 496, f30
    RESTORE_FP 512, f31
    LOAD64 C_FPSR, r28, r20
    mov.m ar.fpsr = r20 ;;
    LOAD64 C_TP, r28, r20
    mov r13 = r20 ;;
    LOAD64 C_GP, r28, r20
    mov r1 = r20 ;;
    LOAD64 C_SP, r28, r20
    mov r12 = r20 ;;
    LOAD64 C_RP, r28, r20
    mov b0 = r20 ;;

    LOAD64 C_RSC, r28, r20
    mov.m ar.rsc = r20 ;;
    srlz.d ;;
    br.ret.sptk.many b0 ;;
.endp ia64_coop_context_switch
.size ia64_coop_context_switch, .-ia64_coop_context_switch

.align 16
ia64_coop_switch_fail:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
1:  hint @pause ;;
    br.sptk 1b ;;
