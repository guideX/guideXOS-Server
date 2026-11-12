/* Establish and clear the stacked-test GR8.NaT before any C++ frame exists. */

.equ STACKED_NAT_RSE_RESULT, 32

.section .data.stacked_nat_rse_entry, "aw"
.align 8
stacked_nat_rse_boot_pointer:
    .8byte stacked_nat_rse_boot_target
stacked_nat_rse_boot_target:
    .8byte 0x4758534f53505242

.section .bss.stacked_nat_rse_entry, "aw", @nobits
.align 8
stacked_nat_rse_boot_saved_r12:
    .skip 8
stacked_nat_rse_boot_saved_r13:
    .skip 8
stacked_nat_rse_boot_saved_pr:
    .skip 8
stacked_nat_rse_boot_saved_b0:
    .skip 8

.section .text.stacked_nat_rse_entry, "ax"
.align 16
.global ia64_stacked_nat_rse_boot_test
.type ia64_stacked_nat_rse_boot_test, @function
.proc ia64_stacked_nat_rse_boot_test
ia64_stacked_nat_rse_boot_test:
    mov r10 = r12 ;;
    movl r11 = stacked_nat_rse_boot_saved_r12 ;;
    st8 [r11] = r10 ;;
    mov r10 = r13 ;;
    movl r11 = stacked_nat_rse_boot_saved_r13 ;;
    st8 [r11] = r10 ;;
    mov r10 = pr ;;
    movl r11 = stacked_nat_rse_boot_saved_pr ;;
    st8 [r11] = r10 ;;
    mov r10 = b0 ;;
    movl r11 = stacked_nat_rse_boot_saved_b0 ;;
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
    movl r15 = stacked_nat_rse_boot_pointer ;;
    movl r11 = ia64_stacked_nat_rse_boot_spec_load ;;
    rsm psr.ic ;;
    srlz.d ;;
    mov cr.iip = r11 ;;
    mov cr.ipsr = r10 ;;
    srlz.i ;;
    rfi ;;
    ;;

.align 16
ia64_stacked_nat_rse_boot_spec_load:
    { .mii
        ld8.s r8 = [r15]
        nop.i 0
        nop.i 0 ;;
    }
    tnat.nz p6, p7 = r8 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p6) br.cond.sptk ia64_stacked_nat_rse_boot_missing_initial_nat ;;
    br.call.sptk.many b0 = ia64_stacked_nat_rse_test ;;
    tnat.nz p6, p7 = r8 ;;
    mov r10 = 0 ;;
(p6) mov r10 = 1 ;;
    cmp.eq p6, p7 = r10, r0 ;;
(p6) br.cond.sptk ia64_stacked_nat_rse_boot_missing_returned_nat ;;
    br.cond.sptk ia64_stacked_nat_rse_boot_restore ;;

ia64_stacked_nat_rse_boot_missing_initial_nat:
    mov r10 = 0x100 ;;
    br.cond.sptk ia64_stacked_nat_rse_boot_record_failure ;;
ia64_stacked_nat_rse_boot_missing_returned_nat:
    mov r10 = 0x101 ;;
ia64_stacked_nat_rse_boot_record_failure:
    movl r11 = ia64_stacked_nat_rse_record + STACKED_NAT_RSE_RESULT ;;
    st8 [r11] = r10 ;;

ia64_stacked_nat_rse_boot_restore:
    mov r8 = r0 ;;
    movl r11 = stacked_nat_rse_boot_saved_pr ;;
    ld8 r10 = [r11] ;;
    mov pr = r10, -1 ;;
    movl r11 = stacked_nat_rse_boot_saved_b0 ;;
    ld8 r10 = [r11] ;;
    mov b0 = r10 ;;
    movl r11 = stacked_nat_rse_boot_saved_r13 ;;
    ld8 r13 = [r11] ;;
    movl r11 = stacked_nat_rse_boot_saved_r12 ;;
    ld8 r12 = [r11] ;;
    br.ret.sptk.many b0 ;;
.endp ia64_stacked_nat_rse_boot_test
.size ia64_stacked_nat_rse_boot_test, .-ia64_stacked_nat_rse_boot_test
