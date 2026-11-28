;
; AMD64 CPU exception entry (vectors 0-31), Microsoft x64 ABI.
;
; CPU7C1. Each vector has its own IDT target. A vector for which the CPU
; pushes a hardware error code keeps that word. Every other vector pushes a
; synthetic zero in the same slot, so the C++ dispatcher always sees one
; normalized frame (kernel::native_elf::NativeExceptionFrame):
;
;   [frame +   0] rax rcx rdx rbx rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
;   [frame + 120] vector
;   [frame + 128] error code (hardware or synthetic zero)
;   [frame + 136] rip, cs, rflags, rsp, ss   (pushed by the CPU)
;
; Hardware error code: 8 #DF, 10 #TS, 11 #NP, 12 #SS, 13 #GP, 14 #PF,
; 17 #AC, 21 #CP, 29 #VC, 30 #SX. Everything else has none, including
; 16 #MF, 18 #MC, 19 #XM, 20 #VE, 28 #HV and the reserved vectors. This must
; match kernel::native_elf::exception_vector_has_hardware_error_code().
;
; Dispatcher return contract (RAX):
;   0 resume: restore every GPR, drop exactly the vector and error words, IRETQ.
;   1 Native ELF recovery: continue at the recorded kernel continuation.
;   anything else, or recovery without a recorded continuation: fail-stop.
;
; DF is cleared after the interrupted GPRs are saved and before C++ runs. The
; interrupted RFLAGS (including DF) stay in the CPU frame and IRETQ restores
; them on the resume path. IF is already clear because every vector uses an
; interrupt gate; this file does not change that policy.
;

bits 64
default rel

section .text align=16

extern gxos_native_exception_dispatch
extern gxos_exception_stack
extern gxos_native_fault_recovery_rsp
extern gxos_native_fault_recovery_rip

%define GXOS_EXCEPTION_STACK_BYTES 16384
%define GXOS_EXCEPTION_RESUME   0
%define GXOS_EXCEPTION_RECOVER  1

global gxos_amd64_exception_common
global gxos_amd64_exception_entry_begin
global gxos_amd64_exception_entry_end

gxos_amd64_exception_entry_begin:

%macro EXCEPTION_STUB_NO_ERROR 1
global exception_stub_%1
align 16
exception_stub_%1:
    push qword 0                    ; synthetic error code
    push qword %1
    jmp gxos_amd64_exception_common
%endmacro

%macro EXCEPTION_STUB_HW_ERROR 1
global exception_stub_%1
align 16
exception_stub_%1:
    push qword %1                   ; hardware error code is already at [rsp]
    jmp gxos_amd64_exception_common
%endmacro

EXCEPTION_STUB_NO_ERROR 0           ; #DE
EXCEPTION_STUB_NO_ERROR 1           ; #DB
EXCEPTION_STUB_NO_ERROR 2           ; NMI
EXCEPTION_STUB_NO_ERROR 3           ; #BP
EXCEPTION_STUB_NO_ERROR 4           ; #OF
EXCEPTION_STUB_NO_ERROR 5           ; #BR
EXCEPTION_STUB_NO_ERROR 6           ; #UD
EXCEPTION_STUB_NO_ERROR 7           ; #NM
EXCEPTION_STUB_HW_ERROR 8           ; #DF (always zero)
EXCEPTION_STUB_NO_ERROR 9           ; reserved (legacy coprocessor overrun)
EXCEPTION_STUB_HW_ERROR 10          ; #TS
EXCEPTION_STUB_HW_ERROR 11          ; #NP
EXCEPTION_STUB_HW_ERROR 12          ; #SS
EXCEPTION_STUB_HW_ERROR 13          ; #GP
EXCEPTION_STUB_HW_ERROR 14          ; #PF
EXCEPTION_STUB_NO_ERROR 15          ; reserved
EXCEPTION_STUB_NO_ERROR 16          ; #MF
EXCEPTION_STUB_HW_ERROR 17          ; #AC (always zero)
EXCEPTION_STUB_NO_ERROR 18          ; #MC
EXCEPTION_STUB_NO_ERROR 19          ; #XM
EXCEPTION_STUB_NO_ERROR 20          ; #VE (Intel)
EXCEPTION_STUB_HW_ERROR 21          ; #CP
EXCEPTION_STUB_NO_ERROR 22          ; reserved
EXCEPTION_STUB_NO_ERROR 23          ; reserved
EXCEPTION_STUB_NO_ERROR 24          ; reserved
EXCEPTION_STUB_NO_ERROR 25          ; reserved
EXCEPTION_STUB_NO_ERROR 26          ; reserved
EXCEPTION_STUB_NO_ERROR 27          ; reserved
EXCEPTION_STUB_NO_ERROR 28          ; #HV (AMD)
EXCEPTION_STUB_HW_ERROR 29          ; #VC (AMD SEV-ES)
EXCEPTION_STUB_HW_ERROR 30          ; #SX (AMD)
EXCEPTION_STUB_NO_ERROR 31          ; reserved

align 16
gxos_amd64_exception_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rbx
    push rdx
    push rcx
    push rax
    cld                             ; Microsoft x64: DF=0 on every call
    mov r12, rsp                    ; nonvolatile: survives the call
    lea rsp, [gxos_exception_stack + GXOS_EXCEPTION_STACK_BYTES]
    and rsp, -16
    sub rsp, 32                     ; shadow space; RSP is 16-aligned at CALL
    mov rcx, r12
    call gxos_native_exception_dispatch
    cmp rax, GXOS_EXCEPTION_RESUME
    je .resume
    cmp rax, GXOS_EXCEPTION_RECOVER
    jne .fail_stop
    mov rcx, [gxos_native_fault_recovery_rsp]
    mov rdx, [gxos_native_fault_recovery_rip]
    test rcx, rcx
    jz .fail_stop
    test rdx, rdx
    jz .fail_stop
    mov rsp, rcx
    jmp rdx

.resume:
    mov rsp, r12
    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16                     ; vector + (hardware or synthetic) error code
    iretq

.fail_stop:
    cli
.halt:
    hlt
    jmp .halt

gxos_amd64_exception_entry_end:
