;
; Hosted harness for kernel/arch/amd64/exception_entry.asm (CPU7C1).
;
; gxos_test_deliver builds the same 64-bit frame the CPU builds for an
; exception without an IST switch (align RSP to 16, push SS, RSP, RFLAGS, CS,
; RIP and, for error-code vectors, the error word), loads 15 known GPR values
; and jumps to the production stub. A same-privilege IRETQ is legal at CPL3,
; so the production resume path returns to .continuation. This exercises the
; real stub object but is not hardware exception delivery.
;
; The production stub calls gxos_native_exception_dispatch, which this file
; provides as a shim. It records the entry RSP and RFLAGS (to check alignment,
; shadow space and DF) and forwards to the C++ fixture.
;

bits 64
default rel

section .text

global gxos_test_deliver
global gxos_test_recover_label
global gxos_native_exception_dispatch
extern gxos_test_dispatch_record

; Request layout (C++ DeliveryRequest)
%define REQ_STUB        0
%define REQ_PUSH_ERROR  8
%define REQ_ERROR       16
%define REQ_RFLAGS_SET  24
%define REQ_GPRS        32      ; 15 qwords, frame order rax..r15
; Result layout (C++ DeliveryResult)
%define RES_GPRS        0
%define RES_RFLAGS      120
%define RES_INTERRUPTED 128
%define RES_RSP_AFTER   136
%define RES_PATH        144

; uint64_t gxos_test_deliver(const DeliveryRequest* rcx, DeliveryResult* rdx)
gxos_test_deliver:
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    mov [harness_saved_rsp], rsp
    mov [harness_request], rcx
    mov [harness_result], rdx

    ; Simulated interrupted stack pointer, deliberately 8 mod 16 so that the
    ; CPU-style alignment below actually moves RSP.
    sub rsp, 72
    mov rax, rsp
    and rax, 0xF
    cmp rax, 8
    je .misaligned
    sub rsp, 8
.misaligned:
    mov [harness_interrupted_rsp], rsp
    mov rax, [harness_result]
    mov [rax + RES_INTERRUPTED], rsp

    mov r10, rsp
    and rsp, -16
    xor eax, eax
    mov ax, ss
    push rax                        ; SS
    push r10                        ; RSP
    pushfq
    pop rax
    mov r11, [rcx + REQ_RFLAGS_SET]
    or rax, r11
    push rax                        ; RFLAGS (DF may be set here)
    xor eax, eax
    mov ax, cs
    push rax                        ; CS
    lea rax, [.continuation]
    push rax                        ; RIP
    cmp qword [rcx + REQ_PUSH_ERROR], 0
    je .no_error
    push qword [rcx + REQ_ERROR]    ; hardware error word
.no_error:
    mov rax, [rcx + REQ_STUB]
    mov [harness_target], rax
    ; Interrupt gates do not clear DF, so the handler starts with the
    ; interrupted context's live DF. Reproduce that before entering the stub.
    test qword [rcx + REQ_RFLAGS_SET], 0x400
    jz .live_df_clear
    std
.live_df_clear:

    mov r15, rcx
    mov rax, [r15 + REQ_GPRS + 0]
    mov rcx, [r15 + REQ_GPRS + 8]
    mov rdx, [r15 + REQ_GPRS + 16]
    mov rbx, [r15 + REQ_GPRS + 24]
    mov rbp, [r15 + REQ_GPRS + 32]
    mov rsi, [r15 + REQ_GPRS + 40]
    mov rdi, [r15 + REQ_GPRS + 48]
    mov r8,  [r15 + REQ_GPRS + 56]
    mov r9,  [r15 + REQ_GPRS + 64]
    mov r10, [r15 + REQ_GPRS + 72]
    mov r11, [r15 + REQ_GPRS + 80]
    mov r12, [r15 + REQ_GPRS + 88]
    mov r13, [r15 + REQ_GPRS + 96]
    mov r14, [r15 + REQ_GPRS + 104]
    mov r15, [r15 + REQ_GPRS + 112]
    jmp [harness_target]

.continuation:
    ; Reached only through the stub's IRETQ. Capture before touching flags.
    mov [harness_scratch], rax
    pushfq
    pop rax
    mov [harness_scratch_flags], rax
    mov [harness_scratch_rsp], rsp
    mov rax, [harness_result]
    mov [rax + RES_GPRS + 8], rcx
    mov [rax + RES_GPRS + 16], rdx
    mov [rax + RES_GPRS + 24], rbx
    mov [rax + RES_GPRS + 32], rbp
    mov [rax + RES_GPRS + 40], rsi
    mov [rax + RES_GPRS + 48], rdi
    mov [rax + RES_GPRS + 56], r8
    mov [rax + RES_GPRS + 64], r9
    mov [rax + RES_GPRS + 72], r10
    mov [rax + RES_GPRS + 80], r11
    mov [rax + RES_GPRS + 88], r12
    mov [rax + RES_GPRS + 96], r13
    mov [rax + RES_GPRS + 104], r14
    mov [rax + RES_GPRS + 112], r15
    mov rcx, [harness_scratch]
    mov [rax + RES_GPRS + 0], rcx
    mov rcx, [harness_scratch_flags]
    mov [rax + RES_RFLAGS], rcx
    mov rcx, [harness_scratch_rsp]
    mov [rax + RES_RSP_AFTER], rcx
    mov qword [rax + RES_PATH], 1
    jmp harness_finish

gxos_test_recover_label:
    ; Reached through the stub's Native ELF recovery jump, with RSP set to
    ; gxos_native_fault_recovery_rsp and no IRETQ.
    mov [harness_scratch_rsp], rsp
    pushfq
    pop rcx
    mov rax, [harness_result]
    mov [rax + RES_RFLAGS], rcx
    mov rcx, [harness_scratch_rsp]
    mov [rax + RES_RSP_AFTER], rcx
    mov qword [rax + RES_PATH], 2

harness_finish:
    cld
    mov rsp, [harness_saved_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    xor eax, eax
    ret

; uint64_t gxos_native_exception_dispatch(const NativeExceptionFrame* rcx)
; Shim reached by the production stub's CALL.
gxos_native_exception_dispatch:
    mov [shim_entry_rsp], rsp
    pushfq
    pop rax
    mov [shim_entry_rflags], rax
    ; The caller owns 32 bytes of shadow space at [rsp+8]; the callee may use
    ; it. Fill it to prove it lies inside the caller's stack allocation.
    mov rax, 0x5AD0_5AD0_5AD0_5AD0
    mov [rsp + 8], rax
    mov [rsp + 16], rax
    mov [rsp + 24], rax
    mov [rsp + 32], rax
    ; Clobber every volatile GPR the Microsoft x64 ABI allows a callee to use.
    mov rax, 0xDEAD_0000_0000_0000
    mov rdx, rax
    mov r8, rax
    mov r9, rax
    mov r10, rax
    mov r11, rax
    sub rsp, 40
    mov rdx, [shim_entry_rsp]
    mov r8, [shim_entry_rflags]
    call gxos_test_dispatch_record
    add rsp, 40
    ret

section .bss
align 16
harness_saved_rsp:        resq 1
harness_request:          resq 1
harness_result:           resq 1
harness_target:           resq 1
harness_interrupted_rsp:  resq 1
harness_scratch:          resq 1
harness_scratch_flags:    resq 1
harness_scratch_rsp:      resq 1
shim_entry_rsp:           resq 1
shim_entry_rflags:        resq 1
