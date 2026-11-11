/* One-site General Exception return for the Ski early-exec profile. */

.equ GENERAL_EXCEPTION_VECTOR,       0x5400
.equ GENERAL_EXCEPTION_SUBTYPE_ILLEGAL_OPERATION, 0
.equ GENERAL_EXCEPTION_RSC_EXPECTED, 3
.equ GENERAL_EXCEPTION_VALID,       0xbeefc0de47455631
.equ GENERAL_EXCEPTION_HANDLER_OK,  0xbeefc0de47454831
.equ GENERAL_EXCEPTION_COMPLETION,  0xbeefc0de47454331
.equ GENERAL_EXCEPTION_RETURN,      0xbeefc0de47455231
.equ GENERAL_EXCEPTION_POST_RFI_VALID, 0xbeefc0de47505331

.equ REC_VALID,              0
.equ REC_VERSION,             8
.equ REC_SIZE,               16
.equ REC_VECTOR,              24
.equ REC_SUBTYPE,             32
.equ REC_IIP,                 40
.equ REC_SLOT,                48
.equ REC_IPSR,                56
.equ REC_IFS,                 64
.equ REC_IFS_VALID,           72
.equ REC_ISR,                 80
.equ REC_HANDLER_PSR,         88
.equ REC_HANDLER_RSC,         96
.equ REC_HANDLER_BSP,        104
.equ REC_SAVED_PR,           112
.equ REC_FAULT_SITE,         120
.equ REC_RETURN_TARGET,      128
.equ REC_HANDLER_STATUS,     136
.equ REC_COMPLETION,         144
.equ REC_COUNT,              152
.equ REC_RETURN,             160
.equ REC_POST_PSR,           168
.equ REC_POST_RSC,           176
.equ REC_POST_BSP,           184
.equ REC_POST_IVA,           192
.equ REC_PRE_BSP,            200
.equ REC_PRE_RSC,            208
.equ REC_PRE_UNAT,           216
.equ REC_POST_UNAT,          224
.equ REC_PRE_GR_CHECKSUM,    232
.equ REC_POST_GR_CHECKSUM,   240
.equ REC_POST_VALID,         248

.macro STORE_REC value, offset
    adds r8 = \offset, r9 ;;
    st8 [r8] = \value ;;
.endm

.macro LOAD_REC destination, offset
    adds r8 = \offset, r9 ;;
    ld8 \destination = [r8] ;;
.endm

.section .bss.general_exception, "aw", @nobits
.align 16
.global ia64_general_exception_record
.type ia64_general_exception_record, @object
ia64_general_exception_record:
    .skip 256
.size ia64_general_exception_record, 256

.section .text.general_exception, "ax"
.align 16
.global ia64_general_exception_entry
.type ia64_general_exception_entry, @function
.proc ia64_general_exception_entry
ia64_general_exception_entry:
    /* R14 is the authorized fault destination and must remain untouched. */
    mov r8 = pr ;;
    movl r9 = ia64_general_exception_record ;;
    ld8 r10 = [r9] ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;;

    /* Capture hardware state before changing any interruption register. */
    mov r10 = r8 ;;
    STORE_REC r10, REC_SAVED_PR
    mov r10 = 1 ;;
    STORE_REC r10, REC_VERSION
    mov r10 = 256 ;;
    STORE_REC r10, REC_SIZE
    mov r10 = psr ;;
    STORE_REC r10, REC_HANDLER_PSR
    mov r11 = ar.rsc ;;
    STORE_REC r11, REC_HANDLER_RSC
    mov r12 = ar.bsp ;;
    STORE_REC r12, REC_HANDLER_BSP
    mov r13 = cr.iip ;;
    STORE_REC r13, REC_IIP
    mov r15 = cr.ipsr ;;
    STORE_REC r15, REC_IPSR
    mov r10 = cr.ifs ;;
    STORE_REC r10, REC_IFS
    extr.u r10 = r10, 63, 1 ;;
    STORE_REC r10, REC_IFS_VALID
    mov r10 = cr.isr ;;
    STORE_REC r10, REC_ISR
    movl r10 = GENERAL_EXCEPTION_VECTOR ;;
    STORE_REC r10, REC_VECTOR
    mov r10 = GENERAL_EXCEPTION_SUBTYPE_ILLEGAL_OPERATION ;;
    STORE_REC r10, REC_SUBTYPE
    movl r10 = ia64_general_exception_fault_site ;;
    STORE_REC r10, REC_FAULT_SITE
    movl r10 = ia64_general_exception_continuation ;;
    STORE_REC r10, REC_RETURN_TARGET

    /* Strict entry contract: only Ski's intended mode-3 BSPSTORE read. */
    extr.u r10 = r11, 0, 2 ;;
    mov r8 = GENERAL_EXCEPTION_RSC_EXPECTED ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk general_exception_fail ;;

    movl r10 = ia64_general_exception_fault_site ;;
    cmp.eq p6, p7 = r13, r10 ;;
(p7) br.cond.sptk general_exception_fail ;;
    extr.u r10 = r15, 41, 2 ;;
    STORE_REC r10, REC_SLOT
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;;
    extr.u r10 = r15, 13, 1 ;;
    mov r8 = 1 ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk general_exception_fail ;;
    extr.u r10 = r15, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;
    extr.u r10 = r15, 32, 2 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;
    extr.u r10 = r15, 46, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;;

    mov r10 = cr.ifs ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;;
    mov r10 = cr.isr ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;

    mov r10 = psr ;;
    extr.u r10 = r10, 13, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;;
    mov r10 = psr ;;
    extr.u r10 = r10, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;;

    /* BSP is legal in every RSC mode. It must remain in this RSE store. */
    movl r10 = rse_backing_store_base ;;
    cmp.geu p6, p7 = r12, r10 ;;
(p7) br.cond.sptk general_exception_fail ;;
    movl r10 = rse_backing_store_top ;;
    cmp.ltu p6, p7 = r12, r10 ;;
(p7) br.cond.sptk general_exception_fail ;;
    LOAD_REC r10, REC_PRE_RSC
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk general_exception_fail ;;
    LOAD_REC r10, REC_PRE_BSP
    cmp.eq p6, p7 = r10, r12 ;;
(p7) br.cond.sptk general_exception_fail ;

    /* The faulting MOV must not update R14; the handler does not use it. */
    movl r10 = 0x47454e4552414c44 ;;
    cmp.eq p6, p7 = r14, r10 ;;
(p7) br.cond.sptk general_exception_fail ;;
    LOAD_REC r10, REC_SAVED_PR
    movl r8 = 0x401 ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk general_exception_fail ;;

    /* Entry PSR.i and PSR.ic are both required clear before rfi. */
    mov r10 = psr ;;
    extr.u r10 = r10, 13, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;;
    mov r10 = psr ;;
    extr.u r10 = r10, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_fail ;

    movl r10 = GENERAL_EXCEPTION_HANDLER_OK ;;
    STORE_REC r10, REC_HANDLER_STATUS
    movl r10 = ia64_general_exception_continuation ;;
    mov cr.iip = r10 ;;
    mov cr.ipsr = r15 ;;
    srlz.i ;;
    LOAD_REC r8, REC_SAVED_PR
    mov pr = r8, -1 ;;
    mf ;;
    movl r10 = GENERAL_EXCEPTION_VALID ;;
    STORE_REC r10, REC_VALID
    mf ;;
    rfi ;;
    ;;
.endp ia64_general_exception_entry

general_exception_fail:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
general_exception_fail_spin:
    hint @pause ;;
    br.sptk general_exception_fail_spin ;;
.size ia64_general_exception_entry, .-ia64_general_exception_entry

.align 16
.global ia64_general_exception_test
.type ia64_general_exception_test, @function
.proc ia64_general_exception_test
ia64_general_exception_test:
    mov r10 = pr ;;
    movl r11 = ia64_general_exception_original_pr ;;
    st8 [r11] = r10 ;;
    /* Require the early-exec RSE configuration and collected interruptions. */
    ssm psr.ic ;;
    srlz.d ;;
    mov r8 = psr ;;
    extr.u r10 = r8, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    mov r9 = ar.rsc ;;
    mov r10 = 3 ;;
    cmp.eq p6, p7 = r9, r10 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    mov r10 = cr.iva ;;
    movl r11 = ia64_ivt ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk general_exception_test_fail ;

    movl r9 = ia64_general_exception_record ;;
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_PRE_RSC
    mov r10 = ar.bsp ;
    STORE_REC r10, REC_PRE_BSP
    mov r10 = ar.unat ;
    STORE_REC r10, REC_PRE_UNAT

    /* Check the live GP plus the sentinel destination, both non-NaT values. */
    movl r14 = 0x47454e4552414c44 ;;
    mov r10 = r1 ;;
    xor r10 = r10, r14 ;;
    STORE_REC r10, REC_PRE_GR_CHECKSUM

    /* Preserve the caller predicate state, then make a known test value. */
    movl r10 = 0x401 ;;
    mov pr = r10, -1 ;;

.align 16
.global ia64_general_exception_fault_site
.type ia64_general_exception_fault_site, @function
ia64_general_exception_fault_site:
    { .mii
        mov r14 = ar.bspstore
        nop.i 0
        nop.i 0 ;;
    }

.align 16
.global ia64_general_exception_continuation
.type ia64_general_exception_continuation, @function
ia64_general_exception_continuation:
    { .mii
        /* First instruction after rfi: capture PSR before any cleanup. */
        mov r15 = psr
        nop.i 0
        nop.i 0 ;;
    }

    movl r9 = ia64_general_exception_record ;;
    STORE_REC r15, REC_POST_PSR
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_POST_RSC
    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_POST_BSP
    mov r10 = cr.iva ;;
    STORE_REC r10, REC_POST_IVA
    mov r10 = ar.unat ;;
    STORE_REC r10, REC_POST_UNAT

    /* Sample the restored predicate image before this code uses p6/p7. */
    mov r10 = pr ;;
    movl r11 = 0x401 ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk general_exception_test_fail ;;

    mov r10 = 0x2000 ;;
    and r11 = r15, r10 ;;
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    extr.u r11 = r15, 14, 1 ;;
    cmp.eq p6, p7 = r11, r0 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    mov r10 = 3 ;;
    LOAD_REC r11, REC_POST_RSC
    mov r10 = 3 ;;
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    LOAD_REC r11, REC_POST_BSP
    LOAD_REC r12, REC_PRE_BSP
    cmp.eq p6, p7 = r11, r12 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    movl r10 = ia64_ivt ;;
    LOAD_REC r11, REC_POST_IVA
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk general_exception_test_fail ;
    LOAD_REC r11, REC_PRE_UNAT
    LOAD_REC r12, REC_POST_UNAT
    cmp.eq p6, p7 = r11, r12 ;;
(p7) br.cond.sptk general_exception_test_fail ;

    /* Test that the fault destination and predicate image survived rfi. */
    movl r10 = 0x47454e4552414c44 ;;
    cmp.eq p6, p7 = r14, r10 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    /* Recheck GP and the destination; other handler scratch GRs are dead. */
    mov r10 = r1 ;;
    xor r10 = r10, r14 ;;
    STORE_REC r10, REC_POST_GR_CHECKSUM
    LOAD_REC r11, REC_PRE_GR_CHECKSUM
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk general_exception_test_fail ;;

    LOAD_REC r10, REC_COUNT
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk general_exception_test_fail ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_COUNT
    movl r10 = GENERAL_EXCEPTION_RETURN ;;
    STORE_REC r10, REC_RETURN
    movl r10 = GENERAL_EXCEPTION_COMPLETION ;;
    STORE_REC r10, REC_COMPLETION
    mf ;;
    movl r10 = GENERAL_EXCEPTION_POST_RFI_VALID ;;
    STORE_REC r10, REC_POST_VALID
    mf ;;

    /* Prove post-rfi RSE use after restoring the caller predicate image. */
    movl r11 = ia64_general_exception_original_pr ;;
    ld8 r12 = [r11] ;;
    mov pr = r12, -1 ;;
    alloc r14 = ar.pfs, 1, 1, 0, 0 ;;
    movl r32 = 0x47454e4552534531 ;;
    mov r8 = r32 ;;
    movl r10 = 0x47454e4552534531 ;;
    cmp.eq p6, p7 = r8, r10 ;;
(p6) mov r8 = r0
(p7) mov r8 = 1 ;;
    movl r11 = ia64_general_exception_original_pr ;;
    ld8 r12 = [r11] ;;
    mov pr = r12, -1 ;;
    mov ar.pfs = r14 ;;
    br.ret.sptk.many b0 ;;

general_exception_test_fail:
    mov r8 = 1 ;;
    br.cond.sptk general_exception_test_restore_pr ;;
general_exception_test_restore_pr:
    mov r10 = r8 ;;
    movl r11 = ia64_general_exception_original_pr ;;
    ld8 r12 = [r11] ;;
    mov pr = r12, -1 ;;
    mov r8 = r10 ;;
    br.ret.sptk.many b0 ;;
.endp ia64_general_exception_test
.size ia64_general_exception_test, .-ia64_general_exception_test

.section .data.general_exception, "aw"
.align 8
.global ia64_general_exception_original_pr
ia64_general_exception_original_pr:
    .8byte 0
