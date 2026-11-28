;
; CPU7C1 opt-in guest self-test of the production exception entry
; (EXCEPTION_ENTRY_DIAGNOSTIC=1 only; the stock build does not assemble this).
;
; Runs once on the BSP during cold interrupt initialization, after the live
; IDT is loaded and before STI. Each probe loads known values into every GPR,
; sets DF, and raises one exception through the live IDT and the production
; exception_stub_N / common entry:
;
;   #BP  INT3 (vector 3, trap, no hardware error code: synthetic zero)
;   #GP  RDMSR of reserved x2APIC MSR 0x800 (vector 13, hardware error 0;
;        TCG delivers it, QEMU 11.0.0 WHPX returns 0 and nothing is raised)
;
; The armed C++ intercept in the dispatcher accepts only the exact RIP and
; RSP below and returns the resume disposition (for #GP after moving the saved
; RIP past the RDMSR). The production stub then IRETQs here. The probe records
; every GPR, the resumed RFLAGS and RSP, and returns with DF clear and the
; caller's flags restored.
;
; Record layout (ExceptionEntrySelftestRecord in interrupts.cpp):
;   +0   gprs[15] after resume (rax rcx rdx rbx rbp rsi rdi r8..r15)
;   +120 rflags after resume
;   +128 rsp after resume
;   +136 armed rsp
;   +144 reached continuation (1) or RDMSR returned (2)
;

bits 64
default rel

section .text align=16

global gxos_amd64_selftest_bp
global gxos_amd64_selftest_bp_resume
global gxos_amd64_selftest_gp
global gxos_amd64_selftest_gp_rdmsr
global gxos_amd64_selftest_gp_continuation
global gxos_amd64_selftest_armed_rsp

%define REC_RFLAGS  120
%define REC_RSP     128
%define REC_ARMED   136
%define REC_REACHED 144

%macro SELFTEST_PROLOGUE 0
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    pushfq
    mov r15, rcx                    ; record pointer, preserved by the entry
%endmacro

; Known values for every GPR other than R15 (record) and RSP.
%macro SELFTEST_LOAD_PATTERN 0
    mov rax, 0x7C1A000000000000
    mov rbx, 0x7C1A000000000003
    mov rdx, 0x7C1A000000000002
    mov rbp, 0x7C1A000000000004
    mov rsi, 0x7C1A000000000005
    mov rdi, 0x7C1A000000000006
    mov r8,  0x7C1A000000000007
    mov r9,  0x7C1A000000000008
    mov r10, 0x7C1A000000000009
    mov r11, 0x7C1A00000000000A
    mov r12, 0x7C1A00000000000B
    mov r13, 0x7C1A00000000000C
    mov r14, 0x7C1A00000000000D
%endmacro

%macro SELFTEST_RECORD 0
    ; Flags first: nothing above touched them since the IRETQ.
    pushfq
    pop qword [r15 + REC_RFLAGS]
    cld
    mov [r15 + REC_RSP], rsp
    mov [r15 + 0], rax
    mov [r15 + 8], rcx
    mov [r15 + 16], rdx
    mov [r15 + 24], rbx
    mov [r15 + 32], rbp
    mov [r15 + 40], rsi
    mov [r15 + 48], rdi
    mov [r15 + 56], r8
    mov [r15 + 64], r9
    mov [r15 + 72], r10
    mov [r15 + 80], r11
    mov [r15 + 88], r12
    mov [r15 + 96], r13
    mov [r15 + 104], r14
    mov [r15 + 112], r15
    xor eax, eax
    mov [gxos_amd64_selftest_armed_rsp], rax
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
%endmacro

; void gxos_amd64_selftest_bp(ExceptionEntrySelftestRecord* rcx)
gxos_amd64_selftest_bp:
    SELFTEST_PROLOGUE
    SELFTEST_LOAD_PATTERN
    mov rcx, 0x7C1A000000000001
    mov [gxos_amd64_selftest_armed_rsp], rsp
    mov [r15 + REC_ARMED], rsp
    std
    int3
gxos_amd64_selftest_bp_resume:      ; #BP is a trap: saved RIP is this label
    mov qword [r15 + REC_REACHED], 1
    SELFTEST_RECORD

; void gxos_amd64_selftest_gp(ExceptionEntrySelftestRecord* rcx)
gxos_amd64_selftest_gp:
    SELFTEST_PROLOGUE
    SELFTEST_LOAD_PATTERN
    mov rcx, 0x0000000000000800     ; reserved x2APIC MSR; ECX is the index
    mov [gxos_amd64_selftest_armed_rsp], rsp
    mov [r15 + REC_ARMED], rsp
    std
gxos_amd64_selftest_gp_rdmsr:       ; #GP is a fault: saved RIP is the RDMSR
    rdmsr
    mov qword [r15 + REC_REACHED], 2   ; not delivered (RDX:RAX returned)
    SELFTEST_RECORD
gxos_amd64_selftest_gp_continuation:
    mov qword [r15 + REC_REACHED], 1
    SELFTEST_RECORD

section .bss align=16
gxos_amd64_selftest_armed_rsp: resq 1
