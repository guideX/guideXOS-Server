/* Isolated IA-64 cooperative context-switch probe entry for Ski system mode. */
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
    movl sp = ia64_coop_stack_a_top ;;

    mov r14 = r0 ;;
    mov ar.rsc = r14 ;;
    loadrs ;;
    movl r14 = ia64_coop_rbs_a_base ;;
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
1:
    cmp.geu p6, p7 = r14, r15 ;;
(p7) st8 [r14] = r16, 8
(p7) br.cond.dptk 1b ;;

    br.call.sptk.many b0 = ia64_coop_probe_run ;;
    br.call.sptk.many b0 = ia64_coop_probe_main ;;
2:  hint @pause ;;
    br.sptk 2b ;;
.endp _start

.section .bss.ia64_coop_stacks, "aw", @nobits
.align 512
.skip 8
.global ia64_coop_stack_a_guard_low
ia64_coop_stack_a_guard_low:
    .skip 8
.global ia64_coop_stack_a_bottom
ia64_coop_stack_a_bottom:
    .skip 65536
.global ia64_coop_stack_a_top
ia64_coop_stack_a_top:
.global ia64_coop_stack_a_guard_high
ia64_coop_stack_a_guard_high:
    .skip 8

.align 512
.skip 8
.global ia64_coop_stack_b_guard_low
ia64_coop_stack_b_guard_low:
    .skip 8
.global ia64_coop_stack_b_bottom
ia64_coop_stack_b_bottom:
    .skip 65536
.global ia64_coop_stack_b_top
ia64_coop_stack_b_top:
.global ia64_coop_stack_b_guard_high
ia64_coop_stack_b_guard_high:
    .skip 8

.section .bss.ia64_coop_rbs, "aw", @nobits
.align 512
.skip 504
.global ia64_coop_rbs_a_guard_low
ia64_coop_rbs_a_guard_low:
    .skip 8
.global ia64_coop_rbs_a_base
ia64_coop_rbs_a_base:
    .skip 65536
.global ia64_coop_rbs_a_limit
ia64_coop_rbs_a_limit:
.global ia64_coop_rbs_a_guard_high
ia64_coop_rbs_a_guard_high:
    .skip 8

.align 512
.skip 504
.global ia64_coop_rbs_b_guard_low
ia64_coop_rbs_b_guard_low:
    .skip 8
.global ia64_coop_rbs_b_base
ia64_coop_rbs_b_base:
    .skip 65536
.global ia64_coop_rbs_b_limit
ia64_coop_rbs_b_limit:
.global ia64_coop_rbs_b_guard_high
ia64_coop_rbs_b_guard_high:
    .skip 8
