//
// Itanium (IA-64) Architecture-Specific Code
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace kernel {
namespace arch {
namespace ia64 {

// CPU control
void halt();
void enable_interrupts();
void disable_interrupts();

// Break and register operands are encoded as instruction immediates, so
// expose them as compile-time constants.
template <uint64_t Immediate>
inline void break_instruction()
{
    static_assert(Immediate <= 0x1FFFFFu, "IA-64 break immediate exceeds 21 bits");
#if defined(_MSC_VER)
    __debugbreak();
#else
    asm volatile ("break.i %0 ;;" : : "i"(Immediate));
#endif
}

// PSR (Processor Status Register) operations
uint64_t read_psr();
void write_psr(uint64_t value);

// Application registers
template <uint32_t ArNum>
inline uint64_t read_ar()
{
    static_assert(ArNum < 128u, "IA-64 application-register index is out of range");
#if defined(_MSC_VER)
    return 0;
#else
    uint64_t value;
    asm volatile ("mov %0 = ar%1 ;;" : "=r"(value) : "i"(ArNum));
    return value;
#endif
}

template <uint32_t ArNum>
inline void write_ar(uint64_t value)
{
    static_assert(ArNum < 128u, "IA-64 application-register index is out of range");
#if !defined(_MSC_VER)
    asm volatile ("mov ar%0 = %1 ;;" : : "i"(ArNum), "r"(value) : "memory");
#else
    (void)value;
#endif
}

// Control registers
template <uint32_t CrNum>
inline uint64_t read_cr()
{
    static_assert(CrNum < 128u, "IA-64 control-register index is out of range");
#if defined(_MSC_VER)
    return 0;
#else
    uint64_t value;
    asm volatile ("mov %0 = cr%1 ;;" : "=r"(value) : "i"(CrNum));
    return value;
#endif
}

template <uint32_t CrNum>
inline void write_cr(uint64_t value)
{
    static_assert(CrNum < 128u, "IA-64 control-register index is out of range");
#if !defined(_MSC_VER)
    asm volatile ("mov cr%0 = %1 ;;" : : "i"(CrNum), "r"(value) : "memory");
#else
    (void)value;
#endif
}

// RSE (Register Stack Engine) operations
void flush_rse();
uint64_t read_bsp();   // Backing Store Pointer
uint64_t read_bspstore();
void write_bspstore(uint64_t value);

// IVA (Interrupt Vector Address) operations
uint64_t read_iva();
void write_iva(uint64_t value);

// Cache operations
void flush_cache();
void sync_instruction_cache();

// Initialize architecture-specific features
void init();

} // namespace ia64
} // namespace arch
} // namespace kernel
