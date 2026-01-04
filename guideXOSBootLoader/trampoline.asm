; trampoline.asm - Boot handoff trampoline for guideXOS
; Assembled with NASM: nasm -f win64 trampoline.asm -o trampoline.obj
;
; Microsoft x64 ABI (caller provides):
;   RCX = kernelEntry
;   RDX = bootInfo
;   R8  = stackTop
;   R9  = pml4Phys
;
; Kernel expects MS x64 ABI:
;   RCX = BootInfo*

BITS 64
DEFAULT REL

section .text

; Debug: Output character to COM1 serial port
%macro SERIAL_CHAR 1
    push    rax
    push    rdx
    mov     dx, 0x3FD
%%wait_tx:
    in      al, dx
    test    al, 0x20
    jz      %%wait_tx
    mov     dx, 0x3F8
    mov     al, %1
    out     dx, al
    pop     rdx
    pop     rax
%endmacro

global BootHandoffTrampoline
BootHandoffTrampoline:
    ; RCX = kernelEntry, RDX = bootInfo, R8 = stackTop, R9 = pml4Phys
    cli
    SERIAL_CHAR 'T'

    ; Save parameters in callee-saved registers BEFORE any modifications
    mov     r12, rcx            ; r12 = kernelEntry (PRESERVE THIS!)
    mov     r13, rdx            ; r13 = bootInfo
    mov     r14, r8             ; r14 = stackTop
    mov     r15, r9             ; r15 = pml4Phys

    SERIAL_CHAR '1'

    ; Validate parameters
    test    r12, r12
    jz      .panic_null_entry
    test    r13, r13
    jz      .panic_null_bootinfo
    test    r14, r14
    jz      .panic_null_stack

    SERIAL_CHAR '2'

    ; Load new page tables if provided
    test    r15, r15
    jz      .skip_cr3
    mov     rax, r15
    mov     cr3, rax
.skip_cr3:

    SERIAL_CHAR '3'

    ; Switch to new stack (must be identity-mapped in new page tables)
    mov     rsp, r14
    and     rsp, ~0xF           ; 16-byte align

    ; MS x64 ABI: RSP % 16 == 8 at function entry (after CALL pushes return addr)
    ; Since we JMP (no push), subtract 40 = 32 shadow + 8 fake return
    sub     rsp, 40

    SERIAL_CHAR 'S'

    ; Set up kernel argument: RCX = BootInfo*
    mov     rcx, r13

    ; Clear other argument registers
    xor     rdx, rdx
    xor     r8,  r8
    xor     r9,  r9

    ; Clear volatile registers (but NOT r12 which holds kernel entry!)
    xor     rax, rax
    xor     r10, r10
    xor     r11, r11

    SERIAL_CHAR 'J'
    SERIAL_CHAR 10

    ; Jump to kernel - r12 still contains the entry point
    jmp     r12

.panic_null_entry:
    SERIAL_CHAR 'E'
    SERIAL_CHAR '1'
    jmp     .halt

.panic_null_bootinfo:
    SERIAL_CHAR 'E'
    SERIAL_CHAR '2'
    jmp     .halt

.panic_null_stack:
    SERIAL_CHAR 'E'
    SERIAL_CHAR '3'
    jmp     .halt

.halt:
    SERIAL_CHAR '!'
    cli
    hlt
    jmp     .halt
