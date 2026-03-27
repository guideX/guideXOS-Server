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
