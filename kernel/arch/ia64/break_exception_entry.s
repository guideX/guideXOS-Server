/*
 * Bounded Break Instruction qualification path.
 *
 * This file is linked only by the early-exec profile (or the independent
 * Ski diagnostic ELF).  The handler is deliberately tied to two known test
 * sites, two non-reserved immediates, and an interrupted bank-1 frame.  It is
 * not a general IA-64 exception dispatcher.
 */

.equ BREAK_VECTOR_OFFSET,       0x2c00
.equ BREAK_IMMEDIATE_SLOT1,     0x6a17
.equ BREAK_IMMEDIATE_SLOT2,     0x6a18
.equ BREAK_VALID_MARKER,        0xbeefc0de4b56414c
.equ BREAK_COMPLETION_SLOT1,    0xbeefc0de4b000001
.equ BREAK_COMPLETION_SLOT2,    0xbeefc0de4b000002
.equ BREAK_RETURN_SLOT1,        0xbeefc0de4b100001
.equ BREAK_RETURN_SLOT2,        0xbeefc0de4b100002
.equ BREAK_POST_RFI_VALID_SLOT1, 0xbeefc0de4b505231
.equ BREAK_POST_RFI_VALID_SLOT2, 0xbeefc0de4b505232

.equ REC_VALID,                 0
.equ REC_SCHEMA,                8
.equ REC_VECTOR,                16
.equ REC_IMMEDIATE,             24
.equ REC_IIP,                   32
.equ REC_SLOT,                  40
.equ REC_IPSR,                  48
.equ REC_IFS_VALID,             56
.equ REC_ISR,                   64
.equ REC_HANDLER_PSR,           72
.equ REC_HANDLER_RSC,           80
.equ REC_HANDLER_BSP,           88
.equ REC_COMPLETION,            96
.equ REC_RETURN,                104
.equ REC_CONTINUATION_COUNT,    112
.equ REC_SAVED_PR,              120
.equ REC_POST_RFI_PSR,          128
.equ REC_POST_RFI_CONTINUATION, 136
.equ REC_POST_RFI_RETURN_SLOT,  144
.equ REC_POST_RFI_ORDER,        152
.equ REC_POST_RFI_VALID,        160

.section .bss.break_exception, "aw", @nobits
.align 16
.global ia64_break_exception_record_slot1
.type ia64_break_exception_record_slot1, @object
ia64_break_exception_record_slot1:
    .skip 176
.size ia64_break_exception_record_slot1, 176
.global ia64_break_exception_record_slot2
.type ia64_break_exception_record_slot2, @object
ia64_break_exception_record_slot2:
    .skip 176
.size ia64_break_exception_record_slot2, 176
.global ia64_break_post_rfi_sequence
.type ia64_break_post_rfi_sequence, @object
ia64_break_post_rfi_sequence:
    .skip 8
.size ia64_break_post_rfi_sequence, 8

.section .text.break_exception, "ax"
.align 16
.global ia64_break_exception_entry
.type ia64_break_exception_entry, @function
.proc ia64_break_exception_entry
ia64_break_exception_entry:
    /* The source BN=1 guarantee means IVA has selected the untouched bank 0. */
    mov r16 = pr ;;
    mov r17 = psr ;;
    mov r18 = cr.iip ;;
    mov r19 = cr.ipsr ;;
    mov r20 = cr.ifs ;;
    mov r21 = cr.iim ;;
    mov r22 = cr.isr ;;
    mov r23 = ar.rsc ;;
    mov r24 = ar.bsp ;;

    /* Entry PSR must be masked, collected-off, bank 0, privileged, non-VM. */
    movl r26 = 0x6000 ;;
    and r25 = r17, r26 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;;
    extr.u r25 = r17, 44, 1 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;;
    extr.u r25 = r17, 32, 2 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;
    extr.u r25 = r17, 46, 1 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;

    /* The test fault must have come from BN=1, CPL0, VM=0, IC=1, i=0. */
    extr.u r25 = r19, 44, 1 ;;
    mov r26 = 1 ;;
    cmp.eq p6, p7 = r25, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;
    extr.u r25 = r19, 13, 1 ;;
    mov r26 = 1 ;;
    cmp.eq p6, p7 = r25, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;
    extr.u r25 = r19, 14, 1 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;;
    extr.u r25 = r19, 32, 2 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;;
    extr.u r25 = r19, 46, 1 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;

    /* No valid interrupted-frame summary is expected for this fault. */
    extr.u r25 = r20, 63, 1 ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;;
    mov r26 = 3 ;;
    cmp.eq p6, p7 = r23, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;

    /* Select the record only when IIP, RI, immediate, and ISR.EI agree. */
    extr.u r25 = r19, 41, 2 ;;
    movl r26 = ia64_break_slot1_bundle ;;
    cmp.eq p6, p7 = r18, r26 ;;
(p6) br.cond.sptk break_exception_slot1 ;;
    movl r26 = ia64_break_slot2_bundle ;;
    cmp.eq p6, p7 = r18, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;
    mov r26 = 2 ;;
    cmp.eq p6, p7 = r25, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;
    movl r26 = BREAK_IMMEDIATE_SLOT2 ;;
    cmp.eq p6, p7 = r21, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;
    extr.u r27 = r22, 41, 3 ;;
    mov r26 = 2 ;;
    cmp.eq p6, p7 = r27, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;
    movl r28 = ia64_break_exception_record_slot2 ;;
    movl r29 = BREAK_COMPLETION_SLOT2 ;;
    movl r30 = BREAK_RETURN_SLOT2 ;;
    mov r31 = 2 ;;
    br.cond.sptk break_exception_record ;;

break_exception_slot1:
    mov r26 = 1 ;;
    cmp.eq p6, p7 = r25, r26 ;;
(p7) br.cond.sptk break_exception_fail ;;
    movl r26 = BREAK_IMMEDIATE_SLOT1 ;;
    cmp.eq p6, p7 = r21, r26 ;;
(p7) br.cond.sptk break_exception_fail ;
    extr.u r27 = r22, 41, 3 ;;
    mov r26 = 1 ;;
    cmp.eq p6, p7 = r27, r26 ;;
(p7) br.cond.sptk break_exception_fail ;
    movl r28 = ia64_break_exception_record_slot1 ;;
    movl r29 = BREAK_COMPLETION_SLOT1 ;;
    movl r30 = BREAK_RETURN_SLOT1 ;
    mov r31 = 1 ;;

break_exception_record:
    /* Reject replay or stale state before writing any record field. */
    ld8 r25 = [r28] ;;
    cmp.eq p6, p7 = r25, r0 ;;
(p7) br.cond.sptk break_exception_fail ;;

    mov r25 = 2 ;;
    mov r27 = r28 ;;
    adds r28 = REC_SCHEMA, r28 ;;
    st8 [r28] = r25, 8 ;;
    movl r25 = BREAK_VECTOR_OFFSET ;;
    st8 [r28] = r25, 8 ;;
    st8 [r28] = r21, 8 ;;
    st8 [r28] = r18, 8 ;;
    st8 [r28] = r31, 8 ;;
    st8 [r28] = r19, 8 ;;
    extr.u r25 = r20, 63, 1 ;;
    st8 [r28] = r25, 8 ;;
    st8 [r28] = r22, 8 ;;
    st8 [r28] = r17, 8 ;;
    st8 [r28] = r23, 8 ;;
    st8 [r28] = r24, 8 ;;
    st8 [r28] = r29, 8 ;;
    st8 [r28] = r0, 8 ;;
    st8 [r28] = r0, 8 ;;
    st8 [r28] = r16 ;;
    mf ;;
    movl r25 = BREAK_VALID_MARKER ;;
    st8 [r27] = r25 ;;
    mf ;;

    /* Fault return skips exactly the faulting slot. RI=1 resumes at slot 2;
       RI=2 resumes at the next bundle's slot 0. */
    mov r25 = cr.ipsr ;;
    movl r26 = 0xfffff9ffffffffff ;;
    and r25 = r25, r26 ;;
    mov r26 = 1 ;;
    cmp.eq p6, p7 = r31, r26 ;;
(p6) br.cond.sptk break_exception_return_slot1 ;;
    adds r18 = 16, r18 ;;
    mov cr.iip = r18 ;;
    br.cond.sptk break_exception_return ;;
break_exception_return_slot1:
    movl r26 = 0x40000000000 ;;
    or r25 = r25, r26 ;;
    mov cr.iip = r18 ;;
break_exception_return:
    mov cr.ipsr = r25 ;;
    srlz.d ;;
    mov pr = r16, -1 ;;
    rfi ;;
    ;;
.endp ia64_break_exception_entry

break_exception_fail:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
break_exception_fail_spin:
    hint @pause ;;
    br.sptk break_exception_fail_spin ;;
.size ia64_break_exception_entry, .-ia64_break_exception_entry

.align 16
.global ia64_break_exception_test
.type ia64_break_exception_test, @function
.proc ia64_break_exception_test
ia64_break_exception_test:
    /* Save the caller's callee-saved static bank using NaT-aware spills. */
    mov r8 = sp ;;
    adds r15 = -208, sp ;;
    mov sp = r15 ;;
    adds r15 = 184, sp ;;
    st8 [r15] = sp ;;
    adds r15 = 152, sp ;;
    st8 [r15] = r1 ;;
    mov r8 = b6 ;;
    adds r15 = 160, sp ;;
    st8 [r15] = r8 ;;
    mov r8 = ar.bsp ;;
    adds r15 = 168, sp ;;
    st8 [r15] = r8 ;;
    mov r8 = ar.rsc ;;
    adds r15 = 176, sp ;;
    st8 [r15] = r8 ;;
    mov r8 = ar.unat ;;
    mov r15 = sp ;;
    adds r15 = 128, r15 ;;
    st8 [r15] = r8 ;;
    mov r15 = sp ;;
    adds r15 = 192, r15 ;;
    st8.spill [r15] = r13 ;;
    mov r8 = pr ;;
    mov r15 = sp ;;
    adds r15 = 136, r15 ;;
    st8 [r15] = r8 ;;
    mov r15 = sp ;;
    st8.spill [r15] = r16, 8 ;;
    st8.spill [r15] = r17, 8 ;;
    st8.spill [r15] = r18, 8 ;;
    st8.spill [r15] = r19, 8 ;;
    st8.spill [r15] = r20, 8 ;;
    st8.spill [r15] = r21, 8 ;;
    st8.spill [r15] = r22, 8 ;;
    st8.spill [r15] = r23, 8 ;;
    st8.spill [r15] = r24, 8 ;;
    st8.spill [r15] = r25, 8 ;;
    st8.spill [r15] = r26, 8 ;;
    st8.spill [r15] = r27, 8 ;;
    st8.spill [r15] = r28, 8 ;;
    st8.spill [r15] = r29, 8 ;;
    st8.spill [r15] = r30, 8 ;;
    st8.spill [r15] = r31, 8 ;;
    mov r15 = sp ;;

    /* Test input state: alternate bank selected, r16-r31 known, p10 true,
       p11 false, interrupts disabled, and collection enabled only at faults. */
    bsw.1 ;;
    movl r16 = 0x1616161616161616 ;;
    movl r17 = 0x1717171717171717 ;;
    movl r18 = 0x1818181818181818 ;;
    movl r19 = 0x1919191919191919 ;;
    movl r20 = 0x2020202020202020 ;;
    movl r21 = 0x2121212121212121 ;;
    movl r22 = 0x2222222222222222 ;;
    movl r23 = 0x2323232323232323 ;;
    movl r24 = 0x2424242424242424 ;;
    movl r25 = 0x2525252525252525 ;;
    movl r26 = 0x2626262626262626 ;;
    movl r27 = 0x2727272727272727 ;;
    movl r28 = 0x2828282828282828 ;;
    movl r29 = 0x2929292929292929 ;;
    movl r30 = 0x3030303030303030 ;;
    movl r31 = 0x3131313131313131 ;;
    mov r8 = r16 ;;
    xor r8 = r8, r17 ;;
    xor r8 = r8, r18 ;;
    xor r8 = r8, r19 ;;
    xor r8 = r8, r20 ;;
    xor r8 = r8, r21 ;;
    xor r8 = r8, r22 ;;
    xor r8 = r8, r23 ;;
    xor r8 = r8, r24 ;;
    xor r8 = r8, r25 ;;
    xor r8 = r8, r26 ;;
    xor r8 = r8, r27 ;;
    xor r8 = r8, r28 ;;
    xor r8 = r8, r29 ;;
    xor r8 = r8, r30 ;;
    xor r8 = r8, r31 ;;
    adds r15 = 144, sp ;;
    st8 [r15] = r8 ;;
    movl r15 = 0x401 ;;
    mov pr = r15, -1 ;;
    mov r8 = r0 ;;
    mov r10 = 1 ;;
    mov r11 = 2 ;;
    ssm psr.ic ;;

.align 16
.global ia64_break_slot1_bundle
ia64_break_slot1_bundle:
    { .mib
        nop.m 0
        break.i BREAK_IMMEDIATE_SLOT1
        br.sptk.many ia64_break_slot1_post_rfi_capture ;;
    }
.align 16
.global ia64_break_slot1_post_rfi_capture
ia64_break_slot1_post_rfi_capture:
    /* RI=2 lands on the B-slot branch above; this is its first PSR-capable slot. */
    { .mii
        mov r15 = psr
        nop.i 0
        nop.i 0 ;;
    }
.global ia64_break_slot1_resume_tail
ia64_break_slot1_resume_tail:
    { .mii
        nop.m 0
        (p10) adds r8 = 1, r8
        nop.i 0 ;;
    }
    { .mii
        nop.m 0
        (p11) adds r8 = 2, r8
        nop.i 0
    }

    /* Save the sampled PSR first, then attest this is the first return path. */
    movl r14 = ia64_break_exception_record_slot1 ;;
    adds r13 = REC_POST_RFI_VALID, r14 ;;
    ld8 r9 = [r13] ;;
    cmp.eq p6, p7 = r9, r0 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    adds r13 = REC_POST_RFI_PSR, r14 ;;
    st8 [r13] = r15 ;;
    movl r13 = ia64_break_post_rfi_sequence ;;
    ld8 r9 = [r13] ;;
    cmp.eq p6, p7 = r9, r0 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    st8 [r13] = r10 ;;
    adds r13 = REC_POST_RFI_CONTINUATION, r14 ;;
    movl r9 = ia64_break_slot1_post_rfi_capture ;;
    st8 [r13] = r9 ;
    adds r13 = REC_POST_RFI_RETURN_SLOT, r14 ;;
    st8 [r13] = r11 ;
    adds r13 = REC_POST_RFI_ORDER, r14 ;;
    st8 [r13] = r10 ;
    mf ;;
    adds r13 = REC_POST_RFI_VALID, r14 ;;
    movl r9 = BREAK_POST_RFI_VALID_SLOT1 ;;
    st8 [r13] = r9 ;;
    mf ;;

    movl r15 = ia64_break_exception_record_slot1 ;;
    adds r15 = REC_CONTINUATION_COUNT, r15 ;;
    st8 [r15] = r8 ;;
    mov r15 = 1 ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    movl r15 = ia64_break_exception_record_slot1 ;;
    adds r15 = REC_RETURN, r15 ;;
    movl r8 = BREAK_RETURN_SLOT1 ;;
    st8 [r15] = r8 ;;
    mov r8 = r16 ;;
    xor r8 = r8, r17 ;;
    xor r8 = r8, r18 ;;
    xor r8 = r8, r19 ;;
    xor r8 = r8, r20 ;;
    xor r8 = r8, r21 ;;
    xor r8 = r8, r22 ;;
    xor r8 = r8, r23 ;;
    xor r8 = r8, r24 ;;
    xor r8 = r8, r25 ;;
    xor r8 = r8, r26 ;;
    xor r8 = r8, r27 ;;
    xor r8 = r8, r28 ;;
    xor r8 = r8, r29 ;;
    xor r8 = r8, r30 ;;
    xor r8 = r8, r31 ;;
    mov r15 = sp ;;
    adds r15 = 144, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = r1 ;;
    mov r15 = sp ;;
    adds r15 = 152, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = b6 ;;
    mov r15 = sp ;;
    adds r15 = 160, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = ar.bsp ;;
    mov r15 = sp ;;
    adds r15 = 168, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = ar.rsc ;;
    mov r15 = sp ;;
    adds r15 = 176, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = sp ;;
    mov r15 = sp ;;
    adds r15 = 184, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;

    movl r15 = 0x401 ;;
    mov pr = r15, -1 ;;
    mov r8 = r0 ;;
.align 16
.global ia64_break_slot2_bundle
ia64_break_slot2_bundle:
    { .mii
        nop.m 0
        nop.i 0
        break.i BREAK_IMMEDIATE_SLOT2
    }
.align 16
.global ia64_break_slot2_continuation
ia64_break_slot2_continuation:
    { .mii
        /* RFI resumes at slot 0, so the PSR read is the first instruction. */
        mov r15 = psr
        (p10) adds r8 = 1, r8
        nop.i 0 ;;
    }
    { .mii
        nop.m 0
        (p11) adds r8 = 2, r8
        nop.i 0
    }

    /* Save the sampled PSR first, then attest this is the second return path. */
    movl r14 = ia64_break_exception_record_slot2 ;;
    adds r13 = REC_POST_RFI_VALID, r14 ;;
    ld8 r9 = [r13] ;;
    cmp.eq p6, p7 = r9, r0 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    adds r13 = REC_POST_RFI_PSR, r14 ;;
    st8 [r13] = r15 ;
    movl r13 = ia64_break_post_rfi_sequence ;;
    ld8 r9 = [r13] ;;
    cmp.eq p6, p7 = r9, r10 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    st8 [r13] = r11 ;
    adds r13 = REC_POST_RFI_CONTINUATION, r14 ;;
    movl r9 = ia64_break_slot2_continuation ;;
    st8 [r13] = r9 ;
    adds r13 = REC_POST_RFI_RETURN_SLOT, r14 ;;
    st8 [r13] = r0 ;
    adds r13 = REC_POST_RFI_ORDER, r14 ;;
    st8 [r13] = r11 ;
    mf ;;
    adds r13 = REC_POST_RFI_VALID, r14 ;;
    movl r9 = BREAK_POST_RFI_VALID_SLOT2 ;;
    st8 [r13] = r9 ;;
    mf ;;

    movl r15 = ia64_break_exception_record_slot2 ;;
    adds r15 = REC_CONTINUATION_COUNT, r15 ;;
    st8 [r15] = r8 ;;
    mov r15 = 1 ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = r1 ;;
    mov r15 = sp ;;
    adds r15 = 152, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = b6 ;;
    mov r15 = sp ;;
    adds r15 = 160, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = ar.bsp ;;
    mov r15 = sp ;;
    adds r15 = 168, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = ar.rsc ;;
    mov r15 = sp ;;
    adds r15 = 176, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = sp ;;
    mov r15 = sp ;;
    adds r15 = 184, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    movl r15 = ia64_break_exception_record_slot2 ;;
    adds r15 = REC_RETURN, r15 ;;
    movl r8 = BREAK_RETURN_SLOT2 ;;
    st8 [r15] = r8 ;;
    mov r8 = r16 ;;
    xor r8 = r8, r17 ;;
    xor r8 = r8, r18 ;;
    xor r8 = r8, r19 ;;
    xor r8 = r8, r20 ;;
    xor r8 = r8, r21 ;;
    xor r8 = r8, r22 ;;
    xor r8 = r8, r23 ;;
    xor r8 = r8, r24 ;;
    xor r8 = r8, r25 ;;
    xor r8 = r8, r26 ;;
    xor r8 = r8, r27 ;;
    xor r8 = r8, r28 ;;
    xor r8 = r8, r29 ;;
    xor r8 = r8, r30 ;;
    xor r8 = r8, r31 ;;
    mov r15 = sp ;;
    adds r15 = 144, r15 ;;
    ld8 r15 = [r15] ;;
    cmp.eq p6, p7 = r8, r15 ;;
(p7) br.cond.sptk ia64_break_exception_test_fail ;;
    mov r8 = r0 ;;
    br.cond.sptk ia64_break_exception_test_cleanup ;;

ia64_break_exception_test_fail:
    mov r8 = 1 ;;
ia64_break_exception_test_cleanup:
.global ia64_break_exception_test_cleanup
    mov r9 = r8 ;;
    rsm psr.ic ;;
    bsw.0 ;;
    ssm psr.ic ;;
    srlz.i ;;
    mov r15 = sp ;;
    ld8.fill r16 = [r15], 8 ;;
    ld8.fill r17 = [r15], 8 ;;
    ld8.fill r18 = [r15], 8 ;;
    ld8.fill r19 = [r15], 8 ;;
    ld8.fill r20 = [r15], 8 ;;
    ld8.fill r21 = [r15], 8 ;;
    ld8.fill r22 = [r15], 8 ;;
    ld8.fill r23 = [r15], 8 ;;
    ld8.fill r24 = [r15], 8 ;;
    ld8.fill r25 = [r15], 8 ;;
    ld8.fill r26 = [r15], 8 ;;
    ld8.fill r27 = [r15], 8 ;;
    ld8.fill r28 = [r15], 8 ;;
    ld8.fill r29 = [r15], 8 ;;
    ld8.fill r30 = [r15], 8 ;;
    ld8.fill r31 = [r15], 8 ;;
    mov r15 = sp ;;
    adds r15 = 192, r15 ;;
    ld8.fill r13 = [r15] ;;
    mov r15 = sp ;;
    adds r15 = 128, r15 ;;
    ld8 r15 = [r15] ;;
    mov ar.unat = r15 ;;
    mov r15 = sp ;;
    adds r15 = 136, r15 ;;
    ld8 r15 = [r15] ;;
    mov pr = r15, -1 ;;
    mov r8 = r9 ;;
    adds sp = 208, sp ;;
    br.ret.sptk.many b0 ;;
.endp ia64_break_exception_test
.size ia64_break_exception_test, .-ia64_break_exception_test
