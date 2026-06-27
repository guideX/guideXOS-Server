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
// Simple Kernel Heap Allocator
// This is a small free-list allocator backed by a fixed kernel arena.
// It keeps bare-metal image/viewer allocations reclaimable without pulling in
// a full general-purpose heap implementation.
// ============================================================================

namespace {
    // 32 MB kernel heap - large enough for wallpaper-sized decoded PNGs.
    // A 1536x1024 RGBA image is about 6 MB, and the loader may briefly need
    // extra slack for STBI bookkeeping and app/window allocations.
    static constexpr size_t KERNEL_HEAP_SIZE = 32u * 1024u * 1024u;
    static constexpr size_t KERNEL_HEAP_ALIGNMENT = 8u;

    struct KernelHeapBlock {
        size_t size;
        KernelHeapBlock* next;
        KernelHeapBlock* prev;
        uint32_t free;
    };

    static_assert(sizeof(KernelHeapBlock) % KERNEL_HEAP_ALIGNMENT == 0, "kernel heap header must stay aligned");

    alignas(KERNEL_HEAP_ALIGNMENT) static uint8_t g_kernelHeap[KERNEL_HEAP_SIZE];
    static KernelHeapBlock* g_heapHead = nullptr;
    static bool g_heapInitialized = false;

    static size_t align_size(size_t size) {
        return (size + (KERNEL_HEAP_ALIGNMENT - 1)) & ~(KERNEL_HEAP_ALIGNMENT - 1);
    }

    static void kernel_heap_init() {
        if (g_heapInitialized) return;
        g_heapHead = reinterpret_cast<KernelHeapBlock*>(g_kernelHeap);
        g_heapHead->size = KERNEL_HEAP_SIZE - sizeof(KernelHeapBlock);
        g_heapHead->next = nullptr;
        g_heapHead->prev = nullptr;
        g_heapHead->free = 1;
        g_heapInitialized = true;
    }

    static uint8_t* block_payload(KernelHeapBlock* block) {
        return reinterpret_cast<uint8_t*>(block) + sizeof(KernelHeapBlock);
    }

    static KernelHeapBlock* payload_block(void* ptr) {
        return reinterpret_cast<KernelHeapBlock*>(static_cast<uint8_t*>(ptr) - sizeof(KernelHeapBlock));
    }

    static bool blocks_touching(const KernelHeapBlock* left, const KernelHeapBlock* right) {
        if (!left || !right) return false;
        const uint8_t* leftEnd = reinterpret_cast<const uint8_t*>(left) + sizeof(KernelHeapBlock) + left->size;
        return leftEnd == reinterpret_cast<const uint8_t*>(right);
    }

    static void merge_with_next(KernelHeapBlock* block) {
        if (!block || !block->next || !block->next->free || !blocks_touching(block, block->next)) return;
        KernelHeapBlock* next = block->next;
        block->size += sizeof(KernelHeapBlock) + next->size;
        block->next = next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    void* kernel_alloc(size_t size) {
        kernel_heap_init();
        size_t alignedSize = align_size(size ? size : 1u);
        for (KernelHeapBlock* block = g_heapHead; block; block = block->next) {
            if (!block->free || block->size < alignedSize) {
                continue;
            }

            size_t remaining = block->size - alignedSize;
            if (remaining >= sizeof(KernelHeapBlock) + KERNEL_HEAP_ALIGNMENT) {
                KernelHeapBlock* split = reinterpret_cast<KernelHeapBlock*>(block_payload(block) + alignedSize);
                split->size = remaining - sizeof(KernelHeapBlock);
                split->next = block->next;
                split->prev = block;
                split->free = 1;
                if (split->next) {
                    split->next->prev = split;
                }
                block->next = split;
                block->size = alignedSize;
            }

            block->free = 0;
            return block_payload(block);
        }

        return nullptr;
    }

    void kernel_free(void* ptr) {
        if (!ptr) return;
        kernel_heap_init();

        KernelHeapBlock* block = payload_block(ptr);
        const uint8_t* heapBegin = g_kernelHeap;
        const uint8_t* heapEnd = g_kernelHeap + KERNEL_HEAP_SIZE;
        const uint8_t* blockBytes = reinterpret_cast<const uint8_t*>(block);
        if (blockBytes < heapBegin || blockBytes >= heapEnd) {
            return;
        }

        if (block->free) {
            return;
        }

        block->free = 1;
        merge_with_next(block);
        if (block->prev && block->prev->free) {
            block = block->prev;
            merge_with_next(block);
        }
    }
}

// ============================================================================
// C++ operator new/delete
// These use the fixed kernel free-list heap so large image buffers can be
// released and reused after a preview closes or reloads.
// ============================================================================

void* operator new(size_t size) throw()
{
    return kernel_alloc(size);
}

void* operator new[](size_t size) throw()
{
    return kernel_alloc(size);
}

void operator delete(void* ptr) noexcept
{
    kernel_free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    kernel_free(ptr);
}

void operator delete(void* ptr, size_t) noexcept
{
    kernel_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
    kernel_free(ptr);
}

extern "C" size_t gxos_kernel_heap_total_bytes()
{
    return KERNEL_HEAP_SIZE;
}

extern "C" size_t gxos_kernel_heap_used_bytes()
{
    kernel_heap_init();
    size_t freeBytes = 0;
    for (KernelHeapBlock* block = g_heapHead; block; block = block->next) {
        if (block->free) {
            freeBytes += block->size;
        }
    }
    return KERNEL_HEAP_SIZE - freeBytes;
}

extern "C" size_t gxos_kernel_heap_free_bytes()
{
    kernel_heap_init();
    size_t freeBytes = 0;
    for (KernelHeapBlock* block = g_heapHead; block; block = block->next) {
        if (block->free) {
            freeBytes += block->size;
        }
    }
    return freeBytes;
}

extern "C" size_t gxos_kernel_heap_largest_free_bytes()
{
    kernel_heap_init();
    size_t largest = 0;
    for (KernelHeapBlock* block = g_heapHead; block; block = block->next) {
        if (block->free && block->size > largest) {
            largest = block->size;
        }
    }
    return largest;
}

// GCC emits references to __dso_handle for static object destruction
// registration (__cxa_atexit).  In a freestanding kernel there is no
// dynamic linker, so define it as a weak null symbol so the linker is
// satisfied without pulling in atexit machinery.
void* __dso_handle __attribute__((weak)) = nullptr;

#endif // !_MSC_VER
