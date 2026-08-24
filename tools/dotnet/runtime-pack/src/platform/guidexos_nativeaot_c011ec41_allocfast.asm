; C011EC41 diagnostic interposition around the locked NativeAOT array fast entry.
; The allocation arithmetic, Thread allocation-context bump, and rare branch
; mirror locked Runtime/amd64/AllocFast.asm.  The two calls are fixed-storage
; diagnostics only; they do not alter the context, heap, segment, or policy.

OPTION CASEMAP:NONE

EXTERN  tls_CurrentThread:DWORD
EXTERN  RhpNewArrayRare:PROC
EXTERN  RhExceptionHandling_FailedAllocation:PROC
EXTERN  guideXosNativeAotC011EC41NativeHelperEntry:PROC
EXTERN  guideXosNativeAotC011EC41NativeAfterAllocation:PROC

PUBLIC  RhpNewArray

_TEXT SEGMENT

RhpNewArray PROC
        ; RCX == MethodTable, RDX == element count.
        cmp         rdx, 07fffffffh
        ja          ArraySizeOverflow

        mov         r8, rdx
        movzx       eax, word ptr [rcx]
        mul         rdx
        mov         edx, dword ptr [rcx + 4]
        add         rax, rdx
        add         rax, 7
        and         rax, -8
        mov         rdx, r8

        ; The locked entry has derived the aligned object size.  Preserve its
        ; volatile inputs across the diagnostic callback and continue with the
        ; unchanged context-bump sequence below.
        sub         rsp, 38h
        mov         qword ptr [rsp + 20h], rcx
        mov         qword ptr [rsp + 28h], rdx
        mov         qword ptr [rsp + 30h], rax
        mov         rcx, rax
        call        guideXosNativeAotC011EC41NativeHelperEntry
        mov         rax, qword ptr [rsp + 30h]
        mov         rdx, qword ptr [rsp + 28h]
        mov         rcx, qword ptr [rsp + 20h]
        add         rsp, 38h

        ; INLINE_GETTHREAD r10, r8 (locked AllocFast.asm equivalent).
        mov         r10d, dword ptr [tls_CurrentThread]
        mov         r8, qword ptr gs:[58h]
        mov         r8, qword ptr [r8 + r10 * 8]
        xor         r10d, r10d
        add         r10, r8

        mov         r8, rax
        add         rax, qword ptr [r10]
        jc          RhpNewArrayRare
        cmp         rax, qword ptr [r10 + 8]
        ja          RhpNewArrayRare

        mov         qword ptr [r10], rax
        sub         rax, r8
        mov         qword ptr [rax], rcx
        mov         dword ptr [rax + 8], edx

        sub         rsp, 28h
        mov         qword ptr [rsp + 20h], rax
        mov         rcx, rax
        call        guideXosNativeAotC011EC41NativeAfterAllocation
        mov         rax, qword ptr [rsp + 20h]
        add         rsp, 28h
        ret

ArraySizeOverflow:
        mov         edx, 1
        jmp         RhExceptionHandling_FailedAllocation
RhpNewArray ENDP

_TEXT ENDS
END
