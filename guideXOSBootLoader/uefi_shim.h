#pragma once
#include "Uefi.h"
#include <stdarg.h>

// Minimal UEFI shims to avoid linking full EDK II libraries
// These are thin wrappers or simple implementations for a freestanding UEFI app.

// Global SystemTable pointer - must be set early in efi_main
extern EFI_SYSTEM_TABLE* gST;

static inline void UefiOut16(const CHAR16* s) {
    if (gST && gST->ConOut && s) {
        gST->ConOut->OutputString(gST->ConOut, (CHAR16*)s);
    }
}

static inline UINTN UefiStrLen16(const CHAR16* s) {
    UINTN n = 0;
    if (!s) return 0;
    while (s[n] != 0) ++n;
    return n;
}

static inline void UefiAppend16(CHAR16* dst, UINTN capChars, UINTN* ioLen, CHAR16 ch) {
    if (!dst || !ioLen || capChars == 0) return;
    if (*ioLen + 1 >= capChars) return;
    dst[*ioLen] = ch;
    *ioLen = *ioLen + 1;
    dst[*ioLen] = 0;
}

// Normalize LF to CRLF for UEFI console
static inline void UefiAppendEolNormalized(CHAR16* dst, UINTN capChars, UINTN* ioLen, CHAR16 ch) {
    if (ch == (CHAR16)'\n') {
        if (*ioLen == 0 || dst[*ioLen - 1] != (CHAR16)'\r') {
            UefiAppend16(dst, capChars, ioLen, (CHAR16)'\r');
        }
        UefiAppend16(dst, capChars, ioLen, (CHAR16)'\n');
        return;
    }
    UefiAppend16(dst, capChars, ioLen, ch);
}

static inline void UefiAppendStr16(CHAR16* dst, UINTN capChars, UINTN* ioLen, const CHAR16* src) {
    if (!src) src = (const CHAR16*)L"(null)";
    for (UINTN i = 0; src[i] != 0; ++i) {
        UefiAppendEolNormalized(dst, capChars, ioLen, src[i]);
    }
}

static inline void UefiAppendHex64(CHAR16* dst, UINTN capChars, UINTN* ioLen, UINT64 v, UINTN minDigits, BOOLEAN upper) {
    static const char hexL[] = "0123456789abcdef";
    static const char hexU[] = "0123456789ABCDEF";
    const char* hex = upper ? hexU : hexL;

    CHAR16 tmp[32];
    UINTN pos = 0;
    do {
        tmp[pos++] = (CHAR16)hex[(UINTN)(v & 0xFULL)];
        v >>= 4;
    } while (v != 0 && pos < 32);

    while (pos < minDigits && pos < 32) tmp[pos++] = (CHAR16)'0';

    // reverse
    while (pos > 0) {
        UefiAppend16(dst, capChars, ioLen, tmp[--pos]);
    }
}

static inline void UefiAppendDecU64(CHAR16* dst, UINTN capChars, UINTN* ioLen, UINT64 v) {
    CHAR16 tmp[32];
    UINTN pos = 0;
    do {
        tmp[pos++] = (CHAR16)(L'0' + (v % 10));
        v /= 10;
    } while (v != 0 && pos < 32);

    while (pos > 0) {
        UefiAppend16(dst, capChars, ioLen, tmp[--pos]);
    }
}

static inline void UefiAppendStatus(CHAR16* dst, UINTN capChars, UINTN* ioLen, EFI_STATUS st) {
    auto appendAscii = [&](const char* s) {
        for (UINTN i = 0; s && s[i] != 0; ++i) UefiAppend16(dst, capChars, ioLen, (CHAR16)s[i]);
    };

    // Minimal mapping; enough for common debug.
    switch (st) {
    case EFI_SUCCESS: appendAscii("EFI_SUCCESS"); break;
    case EFI_LOAD_ERROR: appendAscii("EFI_LOAD_ERROR"); break;
    case EFI_INVALID_PARAMETER: appendAscii("EFI_INVALID_PARAMETER"); break;
    case EFI_UNSUPPORTED: appendAscii("EFI_UNSUPPORTED"); break;
    case EFI_BAD_BUFFER_SIZE: appendAscii("EFI_BAD_BUFFER_SIZE"); break;
    case EFI_BUFFER_TOO_SMALL: appendAscii("EFI_BUFFER_TOO_SMALL"); break;
    case EFI_NOT_READY: appendAscii("EFI_NOT_READY"); break;
    case EFI_DEVICE_ERROR: appendAscii("EFI_DEVICE_ERROR"); break;
    case EFI_OUT_OF_RESOURCES: appendAscii("EFI_OUT_OF_RESOURCES"); break;
    case EFI_NOT_FOUND: appendAscii("EFI_NOT_FOUND"); break;
    default:
        appendAscii("EFI_STATUS(0x");
        UefiAppendHex64(dst, capChars, ioLen, (UINT64)st, 0, TRUE);
        UefiAppend16(dst, capChars, ioLen, (CHAR16)')');
        break;
    }
}

// Convert MSVC wchar_t* literal to CHAR16* (layout-compatible on Windows)
static inline const CHAR16* AsChar16(const wchar_t* s) {
    return (const CHAR16*)s;
}

static inline const CHAR16* AsChar16(const CHAR16* s) {
    return s;
}

// Very small formatter: supports %% %s %c %u %d %x/%X %lx/%Lx %Lu/%llu %p %r
template <typename TChar>
static inline UINTN UefiVPrintImpl(const TChar* Format, VA_LIST args) {
    const CHAR16* fmt = AsChar16((const wchar_t*)Format);

    CHAR16 out[1024];
    UINTN len = 0;
    out[0] = 0;

    for (UINTN i = 0; fmt[i] != 0; ++i) {
        CHAR16 ch = fmt[i];
        if (ch != L'%') {
            UefiAppendEolNormalized(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, ch);
            continue;
        }

        // Handle %%
        CHAR16 nxt = fmt[++i];
        if (nxt == 0) break;
        if (nxt == L'%') { UefiAppendEolNormalized(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, L'%'); continue; }

        // length modifiers
        BOOLEAN isLong = FALSE;
        BOOLEAN isLongLong = FALSE;
        if (nxt == L'l') {
            isLong = TRUE;
            nxt = fmt[++i];
        } else if (nxt == L'L') {
            isLongLong = TRUE;
            nxt = fmt[++i];
        }
        if (nxt == 0) break;

        switch (nxt) {
        case L's': {
            const CHAR16* s16 = va_arg(args, CHAR16*);
            UefiAppendStr16(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, s16);
            break;
        }
        case L'c': {
            UINTN c = (UINTN)va_arg(args, int);
            UefiAppend16(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, (CHAR16)c);
            break;
        }
        case L'u': {
            UINT64 v;
            if (isLongLong) v = va_arg(args, UINT64);
            else if (isLong) v = (UINT64)va_arg(args, UINTN);
            else v = (UINT64)va_arg(args, UINT32);
            UefiAppendDecU64(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, v);
            break;
        }
        case L'd': {
            INT64 v;
            if (isLongLong) v = va_arg(args, INT64);
            else if (isLong) v = (INT64)va_arg(args, INTN);
            else v = (INT64)va_arg(args, INT32);

            if (v < 0) { UefiAppend16(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, L'-'); v = -v; }
            UefiAppendDecU64(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, (UINT64)v);
            break;
        }
        case L'x':
        case L'X': {
            BOOLEAN upper = (nxt == L'X');
            UINT64 v;
            if (isLongLong) v = va_arg(args, UINT64);
            else if (isLong) v = (UINT64)va_arg(args, UINTN);
            else v = (UINT64)va_arg(args, UINT32);
            UefiAppendHex64(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, v, 0, upper);
            break;
        }
        case L'p': {
            VOID* p = va_arg(args, VOID*);
            UefiAppend16(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, (CHAR16)'0');
            UefiAppend16(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, (CHAR16)'x');
            UefiAppendHex64(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, (UINT64)(UINTN)p, sizeof(void*) * 2, TRUE);
            break;
        }
        case L'r': {
            EFI_STATUS st = va_arg(args, EFI_STATUS);
            UefiAppendStatus(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, st);
            break;
        }
        default:
            // Unknown specifier: print it literally
            UefiAppendEolNormalized(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, L'%');
            if (isLong) UefiAppendEolNormalized(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, L'l');
            if (isLongLong) UefiAppendEolNormalized(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, L'L');
            UefiAppendEolNormalized(out, (UINTN)(sizeof(out) / sizeof(out[0])), &len, nxt);
            break;
        }
    }

    UefiOut16(out);
    return len;
}

static inline UINTN UefiPrint(IN CONST CHAR16* Format, ...) {
    VA_LIST args;
    VA_START(args, Format);
    UINTN n = UefiVPrintImpl((const wchar_t*)Format, args);
    VA_END(args);
    return n;
}

static inline UINTN UefiPrint(IN const wchar_t* Format, ...) {
    VA_LIST args;
    VA_START(args, Format);
    UINTN n = UefiVPrintImpl(Format, args);
    VA_END(args);
    return n;
}

// Simple SetMem replacement (byte-by-byte memset)
static inline VOID* UefiSetMem(VOID* Buffer, UINTN Size, UINT8 Value) {
    UINT8* p = (UINT8*)Buffer;
    for (UINTN i = 0; i < Size; ++i) {
        p[i] = Value;
    }
    return Buffer;
}

// Simple CopyMem replacement (byte-by-byte memcpy)
static inline VOID* UefiCopyMem(VOID* Dest, CONST VOID* Src, UINTN Size) {
    UINT8* d = (UINT8*)Dest;
    const UINT8* s = (const UINT8*)Src;
    for (UINTN i = 0; i < Size; ++i) {
        d[i] = s[i];
    }
    return Dest;
}

// Simple CompareGuid replacement
static inline BOOLEAN LocalCompareGuid(const EFI_GUID* a, const EFI_GUID* b) {
    if (!a || !b) return FALSE;
    const UINT32* pa = (const UINT32*)a;
    const UINT32* pb = (const UINT32*)b;
    return (pa[0] == pb[0] && pa[1] == pb[1] && pa[2] == pb[2] && pa[3] == pb[3]);
}

// Pool allocation wrappers using AllocatePages/FreePages from BootServices
// Note: these require a SystemTable parameter and return page-aligned memory
static inline VOID* LocalAllocatePool(EFI_SYSTEM_TABLE* SystemTable, UINTN Size) {
    if (Size == 0) return nullptr;
    UINTN pages = (Size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS phys = 0;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        pages,
        &phys
    );
    if (EFI_ERROR(status)) return nullptr;
    return (VOID*)(UINTN)phys;
}

static inline VOID LocalFreePool(EFI_SYSTEM_TABLE* SystemTable, VOID* Buffer, UINTN Size) {
    if (!Buffer || Size == 0) return;
    UINTN pages = (Size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    SystemTable->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)Buffer, pages);
}

// Aliases for compatibility with existing code
#define Print       UefiPrint
#define SetMem      UefiSetMem
#define CopyMem     UefiCopyMem
#define CompareGuid LocalCompareGuid
