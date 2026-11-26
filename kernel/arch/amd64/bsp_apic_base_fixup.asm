; Cold BSP APIC_BASE read. Microsoft x64 ABI, no C++ on the fixup path.
bits 64
section .text align=16

global gxos_amd64_read_boot_apic_base
global gxos_amd64_apic_base_gp
global gxos_amd64_apic_base_emergency_stop
extern exception_stub_13

%define APIC_BASE_MSR 0x1b
%define KERNEL_CS 0x08
%define KERNEL_SS 0x10
%define UNSUPPORTED_FLAGS ((1 << 8) | (1 << 10) | (1 << 14) | (1 << 17)) ; TF, DF, NT, VM
%define INTERRUPT_FLAG (1 << 9)
; Only the architectural IA32_APIC_BASE index may publish a value. Every
; qualification index (reserved or x2APIC-reserved) is fail-closed even when a
; hypervisor or emulator returns from RDMSR instead of delivering #GP(0).
%macro require_publishable_index 2 ; MSR index register (32-bit), reject
    cmp %1, APIC_BASE_MSR
    jne %2
%endmacro

%macro require_probe_context_idle 1
    cmp qword [rel apic_base_probe_rsp], 0
    jne %1
    cmp dword [rel apic_base_probe_msr], 0
    jne %1
%endmacro

; Raw 64-bit #GP frame at entry, including the hardware error code and SS:RSP.
%macro match_apic_base_gp 4 ; frame, expected RSP (scratch), MSR index register, reject
    test %2, %2
    jz %4
    cmp %3, dword [rel apic_base_probe_msr]
    jne %4
    cmp qword [%1], 0
    jne %4
    cmp qword [%1 + 16], KERNEL_CS
    jne %4
    test qword [%1 + 24], UNSUPPORTED_FLAGS | INTERRUPT_FLAG
    jnz %4
    cmp qword [%1 + 32], %2
    jne %4
    cmp qword [%1 + 40], KERNEL_SS
    jne %4
    lea %2, [rel apic_base_rdmsr]
    cmp qword [%1 + 8], %2
    jne %4
%endmacro

; RCX=output, RDX=temporary IDTR, R8=expected kernel IDTR, R9D=MSR index.
; The caller holds exclusive ownership. Status in EAX: 0 observed,
; 1 context rejected, 2 #GP. Only an index 0x1B result is ever published.
gxos_amd64_read_boot_apic_base:
    require_probe_context_idle .reject_unarmed
    pushfq
    mov ax, cs
    cmp ax, KERNEL_CS
    jne .reject
    mov ax, ss
    cmp ax, KERNEL_SS
    jne .reject
    test qword [rsp], UNSUPPORTED_FLAGS
    jnz .reject
    cli
    mov rax, cr4
    bt rax, 23                      ; CET can compare the saved RIP to a shadow stack.
    jc .reject
    bt rax, 32                      ; FRED has a different event frame.
    jc .reject
    sub rsp, 16
    sidt [rsp]
    mov ax, [rsp]
    cmp ax, [r8]
    jne .reject_idt
    mov rax, [rsp + 2]
    cmp rax, [r8 + 2]
    jne .reject_idt

    mov qword [rel gxos_amd64_apic_base_gp_evidence + 0], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 8], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 16], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 24], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 32], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 40], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 48], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 56], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 64], 0
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 72], 0
    mov r11d, r9d
    mov r8, rcx
    lidt [rdx]
    mov dword [rel apic_base_probe_msr], r11d
    mov ecx, dword [rel apic_base_probe_msr]
    mov [rel apic_base_probe_rsp], rsp
global gxos_amd64_apic_base_rdmsr
gxos_amd64_apic_base_rdmsr:
apic_base_rdmsr:
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov r9, rax
    xor eax, eax
    jmp apic_base_finish

global gxos_amd64_apic_base_fault
gxos_amd64_apic_base_fault:
apic_base_fault:
    ; Record that IRETQ restored the armed stack and the handler preserved the
    ; index registers. Uses only volatile R10 before the common cleanup.
    xor r10d, r10d
    cmp rsp, [rel apic_base_probe_rsp]
    jne .rsp_checked
    or r10d, 1
.rsp_checked:
    cmp r11d, dword [rel apic_base_probe_msr]
    jne .r11_checked
    or r10d, 2
.r11_checked:
    cmp ecx, dword [rel apic_base_probe_msr]
    jne .ecx_checked
    or r10d, 4
.ecx_checked:
    mov [rel gxos_amd64_apic_base_gp_evidence + 72], r10
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 64], 1
    mov eax, 2
apic_base_finish:
    mov qword [rel apic_base_probe_rsp], 0
    mov dword [rel apic_base_probe_msr], 0
    lidt [rsp]
    test eax, eax
    jnz apic_base_return
    cmp dword [rel gxos_amd64_apic_base_gp_evidence + 0], 0
    jne apic_base_return
    require_publishable_index r11d, apic_base_return
    mov [r8], r9
apic_base_return:
    add rsp, 16
    popfq
    ret

gxos_amd64_read_boot_apic_base.reject_idt:
    add rsp, 16
gxos_amd64_read_boot_apic_base.reject:
    mov eax, 1
    popfq
    ret
gxos_amd64_read_boot_apic_base.reject_unarmed:
    mov eax, 1
    ret

; Installed only at vector 13 in the temporary IDT. Leave the hardware frame,
; registers and flags intact on rejection, for the original #GP entry.
gxos_amd64_apic_base_gp:
    pushfq
    push rax
    push r10
    lea r10, [rsp + 24]
    mov rax, [rel apic_base_probe_rsp]
    match_apic_base_gp r10, rax, ecx, .unrelated
    mov qword [rel gxos_amd64_apic_base_gp_evidence + 0], 13
    mov rax, [r10]
    mov [rel gxos_amd64_apic_base_gp_evidence + 8], rax
    mov rax, [r10 + 8]
    mov [rel gxos_amd64_apic_base_gp_evidence + 16], rax
    mov rax, [r10 + 16]
    mov [rel gxos_amd64_apic_base_gp_evidence + 24], rax
    mov rax, [r10 + 24]
    mov [rel gxos_amd64_apic_base_gp_evidence + 32], rax
    mov rax, [r10 + 32]
    mov [rel gxos_amd64_apic_base_gp_evidence + 40], rax
    mov rax, [r10 + 40]
    mov [rel gxos_amd64_apic_base_gp_evidence + 48], rax
    lea rax, [rel apic_base_fault]
    mov [rel gxos_amd64_apic_base_gp_evidence + 56], rax
    mov [r10 + 8], rax
    pop r10
    pop rax
    popfq
    lea rsp, [rsp + 8]              ; Discard only the hardware #GP error code.
    iretq
.unrelated:
    pop r10
    pop rax
    popfq
    jmp exception_stub_13

; Separate ISTs enter this no-return, no-stack-use halt while the temporary
; IDT is active. It neither runs diagnostics on the PIC stack nor resumes NMI.
gxos_amd64_apic_base_emergency_stop:
    cli
.halt:
    hlt
    jmp .halt

%ifdef GXOS_APIC_BASE_HOSTED_TEST
; Hosted frame fixture exercises the exact comparisons used by the gate.
; RCX=frame, RDX=armed RSP, R8D=MSR, R9D=vector. No privileged instructions.
global gxos_test_apic_base_gp_matches
global gxos_test_apic_base_fault_rip
global gxos_test_apic_base_probe_state_clean
global gxos_test_apic_base_probe_context_idle
global gxos_test_apic_base_probe_set_context
global gxos_test_apic_base_publishes_index
gxos_test_apic_base_gp_matches:
    xor eax, eax
    cmp r9d, 13
    jne .done
    mov dword [rel apic_base_probe_msr], APIC_BASE_MSR
    match_apic_base_gp rcx, rdx, r8d, .done
    mov eax, 1
.done:
    mov dword [rel apic_base_probe_msr], 0
    ret

gxos_test_apic_base_fault_rip:
    lea rax, [rel apic_base_rdmsr]
    ret

gxos_test_apic_base_probe_state_clean:
    xor eax, eax
    cmp qword [rel apic_base_probe_rsp], 0
    jne .state_done
    cmp dword [rel apic_base_probe_msr], 0
    jne .state_done
    mov eax, 1
.state_done:
    ret

gxos_test_apic_base_probe_context_idle:
    xor eax, eax
    require_probe_context_idle .context_done
    mov eax, 1
.context_done:
    ret

gxos_test_apic_base_probe_set_context:
    mov [rel apic_base_probe_rsp], rcx
    mov [rel apic_base_probe_msr], edx
    ret

; RCX=MSR index. Returns 1 only if the reader would publish a value for it.
gxos_test_apic_base_publishes_index:
    xor eax, eax
    mov r11d, ecx
    require_publishable_index r11d, .publish_done
    mov eax, 1
.publish_done:
    ret
%endif

section .bss align=8
global apic_base_probe_rsp
apic_base_probe_rsp: resq 1
global apic_base_probe_msr
apic_base_probe_msr: resd 1
alignb 8
global gxos_amd64_apic_base_gp_evidence
gxos_amd64_apic_base_gp_evidence: resq 10
