/* Establish GR8.NaT with the previously qualified PSR.ed/rfi technique. */
.section .text.stacked_nat_rse_probe, "ax"
.align 16
.global stacked_nat_rse_probe_run
.type stacked_nat_rse_probe_run, @function
.proc stacked_nat_rse_probe_run
stacked_nat_rse_probe_run:
    mov r10 = r12 ;;
    movl r11 = stacked_nat_rse_probe_saved_r12 ;;
    st8 [r11] = r10 ;;
    mov r10 = r13 ;;
    movl r11 = stacked_nat_rse_probe_saved_r13 ;;
    st8 [r11] = r10 ;;
    mov r10 = pr ;;
    movl r11 = stacked_nat_rse_probe_saved_pr ;;
    st8 [r11] = r10 ;;
    mov r10 = b0 ;;
    movl r11 = stacked_nat_rse_probe_saved_b0 ;;
    st8 [r11] = r10 ;;

    mov r12 = 0x401 ;;
    mov pr = r12, -1 ;;
    ssm psr.ic ;;
    srlz.d ;;
    mov r10 = psr ;;
    movl r11 = 0x60000000000 ;;
    andcm r10 = r10, r11 ;;
    movl r11 = 0x80000000000 ;;
    or r10 = r10, r11 ;;
    movl r15 = stacked_nat_rse_probe_pointer ;;
    movl r11 = stacked_nat_rse_probe_spec_load_site ;;
    rsm psr.ic ;;
    srlz.d ;;
    mov cr.iip = r11 ;;
    mov cr.ipsr = r10 ;;
    srlz.i ;;
    rfi ;;
    ;;

.align 16
.global stacked_nat_rse_probe_spec_load_site
stacked_nat_rse_probe_spec_load_site:
    { .mii
        ld8.s r8 = [r15]
        nop.i 0
        nop.i 0 ;;
    }
    tnat.nz p6, p7 = r8 ;;
(p7) br.cond.sptk stacked_nat_rse_probe_fail ;;
    br.call.sptk.many b0 = ia64_stacked_nat_rse_test ;;
    mov r8 = r0 ;;
    br.cond.sptk stacked_nat_rse_probe_restore ;;

stacked_nat_rse_probe_fail:
    movl r10 = ia64_stacked_nat_rse_record + 32 ;;
    mov r11 = 0x100 ;;
    st8 [r10] = r11 ;;
    mov r8 = r0 ;;

stacked_nat_rse_probe_restore:
    movl r11 = stacked_nat_rse_probe_saved_pr ;;
    ld8 r10 = [r11] ;;
    mov pr = r10, -1 ;;
    movl r11 = stacked_nat_rse_probe_saved_b0 ;;
    ld8 r10 = [r11] ;;
    mov b0 = r10 ;;
    movl r11 = stacked_nat_rse_probe_saved_r13 ;;
    ld8 r13 = [r11] ;;
    movl r11 = stacked_nat_rse_probe_saved_r12 ;;
    ld8 r12 = [r11] ;;
    br.ret.sptk.many b0 ;;
.endp stacked_nat_rse_probe_run
.size stacked_nat_rse_probe_run, .-stacked_nat_rse_probe_run

.section .data.stacked_nat_rse_probe, "aw"
.align 16
stacked_nat_rse_probe_pointer:
    .8byte stacked_nat_rse_probe_target
stacked_nat_rse_probe_target:
    .8byte 0x4758534f53505242

.section .bss.stacked_nat_rse_probe, "aw", @nobits
.align 8
stacked_nat_rse_probe_saved_r12:
    .skip 8
stacked_nat_rse_probe_saved_r13:
    .skip 8
stacked_nat_rse_probe_saved_pr:
    .skip 8
stacked_nat_rse_probe_saved_b0:
    .skip 8
