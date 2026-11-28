;
; Narrow AMD64 privilege-transition helpers for the development-only CPL3 task.
; All calls and returns use the Microsoft x64 ABI used by the freestanding kernel.
;

bits 64
section .text align=16

global gxos_amd64_call_on_lifecycle_stack
global gxos_amd64_enter_user
global gxos_amd64_user_exit_stub
global gxos_amd64_user_exit_stub_end
extern gxos_protected_task_kernel_resume_rsp
extern gxos_protected_task_kernel_resume_rip
extern gxos_protected_task_kernel_resume_rflags
extern gxos_protected_task_kernel_fs_base
extern gxos_protected_task_kernel_gs_base
extern gxos_amd64_user_exit_dispatch

; void gxos_amd64_call_on_lifecycle_stack(void (*entry)(void*), void* arg)
; RCX=entry, RDX=arg. Keep the desktop/boot stack untouched while task setup,
; entry, return, and off-stack cleanup run on a separate kernel-owned stack.
gxos_amd64_call_on_lifecycle_stack:
    push rbp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov r15, rsp
    lea rsp, [rel gxos_task_lifecycle_stack_top]
    and rsp, -16
    sub rsp, 32
    mov rax, rcx
    mov rcx, rdx
    call rax
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

; void gxos_amd64_enter_user(uint64_t entry, uint64_t userRsp)
; RCX=entry, RDX=userRsp. The caller has already activated the task CR3 and
; configured the TSS RSP0. Save a normal kernel continuation, then construct
; the five-qword CPL3 IRETQ frame with interrupts and I/O privilege disabled.
gxos_amd64_enter_user:
    push rbp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    ; Keep the call arguments in saved nonvolatile registers while the MSR
    ; sequence below reuses RCX, RDX, RAX, and RDX.
    mov r12, rcx
    mov r13, rdx
    mov [rel gxos_protected_task_kernel_resume_rsp], rsp
    lea rax, [rel .kernel_resume]
    mov [rel gxos_protected_task_kernel_resume_rip], rax
    pushfq
    pop rax
    mov [rel gxos_protected_task_kernel_resume_rflags], rax
    cli

    ; User code starts with no inherited FS/GS bases. Preserve the kernel
    ; values so the exit path can restore them before resuming kernel C++.
    mov ecx, 0xC0000100
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov [rel gxos_protected_task_kernel_fs_base], rax
    xor eax, eax
    xor edx, edx
    wrmsr
    mov ecx, 0xC0000101
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov [rel gxos_protected_task_kernel_gs_base], rax
    xor eax, eax
    xor edx, edx
    wrmsr

    ; Long-mode DS/ES checks still apply to data selectors. Install the
    ; flat DPL3 data selector before IRETQ; the exit stub restores kernel DS/ES.
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x1B       ; user SS (RPL3)
    push r13              ; user RSP
    push qword 0x02       ; sanitized RFLAGS: reserved bit 1, IF=0, IOPL=0
    push qword 0x23       ; user CS (RPL3)
    push r12              ; user RIP

    ; Do not disclose kernel caller register contents to the user diagnostic.
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

.kernel_resume:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret

; CPL3-only vector 0x80 gate. The CPU switches to the current TSS RSP0 and
; pushes SS:RSP:RFLAGS:CS:RIP. Save every GPR before the Microsoft-ABI call.
; The dispatcher validates a fixed exit request, restores kernel CR3, and
; records terminal state. This stub then resumes the saved kernel continuation
; directly; it never irets to the exiting task.
gxos_amd64_user_exit_stub:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    ; CPU7C1: CPL3 controls DF and the interrupt gate does not clear it. The
    ; Microsoft x64 ABI requires DF=0 at the C++ call. The kernel continuation
    ; below restores its own saved RFLAGS, so user DF never reaches it.
    cld
    mov rcx, rsp
    sub rsp, 32

    ; Restore trusted kernel segment bases before entering C++ dispatch.
    mov ecx, 0xC0000100
    mov rax, [rel gxos_protected_task_kernel_fs_base]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    mov ecx, 0xC0000101
    mov rax, [rel gxos_protected_task_kernel_gs_base]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    lea rcx, [rsp + 32]
    call gxos_amd64_user_exit_dispatch

    mov rsp, [rel gxos_protected_task_kernel_resume_rsp]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push qword [rel gxos_protected_task_kernel_resume_rflags]
    popfq
    jmp qword [rel gxos_protected_task_kernel_resume_rip]
gxos_amd64_user_exit_stub_end:

section .bss align=16
global gxos_amd64_user_entry_stack_bottom
global gxos_amd64_user_entry_stack_top
global gxos_task_lifecycle_stack_top

gxos_amd64_user_entry_stack_bottom:
    resb 16384
gxos_amd64_user_entry_stack_top:

align 16
gxos_task_lifecycle_stack_bottom:
    resb 16384
gxos_task_lifecycle_stack_top:
