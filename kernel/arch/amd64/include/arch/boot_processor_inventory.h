#pragma once

#include "cpu_capabilities.h"

namespace guideXOS {
struct BootInfo;
namespace acpi { struct ProcessorInventory; }
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
void report_boot_processor_inventory(const guideXOS::BootInfo* boot);
#endif

} // namespace amd64
} // namespace arch
} // namespace kernel
