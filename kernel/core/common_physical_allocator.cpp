#include "include/kernel/common_physical_allocator.h"

namespace kernel {
namespace memory {

namespace {

static const uint64_t kPageSize = UINT64_C(0x1000);
static const uint64_t kPageMask = kPageSize - 1;
static const uint32_t kMaxRanges = 256;
static const uint32_t kMaxReservations = 32;
static const uint32_t kMaxAllocations = 256;

struct Range { uint64_t start; uint64_t end; };
struct Allocation { uint64_t start; uint64_t pages; bool active; };

static Range g_free[kMaxRanges];
static Range g_reserved[kMaxReservations];
static Allocation g_allocations[kMaxAllocations];
static uint32_t g_free_count = 0;
static uint32_t g_reserved_count = 0;
static uint64_t g_total_pages = 0;
static uint64_t g_free_pages = 0;
static uint64_t g_allocated_pages = 0;
static bool g_ready = false;

static void zero_bytes(void* destination, uint64_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < size; ++i) bytes[i] = 0;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return *result > left;
}

static bool align_up(uint64_t value, uint64_t* result)
{
    if (!result || value > UINT64_MAX - kPageMask) return false;
    *result = (value + kPageMask) & ~kPageMask;
    return true;
}

static bool overlap(uint64_t start, uint64_t end, const Range& range)
{
    return start < range.end && range.start < end;
}

static bool add_reserved(uint64_t base, uint64_t size)
{
    if (size == 0 || g_reserved_count >= kMaxReservations) return false;
    uint64_t raw_end = 0;
    uint64_t end = 0;
    if (!add_u64(base, size, &raw_end) || !align_up(raw_end, &end) || end <= (base & ~kPageMask)) return false;
    g_reserved[g_reserved_count++] = { base & ~kPageMask, end };
    return true;
}

static bool is_reserved_range(uint64_t start, uint64_t end)
{
    for (uint32_t i = 0; i < g_reserved_count; ++i) {
        if (overlap(start, end, g_reserved[i])) return true;
    }
    return false;
}

static bool append_free(uint64_t raw_start, uint64_t raw_end)
{
    uint64_t start = 0;
    if (!align_up(raw_start, &start)) return false;
    const uint64_t end = raw_end & ~kPageMask;
    if (start >= end) return true;
    if (g_free_count >= kMaxRanges) return false;
    g_free[g_free_count++] = { start, end };
    const uint64_t pages = (end - start) / kPageSize;
    g_total_pages += pages;
    g_free_pages += pages;
    return true;
}

static bool append_unreserved(uint64_t start, uint64_t end)
{
    uint64_t cursor = start;
    for (uint32_t i = 0; i < g_reserved_count; ++i) {
        const Range& r = g_reserved[i];
        if (r.end <= cursor || r.start >= end) continue;
        if (r.start > cursor && !append_free(cursor, r.start)) return false;
        if (r.end > cursor) cursor = r.end;
        if (cursor >= end) return true;
    }
    return append_free(cursor, end);
}

static void sort_reserved()
{
    for (uint32_t i = 1; i < g_reserved_count; ++i) {
        const Range value = g_reserved[i];
        uint32_t j = i;
        while (j != 0 && g_reserved[j - 1].start > value.start) {
            g_reserved[j] = g_reserved[j - 1];
            --j;
        }
        g_reserved[j] = value;
    }
}

static void merge_free()
{
    for (uint32_t i = 0; i < g_free_count; ++i) {
        for (uint32_t j = i + 1; j < g_free_count;) {
            if (g_free[j].start <= g_free[i].end && g_free[i].start <= g_free[j].end) {
                if (g_free[j].start < g_free[i].start) g_free[i].start = g_free[j].start;
                if (g_free[j].end > g_free[i].end) g_free[i].end = g_free[j].end;
                g_free[j] = g_free[--g_free_count];
            } else {
                ++j;
            }
        }
    }
}

} // namespace

bool initialize(const boot::CommonBootInfo& boot_info)
{
    g_free_count = 0;
    g_reserved_count = 0;
    g_total_pages = 0;
    g_free_pages = 0;
    g_allocated_pages = 0;
    g_ready = false;
    zero_bytes(g_allocations, sizeof(g_allocations));
    if (!boot::validate(&boot_info)) return false;

    if (!add_reserved(boot_info.kernel_base, boot_info.kernel_size) ||
        !add_reserved(boot_info.bootstrap_stack_base, boot_info.bootstrap_stack_size) ||
        !add_reserved(boot_info.handoff_base, boot_info.handoff_size) ||
        !add_reserved(boot_info.memory_map, boot_info.memory_map_size)) return false;
    if ((boot_info.flags & boot::kBootFlagRamdisk) != 0 &&
        !add_reserved(boot_info.ramdisk_base, boot_info.ramdisk_size)) return false;
    if ((boot_info.flags & boot::kBootFlagDtb) != 0 &&
        !add_reserved(boot_info.dtb_base, boot_info.dtb_size)) return false;
    for (uint32_t i = 0; i < boot_info.mmio_range_count; ++i) {
        if (!add_reserved(boot_info.mmio_ranges[i].base, boot_info.mmio_ranges[i].size)) return false;
    }
    sort_reserved();

    const uint8_t* map = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(boot_info.memory_map));
    for (uint64_t i = 0; i < boot_info.memory_map_entry_count; ++i) {
        const uint64_t offset = i * boot_info.memory_map_descriptor_size;
        const uint8_t* descriptor = map + offset;
        uint32_t type = 0;
        for (uint32_t b = 0; b < 4; ++b) type |= static_cast<uint32_t>(descriptor[b]) << (b * 8);
        if (type != 7) continue; // EfiConventionalMemory only, post-EBS.
        uint64_t physical = 0;
        uint64_t pages = 0;
        for (uint32_t b = 0; b < 8; ++b) physical |= static_cast<uint64_t>(descriptor[8 + b]) << (b * 8);
        for (uint32_t b = 0; b < 8; ++b) pages |= static_cast<uint64_t>(descriptor[24 + b]) << (b * 8);
        uint64_t end = 0;
        if (pages == 0 || pages > UINT64_MAX / kPageSize ||
            !add_u64(physical, pages * kPageSize, &end)) return false;
        for (uint32_t r = 0; r < boot_info.memory_range_count; ++r) {
            const uint64_t ram_start = boot_info.memory_ranges[r].base;
            uint64_t ram_end = 0;
            if (!add_u64(ram_start, boot_info.memory_ranges[r].size, &ram_end)) return false;
            const uint64_t start = physical > ram_start ? physical : ram_start;
            const uint64_t clipped_end = end < ram_end ? end : ram_end;
            if (start < clipped_end && !append_unreserved(start, clipped_end)) return false;
        }
    }
    g_ready = g_free_count != 0 && g_free_pages != 0;
    return g_ready;
}

bool allocate_pages(uint64_t pages, uint64_t* base)
{
    if (!g_ready || !base || pages == 0 || pages > UINT64_MAX / kPageSize) return false;
    const uint64_t bytes = pages * kPageSize;
    for (uint32_t i = 0; i < g_free_count; ++i) {
        const uint64_t available = g_free[i].end - g_free[i].start;
        if (available < bytes) continue;
        if (g_allocated_pages > UINT64_MAX - pages) return false;
        uint32_t slot = kMaxAllocations;
        for (uint32_t a = 0; a < kMaxAllocations; ++a) {
            if (!g_allocations[a].active) { slot = a; break; }
        }
        if (slot == kMaxAllocations) return false;
        const uint64_t result = g_free[i].start;
        g_free[i].start += bytes;
        if (g_free[i].start == g_free[i].end) g_free[i] = g_free[--g_free_count];
        g_allocations[slot] = { result, pages, true };
        g_free_pages -= pages;
        g_allocated_pages += pages;
        *base = result;
        return true;
    }
    return false;
}

bool allocate_pages_at(uint64_t base, uint64_t pages)
{
    if (!g_ready || pages == 0 || (base & kPageMask) != 0 ||
        pages > UINT64_MAX / kPageSize) return false;
    const uint64_t bytes = pages * kPageSize;
    uint64_t end = 0;
    if (!add_u64(base, bytes, &end) || end <= base) return false;
    for (uint32_t i = 0; i < g_free_count; ++i) {
        if (base < g_free[i].start || end > g_free[i].end) continue;
        if (g_allocated_pages > UINT64_MAX - pages) return false;
        uint32_t slot = kMaxAllocations;
        for (uint32_t a = 0; a < kMaxAllocations; ++a) {
            if (!g_allocations[a].active) { slot = a; break; }
        }
        if (slot == kMaxAllocations) return false;
        if (base == g_free[i].start && end == g_free[i].end) {
            g_free[i] = g_free[--g_free_count];
        } else if (base == g_free[i].start) {
            g_free[i].start = end;
        } else if (end == g_free[i].end) {
            g_free[i].end = base;
        } else {
            if (g_free_count >= kMaxRanges) return false;
            const Range right = { end, g_free[i].end };
            g_free[i].end = base;
            g_free[g_free_count++] = right;
        }
        g_allocations[slot] = { base, pages, true };
        g_free_pages -= pages;
        g_allocated_pages += pages;
        return true;
    }
    return false;
}

bool release_pages(uint64_t base, uint64_t pages)
{
    if (!g_ready || pages == 0 || (base & kPageMask) != 0) return false;
    for (uint32_t i = 0; i < kMaxAllocations; ++i) {
        if (!g_allocations[i].active || g_allocations[i].start != base || g_allocations[i].pages != pages) continue;
        uint64_t end = 0;
        if (!add_u64(base, pages * kPageSize, &end) || is_reserved_range(base, end) || g_free_count >= kMaxRanges) return false;
        g_free[g_free_count++] = { base, end };
        g_allocations[i].active = false;
        g_free_pages += pages;
        g_allocated_pages -= pages;
        merge_free();
        return true;
    }
    return false; // includes double free and partial release.
}

bool is_reserved(uint64_t base, uint64_t size)
{
    uint64_t end = 0;
    if (size == 0 || !add_u64(base, size, &end)) return true;
    return is_reserved_range(base, end);
}

bool ready() { return g_ready; }
uint64_t total_pages() { return g_total_pages; }
uint64_t free_pages() { return g_free_pages; }
uint64_t allocated_pages() { return g_allocated_pages; }

uint64_t allocation_count()
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < kMaxAllocations; ++i) if (g_allocations[i].active) ++result;
    return result;
}

} // namespace memory
} // namespace kernel
