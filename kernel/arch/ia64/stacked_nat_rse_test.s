/* Bounded stacked-register NaT preservation through one real RSE spill/fill. */

.equ STACKED_NAT_RSE_MAGIC,       0x53544e4154525345
.equ STACKED_NAT_RSE_VERSION,     1
.equ STACKED_NAT_RSE_RECORD_SIZE, 320
.equ STACKED_NAT_RSE_SENTINEL,    0x4758534f534e4154

.equ REC_MAGIC,                   0
.equ REC_VERSION,                 8
.equ REC_SIZE,                   16
.equ REC_SEQUENCE,               24
.equ REC_RESULT,                  32
.equ REC_STATIC_NAT_BEFORE,       40
.equ REC_STACKED_NAT_BEFORE,      48
.equ REC_STACKED_NAT_AFTER,       56
.equ REC_STATIC_NAT_AFTER,        64
.equ REC_ORDINARY_BEFORE,         72
.equ REC_ORDINARY_AFTER,          80
.equ REC_ORIGINAL_RSC,            88
.equ REC_LAZY_RSC,                96
.equ REC_CHILD_RSC,              104
.equ REC_PARENT_SOF,             112
.equ REC_CHILD_SOF,              120
.equ REC_PARENT_BSP,             128
.equ REC_PARENT_BSPSTORE,        136
.equ REC_CHILD_BSP,              144
.equ REC_CHILD_BSPSTORE,         152
.equ REC_CHILD_RNAT,             160
.equ REC_SPILLED_SENTINEL,       168
.equ REC_RETURN_BSP,             176
.equ REC_RETURN_BSPSTORE,        184
.equ REC_RESTORED_RSC,           192
.equ REC_PARENT_PFS,             200
.equ REC_CHILD_PFS,              208
.equ REC_NAT_SLOT_ADDRESS,       216
.equ REC_NAT_COLLECTION_ADDRESS,224
.equ REC_NAT_SLOT_INDEX,         232
.equ REC_RNAT_SOURCE,            240
.equ REC_RNAT_COLLECTION_VALUE,  248
.equ REC_RNAT_OBSERVED,          256
.equ REC_SAVED_PR,               264
.equ REC_STAGE,                  272
.equ REC_RETURN_MARKER,          280
.equ REC_SAVED_R12,              288
.equ REC_SAVED_R13,              296
.equ REC_RESERVED,               304
.equ REC_RESERVED2,              312

.macro STORE_REC value, offset
    adds r11 = \offset, r9 ;;
    st8 [r11] = \value ;;
.endm

.macro LOAD_REC destination, offset
    adds r11 = \offset, r9 ;;
    ld8 \destination = [r11] ;;
.endm

.section .bss.stacked_nat_rse, "aw", @nobits
.align 16
.global ia64_stacked_nat_rse_record
.type ia64_stacked_nat_rse_record, @object
ia64_stacked_nat_rse_record:
    .skip STACKED_NAT_RSE_RECORD_SIZE
.size ia64_stacked_nat_rse_record, STACKED_NAT_RSE_RECORD_SIZE

.section .text.stacked_nat_rse, "ax"
.align 16
.global ia64_stacked_nat_rse_test
.type ia64_stacked_nat_rse_test, @function
.proc ia64_stacked_nat_rse_test
ia64_stacked_nat_rse_test:
    /* The caller establishes GR8.NaT; this leaf owns only GR8's observation. */
    movl r9 = ia64_stacked_nat_rse_record ;;
    mov r10 = r12 ;;
    STORE_REC r10, REC_SAVED_R12
    mov r10 = r13 ;;
    STORE_REC r10, REC_SAVED_R13
    mov r10 = pr ;;
    STORE_REC r10, REC_SAVED_PR
    LOAD_REC r10, REC_SEQUENCE
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk stacked_nat_rse_stale_record ;;

    movl r10 = STACKED_NAT_RSE_MAGIC ;;
    STORE_REC r10, REC_MAGIC
    mov r10 = STACKED_NAT_RSE_VERSION ;;
    STORE_REC r10, REC_VERSION
    mov r10 = STACKED_NAT_RSE_RECORD_SIZE ;;
    STORE_REC r10, REC_SIZE
    mov r10 = 1 ;;
    STORE_REC r10, REC_SEQUENCE
    movl r10 = 0xffffffff ;;
    STORE_REC r10, REC_RESULT
    mov r10 = 0 ;;
    STORE_REC r10, REC_STAGE

    tnat.nz p6, p7 = r8 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    STORE_REC r10, REC_STATIC_NAT_BEFORE
    cmp.eq p6, p7 = r10, r0 ;;
(p6) br.cond.sptk stacked_nat_rse_no_static_nat ;;

    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_ORIGINAL_RSC
    movl r11 = 3 ;;
    and r12 = r10, r11 ;;
    cmp.eq p6, p7 = r12, r11 ;;
(p7) br.cond.sptk stacked_nat_rse_bad_initial_rsc ;;

    /* Enforced-lazy mode is established before every BSPSTORE/RNAT read. */
    andcm r10 = r10, r11 ;;
    mov ar.rsc = r10 ;;
    srlz.d ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_STAGE
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_LAZY_RSC
    movl r11 = 3 ;;
    and r12 = r10, r11 ;;
    cmp.eq p6, p7 = r12, r0 ;;
(p7) br.cond.sptk stacked_nat_rse_bad_lazy_rsc_before_frame ;;

    /* Three explicit locals: GR32 sentinel, GR33 NaT, GR34 saved return BR. */
    alloc r10 = ar.pfs, 0, 3, 0, 0 ;;
    STORE_REC r10, REC_PARENT_PFS
    mov r10 = 3 ;;
    STORE_REC r10, REC_PARENT_SOF
    movl r32 = STACKED_NAT_RSE_SENTINEL ;;
    mov r33 = r8 ;;
    mov r34 = b0 ;;
    STORE_REC r32, REC_ORDINARY_BEFORE
    tnat.nz p6, p7 = r33 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    STORE_REC r10, REC_STACKED_NAT_BEFORE
    cmp.eq p6, p7 = r10, r0 ;;
(p6) br.cond.sptk stacked_nat_rse_parent_nat_missing ;;

    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_PARENT_BSP
    mov r10 = ar.bspstore ;;
    STORE_REC r10, REC_PARENT_BSPSTORE
    LOAD_REC r12, REC_PARENT_BSP
    extr.u r12 = r12, 3, 6 ;;
    mov r11 = 62 ;;
    cmp.leu p6, p7 = r12, r11 ;;
(p7) br.cond.sptk stacked_nat_rse_boundary_precondition ;;

    /* GR33 follows GR32, except that slot 63 is the RNAT collection word. */
    LOAD_REC r12, REC_PARENT_BSP
    extr.u r10 = r12, 3, 6 ;;
    mov r11 = 62 ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p6) adds r10 = 16, r12 ;;
(p7) adds r10 = 8, r12 ;;
    STORE_REC r10, REC_NAT_SLOT_ADDRESS
    extr.u r10 = r10, 3, 6 ;;
    STORE_REC r10, REC_NAT_SLOT_INDEX
    LOAD_REC r12, REC_NAT_SLOT_ADDRESS
    movl r11 = 0x1ff ;;
    andcm r12 = r12, r11 ;;
    movl r11 = 0x1f8 ;;
    or r12 = r12, r11 ;;
    STORE_REC r12, REC_NAT_COLLECTION_ADDRESS

    mov r10 = ar.bsp ;;
    movl r11 = rse_backing_store_base ;;
    cmp.geu p6, p7 = r10, r11 ;;
(p7) br.cond.sptk stacked_nat_rse_backing_range ;;
    movl r11 = rse_backing_store_top ;;
    adds r12 = 1024, r10 ;;
    cmp.geu p6, p7 = r11, r12 ;;
(p7) br.cond.sptk stacked_nat_rse_backing_range ;;

    /* Drain older dirty frames. The current three-local frame remains live. */
    flushrs ;;
    mov r10 = ar.bspstore ;;
    STORE_REC r10, REC_PARENT_BSPSTORE
    mov r13 = ar.bsp ;;
    LOAD_REC r12, REC_PARENT_BSP
    cmp.eq p6, p7 = r13, r12 ;;
(p7) br.cond.sptk stacked_nat_rse_flush_changed_bsp ;;
    cmp.eq p6, p7 = r10, r12 ;;
(p7) br.cond.sptk stacked_nat_rse_flush_incomplete ;;
    mov r10 = ar.rsc ;;
    movl r11 = 3 ;;
    and r10 = r10, r11 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p7) br.cond.sptk stacked_nat_rse_bad_lazy_rsc ;;
    mov r10 = 2 ;;
    STORE_REC r10, REC_STAGE

    /* The child consumes the full 96-register architectural frame. */
    br.call.sptk.many b0 = ia64_stacked_nat_rse_pressure_child ;;

    /* br.ret from the child has completed its mandatory fill of this frame. */
    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_RETURN_BSP
    mov r10 = ar.bspstore ;;
    STORE_REC r10, REC_RETURN_BSPSTORE
    mov r10 = r32 ;;
    STORE_REC r10, REC_ORDINARY_AFTER
    tnat.nz p6, p7 = r33 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    STORE_REC r10, REC_STACKED_NAT_AFTER
    tnat.nz p6, p7 = r8 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    STORE_REC r10, REC_STATIC_NAT_AFTER
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_LAZY_RSC
    movl r10 = 0x53544e4154524554 ;;
    STORE_REC r10, REC_RETURN_MARKER
    mov r10 = 4 ;;
    STORE_REC r10, REC_STAGE

    br.cond.sptk stacked_nat_rse_restore_frame ;;

stacked_nat_rse_no_static_nat:
    mov r10 = 1 ;;
    br.cond.sptk stacked_nat_rse_fail_pre_rsc ;;
stacked_nat_rse_bad_initial_rsc:
    mov r10 = 2 ;;
    br.cond.sptk stacked_nat_rse_fail_pre_rsc ;;
stacked_nat_rse_bad_lazy_rsc_before_frame:
    mov r10 = 3 ;;
    movl r9 = ia64_stacked_nat_rse_record ;;
    STORE_REC r10, REC_RESULT
    LOAD_REC r10, REC_ORIGINAL_RSC
    mov ar.rsc = r10 ;;
    srlz.d ;;
    br.cond.sptk stacked_nat_rse_restore_caller ;;
stacked_nat_rse_bad_lazy_rsc:
    mov r10 = 11 ;;
    br.cond.sptk stacked_nat_rse_fail_frame ;;
stacked_nat_rse_parent_nat_missing:
    mov r10 = 4 ;;
    br.cond.sptk stacked_nat_rse_fail_frame ;;
stacked_nat_rse_boundary_precondition:
    mov r10 = 5 ;;
    br.cond.sptk stacked_nat_rse_fail_frame ;;
stacked_nat_rse_backing_range:
    mov r10 = 6 ;;
    br.cond.sptk stacked_nat_rse_fail_frame ;;
stacked_nat_rse_flush_changed_bsp:
    mov r10 = 7 ;;
    br.cond.sptk stacked_nat_rse_fail_frame ;;
stacked_nat_rse_flush_incomplete:
    mov r10 = 8 ;;
    br.cond.sptk stacked_nat_rse_fail_frame ;
stacked_nat_rse_stale_record:
    mov r10 = 9 ;;
    br.cond.sptk stacked_nat_rse_fail_pre_rsc ;;

stacked_nat_rse_fail_frame:
    movl r9 = ia64_stacked_nat_rse_record ;;
    STORE_REC r10, REC_RESULT
stacked_nat_rse_restore_frame:
    movl r9 = ia64_stacked_nat_rse_record ;;
    LOAD_REC r10, REC_ORIGINAL_RSC
    mov ar.rsc = r10 ;;
    srlz.d ;;
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_RESTORED_RSC
    LOAD_REC r10, REC_PARENT_PFS
    mov ar.pfs = r10 ;;
    mov b0 = r34 ;;
    br.cond.sptk stacked_nat_rse_restore_caller ;;

stacked_nat_rse_fail_pre_rsc:
    movl r9 = ia64_stacked_nat_rse_record ;;
    STORE_REC r10, REC_RESULT
    br.cond.sptk stacked_nat_rse_restore_caller ;;

stacked_nat_rse_restore_caller:
    LOAD_REC r10, REC_SAVED_PR
    mov pr = r10, -1 ;;
    LOAD_REC r13, REC_SAVED_R13
    LOAD_REC r12, REC_SAVED_R12
    br.ret.sptk.many b0 ;;
.endp ia64_stacked_nat_rse_test
.size ia64_stacked_nat_rse_test, .-ia64_stacked_nat_rse_test

.align 16
.global ia64_stacked_nat_rse_pressure_child
.type ia64_stacked_nat_rse_pressure_child, @function
.proc ia64_stacked_nat_rse_pressure_child
ia64_stacked_nat_rse_pressure_child:
    alloc r10 = ar.pfs, 0, 96, 0, 0 ;;
    movl r9 = ia64_stacked_nat_rse_record ;;
    STORE_REC r10, REC_CHILD_PFS
    mov r10 = 96 ;;
    STORE_REC r10, REC_CHILD_SOF
    mov r10 = ar.rsc ;;
    STORE_REC r10, REC_CHILD_RSC
    movl r11 = 3 ;;
    and r12 = r10, r11 ;;
    cmp.eq p6, p7 = r12, r0 ;;
(p7) br.cond.sptk stacked_nat_rse_child_no_observation ;;

    /* RSC.mode=0 was checked before these architecturally restricted reads. */
    mov r10 = ar.bspstore ;;
    STORE_REC r10, REC_CHILD_BSPSTORE
    mov r10 = ar.bsp ;;
    STORE_REC r10, REC_CHILD_BSP
    mf ;;
    mov r10 = ar.rnat ;;
    STORE_REC r10, REC_CHILD_RNAT

    /* Capture the sentinel only after the completed mandatory RSE stores. */
    LOAD_REC r12, REC_PARENT_BSP
    movl r11 = rse_backing_store_base ;;
    cmp.geu p6, p7 = r12, r11 ;;
(p7) br.cond.sptk stacked_nat_rse_child_skip_sentinel ;;
    adds r13 = 24, r12 ;;
    movl r11 = rse_backing_store_top ;;
    cmp.geu p6, p7 = r11, r13 ;;
(p7) br.cond.sptk stacked_nat_rse_child_skip_sentinel ;;
    LOAD_REC r13, REC_CHILD_BSPSTORE
    adds r10 = 24, r12 ;;
    cmp.geu p6, p7 = r13, r10 ;;
(p7) br.cond.sptk stacked_nat_rse_child_skip_sentinel ;;
    /* The host validator also checks the complete spilled-frame range. */
    mf ;;
    ld8 r10 = [r12] ;;
    STORE_REC r10, REC_SPILLED_SENTINEL

    /* Prefer the partial current RNAT only when its indexed bit is valid. */
    LOAD_REC r12, REC_NAT_SLOT_ADDRESS
    LOAD_REC r13, REC_CHILD_BSPSTORE
    shr.u r10 = r12, 9 ;;
    shr.u r11 = r13, 9 ;;
    cmp.eq p6, p7 = r10, r11 ;;
(p7) br.cond.sptk stacked_nat_rse_check_memory_collection ;;
    extr.u r10 = r13, 3, 6 ;;
    LOAD_REC r11, REC_NAT_SLOT_INDEX
    cmp.geu p6, p7 = r10, r11 ;;
(p7) br.cond.sptk stacked_nat_rse_check_memory_collection ;;
    mov r10 = 1 ;;
    STORE_REC r10, REC_RNAT_SOURCE
    LOAD_REC r10, REC_NAT_SLOT_INDEX
    mov r13 = 1 ;;
    shl r13 = r13, r10 ;;
    LOAD_REC r12, REC_CHILD_RNAT
    and r12 = r12, r13 ;;
    cmp.ne p6, p7 = r12, r0 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    STORE_REC r10, REC_RNAT_OBSERVED
    mov r10 = 0 ;;
    STORE_REC r10, REC_RESULT
    mov r10 = 3 ;;
    STORE_REC r10, REC_STAGE
    br.cond.sptk stacked_nat_rse_child_return ;;

stacked_nat_rse_check_memory_collection:
    LOAD_REC r12, REC_NAT_COLLECTION_ADDRESS
    LOAD_REC r13, REC_CHILD_BSPSTORE
    adds r10 = 8, r12 ;;
    cmp.geu p6, p7 = r13, r10 ;;
(p7) br.cond.sptk stacked_nat_rse_child_return ;;
    mf ;;
    ld8 r12 = [r12] ;;
    STORE_REC r12, REC_RNAT_COLLECTION_VALUE
    mov r10 = 2 ;;
    STORE_REC r10, REC_RNAT_SOURCE
    LOAD_REC r11, REC_NAT_SLOT_INDEX
    mov r10 = 1 ;;
    shl r10 = r10, r11 ;;
    and r10 = r10, r12 ;;
    cmp.ne p6, p7 = r10, r0 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    STORE_REC r10, REC_RNAT_OBSERVED
    mov r10 = 0 ;;
    STORE_REC r10, REC_RESULT
    mov r10 = 3 ;;
    STORE_REC r10, REC_STAGE
    br.cond.sptk stacked_nat_rse_child_return ;;

stacked_nat_rse_child_skip_sentinel:
    mov r10 = 10 ;;
    STORE_REC r10, REC_RESULT
    br.cond.sptk stacked_nat_rse_child_return ;;
stacked_nat_rse_child_no_observation:
    mov r10 = 11 ;;
    STORE_REC r10, REC_RESULT

stacked_nat_rse_child_return:
    movl r9 = ia64_stacked_nat_rse_record ;;
    LOAD_REC r10, REC_CHILD_PFS
    mov ar.pfs = r10 ;;
    br.ret.sptk.many b0 ;;
.endp ia64_stacked_nat_rse_pressure_child
.size ia64_stacked_nat_rse_pressure_child, .-ia64_stacked_nat_rse_pressure_child
