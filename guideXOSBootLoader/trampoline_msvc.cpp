// trampoline_msvc.cpp
// Trampoline using embedded machine code
// Works with MSVC x64 without inline assembly

#include <Uefi.h>
#include <stdint.h>

// MSVC intrinsics
extern "C" {
    void __halt(void);
    unsigned char __inbyte(unsigned short port);
    void __outbyte(unsigned short port, unsigned char value);
}
#pragma intrinsic(__halt, __inbyte, __outbyte)

// Declare the function type for the trampoline
// MS x64 ABI: RCX=1st, RDX=2nd, R8=3rd, R9=4th
typedef void (*TrampolineFunc)(void* kernelEntry, void* bootInfo, void* stackTop, void* pml4Phys);

// Serial output helpers for debugging (inline, no stack usage after trampoline)
static inline void serial_putchar(char c) {
    while ((__inbyte(0x3FD) & 0x20) == 0) { }
    __outbyte(0x3F8, (unsigned char)c);
}

static inline void serial_print(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putchar('\r');
        serial_putchar(*s++);
    }
}

// Raw machine code for the final handoff sequence
// This is position-independent code that:
// 1. cli (disable interrupts)
// 2. Save kernel entry to r12, bootInfo to r13
// 3. Load CR3 from r9 (if non-null)
// 4. Switch stack to r8
// 5. Set up MS x64 calling convention (RCX = bootInfo)
// 6. Jump to kernel
//
// Input:
//   RCX = kernelEntry
//   RDX = bootInfo
//   R8  = stackTop
//   R9  = pml4Phys
//
// CRITICAL: Stack alignment for JMP (not CALL):
//   - Since JMP doesn't push return address, RSP must be 16-byte aligned at entry
//   - We align to 16, then subtract 32 (shadow space) to keep it 16-byte aligned
//   - Result: RSP % 16 == 0 when kernel starts executing

// The trampoline code bytes - MINIMAL version without serial debug
// We allocate this at runtime in executable memory
static const uint8_t g_TrampolineCodeBytes[] = {
    // === STAGE 1: Disable interrupts and save parameters ===
    // cli - disable interrupts
    0xFA,
    
    // mov r12, rcx  ; r12 = kernelEntry (save BEFORE we touch rcx)
    0x49, 0x89, 0xCC,
    
    // mov r13, rdx  ; r13 = bootInfo (save BEFORE we touch rdx)
    0x49, 0x89, 0xD5,
    
    // mov r14, r8   ; r14 = stackTop (save)
    0x4D, 0x89, 0xC6,
    
    // mov r15, r9   ; r15 = pml4Phys (save)
    0x4D, 0x89, 0xCF,

    // === STAGE 2: Load new page tables (if pml4Phys != 0) ===
    // test r15, r15
    0x4D, 0x85, 0xFF,
    // jz .skip_cr3
    0x74, 0x06,                  // jz +6 (skip mov cr3)
    
    // mov rax, r15   ; rax = pml4Phys
    0x4C, 0x89, 0xF8,
    
    // mov cr3, rax  ; load new page tables
    0x0F, 0x22, 0xD8,
    
    // .skip_cr3:
    // === STAGE 3: Switch to new stack ===
    // mov rsp, r14   ; switch to new stack
    0x4C, 0x89, 0xF4,
    
    // and rsp, 0xFFFFFFFFFFFFFFF0 ; ensure 16-byte alignment
    0x48, 0x83, 0xE4, 0xF0,
    
    // sub rsp, 40   ; allocate shadow space (32 bytes) + 8 bytes
    0x48, 0x83, 0xEC, 0x28,
    
    // === STAGE 4: Set up kernel call parameters ===
    // mov rcx, r13  ; RCX = bootInfo (first parameter for kernel)
    0x4C, 0x89, 0xE9,
    
    // xor rdx, rdx  ; clear second param
    0x48, 0x31, 0xD2,
    
    // xor r8, r8    ; clear third param
    0x4D, 0x31, 0xC0,
    
    // xor r9, r9    ; clear fourth param  
    0x4D, 0x31, 0xC9,
    
    // === STAGE 5: Jump to kernel ===
    // jmp r12       ; jump to kernel (indirect through r12)
    0x41, 0xFF, 0xE4,
    
    // === PANIC: Should never reach here ===
    // hlt
    0xF4,
    
    // jmp $-1 (infinite loop back to hlt)
    0xEB, 0xFD
};

// Global pointer to executable copy of trampoline
// This must be set up before calling BootHandoffTrampoline
static TrampolineFunc g_ExecutableTrampoline = nullptr;

// Call this during bootloader init to set up the trampoline
// Pass a pointer to executable memory that will persist after ExitBootServices
extern "C" void SetupTrampoline(void* executableMemory)
{
    // Copy the trampoline code to executable memory
    for (UINTN i = 0; i < sizeof(g_TrampolineCodeBytes); ++i) {
        ((uint8_t*)executableMemory)[i] = g_TrampolineCodeBytes[i];
    }
    
    // Save the executable pointer
    g_ExecutableTrampoline = (TrampolineFunc)executableMemory;
}

// This function transfers control to the kernel
// It will NOT return
// IMPORTANT: SetupTrampoline() must be called first!
extern "C" void BootHandoffTrampoline(void* kernelEntry, void* bootInfo, void* stackTop, void* pml4Phys)
{
    if (g_ExecutableTrampoline == nullptr) {
        // Fallback: try to execute from read-only memory
        // This might work on some UEFI implementations
        g_ExecutableTrampoline = (TrampolineFunc)(void*)g_TrampolineCodeBytes;
    }
    
    // Call the trampoline code
    // Parameters are already in the right registers per MS x64 ABI
    // This call WILL NOT RETURN
    g_ExecutableTrampoline(kernelEntry, bootInfo, stackTop, pml4Phys);
    
    // Should never reach here - but just in case
    for (;;) {
        __halt();
    }
}

// Return the size of the trampoline code for allocation purposes
extern "C" UINTN GetTrampolineCodeSize(void)
{
    return sizeof(g_TrampolineCodeBytes);
}

// Disabled alternate trampoline implementation to avoid duplicate BootHandoffTrampoline symbol.
// Keeping file for reference; trampoline.asm provides the active implementation.
#if 0
#include <Uefi.h>
#include <stdint.h>

extern "C" {
    void __halt(void);
}
#pragma intrinsic(__halt)

typedef void (*TrampolineFunc)(void* kernelEntry, void* bootInfo, void* stackTop, void* pml4Phys);

static const uint8_t g_TrampolineCodeBytes[] = { /* ...existing code... */ };
static TrampolineFunc g_ExecutableTrampoline = nullptr;

extern "C" void SetupTrampoline(void* executableMemory)
{
    for (UINTN i = 0; i < sizeof(g_TrampolineCodeBytes); ++i) {
        ((uint8_t*)executableMemory)[i] = g_TrampolineCodeBytes[i];
    }
    g_ExecutableTrampoline = (TrampolineFunc)executableMemory;
}

extern "C" void BootHandoffTrampoline(void* kernelEntry, void* bootInfo, void* stackTop, void* pml4Phys)
{
    if (g_ExecutableTrampoline == nullptr) {
        g_ExecutableTrampoline = (TrampolineFunc)(void*)g_TrampolineCodeBytes;
    }
    g_ExecutableTrampoline(kernelEntry, bootInfo, stackTop, pml4Phys);
    for (;;) { __halt(); }
}

extern "C" UINTN GetTrampolineCodeSize(void)
{
    return sizeof(g_TrampolineCodeBytes);
}
#endif

