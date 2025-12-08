;
; AMD64 (x86-64) Bootloader Entry Point
;
; Copyright (c) 2024 guideX
;

[BITS 64]

section .boot
global _start

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
    
    ; Clear direction flag
    cld
    
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
