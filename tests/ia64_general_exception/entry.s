/* Independent one-CPU Ski system-mode General Exception diagnostic. */
.equ IVT_BYTES, 0x8000
.equ IVT_LONG_BYTES, 0x400
.equ IVT_SHORT_BYTES, 0x100

.section .ivt, "ax"
.align 32768
.global ia64_ivt
ia64_ivt:

.macro IVT_HALT name, offset, bytes
    .org ia64_ivt + \offset
    .global \name
    .type \name, @function
    .proc \name
\name:
    rsm psr.i | psr.ic ;;
    srlz.d ;;
    hint @pause ;;
    br.sptk \name ;;
    .endp \name
    .if (. - \name) > \bytes
        .error "diagnostic fail-stop vector exceeds its slot"
    .endif
.endm

.macro IVT_GENERAL name, offset, bytes
    .org ia64_ivt + \offset
    .global \name
    .type \name, @function
    .proc \name
\name:
    br.sptk.many ia64_general_exception_entry ;;
    .endp \name
    .if (. - \name) > \bytes
        .error "diagnostic General Exception vector exceeds its slot"
    .endif
.endm

/* Architectural 68-slot vector layout: 20 long and 48 short entries. */
IVT_HALT ivt_vhpt_translation,           0x0000, IVT_LONG_BYTES
IVT_HALT ivt_itlb_miss,                  0x0400, IVT_LONG_BYTES
IVT_HALT ivt_dtlb_miss,                  0x0800, IVT_LONG_BYTES
IVT_HALT ivt_alt_itlb,                   0x0c00, IVT_LONG_BYTES
IVT_HALT ivt_alt_dtlb,                   0x1000, IVT_LONG_BYTES
IVT_HALT ivt_nested_dtlb,                0x1400, IVT_LONG_BYTES
IVT_HALT ivt_ikey_miss,                  0x1800, IVT_LONG_BYTES
IVT_HALT ivt_dkey_miss,                  0x1c00, IVT_LONG_BYTES
IVT_HALT ivt_dirty_bit,                  0x2000, IVT_LONG_BYTES
IVT_HALT ivt_iaccess_bit,                0x2400, IVT_LONG_BYTES
IVT_HALT ivt_daccess_bit,                0x2800, IVT_LONG_BYTES
IVT_HALT ivt_break,                      0x2c00, IVT_LONG_BYTES
IVT_HALT ivt_external_interrupt,         0x3000, IVT_LONG_BYTES
IVT_HALT ivt_virtual_external_interrupt, 0x3400, IVT_LONG_BYTES
IVT_HALT ivt_reserved_0e,                0x3800, IVT_LONG_BYTES
IVT_HALT ivt_reserved_0f,                0x3c00, IVT_LONG_BYTES
IVT_HALT ivt_reserved_10,                0x4000, IVT_LONG_BYTES
IVT_HALT ivt_reserved_11,                0x4400, IVT_LONG_BYTES
IVT_HALT ivt_reserved_12,                0x4800, IVT_LONG_BYTES
IVT_HALT ivt_reserved_13,                0x4c00, IVT_LONG_BYTES
IVT_HALT ivt_page_not_present,           0x5000, IVT_SHORT_BYTES
IVT_HALT ivt_key_permission,             0x5100, IVT_SHORT_BYTES
IVT_HALT ivt_ia_rights,                  0x5200, IVT_SHORT_BYTES
IVT_HALT ivt_da_rights,                  0x5300, IVT_SHORT_BYTES
IVT_GENERAL ivt_general_exception,       0x5400, IVT_SHORT_BYTES
IVT_HALT ivt_disabled_fp,                0x5500, IVT_SHORT_BYTES
IVT_HALT ivt_nat_consumption,            0x5600, IVT_SHORT_BYTES
IVT_HALT ivt_speculation,                0x5700, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_1c,                0x5800, IVT_SHORT_BYTES
IVT_HALT ivt_debug,                      0x5900, IVT_SHORT_BYTES
IVT_HALT ivt_unaligned_ref,              0x5a00, IVT_SHORT_BYTES
IVT_HALT ivt_unsupported_data_ref,       0x5b00, IVT_SHORT_BYTES
IVT_HALT ivt_fp_fault,                   0x5c00, IVT_SHORT_BYTES
IVT_HALT ivt_fp_trap,                    0x5d00, IVT_SHORT_BYTES
IVT_HALT ivt_lp_transfer,                0x5e00, IVT_SHORT_BYTES
IVT_HALT ivt_taken_branch,               0x5f00, IVT_SHORT_BYTES
IVT_HALT ivt_single_step,                0x6000, IVT_SHORT_BYTES
IVT_HALT ivt_virtualization,              0x6100, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_26,                0x6200, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_27,                0x6300, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_28,                0x6400, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_29,                0x6500, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_2a,                0x6600, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_2b,                0x6700, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_2c,                0x6800, IVT_SHORT_BYTES
IVT_HALT ivt_ia32_exception,             0x6900, IVT_SHORT_BYTES
IVT_HALT ivt_ia32_intercept,             0x6a00, IVT_SHORT_BYTES
IVT_HALT ivt_ia32_interrupt,             0x6b00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_30,                0x6c00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_31,                0x6d00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_32,                0x6e00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_33,                0x6f00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_34,                0x7000, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_35,                0x7100, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_36,                0x7200, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_37,                0x7300, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_38,                0x7400, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_39,                0x7500, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_3a,                0x7600, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_3b,                0x7700, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_3c,                0x7800, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_3d,                0x7900, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_3e,                0x7a00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_3f,                0x7b00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_40,                0x7c00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_41,                0x7d00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_42,                0x7e00, IVT_SHORT_BYTES
IVT_HALT ivt_reserved_43,                0x7f00, IVT_SHORT_BYTES

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

    /* Initialize RSE backing storage in enforced-lazy mode. */
    mov r14 = 0 ;;
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

    movl r14 = __bss_start ;;
    movl r15 = __bss_end ;;
    mov r16 = r0 ;;
clear_bss:
    cmp.geu p6, p7 = r14, r15 ;;
(p7) st8 [r14] = r16, 8
(p7) br.cond.dptk clear_bss ;;

    br.call.sptk.many b0 = general_exception_probe_main ;;
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
