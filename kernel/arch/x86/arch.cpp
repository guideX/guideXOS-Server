//
// x86 Architecture Implementation
//
// Copyright (c) 2024 guideX
//

#include <arch/x86.h>

namespace kernel {
namespace arch {
namespace x86 {

void halt()
{
    asm volatile ("hlt");
}

void enable_interrupts()
{
    asm volatile ("sti");
}

void disable_interrupts()
{
    asm volatile ("cli");
}

uint8_t inb(uint16_t port)
{
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outb(uint16_t port, uint8_t value)
{
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint16_t inw(uint16_t port)
{
    uint16_t value;
    asm volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outw(uint16_t port, uint16_t value)
{
    asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t inl(uint16_t port)
{
    uint32_t value;
    asm volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outl(uint16_t port, uint32_t value)
{
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint64_t read_msr(uint32_t msr)
{
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

uint32_t read_cr0()
{
    uint32_t value;
    asm volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}

void write_cr0(uint32_t value)
{
    asm volatile ("mov %0, %%cr0" : : "r"(value));
}

uint32_t read_cr2()
{
    uint32_t value;
    asm volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

uint32_t read_cr3()
{
    uint32_t value;
    asm volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

void write_cr3(uint32_t value)
{
    asm volatile ("mov %0, %%cr3" : : "r"(value));
}

uint32_t read_cr4()
{
    uint32_t value;
    asm volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

void write_cr4(uint32_t value)
{
    asm volatile ("mov %0, %%cr4" : : "r"(value));
}

void init()
{
    // TODO: Initialize x86-specific features
    // - Set up GDT
    // - Set up IDT
    // - Enable PAE if available
    // - Configure paging
}

} // namespace x86
} // namespace arch
} // namespace kernel
