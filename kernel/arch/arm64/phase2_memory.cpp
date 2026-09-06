#include <stdint.h>

#include "phase2_memory.h"
#include "phase2_mmu.h"
#include "../../../aarch64/phase2/phase2_validation.h"

namespace {

static const uint32_t kMaxFreeRanges = 128;
static const uint32_t kMaxProtectedRanges = 16;
static const uint64_t kPageMask = UINT64_C(0xfff);
static const uint64_t kMappedPhysicalLimit = UINT64_C(0x80000000);

struct Range { uint64_t start; uint64_t end; };
static Range gFreeRanges[kMaxFreeRanges];
static Range gProtectedRanges[kMaxProtectedRanges];
static uint32_t gFreeRangeCount = 0;
static uint32_t gProtectedRangeCount = 0;
static uint64_t gCursors[kMaxFreeRanges];
static uint64_t gFreePages = 0;

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64(const uint8_t* p)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (i * 8);
    return value;
}

static bool align_up_page(uint64_t value, uint64_t* result)
{
    if (!result || value > UINT64_MAX - kPageMask) return false;
    *result = (value + kPageMask) & ~kPageMask;
    return true;
}

static bool add_protected(uint64_t start, uint64_t size)
{
    uint64_t end = 0;
    if (size == 0 || !gxos_aarch64_add_u64(start, size, &end) || end <= start ||
        gProtectedRangeCount >= kMaxProtectedRanges) return false;
    uint64_t alignedStart = start & ~kPageMask;
    uint64_t alignedEnd = 0;
    if (!align_up_page(end, &alignedEnd) || alignedEnd <= alignedStart) return false;
    gProtectedRanges[gProtectedRangeCount++] = { alignedStart, alignedEnd };
    return true;
}

static bool protected_overlap(uint64_t start, uint64_t end)
{
    gxos_aarch64_validation_range candidate{ start, end };
    for (uint32_t i = 0; i < gProtectedRangeCount; ++i) {
        gxos_aarch64_validation_range protectedRange{ gProtectedRanges[i].start, gProtectedRanges[i].end };
        if (gxos_aarch64_range_overlaps(candidate, protectedRange)) return true;
    }
    return false;
}

static bool within_dtb_ram(uint64_t start, uint64_t end,
                           const gxos_aarch64_phase2_platform* platform,
                           uint64_t* clippedStart, uint64_t* clippedEnd)
{
    for (uint32_t i = 0; i < platform->ram_count; ++i) {
        uint64_t ramEnd = 0;
        if (!gxos_aarch64_add_u64(platform->ram[i].base, platform->ram[i].size, &ramEnd)) return false;
        uint64_t candidateStart = start > platform->ram[i].base ? start : platform->ram[i].base;
        uint64_t candidateEnd = end < ramEnd ? end : ramEnd;
        if (candidateStart < candidateEnd) {
            *clippedStart = candidateStart;
            *clippedEnd = candidateEnd;
            return true;
        }
    }
    return false;
}

static bool append_free_range(uint64_t start, uint64_t end)
{
    uint64_t alignedStart = 0;
    uint64_t alignedEnd = 0;
    if (!align_up_page(start, &alignedStart)) return false;
    alignedEnd = end & ~kPageMask;
    if (alignedStart >= alignedEnd) return true;
    if (gFreeRangeCount >= kMaxFreeRanges) return false;
    gFreeRanges[gFreeRangeCount] = { alignedStart, alignedEnd };
    gCursors[gFreeRangeCount] = alignedStart;
    gFreePages += (alignedEnd - alignedStart) / 0x1000;
    ++gFreeRangeCount;
    return true;
}

static bool append_unprotected(uint64_t start, uint64_t end)
{
    uint64_t cursor = start;
    for (uint32_t i = 0; i < gProtectedRangeCount; ++i) {
        const Range& protectedRange = gProtectedRanges[i];
        if (protectedRange.end <= cursor || protectedRange.start >= end) continue;
        if (protectedRange.start > cursor && !append_free_range(cursor, protectedRange.start)) return false;
        if (protectedRange.end > cursor) cursor = protectedRange.end;
        if (cursor >= end) return true;
    }
    return append_free_range(cursor, end);
}

} // namespace

uint8_t phase2_memory_validate_handoff(const gxos_aarch64_phase2_handoff* handoff)
{
    if (!handoff || handoff->magic != GXOS_AARCH64_PHASE2_HANDOFF_MAGIC ||
        handoff->version != GXOS_AARCH64_PHASE2_HANDOFF_VERSION ||
        handoff->size != sizeof(*handoff)) return 0;
    const uint32_t flags = GXOS_AARCH64_PHASE2_FLAG_EBS_COMPLETE |
                           GXOS_AARCH64_PHASE2_FLAG_IDENTITY_LOAD |
                           GXOS_AARCH64_PHASE2_FLAG_MMU_OFF_ON_ENTRY |
                           GXOS_AARCH64_PHASE2_FLAG_STACK_ALLOCATED |
                           GXOS_AARCH64_PHASE2_FLAG_MEMORY_MAP_VALID |
                           GXOS_AARCH64_PHASE2_FLAG_DTB_VALID |
                           GXOS_AARCH64_PHASE2_FLAG_DTB_COPIED;
    if ((handoff->flags & flags) != flags || handoff->kernel_base != GXOS_AARCH64_PHASE2_KERNEL_LOAD_ADDRESS ||
        handoff->kernel_size == 0 || handoff->stack_size == 0 || handoff->stack_top == 0 ||
        !gxos_aarch64_memory_map_layout_valid(handoff->memory_map, handoff->memory_map_size,
                                               handoff->memory_map_descriptor_size,
                                               handoff->memory_map_entry_count) ||
        handoff->dtb_base == 0 || handoff->dtb_size == 0 ||
        handoff->uart_base != GXOS_AARCH64_PHASE2_UART_FALLBACK ||
        handoff->initial_current_el < 1 || handoff->initial_current_el > 2) return 0;

    uint64_t end = 0;
    uint64_t kernelEnd = 0;
    if (!gxos_aarch64_add_u64(handoff->kernel_base, handoff->kernel_size, &kernelEnd) ||
        kernelEnd <= handoff->kernel_base || handoff->kernel_entry < handoff->kernel_base ||
        handoff->kernel_entry >= kernelEnd ||
        !gxos_aarch64_add_u64(handoff->stack_base, handoff->stack_size, &end) || end <= handoff->stack_base ||
        handoff->stack_top != end || (handoff->stack_top & 0xf) != 0 ||
        !gxos_aarch64_add_u64(handoff->dtb_base, handoff->dtb_size, &end) || end <= handoff->dtb_base) return 0;
    return 1;
}

uint8_t phase2_early_allocator_init(const gxos_aarch64_phase2_handoff* handoff,
                                    const gxos_aarch64_phase2_platform* platform,
                                    uint64_t handoff_address)
{
    if (!handoff || !platform || !platform->valid || !phase2_memory_validate_handoff(handoff)) return 0;
    gFreeRangeCount = 0;
    gProtectedRangeCount = 0;
    gFreePages = 0;

    if (!add_protected(handoff->kernel_base, handoff->kernel_size) ||
        !add_protected(handoff->stack_base, handoff->stack_size) ||
        !add_protected(handoff_address, 0x1000) ||
        !add_protected(handoff->memory_map, handoff->memory_map_size) ||
        !add_protected(handoff->dtb_base, handoff->dtb_size) ||
        !add_protected(phase2_mmu_tables_start(), phase2_mmu_tables_end() - phase2_mmu_tables_start())) return 0;

    for (uint32_t i = 1; i < gProtectedRangeCount; ++i) {
        Range value = gProtectedRanges[i];
        uint32_t j = i;
        while (j != 0 && gProtectedRanges[j - 1].start > value.start) {
            gProtectedRanges[j] = gProtectedRanges[j - 1];
            --j;
        }
        gProtectedRanges[j] = value;
    }

    for (uint64_t index = 0; index < handoff->memory_map_entry_count; ++index) {
        uint64_t byteOffset = 0;
        if (!gxos_aarch64_mul_u64(index, handoff->memory_map_descriptor_size, &byteOffset)) return 0;
        const uint8_t* descriptor = (const uint8_t*)(uintptr_t)(handoff->memory_map + byteOffset);
        const uint32_t type = read_u32(descriptor);
        // Only EfiConventionalMemory is free after EBS.  Boot/loader/runtime
        // allocations stay reserved; reclaim policy belongs to a later phase.
        if (type != 7) continue;
        uint64_t descriptorEnd = 0;
        const uint64_t physical = read_u64(descriptor + 8);
        const uint64_t pages = read_u64(descriptor + 24);
        if (!gxos_aarch64_page_descriptor_valid(physical, pages, &descriptorEnd)) return 0;
        if (physical >= kMappedPhysicalLimit) continue;
        if (descriptorEnd > kMappedPhysicalLimit) descriptorEnd = kMappedPhysicalLimit;
        uint64_t clippedStart = 0;
        uint64_t clippedEnd = 0;
        if (!within_dtb_ram(physical, descriptorEnd, platform, &clippedStart, &clippedEnd)) continue;
        if (!append_unprotected(clippedStart, clippedEnd)) return 0;
    }
    return gFreeRangeCount != 0;
}

uint8_t phase2_early_allocator_allocate(uint64_t pages, uint64_t* base)
{
    if (!base || pages == 0) return 0;
    uint64_t bytes = 0;
    if (!gxos_aarch64_mul_u64(pages, 0x1000, &bytes)) return 0;
    for (uint32_t i = 0; i < gFreeRangeCount; ++i) {
        uint64_t end = 0;
        if (!gxos_aarch64_add_u64(gCursors[i], bytes, &end) || end > gFreeRanges[i].end) continue;
        const uint64_t result = gCursors[i];
        if (protected_overlap(result, end)) return 0;
        gCursors[i] = end;
        gFreePages -= pages;
        *base = result;
        return 1;
    }
    return 0;
}

uint8_t phase2_early_allocator_release(uint64_t base, uint64_t pages)
{
    uint64_t bytes = 0;
    if (pages == 0 || !gxos_aarch64_mul_u64(pages, 0x1000, &bytes)) return 0;
    uint64_t end = 0;
    if (!gxos_aarch64_add_u64(base, bytes, &end)) return 0;
    // The cursor allocator supports bounded LIFO release.  This is enough for
    // early bring-up and lets a caller recycle the most recent allocation
    // without introducing a production free-list yet.
    for (uint32_t i = 0; i < gFreeRangeCount; ++i) {
        if (base >= gFreeRanges[i].start && end <= gFreeRanges[i].end &&
            end == gCursors[i]) {
            gCursors[i] = base;
            gFreePages += pages;
            return 1;
        }
    }
    return 0;
}

uint8_t phase2_early_allocator_is_protected(uint64_t base, uint64_t size)
{
    uint64_t end = 0;
    if (!gxos_aarch64_add_u64(base, size, &end) || end <= base) return 0;
    return protected_overlap(base, end) ? 1 : 0;
}

uint64_t phase2_early_allocator_free_pages() { return gFreePages; }

uint8_t phase2_early_allocator_self_test()
{
    uint64_t first = 0;
    uint64_t second = 0;
    if (!phase2_early_allocator_allocate(3, &first) ||
        !phase2_early_allocator_allocate(2, &second) ||
        first == second || (first & kPageMask) != 0 || (second & kPageMask) != 0) return 0;
    uint64_t firstEnd = first + 3 * 0x1000;
    uint64_t secondEnd = second + 2 * 0x1000;
    if (first < secondEnd && second < firstEnd ||
        phase2_early_allocator_is_protected(first, 3 * 0x1000) ||
        phase2_early_allocator_is_protected(second, 2 * 0x1000)) return 0;

    volatile uint64_t* firstWord = (volatile uint64_t*)(uintptr_t)first;
    volatile uint64_t* secondWord = (volatile uint64_t*)(uintptr_t)second;
    firstWord[0] = UINT64_C(0x1122334455667788);
    secondWord[0] = UINT64_C(0x8877665544332211);
    if (firstWord[0] != UINT64_C(0x1122334455667788) ||
        secondWord[0] != UINT64_C(0x8877665544332211)) return 0;
    if (!phase2_early_allocator_release(second, 2) || !phase2_early_allocator_release(first, 3)) return 0;
    uint64_t recycledFirst = 0;
    uint64_t recycledSecond = 0;
    if (!phase2_early_allocator_allocate(3, &recycledFirst) || recycledFirst != first ||
        !phase2_early_allocator_allocate(2, &recycledSecond) || recycledSecond != second) return 0;
    if (!phase2_early_allocator_release(recycledSecond, 2) || !phase2_early_allocator_release(recycledFirst, 3)) return 0;
    return 1;
}
