; Minimal UEFI -> kernel handoff trampoline (x86_64, MS x64 ABI)
; Assembled with NASM: nasm -f win64 handoff_trampoline.asm -o handoff_trampoline.obj
;
; Extern C signature:
;   void BootHandoffTrampoline(void* kernelEntry, void* bootInfo, void* stackTop, void* pml4Phys);
;
; Behavior:
;   - cli
;   - Save parameters BEFORE any stack/CR3 changes
;   - Load cr3 = pml4Phys (BEFORE stack switch, since trampoline must be mapped)
;   - rsp = stackTop (16-byte aligned)
;   - jmp kernelEntry(bootInfo) using MS x64 ABI (RCX=bootInfo, 32-byte shadow space)
;
; CRITICAL: The trampoline code AND the new stack must both be identity-mapped
;           in the new page tables before calling this function!

BITS 64
DEFAULT REL

global BootHandoffTrampoline
global SetupTrampoline
global GetTrampolineCodeSize

section .text

; void BootHandoffTrampoline(void* kernelEntry, void* bootInfo, void* stackTop, void* pml4Phys);
BootHandoffTrampoline:
    ; Windows x64 calling convention on entry:
    ;   RCX = kernelEntry
    ;   RDX = bootInfo
    ;   R8  = stackTop
    ;   R9  = pml4Phys

    cli                         ; Disable interrupts - no going back

    ; === Stage 1: Save all parameters in non-volatile registers FIRST ===
    ; We must do this BEFORE touching stack or CR3!
    mov r12, rcx                ; r12 = kernelEntry
    mov r13, rdx                ; r13 = bootInfo
    mov r14, r8                 ; r14 = stackTop
    mov r15, r9                 ; r15 = pml4Phys

    ; --- Breadcrumb: 'T' = Trampoline entry ---
    mov dx, 03F8h
.wait_T:
    add dx, 5                   ; 0x3FD = line status
    in al, dx
    test al, 20h
    jz .wait_T
    sub dx, 5                   ; back to 0x3F8
    mov al, 'T'
    out dx, al

    ; === Stage 2: Load CR3 with new page tables ===
    ; Do this BEFORE stack switch! The trampoline code is executing from
    ; memory that must be identity-mapped in both old and new page tables.
    test r15, r15               ; Check if pml4Phys is NULL
    jz .skip_cr3                ; If NULL, keep current page tables

    mov rax, r15
    mov cr3, rax                ; Load new page tables (TLB flush)

    ; --- Breadcrumb: '3' = CR3 loaded ---
    mov dx, 03F8h
.wait_3:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_3
    sub dx, 5
    mov al, '3'
    out dx, al

.skip_cr3:
    ; === Stage 3: Switch to new stack ===
    ; Now that we have new page tables, switch to the new stack
    ; (which must be mapped in the new page tables)
    mov rsp, r14                ; rsp = stackTop
    and rsp, ~0Fh               ; Ensure 16-byte alignment

    ; --- Breadcrumb: 'S' = Stack switched ---
    mov dx, 03F8h
.wait_S:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_S
    sub dx, 5
    mov al, 'S'
    out dx, al

    ; === Stage 4: Set up kernel call ===
    ; MS x64 ABI: RCX = first parameter (bootInfo)
    mov rcx, r13                ; rcx = bootInfo

    ; Allocate 32-byte shadow space (required by MS x64 ABI)
    ; Stack is 16-byte aligned, sub 32 keeps it aligned
    sub rsp, 20h

    ; Clear other parameter registers (not strictly necessary but clean)
    xor rdx, rdx
    xor r8, r8
    xor r9, r9

    ; --- Breadcrumb: 'J' = About to Jump ---
    mov dx, 03F8h
.wait_J:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_J
    sub dx, 5
    mov al, 'J'
    out dx, al

    ; --- Debug: Print the target address (r12) as hex ---
    ; Print low 32 bits (should be enough to identify virtual vs physical)
    mov r10, r12                ; Save r12 to r10 (we'll use this as the value to print)
    
    ; Print each nibble (8 hex digits for low 32 bits)
    mov r11d, 8                 ; Use r11 as loop counter (8 nibbles)
.print_addr:
    rol r10d, 4                 ; Rotate left, bringing high nibble to low position
    mov al, r10b
    and al, 0Fh
    add al, '0'
    cmp al, '9'
    jbe .digit_ok
    add al, 7                   ; 'A'-'9'-1 = 7
.digit_ok:
    ; Wait for serial ready
    mov dx, 03FDh
.wait_addr:
    push rax                    ; Save the digit
    in al, dx
    test al, 20h
    pop rax                     ; Restore the digit  
    jz .wait_addr
    ; Output the digit
    mov dx, 03F8h
    out dx, al
    dec r11d
    jnz .print_addr

    ; Print newline
.wait_nl:
    mov dx, 03FDh
    in al, dx
    test al, 20h
    jz .wait_nl
    mov dx, 03F8h
    mov al, 0Dh                 ; CR
    out dx, al
.wait_nl2:
    mov dx, 03FDh
    in al, dx
    test al, 20h
    jz .wait_nl2
    mov dx, 03F8h
    mov al, 0Ah                 ; LF
    out dx, al

    ; === Stage 5: Jump to kernel ===
    ; IMPORTANT: Before jumping, verify we can write to the framebuffer
    ; This proves our page tables work for the framebuffer region
    
    ; Write CYAN pixels at y=45 (above where kernel would write at y=50)
    ; Framebuffer at 0x80000000, pitch 1280 pixels, y=45 means offset 45*1280=57600 pixels
    ; Each pixel is 4 bytes, so byte offset = 57600*4 = 230400 = 0x38400
    mov rax, 0x80000000
    add rax, 0x38400        ; y=45, x=0
    mov ecx, 0x00FFFF00     ; CYAN color (BGR format: yellow actually)
    mov dword [rax], ecx    ; pixel at y=45, x=0
    mov dword [rax+4], ecx  ; pixel at y=45, x=1
    mov dword [rax+8], ecx  ; pixel at y=45, x=2
    mov dword [rax+12], ecx ; pixel at y=45, x=3
    mov dword [rax+16], ecx ; pixel at y=45, x=4
    
    ; Write 'F' to serial to confirm framebuffer write completed
    mov dx, 03F8h
.wait_F:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_F
    sub dx, 5
    mov al, 'F'
    out dx, al
    
    ; Write 'G' to serial to show we're about to jump to kernel
    mov dx, 03F8h
.wait_G:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_G
    sub dx, 5
    mov al, 'G'
    out dx, al
    
    ; Write newline before jumping
    mov dx, 03F8h
.wait_nl3:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_nl3
    sub dx, 5
    mov al, 0Dh
    out dx, al
.wait_nl4:
    mov dx, 03FDh
    in al, dx
    test al, 20h
    jz .wait_nl4
    mov dx, 03F8h
    mov al, 0Ah
    out dx, al
    
    ; === INLINE TEST: Try to execute at the kernel entry and see what happens ===
    ; Instead of jumping directly, let's manually execute the prologue
    ; and see if we can at least push to the stack
    
    ; Save the entry point for later
    push r12                    ; Save kernel entry on stack
    
    ; Write 'P' = about to test prologue manually
    mov dx, 03F8h
.wait_P:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_P
    sub dx, 5
    mov al, 'P'
    out dx, al
    
    ; Read the first byte at the kernel entry point
    mov rax, r12                ; RAX = kernel entry
    mov al, [rax]               ; Try to READ from kernel entry
    
    ; Write 'R' = read succeeded
    mov dx, 03F8h
.wait_R:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_R
    sub dx, 5
    mov al, 'R'
    out dx, al
    
    ; === Debug: Read and print the actual PTE for the kernel entry ===
    ; PML4 is in r15, kernel entry is in r12
    ; We need to walk the page tables to find the PTE
    ; Virtual address breakdown for r12:
    ;   PML4 index = bits 47:39 = (r12 >> 39) & 0x1FF
    ;   PDPT index = bits 38:30 = (r12 >> 30) & 0x1FF
    ;   PD index   = bits 29:21 = (r12 >> 21) & 0x1FF
    ;   PT index   = bits 20:12 = (r12 >> 12) & 0x1FF
    
    ; Get PML4 entry
    mov rax, r12
    shr rax, 39
    and rax, 0x1FF
    shl rax, 3                  ; multiply by 8 (entry size)
    add rax, r15                ; add PML4 base
    mov rbx, [rax]              ; rbx = PML4 entry
    
    ; Get PDPT base from PML4 entry
    mov rax, rbx
    and rax, 0x000FFFFFFFFFF000 ; mask to get physical address
    ; rax = PDPT physical base
    
    ; Get PDPT index
    mov rcx, r12
    shr rcx, 30
    and rcx, 0x1FF
    shl rcx, 3
    add rax, rcx
    mov rbx, [rax]              ; rbx = PDPT entry
    
    ; Get PD base from PDPT entry
    mov rax, rbx
    and rax, 0x000FFFFFFFFFF000
    
    ; Get PD index
    mov rcx, r12
    shr rcx, 21
    and rcx, 0x1FF
    shl rcx, 3
    add rax, rcx
    mov rbx, [rax]              ; rbx = PD entry
    
    ; Get PT base from PD entry
    mov rax, rbx
    and rax, 0x000FFFFFFFFFF000
    
    ; Get PT index
    mov rcx, r12
    shr rcx, 12
    and rcx, 0x1FF
    shl rcx, 3
    add rax, rcx
    mov rbx, [rax]              ; rbx = PT entry (the actual PTE!)
    
    ; Print 'E' then the full 64-bit PTE value
    mov dx, 03F8h
.wait_E:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_E
    sub dx, 5
    mov al, 'E'
    out dx, al
    
    ; Print the PTE as 16 hex digits
    mov r10, rbx                ; r10 = PTE value to print
    mov r11d, 16                ; 16 nibbles for 64 bits
.print_pte:
    rol r10, 4                  ; Rotate left to get next nibble
    mov al, r10b
    and al, 0Fh
    add al, '0'
    cmp al, '9'
    jbe .pte_digit_ok
    add al, 7
.pte_digit_ok:
    mov dx, 03FDh
.wait_pte:
    push rax
    in al, dx
    test al, 20h
    pop rax
    jz .wait_pte
    mov dx, 03F8h
    out dx, al
    dec r11d
    jnz .print_pte
    
    ; Print newline
    mov dx, 03FDh
.wait_pte_nl:
    in al, dx
    test al, 20h
    jz .wait_pte_nl
    mov dx, 03F8h
    mov al, 0Dh
    out dx, al
.wait_pte_nl2:
    mov dx, 03FDh
    in al, dx
    test al, 20h
    jz .wait_pte_nl2
    mov dx, 03F8h
    mov al, 0Ah
    out dx, al
    
    ; Restore kernel entry
    pop r12
    
    ; === FINAL TEST: Use CALL instead of JMP ===
    ; If the kernel hangs immediately, the return address won't matter.
    ; But if it does something and crashes, we might see output.
    ; Also, let's write a marker RIGHT BEFORE the jump.
    
    ; Print '*' right before jumping
    mov dx, 03F8h
.wait_star:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_star
    sub dx, 5
    mov al, '*'
    out dx, al
    
    ; Restore RCX = bootInfo for kernel (we clobbered it during page table walk)
    mov rcx, r13
    
    ; Use CALL instead of JMP - this puts a return address on stack
    ; If the kernel ever returns (it shouldn't), we'll catch it
    call r12
    
    ; If we get here, kernel returned (shouldn't happen)
    mov dx, 03F8h
.wait_ret:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_ret
    sub dx, 5
    mov al, 'X'         ; 'X' = kernel returned
    out dx, al
    jmp .hang

    ; --- Breadcrumb: '?' = Jump didn't work (should never reach here) ---
    mov dx, 03F8h
.wait_Q:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_Q
    sub dx, 5
    mov al, '?'
    out dx, al

    ; === PANIC: Should never reach here ===
.hang:
    mov dx, 03F8h
.wait_X:
    add dx, 5
    in al, dx
    test al, 20h
    jz .wait_X
    sub dx, 5
    mov al, '!'
    out dx, al
    
    hlt
    jmp .hang

; Stub functions for compatibility with trampoline_msvc.cpp interface
; (These are no-ops since the code is already in executable memory)
SetupTrampoline:
    ret

GetTrampolineCodeSize:
    mov rax, 256                ; Return a reasonable size estimate
    ret
