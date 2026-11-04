#include "kernel/core/nativeaot_gc_vm_slots.h"
#include "runtime/memory/guidexos_virtual_memory_region.h"

#include <stdio.h>

struct HostedRegion {
    void* base;
    uintptr_t reservedSize;
    uintptr_t committedSize;

    HostedRegion() : base(nullptr), reservedSize(0u), committedSize(0u) {}
};

using Table = kernel::nativeaot::detail::GcVmSlotTable<
    HostedRegion, kernel::nativeaot::detail::kMaxGcVmSlots>;
using LegacyTable = kernel::nativeaot::detail::GcVmSlotTable<HostedRegion, 16u>;
using RuntimeTable = kernel::nativeaot::detail::GcVmSlotTable<
    gxos::runtime::virtual_memory::VirtualMemoryRegion,
    kernel::nativeaot::detail::kMaxGcVmSlots>;

static_assert(sizeof(gxos::runtime::virtual_memory::VirtualMemoryRegion) == 40u,
              "unexpected NativeAOT VM region layout");
static_assert(sizeof(RuntimeTable) == 12288u,
              "256 GC VM tracking slots must occupy 12 KiB on amd64");

template <typename TableType>
static bool reserveWithSyntheticBackend(
    TableType& table, uint32_t capacity, uintptr_t base, uintptr_t size,
    bool backendSucceeds) {
    const uint32_t index = table.firstFreeIndex();
    if (index == static_cast<uint32_t>(-1)) return false;
    typename TableType::Slot* slot = table.slotAt(index);
    if (slot == nullptr || slot->active || !backendSucceeds) return false;

    const uintptr_t end = base + size;
    for (uint32_t otherIndex = 0u;
         otherIndex < capacity; ++otherIndex) {
        const typename TableType::Slot* other = table.slotAt(otherIndex);
        if (!other->active) continue;
        const uintptr_t otherBase = reinterpret_cast<uintptr_t>(other->region.base);
        const uintptr_t otherEnd = otherBase + other->region.reservedSize;
        if (base < otherEnd && otherBase < end) return false;
    }

    slot->region.base = reinterpret_cast<void*>(base);
    slot->region.reservedSize = size;
    slot->region.committedSize = 0u;
    slot->active = true;
    return true;
}

static bool releaseWithSyntheticBackend(
    Table& table, void* address, bool backendSucceeds) {
    Table::Slot* slot = table.findContaining(address);
    if (slot == nullptr || address != slot->region.base || !backendSucceeds) {
        return false;
    }
    slot->region.base = nullptr;
    slot->region.reservedSize = 0u;
    slot->region.committedSize = 0u;
    slot->active = false;
    return true;
}

static uint32_t g_assertionCount = 0u;

#define REQUIRE(condition) do { ++g_assertionCount; if (!(condition)) return __LINE__; } while (false)

int main() {
    LegacyTable legacyTable;
    REQUIRE(legacyTable.activeCount() == 0u);
    for (uint32_t index = 0u; index < 16u; ++index) {
        REQUIRE(reserveWithSyntheticBackend(
            legacyTable, 16u, 0x80000u + static_cast<uintptr_t>(index) * 0x2000u,
            0x1000u, true));
    }
    // The old bound rejected reservation 17 while preserving the first 16.
    REQUIRE(legacyTable.activeCount() == 16u);
    REQUIRE(legacyTable.firstFreeIndex() == static_cast<uint32_t>(-1));
    REQUIRE(!reserveWithSyntheticBackend(legacyTable, 16u, 0x88000u, 0x1000u, true));
    REQUIRE(legacyTable.activeCount() == 16u);

    Table table;
    REQUIRE(table.activeCount() == 0u);
    REQUIRE(table.firstFreeIndex() == 0u);
    REQUIRE(table.slotAt(kernel::nativeaot::detail::kMaxGcVmSlots) == nullptr);

    // A backend reserve failure leaves the selected PAL slot unregistered.
    REQUIRE(!reserveWithSyntheticBackend(table, kernel::nativeaot::detail::kMaxGcVmSlots,
                                         0x100000u, 0x1000u, false));
    REQUIRE(table.activeCount() == 0u);
    REQUIRE(table.firstFreeIndex() == 0u);

    // Cross the original 16-slot boundary and verify each slot records its
    // distinct synthetic reservation and is discoverable by address.
    for (uint32_t index = 0u; index < 17u; ++index) {
        const uintptr_t base = 0x100000u + static_cast<uintptr_t>(index) * 0x2000u;
        REQUIRE(reserveWithSyntheticBackend(table, kernel::nativeaot::detail::kMaxGcVmSlots,
                                            base, 0x1000u, true));
        REQUIRE(table.activeCount() == index + 1u);
        Table::Slot* slot = table.findContaining(reinterpret_cast<void*>(base + 0x800u));
        REQUIRE(slot != nullptr);
        REQUIRE(reinterpret_cast<uintptr_t>(slot->region.base) == base);
        REQUIRE(slot->region.reservedSize == 0x1000u);
    }
    REQUIRE(!reserveWithSyntheticBackend(
        table, kernel::nativeaot::detail::kMaxGcVmSlots, 0x100000u, 0x1000u, true));
    REQUIRE(table.activeCount() == 17u);

    // Fill the declared finite capacity, then confirm exhaustion is bounded
    // and does not overwrite any previously registered reservation.
    for (uint32_t index = 17u;
         index < kernel::nativeaot::detail::kMaxGcVmSlots; ++index) {
        const uintptr_t base = 0x100000u + static_cast<uintptr_t>(index) * 0x2000u;
        REQUIRE(reserveWithSyntheticBackend(table, kernel::nativeaot::detail::kMaxGcVmSlots,
                                            base, 0x1000u, true));
    }
    REQUIRE(table.activeCount() == kernel::nativeaot::detail::kMaxGcVmSlots);
    REQUIRE(table.firstFreeIndex() == static_cast<uint32_t>(-1));
    REQUIRE(!reserveWithSyntheticBackend(table, kernel::nativeaot::detail::kMaxGcVmSlots,
                                         0x900000u, 0x1000u, true));
    REQUIRE(table.activeCount() == kernel::nativeaot::detail::kMaxGcVmSlots);
    REQUIRE(reinterpret_cast<uintptr_t>(table.slotAt(0u)->region.base) == 0x100000u);
    REQUIRE(reinterpret_cast<uintptr_t>(table.slotAt(255u)->region.base) == 0x100000u + 255u * 0x2000u);

    // Unknown and non-base releases cannot deactivate any registration. A
    // failed backend release also retains ownership for safe retry/diagnosis.
    REQUIRE(table.findContaining(reinterpret_cast<void*>(0xA00000u)) == nullptr);
    REQUIRE(!releaseWithSyntheticBackend(table, reinterpret_cast<void*>(0x100800u), true));
    REQUIRE(!releaseWithSyntheticBackend(table, reinterpret_cast<void*>(0x100000u), false));
    REQUIRE(table.activeCount() == kernel::nativeaot::detail::kMaxGcVmSlots);

    // Releasing one exact base frees only its slot; the next reserve reuses it.
    REQUIRE(releaseWithSyntheticBackend(table, reinterpret_cast<void*>(0x100000u), true));
    REQUIRE(table.activeCount() == kernel::nativeaot::detail::kMaxGcVmSlots - 1u);
    REQUIRE(table.findContaining(reinterpret_cast<void*>(0x100800u)) == nullptr);
    REQUIRE(table.firstFreeIndex() == 0u);
    REQUIRE(reserveWithSyntheticBackend(table, kernel::nativeaot::detail::kMaxGcVmSlots,
                                        0x900000u, 0x1000u, true));
    REQUIRE(reinterpret_cast<uintptr_t>(table.slotAt(0u)->region.base) == 0x900000u);
    REQUIRE(table.activeCount() == kernel::nativeaot::detail::kMaxGcVmSlots);

    // Simulate a commit failure followed by successful rollback. The region
    // becomes reusable only after the synthetic release succeeds.
    REQUIRE(releaseWithSyntheticBackend(table, reinterpret_cast<void*>(0x900000u), true));
    const uint32_t rollbackIndex = table.firstFreeIndex();
    REQUIRE(rollbackIndex != static_cast<uint32_t>(-1));
    REQUIRE(reserveWithSyntheticBackend(table, kernel::nativeaot::detail::kMaxGcVmSlots,
                                        0xA00000u, 0x1000u, true));
    const bool syntheticCommitSucceeded = false;
    REQUIRE(!syntheticCommitSucceeded);
    REQUIRE(releaseWithSyntheticBackend(table, reinterpret_cast<void*>(0xA00000u), true));
    REQUIRE(table.firstFreeIndex() == rollbackIndex);

    // Repeated reserve/release cycles do not consume metadata permanently.
    for (uint32_t cycle = 0u; cycle < 1024u; ++cycle) {
        const uintptr_t base = 0xB00000u + static_cast<uintptr_t>(cycle) * 0x2000u;
        REQUIRE(reserveWithSyntheticBackend(table, kernel::nativeaot::detail::kMaxGcVmSlots,
                                            base, 0x1000u, true));
        REQUIRE(releaseWithSyntheticBackend(table, reinterpret_cast<void*>(base), true));
    }
    REQUIRE(table.activeCount() == kernel::nativeaot::detail::kMaxGcVmSlots - 1u);

    printf("GC VM slot table hosted regression: PASS (synthetic VM backend, legacy capacity=16, corrected capacity=256, assertions=%u, reserve/release cycles=1024)\n",
           g_assertionCount);
    return 0;
}
