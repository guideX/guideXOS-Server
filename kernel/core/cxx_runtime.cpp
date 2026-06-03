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

size_t strlen(const char* s)
{
    size_t len = 0;
    if (!s) return 0;
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}

int strcmp(const char* a, const char* b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

int strncmp(const char* a, const char* b, size_t n)
{
    if (n == 0 || a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca != cb || ca == '\0' || cb == '\0') {
            return ca - cb;
        }
    }
    return 0;
}

char* strchr(const char* s, int c)
{
    if (!s) return nullptr;
    const char target = static_cast<char>(c);
    while (*s) {
        if (*s == target) return const_cast<char*>(s);
        ++s;
    }
    return target == '\0' ? const_cast<char*>(s) : nullptr;
}

char* strrchr(const char* s, int c)
{
    if (!s) return nullptr;
    const char target = static_cast<char>(c);
    const char* last = nullptr;
    while (*s) {
        if (*s == target) last = s;
        ++s;
    }
    if (target == '\0') return const_cast<char*>(s);
    return const_cast<char*>(last);
}

char* strstr(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return nullptr;
    if (*needle == '\0') return const_cast<char*>(haystack);

    for (const char* h = haystack; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return const_cast<char*>(h);
        }
    }
    return nullptr;
}

} // extern "C"

// ============================================================================
// Simple Kernel Heap Allocator (Bump Allocator)
// This is a basic allocator that only allocates, never frees.
// Sufficient for kernel GUI apps that have a fixed lifetime.
// ============================================================================

namespace {
    // 1 MB kernel heap - required for Navigator and multiple concurrent app windows
    static constexpr size_t KERNEL_HEAP_SIZE = 1024 * 1024;
    static uint8_t g_kernelHeap[KERNEL_HEAP_SIZE];
    static size_t g_heapOffset = 0;
    
    void* kernel_alloc(size_t size) {
        // Align to 8 bytes for proper alignment
        size_t alignedSize = (size + 7) & ~7;
        
        if (g_heapOffset + alignedSize > KERNEL_HEAP_SIZE) {
            return nullptr;  // Out of memory
        }
        
        void* ptr = &g_kernelHeap[g_heapOffset];
        g_heapOffset += alignedSize;
        return ptr;
    }
}

// ============================================================================
// C++ operator new/delete
// These use the kernel bump allocator
// ============================================================================

void* operator new(size_t size) throw()
{
    return kernel_alloc(size);
}

void* operator new[](size_t size) throw()
{
    return kernel_alloc(size);
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

// GCC emits references to __dso_handle for static object destruction
// registration (__cxa_atexit).  In a freestanding kernel there is no
// dynamic linker, so define it as a weak null symbol so the linker is
// satisfied without pulling in atexit machinery.
void* __dso_handle __attribute__((weak)) = nullptr;

#endif // !_MSC_VER
