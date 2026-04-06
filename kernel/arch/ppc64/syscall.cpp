//
// PowerPC64 System Call Implementation
//
// Stub implementation for system calls on PowerPC64.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/ppc64.h"

namespace kernel {
namespace arch {
namespace ppc64 {
namespace syscall {

namespace {

// Syscall handler table
static syscall_handler_t s_handlers[SYS_MAX] = { nullptr };

// Default handler for unimplemented syscalls
static int64_t default_handler(uint64_t a0, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2;
    (void)a3; (void)a4; (void)a5;
    
    // Return -ENOSYS (function not implemented)
    return -38;
}

} // anonymous namespace

// ================================================================
// Syscall subsystem API
// ================================================================

void init()
{
    // Initialize all handlers to default
    for (uint64_t i = 0; i < SYS_MAX; ++i) {
        s_handlers[i] = default_handler;
    }
}

void register_handler(uint64_t num, syscall_handler_t handler)
{
    if (num < SYS_MAX && handler != nullptr) {
        s_handlers[num] = handler;
    }
}

int64_t dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2,
                 uint64_t a3, uint64_t a4, uint64_t a5)
{
    if (num >= SYS_MAX) {
        return -38;  // -ENOSYS
    }
    
    syscall_handler_t handler = s_handlers[num];
    if (handler == nullptr) {
        return -38;  // -ENOSYS
    }
    
    return handler(a0, a1, a2, a3, a4, a5);
}

// ================================================================
// Syscall entry point (called from assembly)
//
// This is invoked by the system call exception handler (vector 0xC00)
// after saving the user context.
//
// Arguments from user space:
//   r0 = syscall number
//   r3-r8 = arguments
//
// Return value in r3
// ================================================================

extern "C" int64_t ppc64_syscall_entry(uint64_t num,
                                        uint64_t a0, uint64_t a1, uint64_t a2,
                                        uint64_t a3, uint64_t a4, uint64_t a5)
{
    return dispatch(num, a0, a1, a2, a3, a4, a5);
}

} // namespace syscall
} // namespace ppc64
} // namespace arch
} // namespace kernel
