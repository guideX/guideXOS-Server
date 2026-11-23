#pragma once

#include "per_cpu.h"

namespace kernel {
namespace arch {
namespace amd64 {

enum class ApicBaseAccess : uint8_t { Unavailable, NotEnumerated, Observed };
enum class LocalApicMode : uint8_t { Unknown, Disabled, XApic, X2Apic, Invalid };
enum class ApicBaseValidity : uint8_t { Unknown, Valid, Invalid };
enum class ApicComparison : uint8_t { Unavailable, Match, Mismatch, Insufficient };

struct BspApicObservation {
    ApicBaseAccess access;
    LocalApicMode mode;
    // These fields are populated only when access is Observed.
    uint64_t raw_msr;
    uint64_t reserved_bits;
    bool hardware_bsp;
    bool global_enable;
    bool x2apic_enable;
    uint64_t reported_base;
    ApicBaseValidity base_validity;

    uint64_t madt_base; // Zero when CPU5 supplies no resolved candidate.
    ApicBaseValidity madt_base_validity;
    ApicComparison madt_comparison;
    ApicComparison roster_comparison;
};

// The only requested register is IA32_APIC_BASE. A false result leaves the
// sample unavailable. The native reader relies on architectural enumeration.
using ApicBaseReader = bool (*)(void*, uint64_t&);

BspApicObservation characterize_bootstrap_apic(
    const CpuCapabilities& cpu, const CpuDescriptor* bsp,
    const guideXOS::acpi::InterruptTopology* firmware, ApicBaseReader read, void* context);

#if defined(GXOS_ENABLE_BSP_APIC_DIAGNOSTIC)
void report_bootstrap_apic(const CpuCapabilities& cpu, const PerCpuState& roster,
                           const guideXOS::acpi::InterruptTopology* firmware);
#endif

} // namespace amd64
} // namespace arch
} // namespace kernel
