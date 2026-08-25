//
// C++ Runtime Support for Freestanding Kernel
// Provides stub implementations for C++ runtime functions required by
// the compiler but not available in freestanding environments.
//
// Copyright (c) 2026 guideXOS Server
//

#include <kernel/types.h>
#include <kernel/serial_debug.h>

// These symbols are authoritative linker-provided bounds for the executable
// kernel text.  They remain weak so non-current architectures whose linker
// scripts do not yet publish the range keep their existing runtime behavior;
// the AMD64 and x86 RC linkers publish both symbols below.
extern "C" char __text_start[] __attribute__((weak));
extern "C" char __text_end[] __attribute__((weak));

namespace {

const char* g_kernelTextGuardScenario = "boot";
const char* g_kernelTextGuardStage = "unreported";

static uintptr_t kernel_text_start()
{
    return reinterpret_cast<uintptr_t>(__text_start);
}

static uintptr_t kernel_text_end()
{
    return reinterpret_cast<uintptr_t>(__text_end);
}

static uintptr_t current_stack_pointer()
{
#if defined(__x86_64__)
    uintptr_t value;
    asm volatile ("mov %%rsp, %0" : "=r"(value));
    return value;
#elif defined(__i386__)
    uintptr_t value;
    asm volatile ("mov %%esp, %0" : "=r"(value));
    return value;
#else
    return 0;
#endif
}

static bool kernel_text_range_overlaps(uintptr_t destination, size_t length,
                                       uintptr_t textStart, uintptr_t textEnd,
                                       uintptr_t* overlapStart, uintptr_t* overlapEnd,
                                       bool* rangeOverflow)
{
    *overlapStart = 0;
    *overlapEnd = 0;
    *rangeOverflow = false;
    if (length == 0 || textStart == 0 || textEnd <= textStart) return false;

    const uintptr_t addressMax = static_cast<uintptr_t>(~static_cast<uintptr_t>(0));
    if (destination > addressMax - static_cast<uintptr_t>(length)) {
        // An overflowing destination range is itself invalid.  Treat it as a
        // fail-closed guard hit so no potentially destructive loop starts.
        *overlapStart = destination;
        *overlapEnd = addressMax;
        *rangeOverflow = true;
        return true;
    }

    const uintptr_t destinationEnd = destination + static_cast<uintptr_t>(length);
    *overlapStart = destination > textStart ? destination : textStart;
    *overlapEnd = destinationEnd < textEnd ? destinationEnd : textEnd;
    return *overlapStart < *overlapEnd;
}

static void halt_after_kernel_text_guard()
{
    kernel::serial::puts("[KERNEL-TEXT-WRITE] action=halted_before_write\n");
    while (1) {
        kernel::arch::halt();
    }
}

static void guard_kernel_text_write(const char* primitive, void* destination,
                                    const void* source, size_t length, uint8_t fill,
                                    uintptr_t callerReturn, uintptr_t stackPointer,
                                    uintptr_t framePointer)
{
    const uintptr_t textStart = kernel_text_start();
    const uintptr_t textEnd = kernel_text_end();
    uintptr_t overlapStart = 0;
    uintptr_t overlapEnd = 0;
    bool rangeOverflow = false;
    if (!kernel_text_range_overlaps(reinterpret_cast<uintptr_t>(destination), length,
                                    textStart, textEnd, &overlapStart, &overlapEnd,
                                    &rangeOverflow)) {
        return;
    }

    kernel::serial::puts("[KERNEL-TEXT-WRITE] primitive=");
    kernel::serial::puts(primitive);
    kernel::serial::puts(" destination=0x");
    kernel::serial::put_hex64(reinterpret_cast<uintptr_t>(destination));
    kernel::serial::puts(" source=0x");
    kernel::serial::put_hex64(reinterpret_cast<uintptr_t>(source));
    kernel::serial::puts(" length=0x");
    kernel::serial::put_hex64(static_cast<uint64_t>(length));
    kernel::serial::puts(" fill=0x");
    kernel::serial::put_hex8(fill);
    kernel::serial::puts(" text_start=0x");
    kernel::serial::put_hex64(textStart);
    kernel::serial::puts(" text_end=0x");
    kernel::serial::put_hex64(textEnd);
    kernel::serial::puts(" overlap_start=0x");
    kernel::serial::put_hex64(overlapStart);
    kernel::serial::puts(" overlap_end=0x");
    kernel::serial::put_hex64(overlapEnd);
    kernel::serial::puts(" caller_return=0x");
    kernel::serial::put_hex64(callerReturn);
    kernel::serial::puts(" caller_symbol=(unavailable) caller_offset=");
    if (callerReturn >= textStart && callerReturn < textEnd) {
        kernel::serial::puts("0x");
        kernel::serial::put_hex64(callerReturn - textStart);
    } else {
        kernel::serial::puts("outside-text");
    }
    kernel::serial::puts(" stack_pointer=0x");
    kernel::serial::put_hex64(stackPointer);
    kernel::serial::puts(" frame_pointer=0x");
    kernel::serial::put_hex64(framePointer);
    kernel::serial::puts(" scenario=");
    kernel::serial::puts(g_kernelTextGuardScenario ? g_kernelTextGuardScenario : "unknown");
    kernel::serial::puts(" stage=");
    kernel::serial::puts(g_kernelTextGuardStage ? g_kernelTextGuardStage : "unknown");
    kernel::serial::puts(" cpu=0 range_overflow=");
    kernel::serial::puts(rangeOverflow ? "yes\n" : "no\n");
    halt_after_kernel_text_guard();
}

} // namespace

// Avoid GCC generating libstdc++ calls
#if !defined(_MSC_VER)

extern "C" {

void gxos_kernel_text_guard_set_context(const char* scenario, const char* stage)
{
    g_kernelTextGuardScenario = scenario ? scenario : "unknown";
    g_kernelTextGuardStage = stage ? stage : "unknown";
}

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
    kernel::serial::puts("[KERNEL-PANIC] assertion=pure-virtual-call halted=1\n");
    while (1) {
        // Infinite loop - this is a fatal error.
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
    guard_kernel_text_write("memcpy", dest, src, n, 0,
                            reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
                            current_stack_pointer(),
                            reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));
    uint8_t* d = static_cast<uint8_t*>(dest);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* dest, int c, size_t n)
{
    guard_kernel_text_write("memset", dest, nullptr, n, static_cast<uint8_t>(c),
                            reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
                            current_stack_pointer(),
                            reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));
    uint8_t* d = static_cast<uint8_t*>(dest);
    for (size_t i = 0; i < n; ++i) {
        d[i] = static_cast<uint8_t>(c);
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t n)
{
    guard_kernel_text_write("memmove", dest, src, n, 0,
                            reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
                            current_stack_pointer(),
                            reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));
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
    // Keep production bare metal bounded to the original 32 MiB arena.  The
    // opt-in Navigator HTTP smoke intentionally retains several 2048x2048
    // decoded fixtures at once so it can exercise the shared 64 MiB document
    // budget; give that diagnostic image enough physical heap to reach the
    // policy boundary instead of faulting inside STBI first.
#if defined(GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE)
    static constexpr size_t KERNEL_HEAP_SIZE = 128u * 1024u * 1024u;
#else
    static constexpr size_t KERNEL_HEAP_SIZE = 32u * 1024u * 1024u;
#endif
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
