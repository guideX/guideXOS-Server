//
// ski Simulator Console Implementation
//
// Uses SSC (Simulator System Call) via "break.i 0x80000" to
// communicate with the HP ski IA-64 simulator's built-in console.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/ski_console.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace ia64 {
namespace ski_console {

void ssc(uint64_t func, uint64_t arg0)
{
#if GXOS_MSVC_STUB
    (void)func; (void)arg0;
#else
    register uint64_t r15 asm("r15") = func;
    register uint64_t r32 asm("r32") = arg0;
    asm volatile (
        "break.i 0x80000 ;;\n"
        :
        : "r"(r15), "r"(r32)
        : "memory"
    );
#endif
}

void init()
{
    ssc(SSC_CONSOLE_INIT);
}

void putchar(char c)
{
    ssc(SSC_PUTCHAR, static_cast<uint64_t>(c));
}

void puts(const char* str)
{
    if (!str) return;
    while (*str) {
        putchar(*str);
        ++str;
    }
}

void write(const char* str, uint64_t len)
{
    if (!str) return;
    for (uint64_t i = 0; i < len; ++i) {
        putchar(str[i]);
    }
}

void put_hex(uint64_t value)
{
    puts("0x");
    // 16 hex digits for 64-bit value
    static const char hex[] = "0123456789ABCDEF";
    bool leading = true;
    for (int i = 60; i >= 0; i -= 4) {
        uint64_t nibble = (value >> i) & 0xF;
        if (nibble != 0) leading = false;
        if (!leading || i == 0) {
            putchar(hex[nibble]);
        }
    }
}

int getchar()
{
#if GXOS_MSVC_STUB
    return -1;
#else
    register uint64_t r15 asm("r15") = SSC_GETCHAR;
    register uint64_t result asm("r8");
    asm volatile (
        "break.i 0x80000 ;;\n"
        : "=r"(result)
        : "r"(r15)
        : "memory"
    );
    return static_cast<int>(result);
#endif
}

void stop()
{
    ssc(SSC_STOP);
}

} // namespace ski_console
} // namespace ia64
} // namespace arch
} // namespace kernel
