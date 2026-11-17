/* Context setup and the bounded A -> B -> A -> B -> A sequence. */

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
.equ C_FIRST_ENTRIES, 576
.equ C_RESUME_ENTRIES, 584
.equ C_PROFILE_MAGIC, 600
.equ C_STACK_CANARY_LOW, 608
.equ C_STACK_CANARY_HIGH, 616
.equ C_RBS_CANARY_LOW, 624
.equ C_RBS_CANARY_HIGH, 632
.equ C_RNAT_VALID_MASK, 640
.equ C_RNAT_DEFINED_BITS, 648
.equ C_CODE_BASE, 688
.equ C_CODE_LIMIT, 696

.equ R_EVENT_COUNT, 0
.equ R_ERRORS, 200
.equ R_NAT_A_BEFORE, 208
.equ R_NAT_A_AFTER, 216
.equ R_NAT_B_BEFORE, 224
.equ R_NAT_B_AFTER, 232
.equ R_PSR_A, 240
.equ R_PSR_B, 248
.equ R_FP_A, 256
.equ R_FP_B, 272
.equ R_PFS_A_BEFORE, 288
.equ R_PFS_A_AFTER, 296
.equ R_PFS_B_BEFORE, 304
.equ R_PFS_B_AFTER, 312
.equ R_FP_B_FIRST, 320
.equ R_A_FINAL_COUNT, 336
.equ R_FP_A_FIRST_RESUME, 352
.equ R_FP_A_FINAL_RESUME, 672
.equ R_FP_B_FIRST_ENTRY, 992
.equ R_FP_B_RESUME, 1312
.equ B_TEST_BSP_FRAME_BYTES, 96

.equ CTX_PROFILE_MAGIC, 0x3541434f4f505631
.equ CTX_A_ID, 0xA
.equ CTX_B_ID, 0xB
.equ EVENT_A_TO_B, 1
.equ EVENT_B_FIRST_ENTRY, 2
.equ EVENT_B_TO_A_FIRST, 3
.equ EVENT_A_RESUMED_FIRST, 4
.equ EVENT_A_TO_B_SECOND, 5
.equ EVENT_B_RESUMED, 6
.equ EVENT_B_TO_A_SECOND, 7
.equ EVENT_A_RESUMED_FINAL, 8
.equ FP_A_LOW, 0x415f46505f303031
.equ FP_A_HIGH, 0x3fff800000000000
.equ FP_B_LOW, 0x425f46505f303032
.equ FP_B_HIGH, 0x3fff800000000000
.equ GR8_B_FIRST, 0x4758383842423032
.equ FPSR_TEST, 0x0009804c0270033f
.equ PR_TEST, 0x0000000000010003
.equ GR4_A, 0x4758343441413031
.equ GR5_A, 0x4758353541413031
.equ GR6_A, 0x4758363641413031
.equ GR7_A, 0x4758373741413031
.equ GR4_B, 0x4758343442423032
.equ GR5_B, 0x4758353542423032
.equ GR6_B, 0x4758363642423032
.equ GR7_B, 0x4758373742423032
.equ BR1_A, 0x4252314141413031
.equ BR2_A, 0x4252324141413031
.equ BR3_A, 0x4252334141413031
.equ BR4_A, 0x4252344141413031
.equ BR5_A, 0x4252354141413031
.equ BR1_B, 0x4252314242423032
.equ BR2_B, 0x4252324242423032
.equ BR3_B, 0x4252334242423032
.equ BR4_B, 0x4252344242423032
.equ BR5_B, 0x4252354242423032
.equ BR1_A_INITIAL, 0x4252314141413031
.equ BR2_A_INITIAL, 0x4252324141413031
.equ BR3_A_INITIAL, 0x4252334141413031
.equ BR4_A_INITIAL, 0x4252344141413031
.equ BR5_A_INITIAL, 0x4252354141413031
.equ BR1_B_INITIAL, 0x4252314242423032
.equ BR2_B_INITIAL, 0x4252324242423032
.equ BR3_B_INITIAL, 0x4252334242423032
.equ BR4_B_INITIAL, 0x4252344242423032
.equ BR5_B_INITIAL, 0x4252354242423032
.equ LC_A, 0x31
.equ LC_B, 0x42
.equ EC_A, 0x51
.equ EC_B, 0x62
.equ CCV_A, 0xA5A5
.equ CCV_B, 0x5A5A
.equ STACK_CANARY_LOW, 0x535441434b4c4f57
.equ STACK_CANARY_HIGH, 0x535441434b484947
.equ STACK_USE_A_LOW, 0x415f535441434b30
.equ STACK_USE_A_HIGH, 0x415f535441434b31
.equ STACK_USE_B_LOW, 0x425f535441434b30
.equ STACK_USE_B_HIGH, 0x425f535441434b31
.equ RBS_CANARY_LOW, 0x5242534c4f574341
.equ RBS_CANARY_HIGH, 0x5242534849474843
.equ SENTINEL_A, 0x47584f5341413031
.equ SENTINEL_B, 0x47584f5342423032
.equ NAT_DATA, 0x4e41545354415445

.macro ASSERT_EQ reg, expected
    movl r15 = \expected ;;
    cmp.eq p6, p7 = \reg, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
.endm

.macro CHECK_PRESERVED_REGS gr4, gr5, gr6, gr7, br1, br2, br3, br4, br5
    ASSERT_EQ r4, \gr4
    ASSERT_EQ r5, \gr5
    ASSERT_EQ r6, \gr6
    ASSERT_EQ r7, \gr7
    mov r14 = b1 ;;
    ASSERT_EQ r14, \br1
    mov r14 = b2 ;;
    ASSERT_EQ r14, \br2
    mov r14 = b3 ;;
    ASSERT_EQ r14, \br3
    mov r14 = b4 ;;
    ASSERT_EQ r14, \br4
    mov r14 = b5 ;;
    ASSERT_EQ r14, \br5
.endm

.macro CHECK_APP_STATE lc_value, ec_value, ccv_value
    mov r14 = pr ;;
    movl r15 = 0x10002 ;;
    and r14 = r14, r15 ;;
    cmp.eq p6, p7 = r14, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    mov r14 = ar.lc ;;
    ASSERT_EQ r14, \lc_value
    mov r14 = ar.ec ;;
    ASSERT_EQ r14, \ec_value
    mov r14 = ar.ccv ;;
    ASSERT_EQ r14, \ccv_value
    mov r14 = ar.fpsr ;;
    ASSERT_EQ r14, FPSR_TEST
    mov r14 = ar.rsc ;;
    movl r15 = 3 ;;
    and r14 = r14, r15 ;;
    cmp.eq p6, p7 = r14, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
.endm

.macro CHECK_CONTEXT_GP context
    movl r14 = \context ;;
    adds r28 = C_GP, r14 ;;
    ld8 r15 = [r28] ;;
    cmp.eq p6, p7 = r1, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
.endm

.macro STORE_FP_AT offset, freg
    adds r28 = \offset, r14 ;;
    stf.spill [r28] = \freg ;;
.endm

.macro CAPTURE_FP_SET offset
    movl r14 = ia64_coop_probe_record + \offset ;;
    STORE_FP_AT 0, f2
    STORE_FP_AT 16, f3
    STORE_FP_AT 32, f4
    STORE_FP_AT 48, f5
    STORE_FP_AT 64, f16
    STORE_FP_AT 80, f17
    STORE_FP_AT 96, f18
    STORE_FP_AT 112, f19
    STORE_FP_AT 128, f20
    STORE_FP_AT 144, f21
    STORE_FP_AT 160, f22
    STORE_FP_AT 176, f23
    STORE_FP_AT 192, f24
    STORE_FP_AT 208, f25
    STORE_FP_AT 224, f26
    STORE_FP_AT 240, f27
    STORE_FP_AT 256, f28
    STORE_FP_AT 272, f29
    STORE_FP_AT 288, f30
    STORE_FP_AT 304, f31
.endm

.macro SET64 base, offset, value
    adds r28 = \offset, \base ;;
    st8 [r28] = \value ;;
.endm

.macro RECORD_EVENT event_code, context_id
    movl r14 = ia64_coop_probe_record ;;
    ld8 r15 = [r14] ;;
    shl r16 = r15, 3 ;;
    shl r17 = r15, 4 ;;
    add r16 = r16, r17 ;;
    adds r18 = 8, r14 ;;
    add r18 = r18, r16 ;;
    adds r19 = 1, r15 ;;
    st8 [r18] = r19 ;;
    adds r18 = 8, r18 ;;
    mov r19 = \context_id ;;
    st8 [r18] = r19 ;;
    adds r18 = 8, r18 ;;
    mov r19 = \event_code ;;
    st8 [r18] = r19 ;;
    adds r19 = 1, r15 ;;
    mf ;;
    st8 [r14] = r19 ;;
.endm

.section .data.ia64_coop_nat_seed, "aw"
.align 8
ia64_coop_nat_pointer:
    .8byte ia64_coop_nat_target
ia64_coop_nat_target:
    .8byte NAT_DATA

.section .data.ia64_coop_fp_seed, "aw"
.align 16
ia64_coop_fp_seed_a:
    .8byte FP_A_LOW, FP_A_HIGH
    .8byte FP_A_LOW + 0x100, FP_A_HIGH
    .8byte FP_A_LOW + 0x200, FP_A_HIGH
    .8byte FP_A_LOW + 0x300, FP_A_HIGH
    .8byte FP_A_LOW + 0x400, FP_A_HIGH
    .8byte FP_A_LOW + 0x500, FP_A_HIGH
    .8byte FP_A_LOW + 0x600, FP_A_HIGH
    .8byte FP_A_LOW + 0x700, FP_A_HIGH
    .8byte FP_A_LOW + 0x800, FP_A_HIGH
    .8byte FP_A_LOW + 0x900, FP_A_HIGH
    .8byte FP_A_LOW + 0xa00, FP_A_HIGH
    .8byte FP_A_LOW + 0xb00, FP_A_HIGH
    .8byte FP_A_LOW + 0xc00, FP_A_HIGH
    .8byte FP_A_LOW + 0xd00, FP_A_HIGH
    .8byte FP_A_LOW + 0xe00, FP_A_HIGH
    .8byte FP_A_LOW + 0xf00, FP_A_HIGH
    .8byte FP_A_LOW + 0x1000, FP_A_HIGH
    .8byte FP_A_LOW + 0x1100, FP_A_HIGH
    .8byte FP_A_LOW + 0x1200, FP_A_HIGH
    .8byte FP_A_LOW + 0x1300, FP_A_HIGH
ia64_coop_fp_seed_b:
    .8byte FP_B_LOW, FP_B_HIGH
    .8byte FP_B_LOW + 0x100, FP_B_HIGH
    .8byte FP_B_LOW + 0x200, FP_B_HIGH
    .8byte FP_B_LOW + 0x300, FP_B_HIGH
    .8byte FP_B_LOW + 0x400, FP_B_HIGH
    .8byte FP_B_LOW + 0x500, FP_B_HIGH
    .8byte FP_B_LOW + 0x600, FP_B_HIGH
    .8byte FP_B_LOW + 0x700, FP_B_HIGH
    .8byte FP_B_LOW + 0x800, FP_B_HIGH
    .8byte FP_B_LOW + 0x900, FP_B_HIGH
    .8byte FP_B_LOW + 0xa00, FP_B_HIGH
    .8byte FP_B_LOW + 0xb00, FP_B_HIGH
    .8byte FP_B_LOW + 0xc00, FP_B_HIGH
    .8byte FP_B_LOW + 0xd00, FP_B_HIGH
    .8byte FP_B_LOW + 0xe00, FP_B_HIGH
    .8byte FP_B_LOW + 0xf00, FP_B_HIGH
    .8byte FP_B_LOW + 0x1000, FP_B_HIGH
    .8byte FP_B_LOW + 0x1100, FP_B_HIGH
    .8byte FP_B_LOW + 0x1200, FP_B_HIGH
    .8byte FP_B_LOW + 0x1300, FP_B_HIGH

.section .bss.ia64_coop_helper, "aw", @nobits
.align 8
ia64_coop_nat_saved_r12: .skip 8
ia64_coop_nat_saved_r13: .skip 8
ia64_coop_nat_saved_pr:  .skip 8
ia64_coop_nat_saved_b0:  .skip 8

.section .text.ia64_coop_sequence, "ax"
.align 16
.global ia64_coop_probe_setup
.type ia64_coop_probe_setup, @function
.proc ia64_coop_probe_setup
ia64_coop_probe_setup:
    /* This non-leaf routine keeps its incoming PFS and return link in its
       stacked frame; the nested helper owns b0 while it is executing. */
    alloc r16 = ar.pfs, 0, 2, 0, 0 ;;
    mov r32 = r16 ;;
    mov r33 = b0 ;;
    /* NaT is established before any C++ frame, then saved in B's own record. */
.global ia64_coop_setup_nested_call
ia64_coop_setup_nested_call:
    br.call.sptk.many b0 = ia64_coop_set_nat8 ;;
.global ia64_coop_setup_nested_return
ia64_coop_setup_nested_return:

    movl r14 = ia64_coop_context_a ;;
    movl r15 = ia64_coop_stack_a_bottom ;;
    SET64 r14, C_STACK_BASE, r15
    movl r15 = ia64_coop_stack_a_top ;;
    SET64 r14, C_STACK_LIMIT, r15
    movl r15 = ia64_coop_rbs_a_base ;;
    SET64 r14, C_RBS_BASE, r15
    movl r15 = ia64_coop_rbs_a_limit ;;
    SET64 r14, C_RBS_LIMIT, r15
    movl r15 = ia64_coop_stack_a_top ;;
    SET64 r14, C_SP, r15
    movl r15 = ia64_coop_rbs_a_base ;;
    SET64 r14, C_BSP, r15
    SET64 r14, C_BSPSTORE, r15
    movl r15 = CTX_PROFILE_MAGIC ;;
    SET64 r14, C_PROFILE_MAGIC, r15
    movl r15 = CTX_A_ID ;;
    SET64 r14, C_CONTEXT_ID, r15
    movl r15 = STACK_CANARY_LOW ;;
    SET64 r14, C_STACK_CANARY_LOW, r15
    movl r15 = STACK_CANARY_HIGH ;;
    SET64 r14, C_STACK_CANARY_HIGH, r15
    movl r15 = RBS_CANARY_LOW ;;
    SET64 r14, C_RBS_CANARY_LOW, r15
    movl r15 = RBS_CANARY_HIGH ;;
    SET64 r14, C_RBS_CANARY_HIGH, r15
    movl r15 = __text_start ;;
    SET64 r14, C_CODE_BASE, r15
    movl r15 = __text_end ;;
    SET64 r14, C_CODE_LIMIT, r15

    /* A owns the deliberately NaTed first-entry GR8 image. */
    mov r20 = ar.unat ;;
    mov ar.unat = r0 ;;
    adds r28 = C_GR8, r14 ;;
    st8.spill [r28] = r8 ;;
    mov r21 = ar.unat ;;
    SET64 r14, C_SPILL_UNAT, r21
    SET64 r14, C_CALLER_UNAT, r21
    mov ar.unat = r20 ;;

    movl r14 = ia64_coop_context_b ;;
    movl r15 = ia64_coop_stack_b_bottom ;;
    SET64 r14, C_STACK_BASE, r15
    movl r15 = ia64_coop_stack_b_top ;;
    SET64 r14, C_STACK_LIMIT, r15
    SET64 r14, C_SP, r15
    movl r15 = ia64_coop_rbs_b_base ;;
    SET64 r14, C_RBS_BASE, r15
    SET64 r14, C_BSP, r15
    SET64 r14, C_BSPSTORE, r15
    movl r15 = ia64_coop_rbs_b_limit ;;
    SET64 r14, C_RBS_LIMIT, r15
    movl r15 = ia64_coop_b_first_entry ;;
    SET64 r14, C_RP, r15
    mov r15 = r0 ;;
    SET64 r14, C_PFS, r15
    mov r15 = 3 ;;
    SET64 r14, C_RSC, r15
    SET64 r14, C_RNAT, r0
    SET64 r14, C_CALLER_UNAT, r0
    movl r15 = LC_B ;;
    SET64 r14, C_LC, r15
    movl r15 = EC_B ;;
    SET64 r14, C_EC, r15
    movl r15 = CCV_B ;;
    SET64 r14, C_CCV, r15
    movl r15 = PR_TEST ;;
    SET64 r14, C_PR, r15
    movl r15 = BR1_B_INITIAL ;;
    SET64 r14, C_BR1, r15
    movl r15 = BR2_B_INITIAL ;;
    SET64 r14, C_BR2, r15
    movl r15 = BR3_B_INITIAL ;;
    SET64 r14, C_BR3, r15
    movl r15 = BR4_B_INITIAL ;;
    SET64 r14, C_BR4, r15
    movl r15 = BR5_B_INITIAL ;;
    SET64 r14, C_BR5, r15
    movl r15 = FPSR_TEST ;;
    SET64 r14, C_FPSR, r15
    movl r15 = 1 ;;
    SET64 r14, C_FIRST_ENTRY, r15
    movl r15 = CTX_B_ID ;;
    SET64 r14, C_CONTEXT_ID, r15
    movl r15 = CTX_PROFILE_MAGIC ;;
    SET64 r14, C_PROFILE_MAGIC, r15
    movl r15 = STACK_CANARY_LOW ;;
    SET64 r14, C_STACK_CANARY_LOW, r15
    movl r15 = STACK_CANARY_HIGH ;;
    SET64 r14, C_STACK_CANARY_HIGH, r15
    movl r15 = RBS_CANARY_LOW ;;
    SET64 r14, C_RBS_CANARY_LOW, r15
    movl r15 = RBS_CANARY_HIGH ;;
    SET64 r14, C_RBS_CANARY_HIGH, r15
    movl r15 = __text_start ;;
    SET64 r14, C_CODE_BASE, r15
    movl r15 = __text_end ;;
    SET64 r14, C_CODE_LIMIT, r15

    /* Distinct initial callee-saved state for B. */
    movl r15 = GR4_B ;;
    SET64 r14, 0, r15
    movl r15 = 0x4758353542423032 ;;
    SET64 r14, 8, r15
    movl r15 = 0x4758363642423032 ;;
    SET64 r14, 16, r15
    movl r15 = 0x4758373742423032 ;;
    SET64 r14, 24, r15
    /* Seed every ABI-preserved FP image for the first Context B entry. */
    movl r28 = ia64_coop_fp_seed_b ;;
    adds r29 = C_FP2, r14 ;;
    mov r30 = 20 ;;
1:
    ld8 r15 = [r28], 8 ;;
    st8 [r29] = r15, 8 ;;
    ld8 r15 = [r28], 8 ;;
    st8 [r29] = r15, 8 ;;
    adds r30 = -1, r30 ;;
    cmp.ne p6, p7 = r30, r0 ;;
(p6) br.cond.sptk 1b ;;

    /* B begins with a separate ordinary GR8 sentinel and clear NaT state. */
    mov r20 = ar.unat ;;
    mov ar.unat = r0 ;;
    movl r8 = GR8_B_FIRST ;;
    mov r28 = r14 ;;
    adds r28 = C_GR8, r28 ;;
    st8.spill [r28] = r8 ;;
    mov r21 = ar.unat ;;
    SET64 r14, C_SPILL_UNAT, r21
    mov ar.unat = r20 ;;

    /* Independent canaries sit immediately outside all four stack ranges. */
    movl r14 = ia64_coop_stack_a_guard_low ;;
    movl r15 = STACK_CANARY_LOW ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_stack_a_guard_high ;;
    movl r15 = STACK_CANARY_HIGH ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_stack_b_guard_low ;;
    movl r15 = STACK_CANARY_LOW ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_stack_b_guard_high ;;
    movl r15 = STACK_CANARY_HIGH ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_rbs_a_guard_low ;;
    movl r15 = RBS_CANARY_LOW ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_rbs_a_guard_high ;;
    movl r15 = RBS_CANARY_HIGH ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_rbs_b_guard_low ;;
    movl r15 = RBS_CANARY_LOW ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_rbs_b_guard_high ;;
    movl r15 = RBS_CANARY_HIGH ;;
    st8 [r14] = r15 ;;
    mf ;;
.global ia64_coop_setup_exit
ia64_coop_setup_exit:
    mov ar.pfs = r32 ;;
    mov b0 = r33 ;;
    br.ret.sptk.many b0 ;;
.endp ia64_coop_probe_setup
.size ia64_coop_probe_setup, .-ia64_coop_probe_setup

/* Set GR8.NaT using the already-qualified Ski PSR.ed/RFI technique. */
.align 16
.global ia64_coop_set_nat8
.type ia64_coop_set_nat8, @function
.proc ia64_coop_set_nat8
ia64_coop_set_nat8:
    mov r10 = r12 ;;
    movl r11 = ia64_coop_nat_saved_r12 ;;
    st8 [r11] = r10 ;;
    mov r10 = r13 ;;
    movl r11 = ia64_coop_nat_saved_r13 ;;
    st8 [r11] = r10 ;;
    mov r10 = pr ;;
    movl r11 = ia64_coop_nat_saved_pr ;;
    st8 [r11] = r10 ;;
    mov r10 = b0 ;;
    movl r11 = ia64_coop_nat_saved_b0 ;;
    st8 [r11] = r10 ;;

    movl r12 = 0x401 ;;
    mov pr = r12, -1 ;;
    ssm psr.ic ;;
    srlz.d ;;
    mov r10 = psr ;;
    movl r11 = 0x60000000000 ;;
    andcm r10 = r10, r11 ;;
    movl r11 = 0x80000000000 ;;
    or r10 = r10, r11 ;;
    movl r15 = ia64_coop_nat_pointer ;;
    movl r11 = ia64_coop_nat8_resume ;;
    rsm psr.ic ;;
    srlz.d ;;
    mov cr.iip = r11 ;;
    mov cr.ipsr = r10 ;;
    srlz.i ;;
    rfi ;;
    ;;

.align 16
ia64_coop_nat8_resume:
    { .mii
        ld8.s r8 = [r15]
        nop.i 0
        nop.i 0 ;;
    }
    tnat.nz p6, p7 = r8 ;;
    (p7) br.cond.sptk ia64_coop_nat8_missing ;;
    br.cond.sptk ia64_coop_nat8_restore ;;
ia64_coop_nat8_missing:
    movl r10 = ia64_coop_probe_record + 200 ;;
    ld8 r11 = [r10] ;;
    adds r11 = 1, r11 ;;
    st8 [r10] = r11 ;;
ia64_coop_nat8_restore:
    movl r11 = ia64_coop_nat_saved_pr ;;
    ld8 r10 = [r11] ;;
    mov pr = r10, -1 ;;
    movl r11 = ia64_coop_nat_saved_b0 ;;
    ld8 r10 = [r11] ;;
    mov b0 = r10 ;;
    movl r11 = ia64_coop_nat_saved_r13 ;;
    ld8 r13 = [r11] ;;
    movl r11 = ia64_coop_nat_saved_r12 ;;
    ld8 r12 = [r11] ;;
    br.ret.sptk.many b0 ;;
.endp ia64_coop_set_nat8
.size ia64_coop_set_nat8, .-ia64_coop_set_nat8

.align 16
.global ia64_coop_probe_run
.type ia64_coop_probe_run, @function
.proc ia64_coop_probe_run
ia64_coop_probe_run:
    alloc r16 = ar.pfs, 3, 3, 0, 0 ;;
    mov r32 = r16 ;;
    mov r33 = b0 ;;
    mov r34 = ar.unat ;;
    br.call.sptk.many b0 = ia64_coop_probe_setup ;;
.global ia64_coop_probe_after_setup
ia64_coop_probe_after_setup:
    /* Load B's independently stored NaTed GR8 for A's test frame. */
    mov r20 = ar.unat ;;
    movl r14 = ia64_coop_context_a ;;
    adds r28 = C_SPILL_UNAT, r14 ;;
    ld8 r21 = [r28] ;;
    mov ar.unat = r21 ;;
    adds r28 = C_GR8, r14 ;;
    ld8.fill r8 = [r28] ;;
    br.call.sptk.many b0 = ia64_coop_a_test ;;
.global ia64_coop_probe_after_a_test
ia64_coop_probe_after_a_test:
    mov ar.unat = r34 ;;
    mov ar.pfs = r32 ;;
    mov b0 = r33 ;;
    br.ret.sptk.many b0 ;;
.endp ia64_coop_probe_run
.size ia64_coop_probe_run, .-ia64_coop_probe_run

.align 16
.global ia64_coop_b_first_entry
.type ia64_coop_b_first_entry, @function
.proc ia64_coop_b_first_entry
ia64_coop_b_first_entry:
    movl r14 = ia64_coop_context_b ;;
    adds r28 = C_FIRST_ENTRIES, r14 ;;
    ld8 r15 = [r28] ;;
    adds r15 = 1, r15 ;;
    st8 [r28] = r15 ;;
.global ia64_coop_b_first_entry_call
ia64_coop_b_first_entry_call:
    br.call.sptk.many b0 = ia64_coop_b_test ;;
1:  hint @pause ;;
    br.sptk 1b ;;
.endp ia64_coop_b_first_entry
.size ia64_coop_b_first_entry, .-ia64_coop_b_first_entry

.align 16


.align 16
.global ia64_coop_a_test
.type ia64_coop_a_test, @function
.proc ia64_coop_a_test
ia64_coop_a_test:
    /* Reserve two outputs for ia64_coop_context_switch(in0, in1). */
    alloc r16 = ar.pfs, 6, 4, 2, 0 ;;
    mov r34 = b0 ;;
    mov r35 = r16 ;;
    movl r32 = SENTINEL_A ;;
    mov r33 = r8 ;;
    movl r4 = GR4_A ;;
    movl r5 = GR5_A ;;
    movl r6 = GR6_A ;;
    movl r7 = GR7_A ;;
    movl r14 = BR1_A_INITIAL ;;
    mov b1 = r14 ;;
    movl r14 = BR2_A_INITIAL ;;
    mov b2 = r14 ;;
    movl r14 = BR3_A_INITIAL ;;
    mov b3 = r14 ;;
    movl r14 = BR4_A_INITIAL ;;
    mov b4 = r14 ;;
    movl r14 = BR5_A_INITIAL ;;
    mov b5 = r14 ;;
    movl r14 = PR_TEST ;;
    mov pr = r14, -1 ;;
    movl r14 = ia64_coop_fp_seed_a ;;
    ldf.fill f2 = [r14], 16 ;;
    ldf.fill f3 = [r14], 16 ;;
    ldf.fill f4 = [r14], 16 ;;
    ldf.fill f5 = [r14], 16 ;;
    ldf.fill f16 = [r14], 16 ;;
    ldf.fill f17 = [r14], 16 ;;
    ldf.fill f18 = [r14], 16 ;;
    ldf.fill f19 = [r14], 16 ;;
    ldf.fill f20 = [r14], 16 ;;
    ldf.fill f21 = [r14], 16 ;;
    ldf.fill f22 = [r14], 16 ;;
    ldf.fill f23 = [r14], 16 ;;
    ldf.fill f24 = [r14], 16 ;;
    ldf.fill f25 = [r14], 16 ;;
    ldf.fill f26 = [r14], 16 ;;
    ldf.fill f27 = [r14], 16 ;;
    ldf.fill f28 = [r14], 16 ;;
    ldf.fill f29 = [r14], 16 ;;
    ldf.fill f30 = [r14], 16 ;;
    ldf.fill f31 = [r14] ;;
    movl r14 = LC_A ;;
    mov ar.lc = r14 ;;
    movl r14 = EC_A ;;
    mov ar.ec = r14 ;;
    movl r14 = CCV_A ;;
    mov ar.ccv = r14 ;;
    movl r14 = FPSR_TEST ;;
    mov.m ar.fpsr = r14 ;;
    tnat.nz p6, p7 = r33 ;;
    (p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_probe_record ;;
    movl r15 = 1 ;;
    adds r28 = R_NAT_A_BEFORE, r14 ;;
    st8 [r28] = r15 ;;
    mov r15 = ar.pfs ;;
    adds r28 = R_PFS_A_BEFORE, r14 ;;
    st8 [r28] = r15 ;;

    RECORD_EVENT EVENT_A_TO_B, CTX_A_ID
    movl r14 = ia64_coop_context_a ;;
    mov out0 = r14 ;;
    movl r14 = ia64_coop_context_b ;;
    mov out1 = r14 ;;
    br.call.sptk.many b0 = ia64_coop_context_switch ;;

.global ia64_coop_a_resumed_first
ia64_coop_a_resumed_first:
    RECORD_EVENT EVENT_A_RESUMED_FIRST, CTX_A_ID
    tnat.nz p6, p7 = r33 ;;
    (p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_probe_record ;;
    movl r15 = 1 ;;
    adds r28 = R_NAT_A_AFTER, r14 ;;
    st8 [r28] = r15 ;;
    mov r15 = psr ;;
    adds r28 = R_PSR_A, r14 ;;
    st8 [r28] = r15 ;;
    mov r15 = ar.pfs ;;
    adds r28 = R_PFS_A_AFTER, r14 ;;
    st8 [r28] = r15 ;;
    CHECK_CONTEXT_GP ia64_coop_context_a
    CAPTURE_FP_SET R_FP_A_FIRST_RESUME
    movl r14 = ia64_coop_probe_record + R_FP_A ;;
    stf.spill [r14] = f2 ;;
    CHECK_PRESERVED_REGS GR4_A, GR5_A, GR6_A, GR7_A, BR1_A, BR2_A, BR3_A, BR4_A, BR5_A
    CHECK_APP_STATE LC_A, EC_A, CCV_A
    CHECK_CONTEXT_GP ia64_coop_context_a
    mov r14 = r32 ;;
    ASSERT_EQ r14, SENTINEL_A
    ld8 r14 = [r12] ;;
    ASSERT_EQ r14, STACK_USE_A_LOW
    adds r28 = 8, r12 ;;
    ld8 r14 = [r28] ;;
    ASSERT_EQ r14, STACK_USE_A_HIGH
    mov r14 = ar.pfs ;;
    movl r15 = ia64_coop_probe_record ;;
    adds r28 = R_PFS_A_BEFORE, r15 ;;
    ld8 r15 = [r28] ;;
    cmp.eq p6, p7 = r14, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_context_a ;;
    adds r28 = C_RESUME_ENTRIES, r14 ;;
    ld8 r15 = [r28] ;;
    adds r15 = 1, r15 ;;
    st8 [r28] = r15 ;;

    RECORD_EVENT EVENT_A_TO_B_SECOND, CTX_A_ID
    movl r14 = ia64_coop_context_a ;;
    mov out0 = r14 ;;
    movl r14 = ia64_coop_context_b ;;
    mov out1 = r14 ;;
    br.call.sptk.many b0 = ia64_coop_context_switch ;;

.global ia64_coop_a_resumed_final
ia64_coop_a_resumed_final:
    RECORD_EVENT EVENT_A_RESUMED_FINAL, CTX_A_ID
    tnat.nz p6, p7 = r33 ;;
    (p7) br.cond.sptk ia64_coop_test_fail ;;
    CHECK_PRESERVED_REGS GR4_A, GR5_A, GR6_A, GR7_A, BR1_A, BR2_A, BR3_A, BR4_A, BR5_A
    CHECK_APP_STATE LC_A, EC_A, CCV_A
    mov r14 = r32 ;;
    ASSERT_EQ r14, SENTINEL_A
    ld8 r14 = [r12] ;;
    ASSERT_EQ r14, STACK_USE_A_LOW
    adds r28 = 8, r12 ;;
    ld8 r14 = [r28] ;;
    ASSERT_EQ r14, STACK_USE_A_HIGH
    movl r14 = ia64_coop_probe_record ;;
    mov r15 = ar.pfs ;;
    adds r28 = R_PFS_A_AFTER, r14 ;;
    st8 [r28] = r15 ;;
    adds r28 = R_PFS_A_BEFORE, r14 ;;
    ld8 r14 = [r28] ;;
    cmp.eq p6, p7 = r15, r14 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    CAPTURE_FP_SET R_FP_A_FINAL_RESUME
    movl r14 = ia64_coop_probe_record + R_FP_A ;;
    stf.spill [r14] = f2 ;;
    movl r14 = ia64_coop_context_a ;;
    adds r28 = C_RESUME_ENTRIES, r14 ;;
    ld8 r15 = [r28] ;;
    adds r15 = 1, r15 ;;
    st8 [r28] = r15 ;;
    mov r16 = 2 ;;
    cmp.eq p6, p7 = r15, r16 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_probe_record + R_A_FINAL_COUNT ;;
    mov r15 = 1 ;;
    st8 [r14] = r15 ;;
    movl r14 = ia64_coop_stack_a_guard_low ;;
    ld8 r16 = [r14] ;;
    movl r17 = STACK_CANARY_LOW ;;
    cmp.eq p6, p7 = r16, r17 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_stack_a_guard_high ;;
    ld8 r16 = [r14] ;;
    movl r17 = STACK_CANARY_HIGH ;;
    cmp.eq p6, p7 = r16, r17 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_rbs_a_guard_low ;;
    ld8 r16 = [r14] ;;
    movl r17 = RBS_CANARY_LOW ;;
    cmp.eq p6, p7 = r16, r17 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_rbs_a_guard_high ;;
    ld8 r16 = [r14] ;;
    movl r17 = RBS_CANARY_HIGH ;;
    cmp.eq p6, p7 = r16, r17 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    adds r12 = 4096, r12 ;;
    mov ar.pfs = r35 ;;
    mov b0 = r34 ;;
.global ia64_coop_a_final_exit
ia64_coop_a_final_exit:
    br.ret.sptk.many b0 ;;
.endp ia64_coop_a_test
.size ia64_coop_a_test, .-ia64_coop_a_test

.align 16
.global ia64_coop_b_test
.type ia64_coop_b_test, @function
.proc ia64_coop_b_test
ia64_coop_b_test:
    /* Reserve two outputs for ia64_coop_context_switch(in0, in1). */
    alloc r16 = ar.pfs, 6, 4, 2, 0 ;;
    mov r34 = b0 ;;
    mov r35 = r16 ;;
    adds r12 = -4096, r12 ;;
    movl r14 = STACK_USE_B_LOW ;;
    st8 [r12] = r14 ;;
    adds r28 = 8, r12 ;;
    movl r14 = STACK_USE_B_HIGH ;;
    st8 [r28] = r14 ;;
    movl r32 = SENTINEL_B ;;
    mov r33 = r8 ;;
    /* B must enter on its own memory stack and RSE backing store. */
    mov r14 = r12 ;;
    movl r15 = ia64_coop_stack_b_bottom ;;
    cmp.leu p6, p7 = r15, r14 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r15 = ia64_coop_stack_b_top ;;
    cmp.leu p6, p7 = r14, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    /* alloc leaves BSP unchanged; the 12-register frame occupies 96 bytes from this base. */
.global ia64_coop_b_bsp_check
ia64_coop_b_bsp_check:
    mov r14 = ar.bsp ;;
    movl r15 = ia64_coop_rbs_b_base ;;
    cmp.geu p6, p7 = r14, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r15 = ia64_coop_rbs_b_limit ;;
    cmp.ltu p6, p7 = r14, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    movl r15 = ia64_coop_rbs_b_base ;;
    cmp.eq p6, p7 = r14, r15 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
.global ia64_coop_b_bsp_bounds_valid
ia64_coop_b_bsp_bounds_valid:
    CHECK_PRESERVED_REGS GR4_B, GR5_B, GR6_B, GR7_B, BR1_B, BR2_B, BR3_B, BR4_B, BR5_B
.global ia64_coop_b_branch_registers_passed
ia64_coop_b_branch_registers_passed:
    CHECK_APP_STATE LC_B, EC_B, CCV_B
    mov r14 = r32 ;;
    ASSERT_EQ r14, SENTINEL_B
    ld8 r14 = [r12] ;;
    ASSERT_EQ r14, STACK_USE_B_LOW
    adds r28 = 8, r12 ;;
    ld8 r14 = [r28] ;;
    ASSERT_EQ r14, STACK_USE_B_HIGH
    mov r14 = r33 ;;
    ASSERT_EQ r14, GR8_B_FIRST
    tnat.nz p6, p7 = r33 ;;
    (p6) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_probe_record ;;
    adds r28 = R_NAT_B_BEFORE, r14 ;;
    st8 [r28] = r0 ;;
    mov r15 = ar.pfs ;;
    adds r28 = R_PFS_B_BEFORE, r14 ;;
    st8 [r28] = r15 ;;
    movl r14 = ia64_coop_probe_record + R_FP_B_FIRST ;;
    stf.spill [r14] = f2 ;;
    CHECK_CONTEXT_GP ia64_coop_context_b
    CAPTURE_FP_SET R_FP_B_FIRST_ENTRY
    RECORD_EVENT EVENT_B_FIRST_ENTRY, CTX_B_ID

    movl r14 = ia64_coop_context_b ;;
    mov out0 = r14 ;;
    movl r14 = ia64_coop_context_a ;;
    mov out1 = r14 ;;
    RECORD_EVENT EVENT_B_TO_A_FIRST, CTX_B_ID
.global ia64_coop_b_to_a_first_switch
ia64_coop_b_to_a_first_switch:
    br.call.sptk.many b0 = ia64_coop_context_switch ;;

.global ia64_coop_b_resumed
ia64_coop_b_resumed:
    RECORD_EVENT EVENT_B_RESUMED, CTX_B_ID
    CHECK_PRESERVED_REGS GR4_B, GR5_B, GR6_B, GR7_B, BR1_B, BR2_B, BR3_B, BR4_B, BR5_B
    CHECK_APP_STATE LC_B, EC_B, CCV_B
    CHECK_CONTEXT_GP ia64_coop_context_b
    mov r14 = r32 ;;
    ASSERT_EQ r14, SENTINEL_B
    ld8 r14 = [r12] ;;
    ASSERT_EQ r14, STACK_USE_B_LOW
    adds r28 = 8, r12 ;;
    ld8 r14 = [r28] ;;
    ASSERT_EQ r14, STACK_USE_B_HIGH
    mov r14 = r33 ;;
    ASSERT_EQ r14, GR8_B_FIRST
    tnat.nz p6, p7 = r33 ;;
    (p6) br.cond.sptk ia64_coop_test_fail ;;
    movl r14 = ia64_coop_probe_record ;;
    adds r28 = R_NAT_B_AFTER, r14 ;;
    st8 [r28] = r0 ;;
    mov r15 = psr ;;
    adds r28 = R_PSR_B, r14 ;;
    st8 [r28] = r15 ;;
    mov r15 = ar.pfs ;;
    adds r28 = R_PFS_B_AFTER, r14 ;;
    st8 [r28] = r15 ;;
    adds r28 = R_PFS_B_BEFORE, r14 ;;
    ld8 r14 = [r28] ;;
    cmp.eq p6, p7 = r15, r14 ;;
(p7) br.cond.sptk ia64_coop_test_fail ;;
    CAPTURE_FP_SET R_FP_B_RESUME
    movl r14 = ia64_coop_probe_record + R_FP_B ;;
    stf.spill [r14] = f2 ;;
    movl r14 = ia64_coop_context_b ;;
    adds r28 = C_RESUME_ENTRIES, r14 ;;
    ld8 r15 = [r28] ;;
    adds r15 = 1, r15 ;;
    st8 [r28] = r15 ;;

    RECORD_EVENT EVENT_B_TO_A_SECOND, CTX_B_ID
    movl r14 = ia64_coop_context_b ;;
    mov out0 = r14 ;;
    movl r14 = ia64_coop_context_a ;;
    mov out1 = r14 ;;
    br.call.sptk.many b0 = ia64_coop_context_switch ;;
.global ia64_coop_b_unexpected_resume
ia64_coop_b_unexpected_resume:
    br.cond.sptk ia64_coop_test_fail ;;
.endp ia64_coop_b_test
.size ia64_coop_b_test, .-ia64_coop_b_test

.align 16
ia64_coop_test_fail:
    movl r14 = ia64_coop_probe_record ;;
    adds r28 = 200, r14 ;;
    ld8 r15 = [r28] ;;
    adds r15 = 1, r15 ;;
    st8 [r28] = r15 ;;
    rsm psr.i | psr.ic ;;
    srlz.d ;;
1:  hint @pause ;;
    br.sptk 1b ;;
.size ia64_coop_test_fail, .-ia64_coop_test_fail
