//
// AMD64 Architecture Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/amd64.h"
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_enable)
#pragma intrinsic(_disable)
#endif

namespace kernel {
namespace arch {
namespace amd64 {

void halt()
{
#if defined(_MSC_VER)
    __halt();
#else
    asm volatile ("hlt");
#endif
}

void enable_interrupts()
{
#if defined(_MSC_VER)
    _enable();
#else
    asm volatile ("sti");
#endif
}

void disable_interrupts()
{
#if defined(_MSC_VER)
    _disable();
#else
    asm volatile ("cli");
#endif
}

uint8_t inb(uint16_t port)
{
#if defined(_MSC_VER)
    return __inbyte(port);
#else
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
#endif
}

void outb(uint16_t port, uint8_t value)
{
#if defined(_MSC_VER)
    __outbyte(port, value);
#else
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
#endif
}

uint16_t inw(uint16_t port)
{
#if defined(_MSC_VER)
    return __inword(port);
#else
    uint16_t value;
    asm volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
#endif
}

void outw(uint16_t port, uint16_t value)
{
#if defined(_MSC_VER)
    __outword(port, value);
#else
    asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
#endif
}

uint32_t inl(uint16_t port)
{
#if defined(_MSC_VER)
    return __indword(port);
#else
    uint32_t value;
    asm volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
#endif
}

void outl(uint16_t port, uint32_t value)
{
#if defined(_MSC_VER)
    __outdword(port, value);
#else
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
#endif
}

uint64_t read_msr(uint32_t msr)
{
#if defined(_MSC_VER)
    return __readmsr(msr);
#else
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
#endif
}

void write_msr(uint32_t msr, uint64_t value)
{
#if defined(_MSC_VER)
    __writemsr(msr, value);
#else
    uint32_t low = static_cast<uint32_t>(value & 0xFFFFFFFFULL);
    uint32_t high = static_cast<uint32_t>(value >> 32);
    asm volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
#endif
}

uint64_t read_cr0()
{
#if defined(_MSC_VER)
    return __readcr0();
#else
    uint64_t value;
    asm volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
#endif
}

void write_cr0(uint64_t value)
{
#if defined(_MSC_VER)
    __writecr0(value);
#else
    asm volatile ("mov %0, %%cr0" : : "r"(value));
#endif
}

uint64_t read_cr2()
{
#if defined(_MSC_VER)
    return __readcr2();
#else
    uint64_t value;
    asm volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
#endif
}

uint64_t read_cr3()
{
#if defined(_MSC_VER)
    return __readcr3();
#else
    uint64_t value;
    asm volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
#endif
}

void write_cr3(uint64_t value)
{
#if defined(_MSC_VER)
    __writecr3(value);
#else
    asm volatile ("mov %0, %%cr3" : : "r"(value));
#endif
}

uint64_t read_cr4()
{
#if defined(_MSC_VER)
    return __readcr4();
#else
    uint64_t value;
    asm volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
#endif
}

void write_cr4(uint64_t value)
{
#if defined(_MSC_VER)
    __writecr4(value);
#else
    asm volatile ("mov %0, %%cr4" : : "r"(value));
#endif
}

bool supports_nx()
{
#if defined(_MSC_VER)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 0x80000000);
    const uint32_t maxExtended = static_cast<uint32_t>(info[0]);
    if (maxExtended < 0x80000001U) return false;
    __cpuid(info, 0x80000001);
    return (static_cast<uint32_t>(info[3]) & (1U << 20)) != 0;
#else
    uint32_t maxExtended = 0;
    uint32_t edx = 0;
    asm volatile (
        "cpuid"
        : "=a"(maxExtended)
        : "a"(0x80000000U)
        : "rbx", "rcx", "rdx"
    );
    if (maxExtended < 0x80000001U) return false;
    asm volatile (
        "cpuid"
        : "=d"(edx)
        : "a"(0x80000001U)
        : "rbx", "rcx"
    );
    return (edx & (1U << 20)) != 0;
#endif
}

bool enable_nx()
{
    if (!supports_nx()) return false;
    static const uint32_t EFER_MSR = 0xC0000080U;
    static const uint64_t EFER_NXE = 1ULL << 11;
    const uint64_t efer = read_msr(EFER_MSR);
    if ((efer & EFER_NXE) == 0) write_msr(EFER_MSR, efer | EFER_NXE);
    return (read_msr(EFER_MSR) & EFER_NXE) != 0;
}

void invlpg(void* address)
{
#if defined(_MSC_VER)
    __invlpg(address);
#else
    asm volatile ("invlpg (%0)" : : "r"(address) : "memory");
#endif
}

void init()
{
    // TODO: Initialize AMD64-specific features
    // - Set up GDT
    // - Set up IDT
    // - Enable SSE/AVX if available
    // - Configure paging
}

} // namespace amd64
} // namespace arch
} // namespace kernel
