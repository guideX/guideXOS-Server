;
; Microsoft x64 ABI cooperative context switch for the freestanding kernel.
; Saves nonvolatile GPRs and every xstate component enabled in XCR0. The
; timer-interrupt continuation uses the separate switch routine below.
; Kernel AMD64 C++ remains compiled without SIMD.
;

[BITS 64]

section .text

global gxos_context_switch
global gxos_context_switch_from_timer
global gxos_context_entry_trampoline
global gxos_scheduler_selftest_probe
global gxos_scheduler_selftest_xstate_probe
global gxos_scheduler_selftest_stack_abi_probe
extern gxos_scheduler_entry
extern gxos_native_scheduler_xstate_mask

; void gxos_context_switch(uint64_t* oldStackPointer, uint64_t newStackPointer,
;                          void* oldXstate, const void* newXstate)
; RCX/RDX/R8/R9 follow Microsoft x64 ABI.
gxos_context_switch:
    mov r10, rdx
    mov r11, [rel gxos_native_scheduler_xstate_mask]
    mov eax, r11d
    shr r11, 32
    mov edx, r11d
    xsave64 [r8]
    mov r11, [rel gxos_native_scheduler_xstate_mask]
    mov eax, r11d
    shr r11, 32
    mov edx, r11d
    xrstor64 [r9]
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov [rcx], rsp
    mov rsp, r10
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret

; Timer IRQ call-site switch using the same cooperative continuation frame.
; The IRQ CPU frame and volatile-register saves remain below this call on the
; interrupted Native ELF stack. Resume returns through the IRQ stub's iretq.
; On the first transfer only, IF is enabled for the suspended root scheduler.
gxos_context_switch_from_timer:
    mov r10, rdx
    mov r11, [rel gxos_native_scheduler_xstate_mask]
    mov eax, r11d
    shr r11, 32
    mov edx, r11d
    xsave64 [r8]
    mov r11, [rel gxos_native_scheduler_xstate_mask]
    mov eax, r11d
    shr r11, 32
    mov edx, r11d
    xrstor64 [r9]
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov [rcx], rsp
    mov rsp, r10
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    sti
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

; uint32_t gxos_scheduler_selftest_stack_abi_probe(void (*yieldCallback)())
; Verify Windows x64 entry alignment and caller shadow space both before and
; after a real scheduler yield/resume. Result bits are entry-align, entry-
; shadow, resumed-align, and resumed-shadow.
gxos_scheduler_selftest_stack_abi_probe:
    push r12
    xor r12d, r12d
    lea rax, [rsp + 8]
    and eax, 15
    cmp eax, 8
    jne .stack_done
    or r12d, 1
.stack_shadow:
    mov qword [rsp + 16], 0x11223344
    mov qword [rsp + 24], 0x22334455
    mov qword [rsp + 32], 0x33445566
    mov qword [rsp + 40], 0x44556677
    cmp qword [rsp + 16], 0x11223344
    jne .stack_done
    cmp qword [rsp + 24], 0x22334455
    jne .stack_done
    cmp qword [rsp + 32], 0x33445566
    jne .stack_done
    cmp qword [rsp + 40], 0x44556677
    jne .stack_done
    or r12d, 2
    mov r11, rcx
    sub rsp, 32
    call r11
    add rsp, 32
    lea rax, [rsp + 8]
    and eax, 15
    cmp eax, 8
    jne .stack_after_shadow
    or r12d, 4
.stack_after_shadow:
    mov qword [rsp + 16], 0x55667711
    mov qword [rsp + 24], 0x66778822
    mov qword [rsp + 32], 0x77889933
    mov qword [rsp + 40], 0x4a5b6c7d
    cmp qword [rsp + 16], 0x55667711
    jne .stack_done
    cmp qword [rsp + 24], 0x66778822
    jne .stack_done
    cmp qword [rsp + 32], 0x77889933
    jne .stack_done
    cmp qword [rsp + 40], 0x4a5b6c7d
    jne .stack_done
    or r12d, 8
.stack_done:
    mov eax, r12d
    pop r12
    ret

; uint32_t gxos_scheduler_selftest_xstate_probe(void (*yieldCallback)())
; The call deliberately changes XMM6-XMM15, MXCSR, x87 control, and x87 stack
; state, yields, checks that state after resumption, then restores root state.
; When AVX is enabled in XCR0, it also checks YMM6-YMM15 including upper halves.
gxos_scheduler_selftest_xstate_probe:
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    sub rsp, 16512
    and rsp, -64
    mov [rsp + 16416], rcx

    mov r11, [rel gxos_native_scheduler_xstate_mask]
    mov eax, r11d
    shr r11, 32
    mov edx, r11d
    xsave64 [rsp]

    mov r11, [rel gxos_native_scheduler_xstate_mask]
    test r11, 4
    jz .xstate_set_xmm
    vmovdqu ymm6, [rel gxos_selftest_y6]
    vmovdqu ymm7, [rel gxos_selftest_y7]
    vmovdqu ymm8, [rel gxos_selftest_y8]
    vmovdqu ymm9, [rel gxos_selftest_y9]
    vmovdqu ymm10, [rel gxos_selftest_y10]
    vmovdqu ymm11, [rel gxos_selftest_y11]
    vmovdqu ymm12, [rel gxos_selftest_y12]
    vmovdqu ymm13, [rel gxos_selftest_y13]
    vmovdqu ymm14, [rel gxos_selftest_y14]
    vmovdqu ymm15, [rel gxos_selftest_y15]
    jmp .xstate_set_control
.xstate_set_xmm:
    movdqu xmm6, [rel gxos_selftest_y6]
    movdqu xmm7, [rel gxos_selftest_y7]
    movdqu xmm8, [rel gxos_selftest_y8]
    movdqu xmm9, [rel gxos_selftest_y9]
    movdqu xmm10, [rel gxos_selftest_y10]
    movdqu xmm11, [rel gxos_selftest_y11]
    movdqu xmm12, [rel gxos_selftest_y12]
    movdqu xmm13, [rel gxos_selftest_y13]
    movdqu xmm14, [rel gxos_selftest_y14]
    movdqu xmm15, [rel gxos_selftest_y15]
.xstate_set_control:
    mov dword [rsp + 16424], 0x3f80
    ldmxcsr [rsp + 16424]
    mov word [rsp + 16428], 0x077f
    fldcw [rsp + 16428]
    fld1
    fldpi

    lea rsp, [rsp + 16384]
    mov rcx, [rsp + 32]
    call [rsp + 32]
    sub rsp, 16384

    xor r10d, r10d
    movdqu xmm0, [rel gxos_selftest_y6]
    pcmpeqb xmm0, xmm6
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm7
    or r10d, 1
.xmm7:
    movdqu xmm0, [rel gxos_selftest_y7]
    pcmpeqb xmm0, xmm7
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm8
    or r10d, 2
.xmm8:
    movdqu xmm0, [rel gxos_selftest_y8]
    pcmpeqb xmm0, xmm8
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm9
    or r10d, 4
.xmm9:
    movdqu xmm0, [rel gxos_selftest_y9]
    pcmpeqb xmm0, xmm9
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm10
    or r10d, 8
.xmm10:
    movdqu xmm0, [rel gxos_selftest_y10]
    pcmpeqb xmm0, xmm10
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm11
    or r10d, 16
.xmm11:
    movdqu xmm0, [rel gxos_selftest_y11]
    pcmpeqb xmm0, xmm11
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm12
    or r10d, 32
.xmm12:
    movdqu xmm0, [rel gxos_selftest_y12]
    pcmpeqb xmm0, xmm12
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm13
    or r10d, 64
.xmm13:
    movdqu xmm0, [rel gxos_selftest_y13]
    pcmpeqb xmm0, xmm13
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm14
    or r10d, 128
.xmm14:
    movdqu xmm0, [rel gxos_selftest_y14]
    pcmpeqb xmm0, xmm14
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .xmm15
    or r10d, 256
.xmm15:
    movdqu xmm0, [rel gxos_selftest_y15]
    pcmpeqb xmm0, xmm15
    pmovmskb eax, xmm0
    cmp eax, 0xffff
    jne .check_mxcsr
    or r10d, 512
.check_mxcsr:
    stmxcsr [rsp + 16432]
    cmp dword [rsp + 16432], 0x3f80
    jne .check_x87_control
    or r10d, 1024
.check_x87_control:
    fnstcw [rsp + 16436]
    cmp word [rsp + 16436], 0x077f
    jne .check_x87_stack
    or r10d, 2048
.check_x87_stack:
    fldpi
    fucomip st0, st1
    jp .check_avx
    jne .check_avx
    fld1
    fucomip st0, st2
    jp .check_avx
    jne .check_avx
    fstp st0
    or r10d, 4096

.check_avx:
    mov r11, [rel gxos_native_scheduler_xstate_mask]
    test r11, 4
    jz .xstate_restore
    vmovdqu ymm1, [rel gxos_selftest_y6]
    vcmpeqps ymm0, ymm6, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx7
    or r10d, 8192
.avx7:
    vmovdqu ymm1, [rel gxos_selftest_y7]
    vcmpeqps ymm0, ymm7, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx8
    or r10d, 16384
.avx8:
    vmovdqu ymm1, [rel gxos_selftest_y8]
    vcmpeqps ymm0, ymm8, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx9
    or r10d, 32768
.avx9:
    vmovdqu ymm1, [rel gxos_selftest_y9]
    vcmpeqps ymm0, ymm9, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx10
    or r10d, 65536
.avx10:
    vmovdqu ymm1, [rel gxos_selftest_y10]
    vcmpeqps ymm0, ymm10, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx11
    or r10d, 131072
.avx11:
    vmovdqu ymm1, [rel gxos_selftest_y11]
    vcmpeqps ymm0, ymm11, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx12
    or r10d, 262144
.avx12:
    vmovdqu ymm1, [rel gxos_selftest_y12]
    vcmpeqps ymm0, ymm12, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx13
    or r10d, 524288
.avx13:
    vmovdqu ymm1, [rel gxos_selftest_y13]
    vcmpeqps ymm0, ymm13, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx14
    or r10d, 1048576
.avx14:
    vmovdqu ymm1, [rel gxos_selftest_y14]
    vcmpeqps ymm0, ymm14, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .avx15
    or r10d, 2097152
.avx15:
    vmovdqu ymm1, [rel gxos_selftest_y15]
    vcmpeqps ymm0, ymm15, ymm1
    vmovmskps eax, ymm0
    cmp eax, 0xff
    jne .xstate_restore
    or r10d, 4194304
.xstate_restore:
    mov r11, [rel gxos_native_scheduler_xstate_mask]
    mov eax, r11d
    shr r11, 32
    mov edx, r11d
    xrstor64 [rsp]
    mov eax, r10d
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret

section .rdata align=32
gxos_selftest_y6:  dq 0x3f8000003f800000, 0x3f8000003f800000, 0x3f8000003f800000, 0x3f8000003f800000
gxos_selftest_y7:  dq 0x4000000040000000, 0x4000000040000000, 0x4000000040000000, 0x4000000040000000
gxos_selftest_y8:  dq 0x4040000040400000, 0x4040000040400000, 0x4040000040400000, 0x4040000040400000
gxos_selftest_y9:  dq 0x4080000040800000, 0x4080000040800000, 0x4080000040800000, 0x4080000040800000
gxos_selftest_y10: dq 0x40a0000040a00000, 0x40a0000040a00000, 0x40a0000040a00000, 0x40a0000040a00000
gxos_selftest_y11: dq 0x40c0000040c00000, 0x40c0000040c00000, 0x40c0000040c00000, 0x40c0000040c00000
gxos_selftest_y12: dq 0x40e0000040e00000, 0x40e0000040e00000, 0x40e0000040e00000, 0x40e0000040e00000
gxos_selftest_y13: dq 0x4100000041000000, 0x4100000041000000, 0x4100000041000000, 0x4100000041000000
gxos_selftest_y14: dq 0x4110000041100000, 0x4110000041100000, 0x4110000041100000, 0x4110000041100000
gxos_selftest_y15: dq 0x4120000041200000, 0x4120000041200000, 0x4120000041200000, 0x4120000041200000
