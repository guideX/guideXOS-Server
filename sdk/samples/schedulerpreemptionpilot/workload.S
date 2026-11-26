    .text
    .globl security3g_run_workload
    .type security3g_run_workload, @function
security3g_run_workload:
    # Microsoft x64 ABI: RCX/RDX/R8 are output pointers. CPUID/XGETBV gates
    # YMM use, so this test remains safe when AVX is unavailable.
    # Entry RSP is 8 mod 16. Saving RBX and subtracting this 16-byte multiple
    # keeps the private frame aligned without touching the caller's home area.
    .equ SECURITY3G_STACK_FRAME_BYTES, 112
    .equ SECURITY3G_CHECKSUM_POINTER_OFFSET, 0
    .equ SECURITY3G_XMM6_POINTER_OFFSET, 8
    .equ SECURITY3G_XSTATE_POINTER_OFFSET, 16
    .equ SECURITY3G_XMM6_SAVE_OFFSET, 24
    .equ SECURITY3G_YMM6_SAVE_OFFSET, 40
    .equ SECURITY3G_YMM6_PROOF_OFFSET, 72
    .equ SECURITY3G_AVX_ENABLED_OFFSET, 104
    .equ SECURITY3G_XSTATE_PROOF_MASK_OFFSET, 108

    # Assemble-time frame assertions prevent locals from overlapping saved
    # arguments/registers or extending beyond the allocated frame.
    .if SECURITY3G_CHECKSUM_POINTER_OFFSET + 8 > SECURITY3G_XMM6_POINTER_OFFSET
    .error "SECURITY3G checksum and XMM6 pointer slots overlap"
    .endif
    .if SECURITY3G_XMM6_POINTER_OFFSET + 8 > SECURITY3G_XSTATE_POINTER_OFFSET
    .error "SECURITY3G XMM6 and xstate pointer slots overlap"
    .endif
    .if SECURITY3G_XSTATE_POINTER_OFFSET + 8 > SECURITY3G_XMM6_SAVE_OFFSET
    .error "SECURITY3G xstate pointer overlaps the saved XMM6 slot"
    .endif
    .if SECURITY3G_XMM6_SAVE_OFFSET + 16 > SECURITY3G_YMM6_SAVE_OFFSET
    .error "SECURITY3G saved XMM6 and YMM6 slots overlap"
    .endif
    .if SECURITY3G_YMM6_SAVE_OFFSET + 32 > SECURITY3G_YMM6_PROOF_OFFSET
    .error "SECURITY3G saved YMM6 and proof slots overlap"
    .endif
    .if SECURITY3G_YMM6_PROOF_OFFSET + 32 > SECURITY3G_AVX_ENABLED_OFFSET
    .error "SECURITY3G YMM6 proof overlaps the AVX flag"
    .endif
    .if SECURITY3G_AVX_ENABLED_OFFSET + 4 > SECURITY3G_XSTATE_PROOF_MASK_OFFSET
    .error "SECURITY3G AVX flag overlaps the xstate proof mask"
    .endif
    .if SECURITY3G_XSTATE_PROOF_MASK_OFFSET + 4 > SECURITY3G_STACK_FRAME_BYTES
    .error "SECURITY3G xstate proof mask exceeds the stack frame"
    .endif

    pushq %rbx
    subq $SECURITY3G_STACK_FRAME_BYTES, %rsp
    movq %rcx, SECURITY3G_CHECKSUM_POINTER_OFFSET(%rsp)
    movq %rdx, SECURITY3G_XMM6_POINTER_OFFSET(%rsp)
    movq %r8, SECURITY3G_XSTATE_POINTER_OFFSET(%rsp)
    movdqu %xmm6, SECURITY3G_XMM6_SAVE_OFFSET(%rsp)

    xorl %r11d, %r11d
    movl $1, %eax
    cpuid
    andl $0x18000000, %ecx
    cmpl $0x18000000, %ecx
    jne .Lsecurity3g_no_avx
    xorl %ecx, %ecx
    xgetbv
    andl $6, %eax
    cmpl $6, %eax
    jne .Lsecurity3g_no_avx

    vmovdqu %ymm6, SECURITY3G_YMM6_SAVE_OFFSET(%rsp)
    vmovdqu .Lsecurity3g_xstate_marker(%rip), %ymm6
    movl $3, %r11d
    movl $1, SECURITY3G_AVX_ENABLED_OFFSET(%rsp)
    jmp .Lsecurity3g_xstate_ready
.Lsecurity3g_no_avx:
    movabsq $0x5345435552495459, %r10
    movq %r10, %xmm6
    movl $1, %r11d
    movl $0, SECURITY3G_AVX_ENABLED_OFFSET(%rsp)
.Lsecurity3g_xstate_ready:
    movl %r11d, SECURITY3G_XSTATE_PROOF_MASK_OFFSET(%rsp)

    movabsq $0x5333474E4F4E5949, %rax
    movabsq $0x9E3779B97F4A7C15, %r8
    movabsq $0xBF58476D1CE4E5B9, %r9
    movabsq $8000000, %r10
    xorq %rcx, %rcx
.Lsecurity3g_work:
    leaq (%rcx,%r8), %r11
    xorq %r11, %rax
    imulq %r9, %rax
    movq %rax, %rdx
    shrq $31, %rdx
    xorq %rdx, %rax
    movq %rax, %rdx
    shlq $13, %rax
    shrq $51, %rdx
    orq %rdx, %rax
    incq %rcx
    cmpq %r10, %rcx
    jb .Lsecurity3g_work

    movq SECURITY3G_CHECKSUM_POINTER_OFFSET(%rsp), %r10
    movq %rax, (%r10)
    movq SECURITY3G_XMM6_POINTER_OFFSET(%rsp), %r10
    movq %xmm6, %r11
    movq %r11, (%r10)
    movq SECURITY3G_XSTATE_POINTER_OFFSET(%rsp), %r10
    movl SECURITY3G_XSTATE_PROOF_MASK_OFFSET(%rsp), %r11d
    movl %r11d, (%r10)
    cmpl $3, %r11d
    jne .Lsecurity3g_restore
    vmovdqu %ymm6, SECURITY3G_YMM6_PROOF_OFFSET(%rsp)
    movq SECURITY3G_YMM6_PROOF_OFFSET(%rsp), %r10
    movabsq $0x5345435552495459, %r11
    cmpq %r11, %r10
    jne .Lsecurity3g_clear_avx_proof
    movq SECURITY3G_YMM6_PROOF_OFFSET + 8(%rsp), %r10
    movabsq $0xA5A55A5A11223344, %r11
    cmpq %r11, %r10
    jne .Lsecurity3g_clear_avx_proof
    movq SECURITY3G_YMM6_PROOF_OFFSET + 16(%rsp), %r10
    movabsq $0x0123456789ABCDEF, %r11
    cmpq %r11, %r10
    jne .Lsecurity3g_clear_avx_proof
    movq SECURITY3G_YMM6_PROOF_OFFSET + 24(%rsp), %r10
    movabsq $0xFEDCBA9876543210, %r11
    cmpq %r11, %r10
    je .Lsecurity3g_restore
.Lsecurity3g_clear_avx_proof:
    movq SECURITY3G_XSTATE_POINTER_OFFSET(%rsp), %r10
    movl $0, (%r10)
.Lsecurity3g_restore:
    cmpl $1, SECURITY3G_AVX_ENABLED_OFFSET(%rsp)
    jne .Lsecurity3g_restore_xmm
    vmovdqu SECURITY3G_YMM6_SAVE_OFFSET(%rsp), %ymm6
.Lsecurity3g_restore_xmm:
    movdqu SECURITY3G_XMM6_SAVE_OFFSET(%rsp), %xmm6
    addq $SECURITY3G_STACK_FRAME_BYTES, %rsp
    popq %rbx
    ret
    .size security3g_run_workload, .-security3g_run_workload

    .section .rodata
    .p2align 5
.Lsecurity3g_xstate_marker:
    .quad 0x5345435552495459
    .quad 0xA5A55A5A11223344
    .quad 0x0123456789ABCDEF
    .quad 0xFEDCBA9876543210

    .section .note.GNU-stack,"",@progbits
