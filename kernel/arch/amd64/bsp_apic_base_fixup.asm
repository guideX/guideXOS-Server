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
%define UNSUPPORTED_FLAGS ((1 << 8) | (1 << 14) | (1 << 17)) ; TF, NT, VM
%define INTERRUPT_FLAG (1 << 9)

; Raw 64-bit #GP frame at entry, including the hardware error code and SS:RSP.
%macro match_apic_base_gp 4 ; frame, expected RSP (scratch), MSR index, reject
    test %2, %2
    jz %4
    cmp %3, APIC_BASE_MSR
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

; RCX=output, RDX=temporary IDTR, R8=expected kernel IDTR. The caller holds
; exclusive ownership. Status in EAX: 0 observed, 1 context rejected, 2 #GP.
gxos_amd64_read_boot_apic_base:
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

    mov r8, rcx
    lidt [rdx]
    mov ecx, APIC_BASE_MSR
    mov [rel apic_base_probe_rsp], rsp
apic_base_rdmsr:
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov r9, rax
    xor eax, eax
    jmp apic_base_finish

apic_base_fault:
    mov eax, 2
apic_base_finish:
    mov qword [rel apic_base_probe_rsp], 0
    lidt [rsp]
    test eax, eax
    jnz apic_base_return
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

; Installed only at vector 13 in the temporary IDT. Leave the hardware frame,
; registers and flags intact on rejection, for the original #GP entry.
gxos_amd64_apic_base_gp:
    pushfq
    push rax
    push r10
    lea r10, [rsp + 24]
    mov rax, [rel apic_base_probe_rsp]
    match_apic_base_gp r10, rax, ecx, .unrelated
    lea rax, [rel apic_base_fault]
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
gxos_test_apic_base_gp_matches:
    xor eax, eax
    cmp r9d, 13
    jne .done
    match_apic_base_gp rcx, rdx, r8d, .done
    mov eax, 1
.done:
    ret

gxos_test_apic_base_fault_rip:
    lea rax, [rel apic_base_rdmsr]
    ret
%endif

section .bss align=8
apic_base_probe_rsp: resq 1
