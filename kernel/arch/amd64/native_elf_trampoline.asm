;
; Trusted NativeElf invocation trampoline, Microsoft x64 ABI.
;
; bool invoke_native_entry_on_stack(entry, context, stackTop, result)
;   RCX = entry point
;   RDX = gx_app_context*
;   R8  = top of the fixed application stack
;   R9  = NativeElfTrampolineResult* on the kernel stack
;
; At the target entry, RSP is stackTop - 0x28.  This gives the target
; the required 32-byte home area and the required 16-byte call alignment.
; The target's return address is therefore entirely contained by the bounded
; application stack.  The kernel stack is restored from R15's saved frame.
;

bits 64
section .text

global invoke_native_entry_on_stack

invoke_native_entry_on_stack:
    ; Reject a malformed internal call before touching either stack.
    test    rcx, rcx
    jz      .fail
    test    rdx, rdx
    jz      .fail
    test    r8, r8
    jz      .fail
    test    r9, r9
    jz      .fail

    ; The frame is 0x130 bytes.  It stores all Microsoft nonvolatile state,
    ; the four incoming arguments, and XMM6-XMM15.
    sub     rsp, 0x130
    mov     [rsp + 0x00], rbx
    mov     [rsp + 0x08], rbp
    mov     [rsp + 0x10], rsi
    mov     [rsp + 0x18], rdi
    mov     [rsp + 0x20], r12
    mov     [rsp + 0x28], r13
    mov     [rsp + 0x30], r14
    mov     [rsp + 0x38], r15
    mov     [rsp + 0x40], rcx
    mov     [rsp + 0x48], rdx
    mov     [rsp + 0x50], r8
    mov     [rsp + 0x58], r9
    movdqu  [rsp + 0x80], xmm6
    movdqu  [rsp + 0x90], xmm7
    movdqu  [rsp + 0xA0], xmm8
    movdqu  [rsp + 0xB0], xmm9
    movdqu  [rsp + 0xC0], xmm10
    movdqu  [rsp + 0xD0], xmm11
    movdqu  [rsp + 0xE0], xmm12
    movdqu  [rsp + 0xF0], xmm13
    movdqu  [rsp + 0x100], xmm14
    movdqu  [rsp + 0x110], xmm15

    ; Keep the kernel frame address in a nonvolatile register while the
    ; application and its host call execute.
    mov     r15, rsp
    mov     rax, [r15 + 0x40]
    mov     rcx, [r15 + 0x48]
    mov     r12, [r15 + 0x58]
    mov     rsp, [r15 + 0x50]
    and     rsp, -16
    sub     rsp, 0x20
    ; The target observes rsp-8, after this call pushes its return address.
    lea     r10, [rsp - 8]
    mov     [r12 + 0x08], r10
    call    rax

    ; EAX is the gx_main result.  R12 is nonvolatile under the target ABI.
    mov     [r12 + 0x00], eax

    ; Restore the kernel caller's exact stack and all nonvolatile state.
    mov     rsp, r15
    movdqu  xmm6, [rsp + 0x80]
    movdqu  xmm7, [rsp + 0x90]
    movdqu  xmm8, [rsp + 0xA0]
    movdqu  xmm9, [rsp + 0xB0]
    movdqu  xmm10, [rsp + 0xC0]
    movdqu  xmm11, [rsp + 0xD0]
    movdqu  xmm12, [rsp + 0xE0]
    movdqu  xmm13, [rsp + 0xF0]
    movdqu  xmm14, [rsp + 0x100]
    movdqu  xmm15, [rsp + 0x110]
    mov     rbx, [rsp + 0x00]
    mov     rbp, [rsp + 0x08]
    mov     rsi, [rsp + 0x10]
    mov     rdi, [rsp + 0x18]
    mov     r12, [rsp + 0x20]
    mov     r13, [rsp + 0x28]
    mov     r14, [rsp + 0x30]
    mov     r15, [rsp + 0x38]
    add     rsp, 0x130
    mov     eax, 1
    ret

.fail:
    xor     eax, eax
    ret
