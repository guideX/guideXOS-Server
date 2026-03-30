//
// SBI Console Implementation
//
// Uses the RISC-V SBI legacy interface (ecall with a7 = extension ID)
// to communicate with OpenSBI firmware for early serial output.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/sbi_console.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace riscv64 {
namespace sbi_console {

SbiRet sbi_call(uint64_t ext, uint64_t arg0)
{
#if GXOS_MSVC_STUB
    (void)ext; (void)arg0;
    SbiRet ret = { 0, 0 };
    return ret;
#else
    register uint64_t a0 asm("a0") = arg0;
    register uint64_t a7 asm("a7") = ext;
    asm volatile (
        "ecall"
        : "+r"(a0)
        : "r"(a7)
        : "memory"
    );
    SbiRet ret;
    ret.error = static_cast<int64_t>(a0);
    ret.value = 0;
    return ret;
#endif
}

void init()
{
    // Legacy SBI console requires no initialisation.
}

void putchar(char c)
{
    sbi_call(SBI_EXT_CONSOLE_PUTCHAR, static_cast<uint64_t>(c));
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
    SbiRet ret = sbi_call(SBI_EXT_CONSOLE_GETCHAR);
    return static_cast<int>(ret.error);
#endif
}

void shutdown()
{
    sbi_call(SBI_EXT_SHUTDOWN);
}

} // namespace sbi_console
} // namespace riscv64
} // namespace arch
} // namespace kernel
