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
IFDEF GUIDEXOS_NATIVEAOT_C011EC62_POST_PROMOTION_REFILL_TOPOLOGY
EXTERN  guideXosNativeAotC011EC62ManagedAllocationEntered:PROC
EXTERN  guideXosNativeAotC011EC62ManagedAllocationReturned:PROC
ENDIF

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

IFDEF GUIDEXOS_NATIVEAOT_C011EC62_POST_PROMOTION_REFILL_TOPOLOGY
        ; Observe the request before the locked bump/rare branch.  The
        ; callback only reads the current context and records diagnostics;
        ; it does not modify any allocator state.
        sub         rsp, 68h
        mov         qword ptr [rsp + 40h], rax
        mov         qword ptr [rsp + 48h], rcx
        mov         qword ptr [rsp + 50h], rdx
        mov         qword ptr [rsp + 58h], r10
        mov         rcx, rax
        mov         rdx, rax
        mov         r8, r10
        mov         r9, qword ptr [r10]
        mov         rax, qword ptr [r10 + 8]
        mov         qword ptr [rsp + 28h], rax
        mov         dword ptr [rsp + 30h], 0
        call        guideXosNativeAotC011EC62ManagedAllocationEntered
        mov         rax, qword ptr [rsp + 40h]
        mov         rcx, qword ptr [rsp + 48h]
        mov         rdx, qword ptr [rsp + 50h]
        mov         r10, qword ptr [rsp + 58h]
        add         rsp, 68h
ENDIF

        mov         r8, rax
        add         rax, qword ptr [r10]
        jc          RhpNewArrayRare
        cmp         rax, qword ptr [r10 + 8]
        ja          RhpNewArrayRare

        mov         qword ptr [r10], rax
        sub         rax, r8
        mov         qword ptr [rax], rcx
        mov         dword ptr [rax + 8], edx

IFDEF GUIDEXOS_NATIVEAOT_C011EC62_POST_PROMOTION_REFILL_TOPOLOGY
        sub         rsp, 28h
        mov         qword ptr [rsp + 20h], rax
        mov         rcx, rax
        mov         rdx, qword ptr [r10]
        mov         r8, qword ptr [r10 + 8]
        call        guideXosNativeAotC011EC62ManagedAllocationReturned
        mov         rax, qword ptr [rsp + 20h]
        add         rsp, 28h
ENDIF

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
