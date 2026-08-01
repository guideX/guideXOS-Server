;
; Call an amd64 Native ELF entry point on an app-owned stack.
; The kernel and guideXOS C ABI both use the Microsoft x64 register ABI on
; this target: entry=RCX, context=RDX, stackTop=R8.  The loaded application
; receives RCX=context and returns a gx_result in RAX.
;

[BITS 64]

section .text
global gxos_native_call_on_stack
global gxos_native_call_on_stack_end
extern gxos_native_set_fault_recovery

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

    ; Record the kernel-side stack and the recovery label before entering an
    ; external ELF.  Exception entry can abandon the app stack and jump back
    ; here after the native fault dispatcher has captured the full frame.
    mov r14, r11
    mov r13, rdx
    mov rcx, r15
    lea rdx, [rel .fault_return]
    call gxos_native_set_fault_recovery
    mov r11, r14

    mov rsp, r8
    and rsp, -16
    sub rsp, 32
    mov rcx, r13
    call r11

.normal_return:
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

.fault_return:
    mov eax, 0xFFFFFFFC            ; GX_ERROR_FAILED
    ; The exception stub installed the saved kernel stack in RSP.
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret

gxos_native_call_on_stack_end:
