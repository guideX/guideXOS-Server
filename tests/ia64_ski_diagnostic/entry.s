/* Independent system-mode Ski register and SSC diagnostic entry. */
.equ SSC_EXIT, 66

.section .text.start, "ax"
.align 32
.global _start
.proc _start
_start:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
    movl gp = __gp ;;
    movl sp = diag_stack_top ;;

    /* Match the kernel's safe RSE setup ordering in this isolated ELF. */
    movl r14 = diag_rse_backing_store_base ;;
    mov ar.rsc = 0 ;;
    loadrs ;;
    mov ar.bspstore = r14 ;;
    mov ar.rsc = 3 ;;

    /* Install a known, aligned IVA, serialize, then snapshot it directly. */
    movl r14 = diag_ivt ;;
    mov cr.iva = r14 ;;
    srlz.i ;;
    mov r15 = cr.iva ;;
    movl r14 = diag_boot_snapshot_iva ;;
    st8 [r14] = r15 ;;

    /* Record initial privileged state before creating any C register frame. */
    mov r15 = ar.bsp ;;
    movl r14 = diag_boot_snapshot_bsp ;;
    st8 [r14] = r15 ;;
    /* BSPSTORE/RNAT reads are restricted to enforced-lazy mode by Ski. */
    mov ar.rsc = 0 ;;
    mov r15 = ar.bspstore ;;
    movl r14 = diag_boot_snapshot_bspstore ;;
    st8 [r14] = r15 ;;
    movl r15 = diag_rse_backing_store_base ;;
    movl r14 = diag_boot_expected_bspstore ;;
    st8 [r14] = r15 ;;
    mov r15 = ar.rnat ;;
    movl r14 = diag_boot_snapshot_rnat ;;
    st8 [r14] = r15 ;
    mov ar.rsc = 3 ;;
    movl r15 = 3 ;;
    movl r14 = diag_boot_snapshot_rsc ;;
    st8 [r14] = r15 ;;
    mov r15 = psr ;;
    movl r14 = diag_boot_snapshot_psr ;;
    st8 [r14] = r15 ;;

    /* This program alone owns these IVA/RSE changes. */
    alloc r14 = ar.pfs, 0, 0, 0, 0 ;;
    br.call.sptk.many rp = diagnostic_main ;;

    mov r15 = SSC_EXIT ;;
    mov r32 = r0 ;;
    break.i 0x80000 ;;
1:
    hint @pause ;;
    br.sptk.few 1b ;;
.endp _start

.section .ivt, "ax"
.align 32768
.global diag_ivt
diag_ivt:
    .skip 32768

.section .bss
.align 512
.global diag_rse_backing_store_base
diag_rse_backing_store_base:
    .skip 65536
.global diag_rse_backing_store_top
diag_rse_backing_store_top:
.align 16
diag_stack_bottom:
    .skip 65536
.global diag_stack_top
diag_stack_top:
