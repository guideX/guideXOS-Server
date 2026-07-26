;
; Call an amd64 Native ELF entry point on an app-owned stack.
; The kernel and guideXOS C ABI both use the Microsoft x64 register ABI on
; this target: entry=RCX, context=RDX, stackTop=R8.  The loaded application
; receives RCX=context and returns a gx_result in RAX.
;

[BITS 64]

section .text
global gxos_native_call_on_stack

gxos_native_call_on_stack:
    push rbp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov r15, rsp
    mov r11, rcx
    mov rsp, r8
    and rsp, -16
    sub rsp, 32
    mov rcx, rdx
    call r11
    mov rsp, r15
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
