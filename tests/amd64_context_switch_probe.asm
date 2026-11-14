; Hosted-only Microsoft x64 ABI probes for the AMD64 voluntary switch test.

bits 64
section .text align=16

extern switch_context
extern task_entry_body

global context_test_task_entry
context_test_task_entry:
    mov     rax, rsp
    and     eax, 0x0F
    mov     [rcx + 8], eax
    sub     rsp, 40
    call    task_entry_body
    add     rsp, 40
    ret

; Seeds all Microsoft x64 nonvolatile GPRs, switches to a target context, and
; reports one bit per register preserved when this continuation resumes.
global context_switch_preservation_probe
context_switch_preservation_probe:
    push    rbx
    push    rbp
    push    rdi
    push    rsi
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 56                ; 32-byte shadow + 2 local qwords; aligned
    mov     [rsp + 32], rcx        ; old context slot
    mov     [rsp + 40], rdx        ; target context

    mov     rbx, 0x1122334455667788
    mov     rbp, 0x2233445566778899
    mov     rdi, 0x33445566778899AA
    mov     rsi, 0x445566778899AABB
    mov     r12, 0x5566778899AABBCC
    mov     r13, 0x66778899AABBCCDD
    mov     r14, 0x778899AABBCCDDEE
    mov     r15, 0x8899AABBCCDDEEFF

    mov     rcx, [rsp + 32]
    mov     rdx, [rsp + 40]
    call    switch_context

    xor     eax, eax
    mov     r10, 0x1122334455667788
    cmp     rbx, r10
    jne     .check_rbp
    or      eax, 1
.check_rbp:
    mov     r10, 0x2233445566778899
    cmp     rbp, r10
    jne     .check_rdi
    or      eax, 2
.check_rdi:
    mov     r10, 0x33445566778899AA
    cmp     rdi, r10
    jne     .check_rsi
    or      eax, 4
.check_rsi:
    mov     r10, 0x445566778899AABB
    cmp     rsi, r10
    jne     .check_r12
    or      eax, 8
.check_r12:
    mov     r10, 0x5566778899AABBCC
    cmp     r12, r10
    jne     .check_r13
    or      eax, 16
.check_r13:
    mov     r10, 0x66778899AABBCCDD
    cmp     r13, r10
    jne     .check_r14
    or      eax, 32
.check_r14:
    mov     r10, 0x778899AABBCCDDEE
    cmp     r14, r10
    jne     .check_r15
    or      eax, 64
.check_r15:
    mov     r10, 0x8899AABBCCDDEEFF
    cmp     r15, r10
    jne     .restore
    or      eax, 128
.restore:
    add     rsp, 56
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rsi
    pop     rdi
    pop     rbp
    pop     rbx
    ret
