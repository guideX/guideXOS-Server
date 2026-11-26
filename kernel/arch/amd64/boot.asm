;
; AMD64 (x86-64) Bootloader Entry Point
;
; Copyright (c) 2024 guideX
;

[BITS 64]

section .boot
global _start

%ifdef GXOS_RAMDISK_INTEGRITY_DIAGNOSTICS
extern kernel_ramdisk_early_entry_checkpoint
%endif

_start:
    ; Disable interrupts
    cli
    
    ; Set up segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Set up stack pointer (will be properly configured by bootloader)
    mov rsp, stack_top
    ; Keep headroom below the fixed stack boundary for the integrated kernel
    ; entry frame's final spill; stack_top itself is one-past the BSS stack.
    sub rsp, 0x20
    cld                         ; C/C++ diagnostics and kernel entry expect DF=0

%ifdef GXOS_RAMDISK_INTEGRITY_DIAGNOSTICS
    ; The UEFI trampoline passes its verified target CR3 in RDX for this
    ; diagnostic-only first-C checkpoint. kernel_main keeps its usual zero
    ; boot_magic for BootInfo boots.
    mov r12, rcx
    mov r13, rdx
    mov rcx, r12
    mov rdx, r13
    call kernel_ramdisk_early_entry_checkpoint
    mov rcx, r12
    xor edx, edx
%endif
    
    ; Jump to kernel main
    extern kernel_main
    call kernel_main
    
    ; Should never reach here
.halt:
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384  ; 16 KB stack
stack_top:
