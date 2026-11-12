/* Isolated one-CPU Ski system-mode stacked NaT/RSE probe entry. */
.equ IVT_BYTES, 0x8000

.section .ivt, "ax"
.align 32768
.global ia64_ivt
ia64_ivt:

.macro IVT_HALT offset
    .org ia64_ivt + \offset
    rsm psr.i | psr.ic ;;
    srlz.d ;;
1:  hint @pause ;;
    br.sptk 1b ;;
.endm

.set vec_offset, 0x0000
.rept 20
    IVT_HALT vec_offset
    .set vec_offset, vec_offset + 0x0400
.endr
.set vec_offset, 0x5000
.rept 48
    IVT_HALT vec_offset
    .set vec_offset, vec_offset + 0x0100
.endr
.org ia64_ivt + IVT_BYTES
.global ia64_ivt_end
ia64_ivt_end:
.size ia64_ivt, IVT_BYTES

.section .text.start, "ax"
.align 16
.global _start
.proc _start
_start:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
    movl gp = __gp ;;
    movl sp = stack_top ;;

    mov r14 = r0 ;;
    mov ar.rsc = r14 ;;
    loadrs ;;
    movl r14 = rse_backing_store_base ;;
    mov ar.bspstore = r14 ;;
    mov ar.rsc = 3 ;;
    rsm psr.dfl | psr.dfh ;;
    srlz.d ;;
    movl r14 = 0x0009804c0270033f ;;
    mov.m ar.fpsr = r14 ;;
    movl r14 = ia64_ivt ;;
    mov cr.iva = r14 ;;
    srlz.i ;;

    mov r15 = 20 ;;
    break.i 0x80000 ;;

    movl r14 = __bss_start ;;
    movl r15 = __bss_end ;;
    mov r16 = r0 ;;
probe_clear_bss:
    cmp.geu p6, p7 = r14, r15 ;;
(p7) st8 [r14] = r16, 8
(p7) br.cond.dptk probe_clear_bss ;;

    br.call.sptk.many b0 = stacked_nat_rse_probe_run ;;
    br.call.sptk.many b0 = stacked_nat_rse_probe_main ;;
probe_halt:
    hint @pause ;;
    br.sptk probe_halt ;;
.endp _start

.section .bss, "aw", @nobits
.align 16
stack_bottom:
    .skip 65536
stack_top:
.align 16
.global rse_backing_store_base
rse_backing_store_base:
    .skip 65536
.global rse_backing_store_top
rse_backing_store_top:
