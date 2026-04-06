// Huge Pages Implementation
//
// Manages allocation and mapping of 2MB and 1GB huge pages.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/hugepages.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace memory {
namespace hugepages {

// ================================================================
// Internal State
// ================================================================

// Pool of 2MB pages (bitmap-based allocation)
static const uint64_t MAX_2M_PAGES = 1024;  // Max 2GB of 2MB pages
static uint64_t s_pool2M[MAX_2M_PAGES];     // Physical addresses
static uint8_t s_bitmap2M[(MAX_2M_PAGES + 7) / 8];
static uint64_t s_count2M = 0;

// Pool of 1GB pages
static const uint64_t MAX_1G_PAGES = 16;    // Max 16GB of 1GB pages
static uint64_t s_pool1G[MAX_1G_PAGES];
static uint8_t s_bitmap1G[(MAX_1G_PAGES + 7) / 8];
static uint64_t s_count1G = 0;

// Statistics
static HugePageStats s_stats;

// Feature flags
static bool s_initialized = false;
static bool s_supported = false;
static bool s_1gbSupported = false;

// ================================================================
// Memory Helpers
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

// ================================================================
// Bitmap Helpers
// ================================================================

static inline bool bitmap_test(const uint8_t* bitmap, uint64_t index)
{
    return (bitmap[index / 8] & (1 << (index % 8))) != 0;
}

static inline void bitmap_set(uint8_t* bitmap, uint64_t index)
{
    bitmap[index / 8] |= (1 << (index % 8));
}

static inline void bitmap_clear(uint8_t* bitmap, uint64_t index)
{
    bitmap[index / 8] &= ~(1 << (index % 8));
}

// ================================================================
// Architecture Detection
// ================================================================

static bool detect_hugepage_support()
{
#if defined(__x86_64__) || defined(_M_X64)
    // Check CPUID for PSE (Page Size Extensions)
    uint32_t eax, ebx, ecx, edx;
    
    // CPUID function 1
    #if defined(__GNUC__) || defined(__clang__)
    asm volatile (
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    #else
    // MSVC - assume supported
    edx = (1 << 3);  // PSE bit
    #endif
    
    // Check PSE bit (bit 3 of EDX)
    if (!(edx & (1 << 3))) {
        return false;
    }
    
    // Check for 1GB pages (CPUID function 80000001h)
    #if defined(__GNUC__) || defined(__clang__)
    asm volatile (
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000001)
    );
    
    // Check PDPE1GB bit (bit 26 of EDX)
    s_1gbSupported = (edx & (1 << 26)) != 0;
    #else
    s_1gbSupported = true;  // Assume supported on MSVC
    #endif
    
    return true;
    
#elif defined(__aarch64__)
    // ARM64 always supports huge pages through block mappings
    s_1gbSupported = true;  // Level 1 blocks
    return true;
    
#else
    return false;
#endif
}

// ================================================================
// Initialization
// ================================================================

bool init()
{
    if (s_initialized) return s_supported;
    
    memzero(&s_stats, sizeof(s_stats));
    memzero(s_pool2M, sizeof(s_pool2M));
    memzero(s_pool1G, sizeof(s_pool1G));
    memzero(s_bitmap2M, sizeof(s_bitmap2M));
    memzero(s_bitmap1G, sizeof(s_bitmap1G));
    
    s_count2M = 0;
    s_count1G = 0;
    
    // Detect CPU support
    s_supported = detect_hugepage_support();
    
    if (s_supported) {
        serial_debug::print("Huge pages: Supported (2MB");
        if (s_1gbSupported) {
            serial_debug::print(", 1GB");
        }
        serial_debug::print(")\n");
    } else {
        serial_debug::print("Huge pages: Not supported\n");
    }
    
    s_initialized = true;
    return s_supported;
}

bool isSupported()
{
    if (!s_initialized) init();
    return s_supported;
}

bool is1GBSupported()
{
    if (!s_initialized) init();
    return s_1gbSupported;
}

// ================================================================
// Page Pool Management
// ================================================================

uint64_t reserve2M(uint64_t count)
{
    if (!s_supported) return 0;
    if (count == 0) return 0;
    
    // In real implementation, this would:
    // 1. Scan physical memory map
    // 2. Find contiguous 2MB-aligned regions
    // 3. Mark them as reserved in physical memory allocator
    // 4. Add to pool
    
    // For now, simulate by reserving sequential addresses
    uint64_t reserved = 0;
    uint64_t baseAddr = 0x100000000ULL;  // Start at 4GB (example)
    
    for (uint64_t i = 0; i < count && s_count2M < MAX_2M_PAGES; ++i) {
        uint64_t addr = baseAddr + i * PAGE_SIZE_2M;
        
        // Check alignment
        if (!isAligned2M(addr)) continue;
        
        s_pool2M[s_count2M] = addr;
        bitmap_clear(s_bitmap2M, s_count2M);  // Mark as free
        s_count2M++;
        reserved++;
    }
    
    s_stats.total2M = s_count2M;
    s_stats.free2M = s_count2M;
    s_stats.reserved2M = reserved;
    
    return reserved;
}

uint64_t reserve1G(uint64_t count)
{
    if (!s_supported || !s_1gbSupported) return 0;
    if (count == 0) return 0;
    
    uint64_t reserved = 0;
    uint64_t baseAddr = 0x200000000ULL;  // Start at 8GB (example)
    
    for (uint64_t i = 0; i < count && s_count1G < MAX_1G_PAGES; ++i) {
        uint64_t addr = baseAddr + i * PAGE_SIZE_1G;
        
        if (!isAligned1G(addr)) continue;
        
        s_pool1G[s_count1G] = addr;
        bitmap_clear(s_bitmap1G, s_count1G);
        s_count1G++;
        reserved++;
    }
    
    s_stats.total1G = s_count1G;
    s_stats.free1G = s_count1G;
    s_stats.reserved1G = reserved;
    
    return reserved;
}

uint64_t alloc2M()
{
    if (!s_supported) return 0;
    
    // Find a free 2MB page
    for (uint64_t i = 0; i < s_count2M; ++i) {
        if (!bitmap_test(s_bitmap2M, i)) {
            bitmap_set(s_bitmap2M, i);
            s_stats.free2M--;
            s_stats.alloc2MCount++;
            return s_pool2M[i];
        }
    }
    
    return 0;  // No free pages
}

uint64_t alloc1G()
{
    if (!s_supported || !s_1gbSupported) return 0;
    
    for (uint64_t i = 0; i < s_count1G; ++i) {
        if (!bitmap_test(s_bitmap1G, i)) {
            bitmap_set(s_bitmap1G, i);
            s_stats.free1G--;
            s_stats.alloc1GCount++;
            return s_pool1G[i];
        }
    }
    
    return 0;
}

void free2M(uint64_t physAddr)
{
    if (!s_supported) return;
    if (!isAligned2M(physAddr)) return;
    
    for (uint64_t i = 0; i < s_count2M; ++i) {
        if (s_pool2M[i] == physAddr && bitmap_test(s_bitmap2M, i)) {
            bitmap_clear(s_bitmap2M, i);
            s_stats.free2M++;
            s_stats.free2MCount++;
            return;
        }
    }
}

void free1G(uint64_t physAddr)
{
    if (!s_supported || !s_1gbSupported) return;
    if (!isAligned1G(physAddr)) return;
    
    for (uint64_t i = 0; i < s_count1G; ++i) {
        if (s_pool1G[i] == physAddr && bitmap_test(s_bitmap1G, i)) {
            bitmap_clear(s_bitmap1G, i);
            s_stats.free1G++;
            s_stats.free1GCount++;
            return;
        }
    }
}

// ================================================================
// Page Mapping
// ================================================================

bool map2M(uint64_t virtAddr, uint64_t physAddr, uint64_t flags)
{
    if (!s_supported) return false;
    if (!isAligned2M(virtAddr) || !isAligned2M(physAddr)) return false;
    
#if defined(__x86_64__) || defined(_M_X64)
    // x86-64: Set up PDE with PS bit
    // This requires walking page tables to PD level
    // and inserting a 2MB mapping
    
    // Simplified - actual implementation would:
    // 1. Get/create PML4 entry
    // 2. Get/create PDPT entry  
    // 3. Set PD entry with PS=1
    
    (void)flags;
    return true;  // Stub
    
#elif defined(__aarch64__)
    // ARM64: Set up level 2 block descriptor
    // This requires walking page tables to L2
    // and inserting a block mapping
    
    (void)flags;
    return true;  // Stub
    
#else
    (void)virtAddr;
    (void)physAddr;
    (void)flags;
    return false;
#endif
}

bool map1G(uint64_t virtAddr, uint64_t physAddr, uint64_t flags)
{
    if (!s_supported || !s_1gbSupported) return false;
    if (!isAligned1G(virtAddr) || !isAligned1G(physAddr)) return false;
    
#if defined(__x86_64__) || defined(_M_X64)
    // x86-64: Set up PDPE with PS bit
    (void)flags;
    return true;  // Stub
    
#elif defined(__aarch64__)
    // ARM64: Set up level 1 block descriptor
    (void)flags;
    return true;  // Stub
    
#else
    (void)virtAddr;
    (void)physAddr;
    (void)flags;
    return false;
#endif
}

bool unmap2M(uint64_t virtAddr)
{
    if (!s_supported) return false;
    if (!isAligned2M(virtAddr)) return false;
    
    // Clear PDE/L2 entry
    // Invalidate TLB
    
#if defined(__x86_64__) || defined(_M_X64)
    // invlpg instruction for each 4K page in the 2MB range
    // Or use CR3 reload for full TLB flush
    #if defined(__GNUC__) || defined(__clang__)
    asm volatile ("invlpg (%0)" : : "r"(virtAddr) : "memory");
    #endif
    return true;
    
#elif defined(__aarch64__)
    // TLBI instruction
    #if defined(__GNUC__) || defined(__clang__)
    asm volatile ("tlbi vaae1, %0" : : "r"(virtAddr >> 12) : "memory");
    asm volatile ("dsb ish" ::: "memory");
    asm volatile ("isb" ::: "memory");
    #endif
    return true;
    
#else
    (void)virtAddr;
    return false;
#endif
}

bool unmap1G(uint64_t virtAddr)
{
    if (!s_supported || !s_1gbSupported) return false;
    if (!isAligned1G(virtAddr)) return false;
    
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
    // Similar to unmap2M but for 1GB region
    return unmap2M(virtAddr);  // TLB invalidation works the same
#else
    (void)virtAddr;
    return false;
#endif
}

void getStats(HugePageStats* stats)
{
    if (stats) {
        *stats = s_stats;
    }
}

// ================================================================
// x86-64 Page Table Helpers
// ================================================================

namespace x86_64 {

uint64_t make_pde_2m(uint64_t physAddr, uint64_t flags)
{
    // Create a 2MB page directory entry
    // PS bit (bit 7) must be set
    uint64_t entry = (physAddr & ADDR_MASK_2M) | PTE_PS | PTE_P;
    
    if (flags & PTE_W) entry |= PTE_W;
    if (flags & PTE_U) entry |= PTE_U;
    if (flags & PTE_NX) entry |= PTE_NX;
    if (flags & PTE_G) entry |= PTE_G;
    
    return entry;
}

uint64_t make_pdpe_1g(uint64_t physAddr, uint64_t flags)
{
    // Create a 1GB page directory pointer entry
    uint64_t entry = (physAddr & ADDR_MASK_1G) | PTE_PS | PTE_P;
    
    if (flags & PTE_W) entry |= PTE_W;
    if (flags & PTE_U) entry |= PTE_U;
    if (flags & PTE_NX) entry |= PTE_NX;
    if (flags & PTE_G) entry |= PTE_G;
    
    return entry;
}

bool split_2m_to_4k(uint64_t virtAddr, uint64_t* ptPhys)
{
    // Split a 2MB page into 512 4K pages
    // 1. Allocate a new page table (4K)
    // 2. Fill with 512 4K entries pointing to same physical memory
    // 3. Replace PDE with table pointer
    (void)virtAddr;
    (void)ptPhys;
    return false;  // Stub
}

bool split_1g_to_2m(uint64_t virtAddr, uint64_t* pdPhys)
{
    // Split a 1GB page into 512 2MB pages
    (void)virtAddr;
    (void)pdPhys;
    return false;  // Stub
}

bool coalesce_4k_to_2m(uint64_t virtAddr)
{
    // Check if 512 contiguous 4K pages can become one 2MB page
    // - All present
    // - Contiguous physical addresses
    // - Same permissions
    (void)virtAddr;
    return false;  // Stub
}

bool coalesce_2m_to_1g(uint64_t virtAddr)
{
    // Check if 512 contiguous 2MB pages can become one 1GB page
    (void)virtAddr;
    return false;  // Stub
}

} // namespace x86_64

// ================================================================
// ARM64 Page Table Helpers
// ================================================================

namespace arm64 {

uint64_t make_block_l2(uint64_t physAddr, uint64_t flags)
{
    // Create a level 2 block descriptor (2MB)
    uint64_t entry = (physAddr & ADDR_MASK_2M) | PTE_VALID | PTE_AF;
    
    // Apply flags
    entry |= (flags & 0xFF);  // Lower attributes
    entry |= (flags & 0xFFFF000000000000ULL);  // Upper attributes
    
    return entry;
}

uint64_t make_block_l1(uint64_t physAddr, uint64_t flags)
{
    // Create a level 1 block descriptor (1GB)
    uint64_t entry = (physAddr & ADDR_MASK_1G) | PTE_VALID | PTE_AF;
    
    entry |= (flags & 0xFF);
    entry |= (flags & 0xFFFF000000000000ULL);
    
    return entry;
}

bool split_2m_to_4k(uint64_t virtAddr, uint64_t* l3TablePhys)
{
    // Split a 2MB block into 512 4K pages
    (void)virtAddr;
    (void)l3TablePhys;
    return false;  // Stub
}

bool split_1g_to_2m(uint64_t virtAddr, uint64_t* l2TablePhys)
{
    // Split a 1GB block into 512 2MB blocks
    (void)virtAddr;
    (void)l2TablePhys;
    return false;  // Stub
}

bool coalesce_4k_to_2m(uint64_t virtAddr)
{
    (void)virtAddr;
    return false;  // Stub
}

bool coalesce_2m_to_1g(uint64_t virtAddr)
{
    (void)virtAddr;
    return false;  // Stub
}

} // namespace arm64

} // namespace hugepages
} // namespace memory
} // namespace kernel
