    .text
    .globl security3g_run_workload
    .type security3g_run_workload, @function
security3g_run_workload:
    # Microsoft x64 ABI: RCX/RDX/R8 are output pointers. CPUID/XGETBV gates
    # YMM use, so this test remains safe when AVX is unavailable.
    pushq %rbx
    subq $112, %rsp
    movq %rcx, 0(%rsp)
    movq %rdx, 8(%rsp)
    movq %r8, 16(%rsp)
    movdqu %xmm6, 24(%rsp)

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

    vmovdqu %ymm6, 40(%rsp)
    vmovdqu .Lsecurity3g_xstate_marker(%rip), %ymm6
    movl $3, %r11d
    movl $1, 104(%rsp)
    jmp .Lsecurity3g_xstate_ready
.Lsecurity3g_no_avx:
    movabsq $0x5345435552495459, %r10
    movq %r10, %xmm6
    movl $1, %r11d
    movl $0, 104(%rsp)
.Lsecurity3g_xstate_ready:
    movl %r11d, 20(%rsp)

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

    movq 0(%rsp), %r10
    movq %rax, (%r10)
    movq 8(%rsp), %r10
    movq %xmm6, %r11
    movq %r11, (%r10)
    movq 16(%rsp), %r10
    movl 20(%rsp), %r11d
    movl %r11d, (%r10)
    cmpl $3, %r11d
    jne .Lsecurity3g_restore
    vmovdqu %ymm6, 72(%rsp)
    movq 72(%rsp), %r10
    movabsq $0x5345435552495459, %r11
    cmpq %r11, %r10
    jne .Lsecurity3g_clear_avx_proof
    movq 80(%rsp), %r10
    movabsq $0xA5A55A5A11223344, %r11
    cmpq %r11, %r10
    jne .Lsecurity3g_clear_avx_proof
    movq 88(%rsp), %r10
    movabsq $0x0123456789ABCDEF, %r11
    cmpq %r11, %r10
    jne .Lsecurity3g_clear_avx_proof
    movq 96(%rsp), %r10
    movabsq $0xFEDCBA9876543210, %r11
    cmpq %r11, %r10
    je .Lsecurity3g_restore
.Lsecurity3g_clear_avx_proof:
    movq 16(%rsp), %r10
    movl $0, (%r10)
.Lsecurity3g_restore:
    cmpl $1, 104(%rsp)
    jne .Lsecurity3g_restore_xmm
    vmovdqu 40(%rsp), %ymm6
.Lsecurity3g_restore_xmm:
    movdqu 24(%rsp), %xmm6
    addq $112, %rsp
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
