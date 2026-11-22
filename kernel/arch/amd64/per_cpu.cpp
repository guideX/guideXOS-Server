#include "include/arch/per_cpu.h"
#include "include/arch/boot_processor_inventory.h"

#if defined(GXOS_ENABLE_CPU_INVENTORY_DIAGNOSTIC)
#include "../../../guideXOSBootLoader/bootinfo_acpi.h"
#include "../../core/include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace arch {
namespace amd64 {
namespace {

using guideXOS::acpi::InventoryStatus;
using guideXOS::acpi::ProcessorInventory;
using guideXOS::acpi::ProcessorKind;
using guideXOS::acpi::ProcessorRecord;

PerCpuState s_boot_per_cpu;

bool valid_firmware_records(const ProcessorInventory& inventory)
{
    if (inventory.stored_count > guideXOS::acpi::kProcessorCapacity ||
        inventory.stored_count != inventory.advertised_count) return false;

    // Validate the whole roster before any firmware descriptor becomes visible.
    for (uint32_t i = 0; i < inventory.stored_count; ++i) {
        const auto& record = inventory.records[i];
        if (record.duplicate || record.conflicting || !record.has_apic_id()) return false;
        switch (record.kind) {
        case ProcessorKind::LocalApic:
            if (record.apic_id > UINT8_MAX || record.acpi_uid > UINT8_MAX) return false;
            break;
        case ProcessorKind::LocalX2Apic:
            break;
        default:
            return false;
        }
        for (uint32_t j = 0; j < i; ++j) {
            const auto& other = inventory.records[j];
            if (record.apic_id == other.apic_id || record.acpi_uid == other.acpi_uid) return false;
        }
    }
    return true;
}

bool known_apic_id(const CpuCapabilities& cpu)
{
    const auto& topology = cpu.topology;
    if (!cpu.cpuid_available || topology.source == CpuTopologySource::Unavailable) return false;
    if (topology.source == CpuTopologySource::LegacyLeaf1) return topology.initial_apic_id < UINT8_MAX;
    return topology.initial_apic_id < UINT32_MAX;
}

CpuDescriptor firmware_descriptor(uint32_t index, const ProcessorInventory& inventory, const ProcessorRecord& record)
{
    auto firmware = FirmwareCpuState::Disabled;
    if (record.enabled()) firmware = FirmwareCpuState::Enabled;
    else if (inventory.online_capable(record)) firmware = FirmwareCpuState::OnlineCapable;
    const bool bootstrap = index == 0;
    return {index, record.apic_id, record.acpi_uid,
            bootstrap ? CpuLifecycle::Initialized : CpuLifecycle::Offline, firmware, bootstrap, true};
}

#if defined(GXOS_ENABLE_CPU_INVENTORY_DIAGNOSTIC)
const char* mapping_name(CpuInventoryMapping mapping)
{
    switch (mapping) {
    case CpuInventoryMapping::Unavailable: return "unavailable";
    case CpuInventoryMapping::Truncated: return "truncated";
    case CpuInventoryMapping::Invalid: return "invalid";
    case CpuInventoryMapping::BootstrapUnmatched: return "bsp-unmatched";
    case CpuInventoryMapping::Mapped: return "mapped";
    }
    return "unknown";
}
#endif

} // namespace

bool PerCpuState::initialize_bootstrap(const CpuCapabilities& cpu, const ProcessorInventory* inventory)
{
    if (count_ != 0) return false;

    const bool apic_id_known = known_apic_id(cpu);
    bootstrap_capabilities_ = cpu;
    descriptors_[0] = {0, apic_id_known ? cpu.topology.initial_apic_id : 0, 0,
                       CpuLifecycle::Initialized, FirmwareCpuState::Undiscovered, true, apic_id_known};
    count_ = 1;

    if (!inventory) return true;
    if (inventory->status == InventoryStatus::Truncated) {
        mapping_ = CpuInventoryMapping::Truncated;
        return true;
    }
    if (inventory->status != InventoryStatus::Complete) return true;
    if (!valid_firmware_records(*inventory)) {
        mapping_ = CpuInventoryMapping::Invalid;
        return true;
    }

    const auto match = match_boot_processor(cpu.topology, inventory);
    if (!apic_id_known || (match.status != BootCpuMatchStatus::MatchedEnabled &&
                           match.status != BootCpuMatchStatus::MatchedDisabled)) {
        mapping_ = CpuInventoryMapping::BootstrapUnmatched;
        return true;
    }

    descriptors_[0] = firmware_descriptor(0, *inventory, inventory->records[match.record_index]);
    uint32_t index = 1;
    for (uint32_t i = 0; i < inventory->stored_count; ++i) {
        if (i == match.record_index) continue;
        descriptors_[index] = firmware_descriptor(index, *inventory, inventory->records[i]);
        ++index;
    }
    count_ = index;
    mapping_ = CpuInventoryMapping::Mapped;
    return true;
}

bool PerCpuState::mark_online(uint32_t logical_index)
{
    if (logical_index != 0 || count_ == 0 || descriptors_[0].lifecycle != CpuLifecycle::Initialized) return false;
    descriptors_[0].lifecycle = CpuLifecycle::Online;
    return true;
}

const CpuDescriptor* PerCpuState::descriptor(uint32_t logical_index) const
{
    return logical_index < count_ ? &descriptors_[logical_index] : nullptr;
}

const CpuCapabilities* PerCpuState::capabilities(uint32_t logical_index) const
{
    return logical_index == 0 && count_ != 0 ? &bootstrap_capabilities_ : nullptr;
}

uint32_t PerCpuState::online_count() const
{
    return count_ != 0 && descriptors_[0].lifecycle == CpuLifecycle::Online ? 1u : 0u;
}

void initialize_boot_per_cpu(const guideXOS::BootInfo* boot)
{
    const ProcessorInventory* inventory = nullptr;
#if defined(GXOS_ENABLE_CPU_INVENTORY_DIAGNOSTIC)
    inventory = guideXOS::bootinfo_acpi_inventory(boot);
#else
    (void)boot;
#endif
    const auto cpu = read_cpu_capabilities();
    if (!s_boot_per_cpu.initialize_bootstrap(cpu, inventory)) return;
    (void)s_boot_per_cpu.mark_online(0);

#if defined(GXOS_ENABLE_CPU_INVENTORY_DIAGNOSTIC)
    report_boot_processor_inventory(cpu, inventory, s_boot_per_cpu.online_count());
    serial::puts("[CPU3] roster=");
    serial::puts(mapping_name(s_boot_per_cpu.inventory_mapping()));
    serial::puts(" descriptors=0x");
    serial::put_hex32(s_boot_per_cpu.descriptor_count());
    serial::puts(" online=0x");
    serial::put_hex32(s_boot_per_cpu.online_count());
    serial::puts(" offline=0x");
    serial::put_hex32(s_boot_per_cpu.offline_count());
    serial::puts("\n");
    report_boot_interrupt_topology(cpu, guideXOS::bootinfo_acpi_interrupt_topology(boot));
#endif
}

const PerCpuState& boot_per_cpu_state()
{
    return s_boot_per_cpu;
}

} // namespace amd64
} // namespace arch
} // namespace kernel
