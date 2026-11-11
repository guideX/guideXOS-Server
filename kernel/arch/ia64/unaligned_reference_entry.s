/* One-site Unaligned Data Reference return for the Ski early-exec profile. */

.equ UNALIGNED_REFERENCE_VECTOR,       0x5a00
.equ UNALIGNED_REFERENCE_SUBTYPE,      0
.equ UNALIGNED_REFERENCE_RECORD_SIZE,  384
.equ UNALIGNED_REFERENCE_VALID,        0xbeefc0de55415631
.equ UNALIGNED_REFERENCE_HANDLER_OK,   0xbeefc0de55414831
.equ UNALIGNED_REFERENCE_COMPLETION,   0xbeefc0de55434331
.equ UNALIGNED_REFERENCE_RETURN,      0xbeefc0de55455231
.equ UNALIGNED_REFERENCE_CLEANUP,     0xbeefc0de55434c31
.equ UNALIGNED_REFERENCE_POST_VALID,  0xbeefc0de55505631

.equ REC_VALID,                  0
.equ REC_VERSION,                8
.equ REC_SIZE,                  16
.equ REC_SEQUENCE,              24
.equ REC_VECTOR,                32
.equ REC_SUBTYPE,               40
.equ REC_FAULT_SITE,            48
.equ REC_IIP,                   56
.equ REC_SLOT,                  64
.equ REC_IPSR,                  72
.equ REC_IFS,                   80
.equ REC_IFS_VALID,             88
.equ REC_IFA,                   96
.equ REC_ISR,                  104
.equ REC_ISR_ED,               112
.equ REC_ISR_EI,               120
.equ REC_ISR_NI,               128
.equ REC_ISR_SP,               136
.equ REC_ISR_READ,             144
.equ REC_ISR_WRITE,            152
.equ REC_HANDLER_PSR,           160
.equ REC_HANDLER_RSC,           168
.equ REC_HANDLER_BSP,           176
.equ REC_SAVED_PR,              184
.equ REC_PRE_PSR,               192
.equ REC_PRE_RSC,               200
.equ REC_PRE_BSP,               208
.equ REC_PRE_UNAT,              216
.equ REC_TEST_ADDRESS,          224
.equ REC_BUFFER_BASE,           232
.equ REC_BUFFER_SIZE,           240
.equ REC_ACCESS_WIDTH,          248
.equ REC_RETURN_TARGET,         256
.equ REC_RETURN_IPSR,           264
.equ REC_HANDLER_STATUS,        272
.equ REC_COMPLETION,            280
.equ REC_COUNT,                 288
.equ REC_RETURN,                296
.equ REC_POST_PSR,              304
.equ REC_POST_RSC,              312
.equ REC_POST_BSP,              320
.equ REC_POST_IVA,              328
.equ REC_POST_UNAT,             336
.equ REC_PRE_GR_CHECKSUM,       344
.equ REC_POST_GR_CHECKSUM,      352
.equ REC_CLEANUP,               360
.equ REC_POST_VALID,            368
.equ REC_RESERVED,              376

.macro STORE_REC value, offset
    adds r8 = \offset, r9 ;;
    st8 [r8] = \value ;;
.endm

.macro LOAD_REC destination, offset
    adds r8 = \offset, r9 ;;
    ld8 \destination = [r8] ;;
.endm

.section .bss.unaligned_reference, "aw", @nobits
.align 16
.global ia64_unaligned_reference_record
.type ia64_unaligned_reference_record, @object
ia64_unaligned_reference_record:
    .skip UNALIGNED_REFERENCE_RECORD_SIZE
.size ia64_unaligned_reference_record, UNALIGNED_REFERENCE_RECORD_SIZE

.section .data.unaligned_reference, "aw"
.align 16
.global ia64_unaligned_reference_buffer
.type ia64_unaligned_reference_buffer, @object
ia64_unaligned_reference_buffer:
    .8byte 0x0123456789abcdef
    .8byte 0xfedcba9876543210
.size ia64_unaligned_reference_buffer, 16

.global ia64_unaligned_reference_original_pr
ia64_unaligned_reference_original_pr:
    .8byte 0

.section .text.unaligned_reference, "ax"
.align 16
.global ia64_unaligned_reference_entry
.type ia64_unaligned_reference_entry, @function
.proc ia64_unaligned_reference_entry
ia64_unaligned_reference_entry:
    /* A sequence claim rejects stale, duplicate, or nested entries. */
    mov r8 = pr ;;
    movl r9 = ia64_unaligned_reference_record ;;
    ld8 r10 = [r9] ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    mov r10 = r8 ;;
    STORE_REC r10, REC_SAVED_PR
    LOAD_REC r10, REC_SEQUENCE
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_SEQUENCE
    mf ;;

    /* Capture the hardware interruption resources before modifying return state. */
    mov r10 = 1 ;;
    STORE_REC r10, REC_VERSION
    mov r10 = UNALIGNED_REFERENCE_RECORD_SIZE ;;
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
    mov r10 = cr.ifa ;;
    STORE_REC r10, REC_IFA
    mov r11 = cr.isr ;;
    STORE_REC r11, REC_ISR
    movl r10 = UNALIGNED_REFERENCE_VECTOR ;;
    STORE_REC r10, REC_VECTOR
    mov r10 = UNALIGNED_REFERENCE_SUBTYPE ;;
    STORE_REC r10, REC_SUBTYPE
    movl r10 = ia64_unaligned_reference_fault_site ;;
    STORE_REC r10, REC_FAULT_SITE
    movl r10 = ia64_unaligned_reference_continuation ;;
    STORE_REC r10, REC_RETURN_TARGET

    /* Decode only the architected ISR fields relevant to this ordinary ld8. */
    extr.u r10 = r11, 43, 1 ;;
    STORE_REC r10, REC_ISR_ED
    extr.u r10 = r11, 41, 2 ;;
    STORE_REC r10, REC_ISR_EI
    extr.u r10 = r11, 39, 1 ;;
    STORE_REC r10, REC_ISR_NI
    extr.u r10 = r11, 36, 1 ;;
    STORE_REC r10, REC_ISR_SP
    extr.u r10 = r11, 34, 1 ;;
    STORE_REC r10, REC_ISR_READ
    extr.u r10 = r11, 33, 1 ;;
    STORE_REC r10, REC_ISR_WRITE

    /* The authorized site is slot zero of one ordinary, non-speculative ld8. */
    movl r10 = ia64_unaligned_reference_fault_site ;;
    cmp.eq p6, p7 = r13, r10 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 41, 2 ;;
    STORE_REC r10, REC_SLOT
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 41, 2 ;;
    extr.u r12 = r11, 41, 2 ;;
    cmp.eq p6, p7 = r10, r12 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 13, 1 ;;
    mov r8 = 1 ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 3, 1 ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 32, 2 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 44, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    extr.u r10 = r15, 46, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;

    /* IFS.v is architecturally cleared; other saved IFS bits are undefined. */
    LOAD_REC r10, REC_IFS_VALID
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;

    /* Ski must report a read (not write/speculative/execute) for the ld8. */
    movl r10 = 0x400000000 ;;
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_ISR_ED
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_ISR_NI
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_ISR_SP
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_ISR_READ
    mov r8 = 1 ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_ISR_WRITE
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;

    /* Validate IFA against the single 8-byte window inside initialized RAM. */
    movl r10 = ia64_unaligned_reference_buffer ;;
    adds r11 = 1, r10 ;;
    cmp.eq p6, p7 = r14, r11 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r12, REC_IFA
    cmp.eq p6, p7 = r12, r14 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    and r10 = 7, r14 ;;
    cmp.ne p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    movl r10 = ia64_unaligned_reference_buffer ;;
    cmp.geu p6, p7 = r12, r10 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    adds r11 = 8, r12 ;;
    adds r10 = 16, r10 ;;
    cmp.ltu p6, p7 = r11, r10 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    STORE_REC r14, REC_TEST_ADDRESS
    movl r10 = ia64_unaligned_reference_buffer ;;
    STORE_REC r10, REC_BUFFER_BASE
    mov r10 = 16 ;;
    STORE_REC r10, REC_BUFFER_SIZE
    mov r10 = 8 ;;
    STORE_REC r10, REC_ACCESS_WIDTH

    /* The handler has no stack frame or C++ call and leaves r14/b0 untouched. */
    mov r10 = psr ;;
    extr.u r10 = r10, 13, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    mov r10 = psr ;;
    extr.u r10 = r10, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    mov r10 = psr ;;
    extr.u r10 = r10, 3, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r11, REC_HANDLER_RSC
    mov r10 = 3 ;;
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r12, REC_HANDLER_BSP
    movl r10 = rse_backing_store_base ;;
    cmp.geu p6, p7 = r12, r10 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    movl r10 = rse_backing_store_top ;;
    cmp.ltu p6, p7 = r12, r10 ;;
(p7) br.cond.sptk unaligned_reference_fail ;
    LOAD_REC r10, REC_PRE_RSC
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_PRE_BSP
    cmp.eq p6, p7 = r10, r12 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_PRE_PSR
    extr.u r10 = r10, 3, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;
    LOAD_REC r10, REC_SAVED_PR
    movl r8 = 0x401 ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk unaligned_reference_fail ;;

    /* Save a direct, pre-return state image and authorize one exact target. */
    movl r10 = UNALIGNED_REFERENCE_HANDLER_OK ;;
    STORE_REC r10, REC_HANDLER_STATUS
    movl r10 = UNALIGNED_REFERENCE_COMPLETION ;;
    STORE_REC r10, REC_COMPLETION
    movl r12 = ia64_unaligned_reference_continuation ;;
    mov cr.iip = r12 ;;
    mov r11 = r15 ;;
    movl r10 = 8 ;;
    andcm r11 = r11, r10 ;;
    STORE_REC r11, REC_RETURN_IPSR
    mov cr.ipsr = r11 ;;
    STORE_REC r12, REC_RETURN_TARGET
    srlz.i ;;
    LOAD_REC r8, REC_SAVED_PR
    mov pr = r8, -1 ;;
    mov r10 = 0 ;;
    STORE_REC r10, REC_RESERVED
    mf ;;
    movl r10 = UNALIGNED_REFERENCE_VALID ;;
    STORE_REC r10, REC_VALID
    mf ;;
    rfi ;;
    ;;
.endp ia64_unaligned_reference_entry

unaligned_reference_fail:
    rsm psr.i | psr.ic | psr.ac ;;
    srlz.d ;;
unaligned_reference_fail_spin:
    hint @pause ;;
    br.sptk unaligned_reference_fail_spin ;;
.size ia64_unaligned_reference_entry, .-ia64_unaligned_reference_entry

.align 16
.global ia64_unaligned_reference_test
.type ia64_unaligned_reference_test, @function
.proc ia64_unaligned_reference_test
ia64_unaligned_reference_test:
    mov r10 = pr ;;
    movl r11 = ia64_unaligned_reference_original_pr ;;
    st8 [r11] = r10 ;;

    /* Establish the already-qualified early-exec interruption state. */
    ssm psr.ic ;;
    srlz.d ;;
    mov r8 = psr ;;
    extr.u r10 = r8, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    mov r9 = ar.rsc ;;
    mov r10 = 3 ;;
    cmp.eq p6, p7 = r9, r10 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    mov r10 = cr.iva ;;
    movl r11 = ia64_ivt ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    extr.u r10 = r8, 3, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;

    movl r9 = ia64_unaligned_reference_record ;;
    mov r10 = psr ;;
    STORE_REC r10, REC_PRE_PSR
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_PRE_RSC
    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_PRE_BSP
    mov r10 = ar.unat ;;
    STORE_REC r10, REC_PRE_UNAT

    /* Preserve the test address in R14; the fault destination R15 is dead. */
    movl r14 = ia64_unaligned_reference_buffer ;;
    adds r14 = 1, r14 ;;
    mov r10 = r1 ;;
    xor r10 = r10, r14 ;;
    STORE_REC r10, REC_PRE_GR_CHECKSUM
    movl r10 = 0x401 ;;
    mov pr = r10, -1 ;;

    /* PSR.ac is enabled for exactly one serialized ordinary ld8 stimulus. */
    ssm psr.ac ;;
    srlz.d ;;
.align 16
.global ia64_unaligned_reference_fault_site
.type ia64_unaligned_reference_fault_site, @function
ia64_unaligned_reference_fault_site:
    { .mii
        ld8 r15 = [r14]
        nop.i 0
        nop.i 0 ;;
    }

.align 16
.global ia64_unaligned_reference_continuation
.type ia64_unaligned_reference_continuation, @function
ia64_unaligned_reference_continuation:
    { .mii
        /* First instruction after rfi: capture PSR before cleanup or checks. */
        mov r15 = psr
        nop.i 0
        nop.i 0 ;;
    }

    /* Idempotently clear AC and serialize before any following data access. */
    rsm psr.ac ;;
    srlz.d ;;

    movl r9 = ia64_unaligned_reference_record ;;
    STORE_REC r15, REC_POST_PSR
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_POST_RSC
    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_POST_BSP
    mov r10 = cr.iva ;;
    STORE_REC r10, REC_POST_IVA
    mov r10 = ar.unat ;;
    STORE_REC r10, REC_POST_UNAT
    movl r10 = UNALIGNED_REFERENCE_CLEANUP ;;
    STORE_REC r10, REC_CLEANUP

    /* The saved load destination is never read; R15 now holds only sampled PSR. */
    mov r10 = pr ;;
    movl r11 = 0x401 ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    movl r10 = 0x2000 ;;
    cmp.eq p6, p7 = r15, r10 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    extr.u r10 = r15, 3, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    mov r10 = ar.rsc ;;
    mov r11 = 3 ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    LOAD_REC r11, REC_POST_BSP
    LOAD_REC r12, REC_PRE_BSP
    cmp.eq p6, p7 = r11, r12 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    movl r10 = rse_backing_store_base ;;
    cmp.geu p6, p7 = r11, r10 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    movl r10 = rse_backing_store_top ;;
    cmp.ltu p6, p7 = r11, r10 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;
    movl r10 = ia64_ivt ;;
    LOAD_REC r11, REC_POST_IVA
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;
    LOAD_REC r11, REC_PRE_UNAT
    LOAD_REC r12, REC_POST_UNAT
    cmp.eq p6, p7 = r11, r12 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;

    movl r10 = ia64_unaligned_reference_buffer ;;
    adds r10 = 1, r10 ;;
    cmp.eq p6, p7 = r14, r10 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;;
    mov r10 = r1 ;;
    xor r10 = r10, r14 ;;
    STORE_REC r10, REC_POST_GR_CHECKSUM
    LOAD_REC r11, REC_PRE_GR_CHECKSUM
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;

    LOAD_REC r10, REC_SEQUENCE
    mov r11 = 1 ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;
    LOAD_REC r10, REC_VALID
    movl r11 = UNALIGNED_REFERENCE_VALID ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;
    LOAD_REC r10, REC_COUNT
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk unaligned_reference_test_fail ;
    mov r10 = 1 ;;
    STORE_REC r10, REC_COUNT
    movl r10 = UNALIGNED_REFERENCE_RETURN ;;
    STORE_REC r10, REC_RETURN
    mf ;;
    movl r10 = UNALIGNED_REFERENCE_POST_VALID ;;
    STORE_REC r10, REC_POST_VALID
    mf ;;

    /* Exercise one post-return RSE allocation without consuming load R15. */
    movl r11 = ia64_unaligned_reference_original_pr ;;
    ld8 r12 = [r11] ;;
    mov pr = r12, -1 ;;
    alloc r14 = ar.pfs, 1, 1, 0, 0 ;;
    movl r32 = 0x554e415245534531 ;;
    mov r8 = r32 ;;
    movl r10 = 0x554e415245534531 ;;
    cmp.eq p6, p7 = r8, r10 ;;
(p6) mov r8 = r0
(p7) mov r8 = 1 ;;
    movl r11 = ia64_unaligned_reference_original_pr ;;
    ld8 r12 = [r11] ;;
    mov pr = r12, -1 ;;
    mov ar.pfs = r14 ;;
    br.ret.sptk.many b0 ;;

unaligned_reference_test_fail:
    mov r8 = 1 ;;
    movl r11 = ia64_unaligned_reference_original_pr ;;
    ld8 r12 = [r11] ;;
    mov pr = r12, -1 ;;
    br.ret.sptk.many b0 ;;
.endp ia64_unaligned_reference_test
.size ia64_unaligned_reference_test, .-ia64_unaligned_reference_test
