;
; Microsoft x64 ABI cooperative context switch for the freestanding kernel.
; Only nonvolatile GPR state is saved. Kernel AMD64 C++ is built with
; -mno-sse -mno-sse2 -mno-mmx; scheduler-managed entries must not use FPU,
; MMX, SSE, or AVX state. Interrupts remain enabled and do not switch tasks.
;

[BITS 64]

section .text

global gxos_context_switch
global gxos_context_entry_trampoline
global gxos_scheduler_selftest_probe
extern gxos_scheduler_entry

; void gxos_context_switch(uint64_t* oldStackPointer, uint64_t newStackPointer)
; RCX = oldStackPointer, RDX = newStackPointer (Microsoft x64 ABI).
gxos_context_switch:
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov [rcx], rsp
    mov rsp, rdx
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret

; First dispatch returns through the synthetic frame with R12 holding the
; context record. RSP has entry alignment (8 mod 16); reserve Microsoft ABI
; shadow space and align before calling C++.
gxos_context_entry_trampoline:
    mov rcx, r12
    sub rsp, 40
    call gxos_scheduler_entry
    ud2

; uint32_t gxos_scheduler_selftest_probe(void (*yieldCallback)())
; RCX = callback. Set every nonvolatile GPR to a sentinel across an actual
; scheduler yield, return one bit per register preserved, and restore the
; original caller values before returning.
gxos_scheduler_selftest_probe:
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 56
    mov [rsp + 40], rcx

    mov rbx, 0x1122334455667788
    mov rbp, 0x2233445566778899
    mov rsi, 0x33445566778899aa
    mov rdi, 0x445566778899aabb
    mov r12, 0x5566778899aabbcc
    mov r13, 0x66778899aabbccdd
    mov r14, 0x778899aabbccddee
    mov r15, 0x8899aabbccddeeff
    call qword [rsp + 40]

    xor r10d, r10d
    mov rax, 0x1122334455667788
    cmp rbx, rax
    jne .probe_rbp
    or r10d, 1
.probe_rbp:
    mov rax, 0x2233445566778899
    cmp rbp, rax
    jne .probe_rsi
    or r10d, 2
.probe_rsi:
    mov rax, 0x33445566778899aa
    cmp rsi, rax
    jne .probe_rdi
    or r10d, 4
.probe_rdi:
    mov rax, 0x445566778899aabb
    cmp rdi, rax
    jne .probe_r12
    or r10d, 8
.probe_r12:
    mov rax, 0x5566778899aabbcc
    cmp r12, rax
    jne .probe_r13
    or r10d, 16
.probe_r13:
    mov rax, 0x66778899aabbccdd
    cmp r13, rax
    jne .probe_r14
    or r10d, 32
.probe_r14:
    mov rax, 0x778899aabbccddee
    cmp r14, rax
    jne .probe_r15
    or r10d, 64
.probe_r15:
    mov rax, 0x8899aabbccddeeff
    cmp r15, rax
    jne .probe_done
    or r10d, 128
.probe_done:
    mov eax, r10d
    add rsp, 56
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
