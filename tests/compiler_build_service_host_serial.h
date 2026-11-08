#pragma once

// The hosted compiler-service harness exercises ARM64 code generation on an
// x86 host. Keep the production serial API on its inert ARM64 callback path.
#include "kernel/arch.h"
#undef ARCH_HAS_PORT_IO
#define ARCH_HAS_PORT_IO 0
#undef ARCH_AMD64
#define ARCH_ARM64 1
