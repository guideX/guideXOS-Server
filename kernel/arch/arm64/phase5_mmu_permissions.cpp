#include <stdint.h>

#include "phase2_mmu.h"
#include "../../../aarch64/phase2/phase2_validation.h"

namespace {

static const uint64_t kPageMask = UINT64_C(0xfff);
static const uint64_t kMappedPhysicalLimit = UINT64_C(0x80000000);
static const uint64_t kPageDescriptor = UINT64_C(0x3);
static const uint64_t kApReadOnlyEl1 = UINT64_C(2) << 6;
static const uint64_t kApMask = UINT64_C(3) << 6;
static const uint64_t kPxn = UINT64_C(1) << 53;
static const uint64_t kUxn = UINT64_C(1) << 54;

static bool range_end(uint64_t base, uint64_t size, uint64_t* end)
{
    return gxos_aarch64_add_u64(base, size, end) && *end > base;
}

} // namespace

extern "C" uint8_t phase2_mmu_set_range_permissions(uint64_t base, uint64_t size,
                                                    uint8_t executable, uint8_t writable)
{
    if (size == 0 || (base & kPageMask) != 0) return 0;
    uint64_t end = 0;
    if (!range_end(base, size, &end)) return 0;
    if ((end & kPageMask) != 0) {
        if (!gxos_aarch64_add_u64(end, kPageMask, &end)) return 0;
        end &= ~kPageMask;
    }
    if (end <= base || end > kMappedPhysicalLimit) return 0;
    for (uint64_t page = base; page < end; page += GXOS_AARCH64_PHASE2_MMU_GRANULE) {
        const uint64_t descriptor = phase2_mmu_descriptor_for(page);
        if ((descriptor & 3u) != kPageDescriptor) return 0;
        uint64_t updated = descriptor & ~(kApMask | kPxn | kUxn);
        if (!writable) updated |= kApReadOnlyEl1;
        if (!executable) updated |= kPxn | kUxn;
        uint64_t* l0 = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(phase2_mmu_root()));
        uint64_t* l1 = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(l0[0] & ~kPageMask));
        uint64_t* l2 = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(l1[(page >> 30) & 0x1ff] & ~kPageMask));
        uint64_t* l3 = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(l2[(page >> 21) & 0x1ff] & ~kPageMask));
        l3[(page >> 12) & 0x1ff] = updated;
    }
    __asm__ volatile("dsb ish\n tlbi vmalle1\n dsb ish\n isb" ::: "memory");
    return 1;
}
