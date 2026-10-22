#include "kernel/arch.h"

// Keep the host test from issuing privileged COM1 port instructions when the
// production serial helpers are inlined by the AMD64 host compiler.
#undef ARCH_HAS_PORT_IO
#define ARCH_HAS_PORT_IO 0
