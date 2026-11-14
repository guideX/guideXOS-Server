; AMD64 voluntary context switch for the Microsoft x64 ABI.
;
; switch_context(SwitchContext** old_ctx, SwitchContext* new_ctx)
;   RCX = old_ctx
;   RDX = new_ctx
;
; This preserves the ABI nonvolatile general-purpose registers. The freestanding
; kernel is compiled with MMX/SSE disabled, so the current kernel-task contract
; has no SIMD state to preserve. It does not save interrupt frames or RFLAGS.

bits 64
section .text align=16

global switch_context
switch_context:
    ; On entry RSP is 8 mod 16. This 80-byte frame stores the 10-qword
    ; SwitchContext and leaves the original return address at [rsp+80].
    sub     rsp, 80
    mov     [rsp + 0], rbx
    mov     [rsp + 8], rbp
    mov     [rsp + 16], rdi
    mov     [rsp + 24], rsi
    mov     [rsp + 32], r12
    mov     [rsp + 40], r13
    mov     [rsp + 48], r14
    mov     [rsp + 56], r15
    lea     rax, [rsp + 88]          ; caller RSP after this function returns
    mov     [rsp + 64], rax
    mov     rax, [rsp + 80]          ; this function's return address
    mov     [rsp + 72], rax
    mov     [rcx], rsp

    ; R8/RAX are volatile under Microsoft x64 and serve as scratch while the
    ; target's nonvolatile registers and continuation are restored.
    mov     rax, [rdx + 72]
    mov     r8,  [rdx + 64]
    mov     rbx, [rdx + 0]
    mov     rbp, [rdx + 8]
    mov     rdi, [rdx + 16]
    mov     rsi, [rdx + 24]
    mov     r12, [rdx + 32]
    mov     r13, [rdx + 40]
    mov     r14, [rdx + 48]
    mov     r15, [rdx + 56]
    mov     rsp, r8
    jmp     rax

global thread_entry_trampoline
thread_entry_trampoline:
    ; A fresh context starts with RSP=top-40 (8 mod 16), with 32 bytes of
    ; caller shadow space and a synthetic return address already installed.
    ; Reserve shadow space for the application entry call and align RSP first.
    sub     rsp, 40
    mov     rcx, r13                ; first argument: task-owned argument
    call    r12                     ; task entry point
    add     rsp, 40
    ret                             ; synthetic return -> thread_returned

global thread_returned
thread_returned:
    ; Reclamation requires a scheduler-owned reaper on another stack. Until
    ; that exists, returning from a kernel context halts instead of freeing
    ; or reusing the active stack.
    cli
.halt_forever:
    hlt
    jmp     .halt_forever
