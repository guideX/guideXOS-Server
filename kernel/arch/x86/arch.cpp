//
// x86 (32-bit) Architecture Implementation  
//
// Copyright (c) 2024 guideX
//

#include "include/arch/x86.h"
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_enable)
#pragma intrinsic(_disable)
#endif

namespace kernel {
namespace arch {
namespace x86 {

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

// Add any other arch-specific functions here...

} // namespace x86
} // namespace arch
} // namespace kernel
