#pragma once

#include <stdint.h>

namespace kernel {
namespace nativeaot {
namespace detail {

// Keep startup PAL tracking aligned with the production NativeAOT PAL's
// fixed VM slot table. The underlying VM region registry remains independently
// bounded and may reject a reservation before this table reaches capacity.
constexpr uint32_t kMaxGcVmSlots = 256u;

template <typename Region, uint32_t Capacity>
class GcVmSlotTable {
public:
    struct Slot {
        bool active;
        Region region;

        Slot() : active(false), region() {}
    };

    static_assert(Capacity != 0u, "GC VM slot table must have finite capacity");

    Slot* slotAt(uint32_t index) {
        return index < Capacity ? &m_slots[index] : nullptr;
    }

    const Slot* slotAt(uint32_t index) const {
        return index < Capacity ? &m_slots[index] : nullptr;
    }

    uint32_t firstFreeIndex() const {
        for (uint32_t index = 0u; index < Capacity; ++index) {
            if (!m_slots[index].active) return index;
        }
        return static_cast<uint32_t>(-1);
    }

    uint32_t activeCount() const {
        uint32_t count = 0u;
        for (uint32_t index = 0u; index < Capacity; ++index) {
            if (m_slots[index].active) ++count;
        }
        return count;
    }

    Slot* findContaining(const void* address) {
        const uintptr_t value = reinterpret_cast<uintptr_t>(address);
        for (uint32_t index = 0u; index < Capacity; ++index) {
            Slot& slot = m_slots[index];
            if (!slot.active || slot.region.base == nullptr) continue;
            const uintptr_t base = reinterpret_cast<uintptr_t>(slot.region.base);
            if (value >= base && value - base < slot.region.reservedSize) {
                return &slot;
            }
        }
        return nullptr;
    }

private:
    // This helper is not independently synchronized; the PAL caller must
    // serialize operations using the table.
    Slot m_slots[Capacity];
};

} // namespace detail
} // namespace nativeaot
} // namespace kernel
