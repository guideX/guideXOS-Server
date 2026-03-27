//
// Architecture Abstraction Layer
//
// Copyright (c) 2024 guideX
//

#pragma once

#include "types.h"

// Include architecture-specific headers based on target
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_AMD64
    #if defined(_MSC_VER)
        #include "../../../arch/amd64/include/arch/amd64.h"
    #else
        #include <arch/amd64.h>
    #endif
    namespace kernel { namespace arch { using namespace amd64; } }
#elif defined(__i386__) || defined(_M_IX86)
    #define ARCH_X86
    #if defined(_MSC_VER)
        #include "../../../arch/x86/include/arch/x86.h"
    #else
        #include <arch/x86.h>
    #endif
    namespace kernel { namespace arch { using namespace x86; } }
#elif defined(__arm__) || defined(__aarch32__) || defined(_M_ARM)
    #define ARCH_ARM
    #if defined(_MSC_VER)
        #include "../../../arch/arm/include/arch/arm.h"
    #else
        #include <arch/arm.h>
    #endif
    namespace kernel { namespace arch { using namespace arm; } }
#elif defined(__ia64__) || defined(_M_IA64)
    #define ARCH_IA64
    #if defined(_MSC_VER)
        #include "../../../arch/ia64/include/arch/ia64.h"
    #else
        #include <arch/ia64.h>
    #endif
    namespace kernel { namespace arch { using namespace ia64; } }
#elif defined(__sparc__)
    #define ARCH_SPARC
    #if defined(_MSC_VER)
        #include "../../../arch/sparc/include/arch/sparc.h"
    #else
        #include <arch/sparc.h>
    #endif
    namespace kernel { namespace arch { using namespace sparc; } }
#else
    #error "Unsupported architecture"
#endif

// ================================================================
// Platform feature macros
//
// Each arch sets the features it supports.  Core code uses these
// guards so that x86-specific hardware (PIC, PS/2, VGA text,
// port I/O) is never compiled on architectures that lack it.
// ================================================================

#if defined(ARCH_X86) || defined(ARCH_AMD64)
    #define ARCH_HAS_PORT_IO    1
    #define ARCH_HAS_VGA_TEXT   1
    #define ARCH_HAS_PIC_8259   1
    #define ARCH_HAS_PS2        1
#else
    #define ARCH_HAS_PORT_IO    0
    #define ARCH_HAS_VGA_TEXT   0
    #define ARCH_HAS_PIC_8259   0
    #define ARCH_HAS_PS2        0
#endif

namespace kernel {
namespace arch {

// Get architecture name
inline const char* get_arch_name()
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

// Get architecture bitness
inline uint32_t get_arch_bits()
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
