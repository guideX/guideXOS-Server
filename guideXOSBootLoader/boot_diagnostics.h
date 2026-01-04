// boot_diagnostics.h
// Comprehensive diagnostics for UEFI bootloader debugging
// Works both before and after ExitBootServices

#pragma once

#include <Uefi.h>
#include <stdint.h>
#include "guidexOSBootInfo.h"

// MSVC intrinsics
extern "C" {
    unsigned char __inbyte(unsigned short port);
    void __outbyte(unsigned short port, unsigned char value);
    unsigned __int64 __readcr0(void);
    unsigned __int64 __readcr3(void);
    unsigned __int64 __readcr4(void);
    unsigned __int64 __readmsr(unsigned long msr);
    void __writecr3(unsigned __int64 value);
}
#pragma intrinsic(__inbyte, __outbyte, __readcr0, __readcr3, __readcr4, __readmsr)

namespace guideXOS {
namespace diag {

// ============================================================================
// SECTION 1: SERIAL PORT OUTPUT (works after ExitBootServices)
// ============================================================================

inline void SerialInit() {
    __outbyte(0x3F9, 0x00);    // Disable interrupts
    __outbyte(0x3FB, 0x80);    // Enable DLAB
    __outbyte(0x3F8, 0x01);    // Divisor low = 1 (115200 baud)
    __outbyte(0x3F9, 0x00);    // Divisor high = 0
    __outbyte(0x3FB, 0x03);    // 8N1
    __outbyte(0x3FA, 0xC7);    // Enable FIFO
    __outbyte(0x3FC, 0x0B);    // Enable IRQs, RTS/DSR
}

inline void SerialPutChar(char c) {
    while ((__inbyte(0x3FD) & 0x20) == 0) { } // Wait for TX empty
    __outbyte(0x3F8, (unsigned char)c);
}

inline void SerialPrint(const char* s) {
    while (s && *s) {
        if (*s == '\n') SerialPutChar('\r');
        SerialPutChar(*s++);
    }
}

inline void SerialPrintHex64(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    SerialPrint("0x");
    for (int i = 60; i >= 0; i -= 4) {
        SerialPutChar(hex[(v >> i) & 0xF]);
    }
}

inline void SerialPrintHex32(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    SerialPrint("0x");
    for (int i = 28; i >= 0; i -= 4) {
        SerialPutChar(hex[(v >> i) & 0xF]);
    }
}

inline void SerialPrintDec(uint64_t v) {
    char buf[24];
    int pos = 0;
    if (v == 0) { SerialPutChar('0'); return; }
    while (v > 0) {
        buf[pos++] = '0' + (v % 10);
        v /= 10;
    }
    while (pos > 0) SerialPutChar(buf[--pos]);
}

// ============================================================================
// SECTION 2: FRAMEBUFFER DIAGNOSTICS
// ============================================================================

inline void FbClearRect(uint32_t* fb, uint32_t pitchPixels, 
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                        uint32_t color) {
    for (uint32_t py = y; py < y + h; py++) {
        for (uint32_t px = x; px < x + w; px++) {
            fb[py * pitchPixels + px] = color;
        }
    }
}

// Draw a progress bar: stage 0-9, each shows cumulative colored blocks
inline void ShowBootProgress(const BootInfo* bi, int stage) {
    if (!bi || !(bi->Flags & 0x2)) return; // No framebuffer
    
    uint32_t* fb = (uint32_t*)(uintptr_t)bi->FramebufferBase;
    uint32_t pitch = bi->FramebufferPitch / 4;
    
    uint32_t colors[] = {
        0xFFFF0000, // 0: Red - Trampoline entered
        0xFFFF7F00, // 1: Orange - CR3 loaded
        0xFFFFFF00, // 2: Yellow - Stack switched
        0xFF7FFF00, // 3: Yellow-green - Parameters set
        0xFF00FF00, // 4: Green - About to jump
        0xFF00FF7F, // 5: Cyan-green - Kernel entry
        0xFF00FFFF, // 6: Cyan - Kernel init
        0xFF007FFF, // 7: Light blue
        0xFF0000FF, // 8: Blue
        0xFFFFFFFF  // 9: White - Success
    };
    
    if (stage >= 0 && stage <= 9) {
        uint32_t x = 10 + stage * 55;
        FbClearRect(fb, pitch, x, 10, 50, 50, colors[stage]);
    }
}

// ============================================================================
// SECTION 3: CPU STATE VERIFICATION
// ============================================================================

struct CpuState {
    uint64_t cr0, cr3, cr4;
    uint64_t efer;
    bool pagingEnabled;
    bool protectedMode;
    bool paeEnabled;
    bool longModeEnabled;
    bool longModeActive;
    bool nxEnabled;
};

inline CpuState GetCpuState() {
    CpuState s{};
    s.cr0 = __readcr0();
    s.cr3 = __readcr3();
    s.cr4 = __readcr4();
    s.efer = __readmsr(0xC0000080);
    
    s.pagingEnabled = (s.cr0 & (1ULL << 31)) != 0;
    s.protectedMode = (s.cr0 & 1) != 0;
    s.paeEnabled = (s.cr4 & (1ULL << 5)) != 0;
    s.longModeEnabled = (s.efer & (1ULL << 8)) != 0;
    s.longModeActive = (s.efer & (1ULL << 10)) != 0;
    s.nxEnabled = (s.efer & (1ULL << 11)) != 0;
    
    return s;
}

inline void PrintCpuState() {
    CpuState s = GetCpuState();
    
    SerialPrint("\n=== CPU State ===\n");
    SerialPrint("CR0: "); SerialPrintHex64(s.cr0); SerialPrint("\n");
    SerialPrint("CR3: "); SerialPrintHex64(s.cr3); SerialPrint("\n");
    SerialPrint("CR4: "); SerialPrintHex64(s.cr4); SerialPrint("\n");
    SerialPrint("EFER: "); SerialPrintHex64(s.efer); SerialPrint("\n");
    
    SerialPrint("Paging: ");
    SerialPrint(s.pagingEnabled ? "[OK]" : "[FAIL]");
    SerialPrint("\n");
    
    SerialPrint("PAE: ");
    SerialPrint(s.paeEnabled ? "[OK]" : "[FAIL]");
    SerialPrint("\n");
    
    SerialPrint("Long Mode Active: ");
    SerialPrint(s.longModeActive ? "[OK]" : "[FAIL - NOT IN 64-BIT!]");
    SerialPrint("\n");
}

// ============================================================================
// SECTION 4: PAGE TABLE VERIFICATION
// ============================================================================

// Check if a virtual address is mapped in the page tables
// Returns: 0 = not mapped, otherwise returns the physical address it maps to
inline uint64_t CheckAddressMapping(uint64_t pml4Phys, uint64_t vaddr) {
    if (pml4Phys == 0) return 0;
    
    const uint64_t PTE_P = 1;
    const uint64_t PTE_PS = (1ULL << 7); // Large page
    
    // PML4 index
    uint64_t pml4i = (vaddr >> 39) & 0x1FF;
    uint64_t* pml4 = (uint64_t*)(uintptr_t)pml4Phys;
    
    if ((pml4[pml4i] & PTE_P) == 0) return 0;
    
    // PDPT
    uint64_t pdptPhys = pml4[pml4i] & ~0xFFFULL;
    uint64_t pdpti = (vaddr >> 30) & 0x1FF;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdptPhys;
    
    if ((pdpt[pdpti] & PTE_P) == 0) return 0;
    if (pdpt[pdpti] & PTE_PS) {
        // 1GB page
        return (pdpt[pdpti] & ~0x3FFFFFFFULL) | (vaddr & 0x3FFFFFFFULL);
    }
    
    // PD
    uint64_t pdPhys = pdpt[pdpti] & ~0xFFFULL;
    uint64_t pdi = (vaddr >> 21) & 0x1FF;
    uint64_t* pd = (uint64_t*)(uintptr_t)pdPhys;
    
    if ((pd[pdi] & PTE_P) == 0) return 0;
    if (pd[pdi] & PTE_PS) {
        // 2MB page
        return (pd[pdi] & ~0x1FFFFFULL) | (vaddr & 0x1FFFFFULL);
    }
    
    // PT
    uint64_t ptPhys = pd[pdi] & ~0xFFFULL;
    uint64_t pti = (vaddr >> 12) & 0x1FF;
    uint64_t* pt = (uint64_t*)(uintptr_t)ptPhys;
    
    if ((pt[pti] & PTE_P) == 0) return 0;
    
    return (pt[pti] & ~0xFFFULL) | (vaddr & 0xFFFULL);
}

inline void VerifyMappings(uint64_t pml4Phys, const BootInfo* bi, uint64_t kernelEntry) {
    SerialPrint("\n=== Mapping Verification ===\n");
    SerialPrint("PML4 at: "); SerialPrintHex64(pml4Phys); SerialPrint("\n");
    
    // Check kernel entry
    SerialPrint("Kernel Entry ");
    SerialPrintHex64(kernelEntry);
    SerialPrint(" -> ");
    uint64_t mapped = CheckAddressMapping(pml4Phys, kernelEntry);
    if (mapped == 0) {
        SerialPrint("NOT MAPPED!\n");
    } else if (mapped == kernelEntry) {
        SerialPrint("OK (identity)\n");
    } else {
        SerialPrintHex64(mapped);
        SerialPrint(" (NOT IDENTITY!)\n");
    }
    
    // Check BootInfo
    if (bi) {
        SerialPrint("BootInfo ");
        SerialPrintHex64((uint64_t)(uintptr_t)bi);
        SerialPrint(" -> ");
        mapped = CheckAddressMapping(pml4Phys, (uint64_t)(uintptr_t)bi);
        if (mapped == 0) {
            SerialPrint("NOT MAPPED!\n");
        } else {
            SerialPrint("OK\n");
        }
        
        // Check framebuffer
        if (bi->Flags & 0x2) {
            SerialPrint("Framebuffer ");
            SerialPrintHex64(bi->FramebufferBase);
            SerialPrint(" -> ");
            mapped = CheckAddressMapping(pml4Phys, bi->FramebufferBase);
            if (mapped == 0) {
                SerialPrint("NOT MAPPED!\n");
            } else {
                SerialPrint("OK\n");
            }
        }
    }
}

// ============================================================================
// SECTION 5: KERNEL ENTRY VALIDATION
// ============================================================================

inline bool ValidateKernelEntry(uint64_t entryPhys) {
    SerialPrint("\n=== Kernel Entry Validation ===\n");
    SerialPrint("Entry at: "); SerialPrintHex64(entryPhys); SerialPrint("\n");
    
    // Try to read first bytes
    uint8_t* entry = (uint8_t*)(uintptr_t)entryPhys;
    
    SerialPrint("First 16 bytes: ");
    for (int i = 0; i < 16; i++) {
        static const char hex[] = "0123456789ABCDEF";
        SerialPutChar(hex[(entry[i] >> 4) & 0xF]);
        SerialPutChar(hex[entry[i] & 0xF]);
        SerialPutChar(' ');
    }
    SerialPrint("\n");
    
    // Check for all zeros (common sign of unmapped/unloaded)
    bool allZero = true;
    for (int i = 0; i < 16; i++) {
        if (entry[i] != 0) { allZero = false; break; }
    }
    
    if (allZero) {
        SerialPrint("WARNING: Entry point is all zeros!\n");
        return false;
    }
    
    // Check for common x64 function prologues
    // push rbp = 0x55
    // sub rsp, N = 0x48 0x83 0xEC or 0x48 0x81 0xEC
    // mov rbp, rsp = 0x48 0x89 0xE5
    bool validPrologue = 
        (entry[0] == 0x55) || // push rbp
        (entry[0] == 0x48 && entry[1] == 0x83) || // sub rsp, imm8
        (entry[0] == 0x48 && entry[1] == 0x81) || // sub rsp, imm32
        (entry[0] == 0x48 && entry[1] == 0x89);   // mov reg, reg
    
    if (validPrologue) {
        SerialPrint("Looks like valid x64 code\n");
    } else {
        SerialPrint("Unknown code pattern (may still be valid)\n");
    }
    
    return true;
}

// ============================================================================
// SECTION 6: MEMORY MAP DUMP
// ============================================================================

inline void DumpMemoryMap(EFI_MEMORY_DESCRIPTOR* map, UINTN count, UINTN descSize) {
    SerialPrint("\n=== Memory Map (first 20 entries) ===\n");
    
    UINTN limit = (count < 20) ? count : 20;
    
    for (UINTN i = 0; i < limit; i++) {
        EFI_MEMORY_DESCRIPTOR* desc = 
            (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map + i * descSize);
        
        SerialPrint("  ");
        SerialPrintHex64(desc->PhysicalStart);
        SerialPrint(" - ");
        SerialPrintHex64(desc->PhysicalStart + desc->NumberOfPages * 0x1000);
        SerialPrint(" Type=");
        SerialPrintDec(desc->Type);
        SerialPrint("\n");
    }
    
    if (count > 20) {
        SerialPrint("  ... (");
        SerialPrintDec(count - 20);
        SerialPrint(" more entries)\n");
    }
}

// ============================================================================
// SECTION 7: COMPREHENSIVE PRE-JUMP CHECK
// ============================================================================

inline bool PreJumpCheck(
    uint64_t kernelEntry,
    const BootInfo* bi,
    void* stackTop,
    uint64_t pml4Phys
) {
    SerialInit();
    SerialPrint("\n\n========================================\n");
    SerialPrint("GuideXOS Pre-Jump Diagnostics\n");
    SerialPrint("========================================\n");
    
    bool ok = true;
    
    // 1. CPU State
    PrintCpuState();
    CpuState cpu = GetCpuState();
    if (!cpu.longModeActive) {
        SerialPrint("FATAL: Not in 64-bit long mode!\n");
        ok = false;
    }
    
    // 2. Parameters
    SerialPrint("\n=== Parameters ===\n");
    SerialPrint("Kernel Entry: "); SerialPrintHex64(kernelEntry); SerialPrint("\n");
    SerialPrint("BootInfo: "); SerialPrintHex64((uint64_t)(uintptr_t)bi); SerialPrint("\n");
    SerialPrint("Stack Top: "); SerialPrintHex64((uint64_t)(uintptr_t)stackTop); SerialPrint("\n");
    SerialPrint("PML4: "); SerialPrintHex64(pml4Phys); SerialPrint("\n");
    
    if (kernelEntry == 0) {
        SerialPrint("FATAL: Kernel entry is NULL!\n");
        ok = false;
    }
    if (bi == nullptr) {
        SerialPrint("FATAL: BootInfo is NULL!\n");
        ok = false;
    }
    if (stackTop == nullptr) {
        SerialPrint("FATAL: Stack is NULL!\n");
        ok = false;
    }
    if (pml4Phys == 0) {
        SerialPrint("WARNING: PML4 is NULL - using current page tables\n");
    }
    
    // 3. BootInfo validation
    if (bi) {
        SerialPrint("\n=== BootInfo ===\n");
        SerialPrint("Magic: "); SerialPrintHex32(bi->Magic);
        if (bi->Magic == 0x49425847) {
            SerialPrint(" [OK]\n");
        } else {
            SerialPrint(" [INVALID!]\n");
            ok = false;
        }
        SerialPrint("Version: "); SerialPrintDec(bi->Version); SerialPrint("\n");
        SerialPrint("Flags: "); SerialPrintHex32(bi->Flags); SerialPrint("\n");
        
        if (bi->Flags & 0x2) {
            SerialPrint("Framebuffer: ");
            SerialPrintHex64(bi->FramebufferBase);
            SerialPrint(" (");
            SerialPrintDec(bi->FramebufferWidth);
            SerialPrint("x");
            SerialPrintDec(bi->FramebufferHeight);
            SerialPrint(")\n");
        }
    }
    
    // 4. Verify mappings
    if (pml4Phys != 0) {
        VerifyMappings(pml4Phys, bi, kernelEntry);
    }
    
    // 5. Validate kernel entry code
    ValidateKernelEntry(kernelEntry);
    
    SerialPrint("\n========================================\n");
    SerialPrint(ok ? "All checks PASSED\n" : "Some checks FAILED!\n");
    SerialPrint("========================================\n\n");
    
    return ok;
}

} // namespace diag
} // namespace guideXOS
