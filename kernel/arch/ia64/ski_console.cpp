//
// ski Simulator Console Implementation
//
// Uses SSC (Simulator System Call) via "break.i 0x80000" to
// communicate with the HP ski IA-64 simulator's built-in console.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/ski_console.h"
#include "include/arch/cpu_state_checkpoint.h"
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

// Implemented in ski_ssc.s. Keeping the SSC boundary in a normal ABI function
// prevents an inlined BREAK sequence from overwriting GCC's live stacked
// registers without a corresponding inline-assembly clobber.
extern "C" uint64_t guidexos_ski_ssc(uint64_t func, uint64_t arg0);

void ssc(uint64_t func, uint64_t arg0)
{
#if GXOS_MSVC_STUB
    (void)func; (void)arg0;
#else
    (void)guidexos_ski_ssc(func, arg0);
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
    return static_cast<int>(guidexos_ski_ssc(SSC_GETCHAR, 0));
#endif
}

void stop()
{
#if !GXOS_MSVC_STUB && defined(GXOS_IA64_EARLY_EXEC_PROFILE)
    // Capture after the startup diagnostics and immediately before the
    // bounded pre-exit report and SSC termination. Reporting uses the
    // ABI-correct SSC bridge and does not change PSR/RSC/FPSR/IVA.
    ia64_capture_checkpoint_d();
    report_preexit_cpu_state_checkpoint();
#endif
    ssc(SSC_EXIT);
}

} // namespace ski_console
} // namespace ia64
} // namespace arch
} // namespace kernel
