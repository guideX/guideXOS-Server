/*
 * Bounded IA-64 CPU-state checkpoint storage and ABI leaf readers.
 *
 * The four 96-byte records live in PROGBITS .data so the early BSS clear
 * cannot erase checkpoints A or B. The C/D readers use only caller-saved
 * static GRs, do not allocate a stacked frame, and leave AR.PFS unchanged.
 */

.equ CKPT_STAGE,       0
.equ CKPT_VALID,       8
.equ CKPT_PSR,        16
.equ CKPT_RSC,        24
.equ CKPT_BSP,        32
.equ CKPT_BSPSTORE,   40
.equ CKPT_RNAT,       48
.equ CKPT_FPSR,       56
.equ CKPT_IVA,        64
.equ CKPT_PFS,        72
.equ CKPT_UNAT,       80
.equ CKPT_RESERVED,   88
.equ CKPT_BYTES,      96

.equ CKPT_HAS_PSR,       0x001
.equ CKPT_HAS_RSC,       0x002
.equ CKPT_HAS_BSP,       0x004
.equ CKPT_HAS_BSPSTORE,  0x008
.equ CKPT_HAS_FPSR,      0x020
.equ CKPT_HAS_IVA,       0x040
.equ CKPT_HAS_PFS,       0x080
.equ CKPT_HAS_UNAT,      0x100
.equ CKPT_C_D_VALID,     0x1e7

.section .text, "ax"
.align 16

.global ia64_capture_checkpoint_c
.type ia64_capture_checkpoint_c, @function
.proc ia64_capture_checkpoint_c
ia64_capture_checkpoint_c:
    movl r2 = ia64_checkpoint_c ;;
    mov r3 = psr ;;
    add r8 = CKPT_PSR, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.rsc ;;
    add r8 = CKPT_RSC, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.bsp ;;
    add r8 = CKPT_BSP, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.fpsr ;;
    add r8 = CKPT_FPSR, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = cr.iva ;;
    add r8 = CKPT_IVA, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.pfs ;;
    add r8 = CKPT_PFS, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.unat ;;
    add r8 = CKPT_UNAT, r2 ;;
    st8 [r8] = r3 ;;
    mf ;;
    movl r3 = CKPT_C_D_VALID ;;
    add r8 = CKPT_VALID, r2 ;;
    st8 [r8] = r3 ;;
    movl r3 = 3 ;;
    st8 [r2] = r3 ;;
    mf ;;
    br.ret.sptk.many b0 ;;
.endp ia64_capture_checkpoint_c
.size ia64_capture_checkpoint_c, .-ia64_capture_checkpoint_c

.align 16
.global ia64_capture_checkpoint_d
.type ia64_capture_checkpoint_d, @function
.proc ia64_capture_checkpoint_d
ia64_capture_checkpoint_d:
    movl r2 = ia64_checkpoint_d ;;
    mov r3 = psr ;;
    add r8 = CKPT_PSR, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.rsc ;;
    add r8 = CKPT_RSC, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.bsp ;;
    add r8 = CKPT_BSP, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.fpsr ;;
    add r8 = CKPT_FPSR, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = cr.iva ;;
    add r8 = CKPT_IVA, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.pfs ;;
    add r8 = CKPT_PFS, r2 ;;
    st8 [r8] = r3 ;;
    mov r3 = ar.unat ;;
    add r8 = CKPT_UNAT, r2 ;;
    st8 [r8] = r3 ;;
    mf ;;
    movl r3 = CKPT_C_D_VALID ;;
    add r8 = CKPT_VALID, r2 ;;
    st8 [r8] = r3 ;;
    movl r3 = 4 ;;
    st8 [r2] = r3 ;;
    mf ;;
    br.ret.sptk.many b0 ;;
.endp ia64_capture_checkpoint_d
.size ia64_capture_checkpoint_d, .-ia64_capture_checkpoint_d

.section .data.cpu_state_checkpoints, "aw", @progbits
.align 16
.global ia64_checkpoint_a
.type ia64_checkpoint_a, @object
ia64_checkpoint_a:
    .skip CKPT_BYTES
.size ia64_checkpoint_a, CKPT_BYTES

.align 16
.global ia64_checkpoint_b
.type ia64_checkpoint_b, @object
ia64_checkpoint_b:
    .skip CKPT_BYTES
.size ia64_checkpoint_b, CKPT_BYTES

.align 16
.global ia64_checkpoint_c
.type ia64_checkpoint_c, @object
ia64_checkpoint_c:
    .skip CKPT_BYTES
.size ia64_checkpoint_c, CKPT_BYTES

.align 16
.global ia64_checkpoint_d
.type ia64_checkpoint_d, @object
ia64_checkpoint_d:
    .skip CKPT_BYTES
.size ia64_checkpoint_d, CKPT_BYTES
