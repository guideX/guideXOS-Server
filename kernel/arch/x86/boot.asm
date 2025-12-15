;
; x86 (32-bit) Bootloader Entry Point
;
; Copyright (c) 2024 guideX
;

[BITS 32]

section .multiboot
align 4
    ; Multiboot header
    dd 0x1BADB002              ; Magic number
    dd 0x00000003              ; Flags (bit 0: mem, bit 1: boot device)
    dd -(0x1BADB002 + 0x00000003)    ; Checksum

section .boot
global _start

_start:
    ; Disable interrupts
    cli
    
    ; Set up segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Set up stack pointer
    mov esp, stack_top
    
    ; Clear direction flag
    cld
    
    ; Push multiboot information (EBX contains multiboot info structure)
    push ebx
    push eax
    
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
