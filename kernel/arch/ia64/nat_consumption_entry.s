/* One-site Register NaT Consumption return for the Ski early-exec profile. */

.equ NAT_CONSUMPTION_VECTOR,       0x5600
.equ NAT_CONSUMPTION_SUBTYPE,      43
.equ NAT_CONSUMPTION_RECORD_SIZE,  512
.equ NAT_CONSUMPTION_VALID,        0xbeefc0de4e415631
.equ NAT_CONSUMPTION_HANDLER_OK,   0xbeefc0de4e414831
.equ NAT_CONSUMPTION_COMPLETION,  0xbeefc0de4e414331
.equ NAT_CONSUMPTION_RETURN,      0xbeefc0de4e415231
.equ NAT_CONSUMPTION_POST_VALID,  0xbeefc0de4e415031
.equ NAT_CONSUMPTION_RSE_EXPECTED, 3

.equ REC_VALID,                    0
.equ REC_VERSION,                  8
.equ REC_SIZE,                    16
.equ REC_SEQUENCE,                24
.equ REC_VECTOR,                  32
.equ REC_SUBTYPE,                 40
.equ REC_FAULT_SITE,              48
.equ REC_IIP,                     56
.equ REC_SLOT,                    64
.equ REC_IPSR,                    72
.equ REC_IIPA,                    80
.equ REC_IFS,                     88
.equ REC_IFS_VALID,               96
.equ REC_IFA,                    104
.equ REC_IFA_VALID,              112
.equ REC_ISR,                    120
.equ REC_ISR_ED,                 128
.equ REC_ISR_EI,                 136
.equ REC_ISR_SO,                 144
.equ REC_ISR_NI,                 152
.equ REC_ISR_IR,                 160
.equ REC_ISR_RS,                 168
.equ REC_ISR_SP,                 176
.equ REC_ISR_NA,                 184
.equ REC_ISR_READ,               192
.equ REC_ISR_WRITE,              200
.equ REC_ISR_EXECUTE,            208
.equ REC_IIB0,                   216
.equ REC_IIB1,                   224
.equ REC_HANDLER_PSR,            232
.equ REC_HANDLER_RSC,            240
.equ REC_HANDLER_BSP,            248
.equ REC_SAVED_PR,               256
.equ REC_SOURCE_GR,              264
.equ REC_DESTINATION_GR,         272
.equ REC_SPEC_SITE,              280
.equ REC_PRE_PSR,                288
.equ REC_SPEC_PSR,               296
.equ REC_PRE_RSC,                304
.equ REC_PRE_BSP,                312
.equ REC_PRE_UNAT,               320
.equ REC_NAT_BEFORE,             328
.equ REC_NAT_HANDLER,            336
.equ REC_NAT_AFTER_RFI,          344
.equ REC_NAT_AFTER_CLEAR,        352
.equ REC_RETURN_TARGET,          360
.equ REC_RETURN_IPSR,            368
.equ REC_HANDLER_STATUS,         376
.equ REC_COMPLETION,             384
.equ REC_COUNT,                  392
.equ REC_RETURN,                 400
.equ REC_POST_PSR,               408
.equ REC_POST_RSC,               416
.equ REC_POST_BSP,               424
.equ REC_POST_UNAT,              432
.equ REC_PRE_GR_CHECKSUM,        440
.equ REC_POST_GR_CHECKSUM,       448
.equ REC_TEST_RESULT,            456
.equ REC_POST_VALID,             464
.equ REC_IIB0_AVAILABLE,         472
.equ REC_IIB1_AVAILABLE,         480
.equ REC_SOURCE_INITIAL,         488
.equ REC_POST_IVA,               496
.equ REC_POST_SPEC_PSR,          504

.macro STORE_REC value, offset
    adds r9 = \offset, r9 ;;
    st8 [r9] = \value ;;
    adds r9 = -\offset, r9 ;;
.endm

.macro LOAD_REC destination, offset
    adds r9 = \offset, r9 ;;
    ld8 \destination = [r9] ;;
    adds r9 = -\offset, r9 ;;
.endm

.section .bss.nat_consumption, "aw", @nobits
.align 16
.global ia64_nat_consumption_record
.type ia64_nat_consumption_record, @object
ia64_nat_consumption_record:
    .skip NAT_CONSUMPTION_RECORD_SIZE
.size ia64_nat_consumption_record, NAT_CONSUMPTION_RECORD_SIZE

.section .data.nat_consumption, "aw"
.align 8
.global ia64_nat_consumption_pointer
ia64_nat_consumption_pointer:
    .8byte 0
.global ia64_nat_consumption_target
ia64_nat_consumption_target:
    .8byte 0x4e41545f54415247
.global ia64_nat_consumption_original_pr
ia64_nat_consumption_original_pr:
    .8byte 0
.global ia64_nat_consumption_interrupted_r12
ia64_nat_consumption_interrupted_r12:
    .8byte 0
.global ia64_nat_consumption_interrupted_r13
ia64_nat_consumption_interrupted_r13:
    .8byte 0

.section .text.nat_consumption, "ax"
.align 16
.global ia64_nat_consumption_entry
.type ia64_nat_consumption_entry, @function
.proc ia64_nat_consumption_entry
ia64_nat_consumption_entry:
    /* GR8 is the interrupted NaTed source: leave it untouched until rfi. */
    movl r9 = ia64_nat_consumption_record ;;
    mov r10 = pr ;;
    STORE_REC r10, REC_SAVED_PR
    ld8 r10 = [r9] ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_SEQUENCE
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_SEQUENCE
    mf ;;

    mov r10 = 1 ;;
    STORE_REC r10, REC_VERSION
    mov r10 = NAT_CONSUMPTION_RECORD_SIZE ;;
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
    mov r10 = cr.iipa ;;
    STORE_REC r10, REC_IIPA
    mov r10 = cr.ifs ;;
    STORE_REC r10, REC_IFS
    extr.u r10 = r10, 63, 1 ;;
    STORE_REC r10, REC_IFS_VALID
    mov r10 = cr.ifa ;;
    STORE_REC r10, REC_IFA
    mov r10 = 0 ;;
    STORE_REC r10, REC_IFA_VALID
    mov r11 = cr.isr ;;
    STORE_REC r11, REC_ISR
    /* Ski 1.5.1 has no CR.IIB0/IIB1 model; zero record initialization says unavailable. */
    movl r10 = NAT_CONSUMPTION_VECTOR ;;
    STORE_REC r10, REC_VECTOR
    mov r10 = NAT_CONSUMPTION_SUBTYPE ;;
    STORE_REC r10, REC_SUBTYPE
    movl r10 = ia64_nat_consumption_fault_site ;;
    STORE_REC r10, REC_FAULT_SITE
    movl r10 = ia64_nat_consumption_spec_load_site ;;
    STORE_REC r10, REC_SPEC_SITE
    movl r10 = ia64_nat_consumption_continuation ;;
    STORE_REC r10, REC_RETURN_TARGET
    mov r10 = 8 ;;
    STORE_REC r10, REC_SOURCE_GR
    mov r10 = 9 ;;
    STORE_REC r10, REC_DESTINATION_GR

    /* Register NaT subtype ISR: a slot-0 ordinary read, with no NaTPage access. */
    extr.u r10 = r11, 43, 1 ;;
    STORE_REC r10, REC_ISR_ED
    extr.u r10 = r11, 41, 2 ;;
    STORE_REC r10, REC_ISR_EI
    extr.u r10 = r11, 40, 1 ;;
    STORE_REC r10, REC_ISR_SO
    extr.u r10 = r11, 39, 1 ;;
    STORE_REC r10, REC_ISR_NI
    extr.u r10 = r11, 38, 1 ;;
    STORE_REC r10, REC_ISR_IR
    extr.u r10 = r11, 37, 1 ;;
    STORE_REC r10, REC_ISR_RS
    extr.u r10 = r11, 36, 1 ;;
    STORE_REC r10, REC_ISR_SP
    extr.u r10 = r11, 35, 1 ;;
    STORE_REC r10, REC_ISR_NA
    extr.u r10 = r11, 34, 1 ;;
    STORE_REC r10, REC_ISR_READ
    extr.u r10 = r11, 33, 1 ;;
    STORE_REC r10, REC_ISR_WRITE
    extr.u r10 = r11, 32, 1 ;;
    STORE_REC r10, REC_ISR_EXECUTE

    movl r10 = ia64_nat_consumption_fault_site ;;
    cmp.eq p6, p7 = r13, r10 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r15, 41, 2 ;;
    STORE_REC r10, REC_SLOT
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r11, 41, 2 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r15, 41, 2 ;;
    extr.u r12 = r11, 41, 2 ;;
    cmp.eq p6, p7 = r10, r12 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r15, 13, 1 ;;
    mov r13 = 1 ;;
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r15, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r15, 3, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r15, 32, 2 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r15, 46, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;

    /* The saved frame is invalid except for its architecturally cleared v bit. */
    LOAD_REC r10, REC_IFS_VALID
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    /* IFA may or may not be written for this subtype: never use it to classify. */
    LOAD_REC r10, REC_IFA_VALID
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;

    /* Ski sets NAT_REG_CODE=0x10 in addition to architectural ISR.r. */
    movl r10 = 0x400000010 ;;
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk nat_consumption_fail ;;

    /* Verify GR8's NaT bit with TNAT, which tests without consuming it. */
    tnat.nz p6, p7 = r8 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_NAT_HANDLER

    /* Handler resource/state contract: no call, alloc, RNAT, or BSPSTORE access. */
    mov r10 = psr ;;
    extr.u r10 = r10, 13, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    mov r10 = psr ;;
    extr.u r10 = r10, 14, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r11, 34, 1 ;;
    mov r13 = 1 ;;
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    extr.u r10 = r11, 33, 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    mov r10 = r11 ;;
    and r10 = 0x3, r10 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    mov r10 = ar.rsc ;;
    mov r13 = NAT_CONSUMPTION_RSE_EXPECTED ;;
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_PRE_RSC
    LOAD_REC r11, REC_HANDLER_RSC
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_PRE_BSP
    LOAD_REC r11, REC_HANDLER_BSP
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_PRE_PSR
    movl r13 = 0x2000 ;;
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_SPEC_PSR
    extr.u r10 = r10, 43, 1 ;;
    mov r13 = 1 ;;
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_NAT_BEFORE
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_PRE_UNAT
    mov r13 = ar.unat ;;
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;
    LOAD_REC r10, REC_SAVED_PR
    movl r13 = 0x401 ;;
    cmp.eq p6, p7 = r10, r13 ;;
(p7) br.cond.sptk nat_consumption_fail ;;

    movl r10 = NAT_CONSUMPTION_HANDLER_OK ;;
    STORE_REC r10, REC_HANDLER_STATUS
    movl r10 = NAT_CONSUMPTION_COMPLETION ;;
    STORE_REC r10, REC_COMPLETION
    movl r10 = ia64_nat_consumption_continuation ;;
    STORE_REC r10, REC_RETURN_TARGET
    mov cr.iip = r10 ;;
    mov r11 = r15 ;;
    movl r10 = 0x80000000000 ;;
    andcm r11 = r11, r10 ;;
    STORE_REC r11, REC_RETURN_IPSR
    mov cr.ipsr = r11 ;;
    srlz.i ;;
    LOAD_REC r15, REC_SAVED_PR
    mov pr = r15, -1 ;;
    movl r10 = NAT_CONSUMPTION_VALID ;;
    STORE_REC r10, REC_VALID
    mf ;;
    rfi ;;
    ;;
.endp ia64_nat_consumption_entry

nat_consumption_fail:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
nat_consumption_fail_spin:
    hint @pause ;;
    br.sptk nat_consumption_fail_spin ;;
.size ia64_nat_consumption_entry, .-ia64_nat_consumption_entry

.align 16
.global ia64_nat_consumption_test
.type ia64_nat_consumption_test, @function
.proc ia64_nat_consumption_test
ia64_nat_consumption_test:
    movl r11 = ia64_nat_consumption_interrupted_r12 ;;
    st8 [r11] = r12 ;;
    movl r11 = ia64_nat_consumption_interrupted_r13 ;;
    st8 [r11] = r13 ;;
    mov r10 = pr ;;
    movl r11 = ia64_nat_consumption_original_pr ;;
    st8 [r11] = r10 ;;
    movl r12 = 0x401 ;;
    mov pr = r12, -1 ;;
    rsm psr.i ;;
    srlz.d ;;
    ssm psr.ic ;;
    srlz.d ;;
    mov r10 = ar.rsc ;;
    mov r8 = NAT_CONSUMPTION_RSE_EXPECTED ;;
    cmp.eq p6, p7 = r10, r8 ;;
(p7) br.cond.sptk nat_consumption_test_fail_rsc ;;
    mov r10 = cr.iva ;;
    movl r11 = ia64_ivt ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk nat_consumption_test_fail_iva ;;
    mov r10 = psr ;;
    extr.u r11 = r10, 13, 1 ;;
    mov r12 = 1 ;;
    cmp.eq p6, p7 = r11, r12 ;;
(p7) br.cond.sptk nat_consumption_test_fail_ic ;;
    extr.u r11 = r10, 14, 1 ;;
    cmp.eq p6, p7 = r11, r0 ;;
(p7) br.cond.sptk nat_consumption_test_fail_i ;;

    movl r9 = ia64_nat_consumption_record ;;
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_PRE_RSC
    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_PRE_BSP
    mov r10 = ar.unat ;;
    STORE_REC r10, REC_PRE_UNAT
    mov r10 = psr ;;
    STORE_REC r10, REC_PRE_PSR
    movl r10 = 8 ;;
    STORE_REC r10, REC_SOURCE_GR
    movl r10 = 9 ;;
    STORE_REC r10, REC_DESTINATION_GR
    movl r10 = ia64_nat_consumption_spec_load_site ;;
    STORE_REC r10, REC_SPEC_SITE
    movl r10 = ia64_nat_consumption_target ;;
    mov r8 = r10 ;;
    STORE_REC r10, REC_SOURCE_INITIAL
    mov r10 = r1 ;;
    xor r10 = r10, r8 ;;
    STORE_REC r10, REC_PRE_GR_CHECKSUM
    movl r11 = ia64_nat_consumption_pointer ;;
    movl r10 = ia64_nat_consumption_target ;;
    st8 [r11] = r10 ;;
    tnat.nz p6, p7 = r8 ;;
(p6) br.cond.sptk nat_consumption_test_fail ;;
    mov r10 = 0 ;;
    STORE_REC r10, REC_NAT_BEFORE

    /* Set IPSR.ed for an rfi whose first restarted instruction is ld8.s. */
    mov r10 = psr ;;
    movl r11 = 0x60000000000 ;;
    andcm r10 = r10, r11 ;;
    movl r11 = 0x80000000000 ;;
    or r10 = r10, r11 ;;
    STORE_REC r10, REC_SPEC_PSR
    movl r15 = ia64_nat_consumption_pointer ;;
    movl r11 = ia64_nat_consumption_spec_load_site ;;
    /* Ski and Itanium forbid interruption-control register writes with IC set. */
    rsm psr.ic ;;
    srlz.d ;;
    mov cr.iip = r11 ;;
    mov cr.ipsr = r10 ;;
    srlz.i ;;
    rfi ;;
    ;;
.global ia64_nat_consumption_spec_load_site
ia64_nat_consumption_spec_load_site:
    { .mii
        ld8.s r8 = [r15]
        nop.i 0
        nop.i 0 ;;
    }
    mov r10 = psr ;;
    STORE_REC r10, REC_POST_SPEC_PSR
    tnat.nz p6, p7 = r8 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_NAT_BEFORE
    /* TNAT updated p6/p7; give the exception frame a stable predicate image. */
    movl r10 = 0x401 ;;
    mov pr = r10, -1 ;;

    /* No operation consumes GR8 before this exact non-speculative ld8. */
    movl r10 = 0x4e4154434f4e5355 ;;
    mov r9 = r10 ;;
    .align 16
.global ia64_nat_consumption_fault_site
.type ia64_nat_consumption_fault_site, @function
ia64_nat_consumption_fault_site:
    { .mii
        ld8 r9 = [r8]
        nop.i 0
        nop.i 0 ;;
    }

.global ia64_nat_consumption_continuation
.type ia64_nat_consumption_continuation, @function
ia64_nat_consumption_continuation:
    { .mii
        /* First instruction after rfi: sample PSR before clearing the NaTed GR. */
        mov r15 = psr
        nop.i 0
        nop.i 0 ;;
    }
    movl r9 = ia64_nat_consumption_record ;;
    STORE_REC r15, REC_POST_PSR
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_POST_RSC
    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_POST_BSP
    mov r10 = ar.unat ;;
    STORE_REC r10, REC_POST_UNAT
    mov r10 = cr.iva ;;
    STORE_REC r10, REC_POST_IVA
    tnat.nz p6, p7 = r8 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_NAT_AFTER_RFI
    mov r8 = r0 ;;
    tnat.nz p6, p7 = r8 ;;
(p6) br.cond.sptk nat_consumption_test_fail ;;
    mov r10 = 0 ;;
    STORE_REC r10, REC_NAT_AFTER_CLEAR

    mov r10 = 0x2000 ;;
    cmp.eq p6, p7 = r15, r10 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    mov r10 = 3 ;;
    LOAD_REC r11, REC_POST_RSC
    cmp.eq p6, p7 = r11, r10 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    LOAD_REC r11, REC_PRE_BSP
    LOAD_REC r12, REC_POST_BSP
    cmp.eq p6, p7 = r11, r12 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    LOAD_REC r11, REC_PRE_UNAT
    LOAD_REC r12, REC_POST_UNAT
    cmp.eq p6, p7 = r11, r12 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    mov r10 = r1 ;;
    movl r11 = ia64_nat_consumption_target ;;
    xor r10 = r10, r11 ;;
    STORE_REC r10, REC_POST_GR_CHECKSUM
    LOAD_REC r11, REC_PRE_GR_CHECKSUM
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    LOAD_REC r10, REC_COUNT
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk nat_consumption_test_fail ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_COUNT
    movl r10 = NAT_CONSUMPTION_RETURN ;;
    STORE_REC r10, REC_RETURN
    mov r10 = 0 ;;
    STORE_REC r10, REC_TEST_RESULT
    movl r10 = NAT_CONSUMPTION_POST_VALID ;;
    STORE_REC r10, REC_POST_VALID
    mf ;;
    mov r8 = 0 ;;
    br.cond.sptk nat_consumption_test_restore_pr ;;

nat_consumption_test_fail:
    mov r10 = 1 ;;
    br.cond.sptk nat_consumption_test_fail_record ;;

nat_consumption_test_fail_rsc:
    mov r10 = 0x11 ;;
    br.cond.sptk nat_consumption_test_fail_record ;;
nat_consumption_test_fail_iva:
    mov r10 = 0x12 ;;
    br.cond.sptk nat_consumption_test_fail_record ;;
nat_consumption_test_fail_ic:
    mov r10 = 0x13 ;;
    br.cond.sptk nat_consumption_test_fail_record ;;
nat_consumption_test_fail_i:
    mov r10 = 0x14 ;;
nat_consumption_test_fail_record:
    mov r8 = r0 ;;
    movl r9 = ia64_nat_consumption_record ;;
    STORE_REC r10, REC_TEST_RESULT
    mov r8 = 1 ;;
    br.cond.sptk nat_consumption_test_restore_pr ;;

nat_consumption_test_restore_pr:
    movl r11 = ia64_nat_consumption_original_pr ;;
    ld8 r12 = [r11] ;;
    mov pr = r12, -1 ;;
    movl r11 = ia64_nat_consumption_interrupted_r13 ;;
    ld8 r13 = [r11] ;;
    movl r11 = ia64_nat_consumption_interrupted_r12 ;;
    ld8 r12 = [r11] ;;
    br.ret.sptk.many b0 ;;
.endp ia64_nat_consumption_test
.size ia64_nat_consumption_test, .-ia64_nat_consumption_test
