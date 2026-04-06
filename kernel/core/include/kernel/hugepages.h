// Huge Pages Support
//
// Extends the paging subsystem to support large page sizes:
//   - 2MB pages (x86-64 PDE with PS bit, ARM64 level 2 block)
//   - 1GB pages (x86-64 PDPE with PS bit, ARM64 level 1 block)
//
// Huge pages reduce TLB pressure and improve performance for
// large contiguous memory regions.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace memory {
namespace hugepages {

// ================================================================
// Page Size Constants
// ================================================================

// Standard page sizes
static const uint64_t PAGE_SIZE_4K   = 0x1000ULL;         // 4 KB
static const uint64_t PAGE_SIZE_2M   = 0x200000ULL;       // 2 MB
static const uint64_t PAGE_SIZE_1G   = 0x40000000ULL;     // 1 GB

// Alignment masks
static const uint64_t PAGE_MASK_4K   = ~(PAGE_SIZE_4K - 1);
static const uint64_t PAGE_MASK_2M   = ~(PAGE_SIZE_2M - 1);
static const uint64_t PAGE_MASK_1G   = ~(PAGE_SIZE_1G - 1);

// Pages per huge page
static const uint64_t PAGES_PER_2M   = PAGE_SIZE_2M / PAGE_SIZE_4K;    // 512
static const uint64_t PAGES_PER_1G   = PAGE_SIZE_1G / PAGE_SIZE_4K;    // 262144

// ================================================================
// Page Table Entry Flags (x86-64)
// ================================================================

namespace x86_64 {
    static const uint64_t PTE_P       = (1ULL << 0);   // Present
    static const uint64_t PTE_W       = (1ULL << 1);   // Writable
    static const uint64_t PTE_U       = (1ULL << 2);   // User accessible
    static const uint64_t PTE_PWT     = (1ULL << 3);   // Page Write-Through
    static const uint64_t PTE_PCD     = (1ULL << 4);   // Page Cache Disable
    static const uint64_t PTE_A       = (1ULL << 5);   // Accessed
    static const uint64_t PTE_D       = (1ULL << 6);   // Dirty
    static const uint64_t PTE_PS      = (1ULL << 7);   // Page Size (huge page)
    static const uint64_t PTE_G       = (1ULL << 8);   // Global
    static const uint64_t PTE_PAT     = (1ULL << 12);  // Page Attribute Table (for 4K)
    static const uint64_t PTE_PAT_HUGE = (1ULL << 12); // PAT for huge pages
    static const uint64_t PTE_NX      = (1ULL << 63);  // No Execute
    
    // Address masks
    static const uint64_t ADDR_MASK_4K = 0x000FFFFFFFFFF000ULL;
    static const uint64_t ADDR_MASK_2M = 0x000FFFFFFFE00000ULL;
    static const uint64_t ADDR_MASK_1G = 0x000FFFFFC0000000ULL;
}

// ================================================================
// Page Table Entry Flags (ARM64)
// ================================================================

namespace arm64 {
    static const uint64_t PTE_VALID   = (1ULL << 0);   // Valid
    static const uint64_t PTE_TABLE   = (1ULL << 1);   // Table descriptor
    static const uint64_t PTE_BLOCK   = (0ULL << 1);   // Block descriptor
    static const uint64_t PTE_PAGE    = (1ULL << 1);   // Page descriptor
    static const uint64_t PTE_AF      = (1ULL << 10);  // Access Flag
    static const uint64_t PTE_nG      = (1ULL << 11);  // Not Global
    static const uint64_t PTE_DBM     = (1ULL << 51);  // Dirty Bit Modifier
    static const uint64_t PTE_CONTIG  = (1ULL << 52);  // Contiguous hint
    static const uint64_t PTE_PXN     = (1ULL << 53);  // Privileged Execute Never
    static const uint64_t PTE_UXN     = (1ULL << 54);  // User Execute Never
    
    // Access permissions
    static const uint64_t PTE_AP_RW_EL1 = (0ULL << 6); // RW at EL1 only
    static const uint64_t PTE_AP_RW_ALL = (1ULL << 6); // RW at EL0 and EL1
    static const uint64_t PTE_AP_RO_EL1 = (2ULL << 6); // RO at EL1 only
    static const uint64_t PTE_AP_RO_ALL = (3ULL << 6); // RO at EL0 and EL1
    
    // Shareability
    static const uint64_t PTE_SH_NS   = (0ULL << 8);   // Non-shareable
    static const uint64_t PTE_SH_OS   = (2ULL << 8);   // Outer shareable
    static const uint64_t PTE_SH_IS   = (3ULL << 8);   // Inner shareable
    
    // Address masks (4K granule)
    static const uint64_t ADDR_MASK_4K = 0x0000FFFFFFFFF000ULL;
    static const uint64_t ADDR_MASK_2M = 0x0000FFFFFFE00000ULL;
    static const uint64_t ADDR_MASK_1G = 0x0000FFFFC0000000ULL;
}

// ================================================================
// Huge Page Allocation Result
// ================================================================

struct HugePageAlloc {
    uint64_t physAddr;       // Physical address of allocated page
    uint64_t virtAddr;       // Virtual address (if mapped)
    uint64_t size;           // Page size (PAGE_SIZE_2M or PAGE_SIZE_1G)
    bool success;            // Allocation succeeded
};

// ================================================================
// Huge Page Pool Statistics
// ================================================================

struct HugePageStats {
    // 2MB pages
    uint64_t total2M;        // Total 2MB pages available
    uint64_t free2M;         // Free 2MB pages
    uint64_t reserved2M;     // Reserved 2MB pages
    
    // 1GB pages
    uint64_t total1G;        // Total 1GB pages available
    uint64_t free1G;         // Free 1GB pages
    uint64_t reserved1G;     // Reserved 1GB pages
    
    // Usage statistics
    uint64_t alloc2MCount;   // Total 2MB allocations
    uint64_t alloc1GCount;   // Total 1GB allocations
    uint64_t free2MCount;    // Total 2MB frees
    uint64_t free1GCount;    // Total 1GB frees
};

// ================================================================
// Huge Page Functions
// ================================================================

// Initialize huge pages subsystem
// Scans physical memory for regions suitable for huge pages
bool init();

// Check if huge pages are supported
bool isSupported();

// Check if 1GB pages are supported (requires CPU feature)
bool is1GBSupported();

// Reserve a pool of 2MB huge pages
// count: number of 2MB pages to reserve
// Returns: number of pages actually reserved
uint64_t reserve2M(uint64_t count);

// Reserve a pool of 1GB huge pages
// count: number of 1GB pages to reserve
// Returns: number of pages actually reserved
uint64_t reserve1G(uint64_t count);

// Allocate a 2MB huge page
// Returns physical address, or 0 on failure
uint64_t alloc2M();

// Allocate a 1GB huge page
// Returns physical address, or 0 on failure
uint64_t alloc1G();

// Free a 2MB huge page
void free2M(uint64_t physAddr);

// Free a 1GB huge page
void free1G(uint64_t physAddr);

// Map a 2MB huge page into virtual address space
// virtAddr: virtual address (must be 2MB aligned)
// physAddr: physical address (must be 2MB aligned)
// flags: protection flags
bool map2M(uint64_t virtAddr, uint64_t physAddr, uint64_t flags);

// Map a 1GB huge page into virtual address space
// virtAddr: virtual address (must be 1GB aligned)
// physAddr: physical address (must be 1GB aligned)
// flags: protection flags
bool map1G(uint64_t virtAddr, uint64_t physAddr, uint64_t flags);

// Unmap a 2MB huge page
// virtAddr: virtual address of mapped page
bool unmap2M(uint64_t virtAddr);

// Unmap a 1GB huge page
// virtAddr: virtual address of mapped page
bool unmap1G(uint64_t virtAddr);

// Get huge page statistics
void getStats(HugePageStats* stats);

// ================================================================
// Helper Functions
// ================================================================

// Check if address is 2MB aligned
inline bool isAligned2M(uint64_t addr)
{
    return (addr & (PAGE_SIZE_2M - 1)) == 0;
}

// Check if address is 1GB aligned
inline bool isAligned1G(uint64_t addr)
{
    return (addr & (PAGE_SIZE_1G - 1)) == 0;
}

// Align address down to 2MB boundary
inline uint64_t alignDown2M(uint64_t addr)
{
    return addr & PAGE_MASK_2M;
}

// Align address down to 1GB boundary
inline uint64_t alignDown1G(uint64_t addr)
{
    return addr & PAGE_MASK_1G;
}

// Align address up to 2MB boundary
inline uint64_t alignUp2M(uint64_t addr)
{
    return (addr + PAGE_SIZE_2M - 1) & PAGE_MASK_2M;
}

// Align address up to 1GB boundary
inline uint64_t alignUp1G(uint64_t addr)
{
    return (addr + PAGE_SIZE_1G - 1) & PAGE_MASK_1G;
}

// Calculate number of 2MB pages needed for size
inline uint64_t pagesNeeded2M(uint64_t size)
{
    return (size + PAGE_SIZE_2M - 1) / PAGE_SIZE_2M;
}

// Calculate number of 1GB pages needed for size
inline uint64_t pagesNeeded1G(uint64_t size)
{
    return (size + PAGE_SIZE_1G - 1) / PAGE_SIZE_1G;
}

// ================================================================
// Page Table Manipulation (Architecture-Specific)
// ================================================================

// x86-64 specific
namespace x86_64 {
    // Create a 2MB page directory entry
    uint64_t make_pde_2m(uint64_t physAddr, uint64_t flags);
    
    // Create a 1GB page directory pointer entry
    uint64_t make_pdpe_1g(uint64_t physAddr, uint64_t flags);
    
    // Split a 2MB page into 4K pages
    bool split_2m_to_4k(uint64_t virtAddr, uint64_t* ptPhys);
    
    // Split a 1GB page into 2MB pages
    bool split_1g_to_2m(uint64_t virtAddr, uint64_t* pdPhys);
    
    // Coalesce 512 contiguous 4K pages into a 2MB page
    bool coalesce_4k_to_2m(uint64_t virtAddr);
    
    // Coalesce 512 contiguous 2MB pages into a 1GB page
    bool coalesce_2m_to_1g(uint64_t virtAddr);
}

// ARM64 specific
namespace arm64 {
    // Create a level 2 block descriptor (2MB)
    uint64_t make_block_l2(uint64_t physAddr, uint64_t flags);
    
    // Create a level 1 block descriptor (1GB)
    uint64_t make_block_l1(uint64_t physAddr, uint64_t flags);
    
    // Split a 2MB block into 4K pages
    bool split_2m_to_4k(uint64_t virtAddr, uint64_t* l3TablePhys);
    
    // Split a 1GB block into 2MB blocks
    bool split_1g_to_2m(uint64_t virtAddr, uint64_t* l2TablePhys);
    
    // Coalesce 512 contiguous 4K pages into a 2MB block
    bool coalesce_4k_to_2m(uint64_t virtAddr);
    
    // Coalesce 512 contiguous 2MB blocks into a 1GB block
    bool coalesce_2m_to_1g(uint64_t virtAddr);
}

} // namespace hugepages
} // namespace memory
} // namespace kernel
