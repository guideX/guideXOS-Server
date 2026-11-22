#pragma once

#include "cpu_capabilities.h"

namespace guideXOS {
namespace acpi {
struct ProcessorInventory;
struct InterruptTopology;
}
}

namespace kernel {
namespace arch {
namespace amd64 {

enum class BootCpuMatchStatus : uint8_t {
    InventoryUnavailable, InventoryTruncated, IdentityUnavailable, IdentityInsufficient,
    Mismatch, Ambiguous, MatchedDisabled, MatchedEnabled
};

struct BootCpuMatch {
    BootCpuMatchStatus status;
    uint32_t record_index; // UINT32_MAX when no unique firmware record applies.
};

BootCpuMatch match_boot_processor(const CpuTopologyHint& cpu,
                                const guideXOS::acpi::ProcessorInventory* inventory);

#if defined(GXOS_ENABLE_CPU_INVENTORY_DIAGNOSTIC)
void report_boot_processor_inventory(const CpuCapabilities& cpu,
                                     const guideXOS::acpi::ProcessorInventory* inventory,
                                     uint32_t running_processors);
void report_boot_interrupt_topology(const CpuCapabilities& cpu,
                                    const guideXOS::acpi::InterruptTopology* topology);
#endif

} // namespace amd64
} // namespace arch
} // namespace kernel
