; Minimal 32-bit Multiboot v1 loader for NDB1R1 QEMU qualification only.
; QEMU's Multiboot loader starts this file in 32-bit protected mode and maps
; the production kernel ELF as one Multiboot module. This shim copies its
; PT_LOAD segments to their linked addresses, enters long mode, and calls the
; kernel entry with the original Multiboot handoff.

[BITS 32]

section .boot progbits alloc exec nowrite align=16
align 4
dd 0x1BADB002
dd 0x00000003
dd -(0x1BADB002 + 0x00000003)

global _start

_start:
    cli
    cmp eax, 0x2BADB002
    jne .failure
    test ebx, ebx
    jz .failure
    mov [loader_multiboot_magic], eax
    mov [loader_multiboot_info], ebx

    ; Multiboot v1 info flags bit 3 announces the module array.
    test dword [ebx], 0x08
    jz .failure
    cmp dword [ebx + 20], 1
    jne .failure
    mov eax, [ebx + 24]
    test eax, eax
    jz .failure
    mov eax, [eax]
    cmp eax, 0x30000000
    jb .failure
    mov [loader_module_start], eax
    mov eax, [ebx + 24]
    mov eax, [eax + 4]
    cmp eax, 0x40000000
    ja .failure
    mov [loader_module_end], eax
    cmp eax, [loader_module_start]
    jbe .failure

    mov esi, [loader_module_start]
    cmp dword [esi], 0x464C457F   ; ELF magic
    jne .failure
    cmp byte [esi + 4], 2        ; ELFCLASS64
    jne .failure
    cmp byte [esi + 5], 1        ; little endian
    jne .failure
    cmp word [esi + 18], 62      ; EM_X86_64
    jne .failure
    cmp dword [esi + 36], 0      ; high half of e_phoff
    jne .failure
    cmp word [esi + 54], 56      ; ELF64 program header size
    jne .failure

    mov eax, [esi + 24]          ; ELF64 entry, low half
    cmp dword [esi + 28], 0
    jne .failure
    mov [loader_kernel_entry], eax
    mov dword [loader_kernel_entry + 4], 0

    mov eax, [esi + 32]          ; e_phoff, low half
    add eax, esi
    jc .failure
    mov [loader_program_header], eax
    movzx eax, word [esi + 56]   ; e_phnum
    test eax, eax
    jz .failure
    mov [loader_headers_remaining], eax
    mov dword [loader_segments_loaded], 0

.load_header:
    mov edi, [loader_program_header]
    cmp dword [edi], 1           ; PT_LOAD
    jne .next_header

    ; The kernel is linked below 768 MiB. Reject any ELF segment that would
    ; overlap this loader or escape the identity-mapped first GiB.
    cmp dword [edi + 28], 0      ; p_paddr high half
    jne .failure
    cmp dword [edi + 12], 0      ; p_offset high half
    jne .failure
    cmp dword [edi + 36], 0      ; p_filesz high half
    jne .failure
    cmp dword [edi + 44], 0      ; p_memsz high half
    jne .failure
    mov eax, [edi + 24]
    cmp eax, 0x30000000
    jae .failure
    mov edx, [edi + 40]
    cmp edx, 0x30000000
    ja .failure
    add edx, eax
    jc .failure
    cmp edx, 0x30000000
    ja .failure
    mov eax, [edi + 32]
    cmp eax, [edi + 40]
    ja .failure

    mov [loader_current_header], edi
    mov esi, [loader_module_start]
    add esi, [edi + 8]
    jc .failure
    mov edi, [loader_current_header]
    mov edi, [edi + 24]
    mov ecx, [loader_current_header]
    mov ecx, [ecx + 32]
    cld
    rep movsb

    ; Recompute the BSS byte count without relying on rep's consumed ECX.
    mov eax, [loader_current_header]
    mov edi, [eax + 24]
    add edi, [eax + 32]
    mov ecx, [eax + 40]
    sub ecx, [eax + 32]
    xor eax, eax
    rep stosb
    inc dword [loader_segments_loaded]

.next_header:
    add dword [loader_program_header], 56
    dec dword [loader_headers_remaining]
    jnz .load_header
    cmp dword [loader_segments_loaded], 1
    jb .failure

    ; Clear and build PML4 -> PDPT -> 512-entry 2 MiB page directory.
    xor eax, eax
    mov edi, loader_pml4
    mov ecx, 3072
    rep stosd
    mov eax, loader_pdpt
    or eax, 0x03
    mov [loader_pml4], eax
    mov dword [loader_pml4 + 4], 0
    mov eax, loader_page_directory
    or eax, 0x03
    mov [loader_pdpt], eax
    mov dword [loader_pdpt + 4], 0
    mov edi, loader_page_directory
    xor ebx, ebx
    mov ecx, 512
.map_first_gib:
    mov eax, ebx
    or eax, 0x83
    mov [edi], eax
    mov dword [edi + 4], 0
    add ebx, 0x200000
    add edi, 8
    loop .map_first_gib

    mov eax, cr4
    or eax, 0x20                 ; CR4.PAE
    mov cr4, eax
    mov eax, loader_pml4
    mov cr3, eax
    mov ecx, 0xC0000080          ; IA32_EFER
    rdmsr
    or eax, 0x100                ; EFER.LME
    wrmsr
    lgdt [loader_gdt_descriptor]
    mov eax, cr0
    or eax, 0x80000000           ; CR0.PG
    mov cr0, eax
    jmp 0x08:loader_long_mode

.failure:
    mov dx, 0x00F4
    mov eax, 0x11
    out dx, eax
.halt:
    cli
    hlt
    jmp .halt

[BITS 64]
loader_long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, loader_stack_top
    and esp, -16
    sub esp, 0x20                ; Microsoft x64 shadow space
    mov ecx, [loader_multiboot_info]
    mov edx, [loader_multiboot_magic]
    mov rax, [loader_kernel_entry]
    call rax
loader_returned:
    mov dx, 0x00F4
    mov eax, 0x11
    out dx, eax
    jmp loader_returned

section .rodata
align 8
loader_gdt:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
loader_gdt_end:
loader_gdt_descriptor:
    dw loader_gdt_end - loader_gdt - 1
    dd loader_gdt

section .bss nobits alloc write align=4096
alignb 4096
loader_pml4:
    resb 4096
loader_pdpt:
    resb 4096
loader_page_directory:
    resb 4096
loader_multiboot_magic:
    resd 1
loader_multiboot_info:
    resd 1
loader_module_start:
    resd 1
loader_module_end:
    resd 1
loader_program_header:
    resd 1
loader_current_header:
    resd 1
loader_headers_remaining:
    resd 1
loader_segments_loaded:
    resd 1
alignb 8
loader_kernel_entry:
    resq 1
alignb 16
loader_stack_bottom:
    resb 16384
loader_stack_top:
