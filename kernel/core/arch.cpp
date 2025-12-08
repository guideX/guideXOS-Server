//
// Architecture Abstraction Layer Implementation
//
// Copyright (c) 2024 guideX
//

#include <kernel/arch.h>

namespace kernel {
namespace arch {

const char* get_arch_name()
{
#if defined(ARCH_AMD64)
    return "AMD64 (x86-64)";
#elif defined(ARCH_X86)
    return "x86 (32-bit)";
#elif defined(ARCH_ARM)
    return "ARM";
#elif defined(ARCH_IA64)
    return "Itanium (IA-64)";
#elif defined(ARCH_SPARC)
    return "SPARC";
#else
    return "Unknown";
#endif
}

uint32_t get_arch_bits()
{
#if defined(ARCH_AMD64) || defined(ARCH_IA64)
    return 64;
#elif defined(ARCH_X86) || defined(ARCH_ARM) || defined(ARCH_SPARC)
    return 32;
#else
    return 0;
#endif
}

} // namespace arch
} // namespace kernel
