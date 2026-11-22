#pragma once

#include "cpu_capabilities.h"
#include "../../../../../guideXOSBootLoader/acpi_processor_inventory.h"

namespace guideXOS { struct BootInfo; }

namespace kernel {
namespace arch {
namespace amd64 {

enum class CpuLifecycle : uint8_t { Offline, Initialized, Online };
enum class FirmwareCpuState : uint8_t { Undiscovered, Disabled, OnlineCapable, Enabled };
enum class CpuInventoryMapping : uint8_t { Unavailable, Truncated, Invalid, BootstrapUnmatched, Mapped };

struct CpuDescriptor {
    uint32_t logical_index;
    uint32_t apic_id;  // Valid when apic_id_known is true.
    uint32_t acpi_uid; // Valid when firmware is not Undiscovered.
    CpuLifecycle lifecycle;
    FirmwareCpuState firmware;
    bool bootstrap;
    bool apic_id_known;
};

// Single-writer boot state. CPU3 has no interrupt or cross-CPU consumers.
// Logical indices can key future scheduler and interrupt state. Index zero
// always belongs to the processor executing the existing AMD64 boot entry.
class PerCpuState {
public:
    // Copy validated boot data into fixed storage. An unusable inventory
    // produces a BSP-only roster. Reinitialization returns false without changes.
    bool initialize_bootstrap(const CpuCapabilities& cpu, const guideXOS::acpi::ProcessorInventory* inventory);

    // Record that the already-executing BSP is online. CPU3 allows this once,
    // after initialization, and has no application-processor transition path.
    bool mark_online(uint32_t logical_index);

    const CpuDescriptor* descriptor(uint32_t logical_index) const;
    const CpuCapabilities* capabilities(uint32_t logical_index) const;
    CpuInventoryMapping inventory_mapping() const { return mapping_; }
    uint32_t descriptor_count() const { return count_; }
    uint32_t online_count() const;
    uint32_t offline_count() const { return count_ - online_count(); }

private:
    CpuDescriptor descriptors_[guideXOS::acpi::kProcessorCapacity]{};
    CpuCapabilities bootstrap_capabilities_{};
    CpuInventoryMapping mapping_ = CpuInventoryMapping::Unavailable;
    uint32_t count_ = 0;
};

void initialize_boot_per_cpu(const guideXOS::BootInfo* boot);
const PerCpuState& boot_per_cpu_state();

} // namespace amd64
} // namespace arch
} // namespace kernel
