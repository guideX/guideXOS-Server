//
// C++ Runtime Support for Freestanding Kernel
// Provides stub implementations for C++ runtime functions required by
// the compiler but not available in freestanding environments.
//
// Copyright (c) 2026 guideXOS Server
//

#include <kernel/types.h>

// Avoid GCC generating libstdc++ calls
#if !defined(_MSC_VER)

extern "C" {

// ============================================================================
// Static object destruction registration
// In a kernel, we never exit, so static destructors don't need to run.
// ============================================================================

// Called by the compiler to register static object destructors
int __cxa_atexit(void (*)(void*), void*, void*)
{
    // In a kernel, we never call static destructors, so just ignore
    return 0;
}

// Placeholder for atexit() which GCC sometimes generates
int atexit(void (*)())
{
    return 0;
}

// ============================================================================
// Pure virtual function handler
// Called if a pure virtual function is somehow invoked
// ============================================================================

void __cxa_pure_virtual()
{
    // Kernel panic or halt - should never happen
    while (1) {
        // Infinite loop - this is a fatal error
    }
}

// ============================================================================
// Guard variables for thread-safe static initialization
// In a single-threaded kernel boot, these can be simple
// ============================================================================

int __cxa_guard_acquire(uint64_t* guard)
{
    if (*guard) return 0;  // Already initialized
    return 1;  // Needs initialization
}

void __cxa_guard_release(uint64_t* guard)
{
    *guard = 1;  // Mark as initialized
}

void __cxa_guard_abort(uint64_t*)
{
    // Initialization failed - nothing to do in kernel
}

// ============================================================================
// Memory functions that GCC may generate calls to
// ============================================================================

void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* d = static_cast<uint8_t*>(dest);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* dest, int c, size_t n)
{
    uint8_t* d = static_cast<uint8_t*>(dest);
    for (size_t i = 0; i < n; ++i) {
        d[i] = static_cast<uint8_t>(c);
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t n)
{
    uint8_t* d = static_cast<uint8_t*>(dest);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    
    if (d < s) {
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    const uint8_t* p1 = static_cast<const uint8_t*>(s1);
    const uint8_t* p2 = static_cast<const uint8_t*>(s2);
    
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] < p2[i]) return -1;
        if (p1[i] > p2[i]) return 1;
    }
    return 0;
}

} // extern "C"

// ============================================================================
// C++ operator new/delete
// These are stubs - in a real kernel, they would call the kernel allocator
// ============================================================================

void* operator new(size_t) throw()
{
    return nullptr;
}

void* operator new[](size_t) throw()
{
    return nullptr;
}

void operator delete(void*) noexcept
{
}

void operator delete[](void*) noexcept
{
}

void operator delete(void*, size_t) noexcept
{
}

void operator delete[](void*, size_t) noexcept
{
}

#endif // !_MSC_VER
