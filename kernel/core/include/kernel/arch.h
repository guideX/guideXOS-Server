//
// Architecture Abstraction Layer
//
// Copyright (c) 2024 guideX
//

#pragma once

#include <kernel/types.h>

// Include architecture-specific headers based on target
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_AMD64
    #include <arch/amd64.h>
    namespace kernel { namespace arch { using namespace amd64; } }
#elif defined(__i386__) || defined(_M_IX86)
    #define ARCH_X86
    #include <arch/x86.h>
    namespace kernel { namespace arch { using namespace x86; } }
#elif defined(__arm__) || defined(__aarch32__) || defined(_M_ARM)
    #define ARCH_ARM
    #include <arch/arm.h>
    namespace kernel { namespace arch { using namespace arm; } }
#elif defined(__ia64__) || defined(_M_IA64)
    #define ARCH_IA64
    #include <arch/ia64.h>
    namespace kernel { namespace arch { using namespace ia64; } }
#elif defined(__sparc__)
    #define ARCH_SPARC
    #include <arch/sparc.h>
    namespace kernel { namespace arch { using namespace sparc; } }
#else
    #error "Unsupported architecture"
#endif

namespace kernel {
namespace arch {

// Get architecture name
const char* get_arch_name();

// Get architecture bitness
uint32_t get_arch_bits();

} // namespace arch
} // namespace kernel
