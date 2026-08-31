bits 64
section .text

global native_elf_trampoline_test_entry

native_elf_trampoline_test_entry:
    ; Deliberately change the compiler-private registers. The trampoline must
    ; restore the caller's original nonvolatile values after this return.
    mov     r14, 0x1122334455667788
    mov     r15, 0x8877665544332211
    mov     r14, 0
    mov     r15, 0
    mov     eax, 42
    ret
