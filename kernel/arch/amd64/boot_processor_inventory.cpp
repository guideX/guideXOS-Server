#include "include/arch/boot_processor_inventory.h"
#include "../../../guideXOSBootLoader/acpi_processor_inventory.h"

#if defined(GXOS_ENABLE_CPU_INVENTORY_DIAGNOSTIC)
#include "../../core/include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace arch {
namespace amd64 {

using guideXOS::acpi::InventoryStatus;
using guideXOS::acpi::ProcessorInventory;
using guideXOS::acpi::ProcessorKind;
using guideXOS::acpi::ProcessorRecord;
using guideXOS::acpi::TableKind;

BootCpuMatch match_boot_processor(const CpuTopologyHint& cpu, const ProcessorInventory* inventory)
{
    if (!inventory || inventory->stored_count > guideXOS::acpi::kProcessorCapacity) {
        return {BootCpuMatchStatus::InventoryUnavailable, UINT32_MAX};
    }
    if (inventory->status == InventoryStatus::Truncated) {
        return {BootCpuMatchStatus::InventoryTruncated, UINT32_MAX};
    }
    if (inventory->status != InventoryStatus::Complete) {
        return {BootCpuMatchStatus::InventoryUnavailable, UINT32_MAX};
    }
    if (cpu.source == CpuTopologySource::Unavailable) {
        return {BootCpuMatchStatus::IdentityUnavailable, UINT32_MAX};
    }

    uint32_t selected = UINT32_MAX;
    bool wide_alias = false;
    const bool legacy = cpu.source == CpuTopologySource::LegacyLeaf1;
    for (uint32_t i = 0; i < inventory->stored_count; ++i) {
        const auto& record = inventory->records[i];
        if (!record.has_apic_id()) continue;
        if (legacy && record.kind == ProcessorKind::LocalX2Apic) {
            wide_alias |= (record.apic_id & 0xFFu) == cpu.initial_apic_id;
            continue;
        }
        if (record.apic_id == cpu.initial_apic_id) selected = i;
    }
    if (wide_alias) return {BootCpuMatchStatus::IdentityInsufficient, UINT32_MAX};
    if (selected == UINT32_MAX) return {BootCpuMatchStatus::Mismatch, UINT32_MAX};
    const auto& record = inventory->records[selected];
    if (record.duplicate || record.conflicting) {
        return {BootCpuMatchStatus::Ambiguous, UINT32_MAX};
    }
    return {record.enabled() ? BootCpuMatchStatus::MatchedEnabled : BootCpuMatchStatus::MatchedDisabled,
            selected};
}

#if defined(GXOS_ENABLE_CPU_INVENTORY_DIAGNOSTIC)
namespace {

const char* name(InventoryStatus status)
{
    switch (status) {
    case InventoryStatus::Unavailable: return "unavailable";
    case InventoryStatus::NoRsdp: return "no-rsdp";
    case InventoryStatus::MemoryMapUnavailable: return "memory-map-unavailable";
    case InventoryStatus::InvalidAddress: return "invalid-address";
    case InventoryStatus::Unreadable: return "unreadable";
    case InventoryStatus::InvalidSignature: return "invalid-signature";
    case InventoryStatus::InvalidChecksum: return "invalid-checksum";
    case InventoryStatus::InvalidLength: return "invalid-length";
    case InventoryStatus::LimitExceeded: return "limit-exceeded";
    case InventoryStatus::UnsupportedRsdpRevision: return "unsupported-rsdp-revision";
    case InventoryStatus::NoMadt: return "no-madt";
    case InventoryStatus::MultipleMadts: return "multiple-madts";
    case InventoryStatus::InvalidRecord: return "invalid-record";
    case InventoryStatus::Complete: return "complete";
    case InventoryStatus::Truncated: return "truncated";
    }
    return "unknown";
}

const char* name(TableKind table)
{
    switch (table) {
    case TableKind::None: return "none";
    case TableKind::Rsdp: return "rsdp";
    case TableKind::Rsdt: return "rsdt";
    case TableKind::Xsdt: return "xsdt";
    case TableKind::RootEntry: return "root-entry";
    case TableKind::Madt: return "madt";
    }
    return "unknown";
}

const char* name(CpuTopologySource source)
{
    switch (source) {
    case CpuTopologySource::Unavailable: return "unavailable";
    case CpuTopologySource::Leaf1F: return "cpuid-1f";
    case CpuTopologySource::Leaf0B: return "cpuid-0b";
    case CpuTopologySource::LegacyLeaf1: return "cpuid-01";
    case CpuTopologySource::AmdLeaf80000026: return "cpuid-80000026";
    case CpuTopologySource::AmdLeaf8000001E: return "cpuid-8000001e";
    }
    return "unknown";
}

const char* name(BootCpuMatchStatus status)
{
    switch (status) {
    case BootCpuMatchStatus::InventoryUnavailable: return "inventory-unavailable";
    case BootCpuMatchStatus::InventoryTruncated: return "inventory-truncated";
    case BootCpuMatchStatus::IdentityUnavailable: return "identity-unavailable";
    case BootCpuMatchStatus::IdentityInsufficient: return "identity-insufficient";
    case BootCpuMatchStatus::Mismatch: return "mismatch";
    case BootCpuMatchStatus::Ambiguous: return "ambiguous";
    case BootCpuMatchStatus::MatchedDisabled: return "matched-firmware-disabled";
    case BootCpuMatchStatus::MatchedEnabled: return "matched-firmware-enabled";
    }
    return "unknown";
}

void hex_field(const char* label, uint32_t value)
{
    serial::puts(label);
    serial::put_hex32(value);
}

void address_field(const char* label, uint64_t value)
{
    serial::puts(label);
    serial::put_hex64(value);
}

const char* name(CpuidBit bit)
{
    switch (bit) {
    case CpuidBit::Clear: return "0";
    case CpuidBit::Set: return "1";
    case CpuidBit::Unreported: return "unreported";
    }
    return "unknown";
}

const char* name(guideXOS::acpi::InterruptPolarity polarity)
{
    using guideXOS::acpi::InterruptPolarity;
    switch (polarity) {
    case InterruptPolarity::Unresolved: return "unresolved";
    case InterruptPolarity::ActiveHigh: return "high";
    case InterruptPolarity::ActiveLow: return "low";
    case InterruptPolarity::Reserved: return "reserved";
    }
    return "unknown";
}

const char* name(guideXOS::acpi::InterruptTrigger trigger)
{
    using guideXOS::acpi::InterruptTrigger;
    switch (trigger) {
    case InterruptTrigger::Unresolved: return "unresolved";
    case InterruptTrigger::Edge: return "edge";
    case InterruptTrigger::Level: return "level";
    case InterruptTrigger::Reserved: return "reserved";
    }
    return "unknown";
}

void report_interrupt_issues(uint8_t issues)
{
    using namespace guideXOS::acpi;
    if (issues & InvalidRecordFields) serial::puts(" invalid-fields=1");
    if (issues & DuplicateRecord) serial::puts(" duplicate=1");
    if (issues & ConflictingRecord) serial::puts(" conflicting=1");
    if (issues & SharedOverrideGsi) serial::puts(" shared-override-gsi=1");
}

void report_physical_width_issue(uint64_t address, uint8_t bits)
{
    if (bits != 0 && bits < 64 && address >> bits) serial::puts(" exceeds-bsp-physical-width=1");
}

void report_firmware(const ProcessorInventory& inventory, const BootCpuMatch& match)
{
    serial::puts("[CPU2] firmware-root=");
    serial::puts(name(inventory.root_kind));
    address_field(" rsdp=0x", inventory.rsdp_address);
    address_field(" root=0x", inventory.root_address);
    address_field(" madt=0x", inventory.madt_address);
    hex_field(" rsdp-revision=0x", inventory.rsdp_revision);
    hex_field(" madt-revision=0x", inventory.madt_revision);
    hex_field(" madt-bytes=0x", inventory.madt_length);
    serial::puts("\n");

    if (inventory.status != InventoryStatus::Complete && inventory.status != InventoryStatus::Truncated) {
        serial::puts("[CPU2] failure-table=");
        serial::puts(name(inventory.failure_table));
        address_field(" physical=0x", inventory.failure_address);
        hex_field(" offset=0x", inventory.failure_offset);
        serial::puts("\n");
        return;
    }

    serial::puts("[CPU2]");
    hex_field(" firmware-advertised-records=0x", inventory.advertised_count);
    hex_field(" firmware-enabled-records=0x", inventory.enabled_count);
    hex_field(" firmware-online-capable-records=0x", inventory.online_capable_count);
    hex_field(" stored=0x", inventory.stored_count);
    hex_field(" capacity=0x", guideXOS::acpi::kProcessorCapacity);
    serial::puts("\n");
    for (uint32_t i = 0; i < inventory.stored_count; ++i) {
        const ProcessorRecord& record = inventory.records[i];
        serial::puts("[CPU2] processor");
        hex_field(" madt-type=0x", static_cast<uint32_t>(record.kind));
        hex_field(" offset=0x", record.madt_offset);
        hex_field(" apic-id=0x", record.apic_id);
        hex_field(" acpi-uid=0x", record.acpi_uid);
        hex_field(" firmware-flags=0x", record.flags);
        serial::puts(record.enabled() ? " enabled=1" : " enabled=0");
        serial::puts(inventory.online_capable(record) ? " online-capable=1" : " online-capable=0");
        if (!record.has_apic_id()) serial::puts(" invalid-apic-id=1");
        if (record.duplicate) serial::puts(" duplicate=1");
        if (record.conflicting) serial::puts(" conflicting=1");
        if (i == match.record_index) serial::puts(" executing-boot-processor=1");
        serial::puts("\n");
    }
}

} // namespace

void report_boot_processor_inventory(const CpuCapabilities& cpu, const ProcessorInventory* inventory,
                                     uint32_t running_processors)
{
    const auto match = match_boot_processor(cpu.topology, inventory);

    serial::puts("[CPU2] inventory=");
    serial::puts(inventory ? name(inventory->status) : "handoff-unavailable");
    serial::puts(" boot-id-source=");
    serial::puts(name(cpu.topology.source));
    if (cpu.topology.source != CpuTopologySource::Unavailable) {
        hex_field(" boot-apic-id=0x", cpu.topology.initial_apic_id);
    }
    serial::puts(" boot-match=");
    serial::puts(name(match.status));
    // CPU3 admits only the BSP. Keep the existing CPU2 serial field format.
    serial::puts(running_processors == 1 ? " guidexos-running-processors=1\n"
                                         : " guidexos-running-processors=0\n");

    if (inventory) report_firmware(*inventory, match);
}

void report_boot_interrupt_topology(const CpuCapabilities& cpu, const guideXOS::acpi::InterruptTopology* topology)
{
    serial::puts("[CPU5] cpuid-apic=");
    serial::puts(name(cpu.features.apic));
    serial::puts(" cpuid-x2apic=");
    serial::puts(name(cpu.features.x2apic));
    serial::puts(" cpuid-tsc-deadline=");
    serial::puts(name(cpu.features.tsc_deadline));
    hex_field(" cpuid-physical-address-bits=0x", cpu.physical_address_bits);
    serial::puts(" controller-registers=uninspected\n");
    if (!topology) {
        serial::puts("[CPU5] madt-interrupt-topology=unavailable\n");
        return;
    }

    serial::puts("[CPU5] madt-interrupt-topology=available");
    hex_field(" madt-flags=0x", topology->madt_flags);
    serial::puts(topology->madt_flags & guideXOS::acpi::kMadtPcatCompat ? " pcat-compat=1" : " pcat-compat=0");
    hex_field(" io-apic-records=0x", topology->io_apic_count);
    hex_field(" source-override-records=0x", topology->source_override_count);
    hex_field(" address-override-records=0x", topology->address_override_count);
    hex_field(" ignored-records=0x", topology->ignored_count);
    hex_field(" ignored-nmi-records=0x", topology->ignored_nmi_count);
    hex_field(" malformed-interrupt-records=0x", topology->malformed_count);
    if (topology->malformed_count) hex_field(" first-malformed-offset=0x", topology->first_malformed_offset);
    if (topology->truncated()) serial::puts(" truncated=1");
    serial::puts("\n");

    serial::puts("[CPU5] local-apic");
    hex_field(" madt-header-address=0x", topology->local_apic_address);
    const uint64_t local = topology->effective_local_apic_address();
    if (local == 0) {
        serial::puts(" madt-effective-address=unresolved");
    } else {
        address_field(" madt-effective-address=0x", local);
        report_physical_width_issue(local, cpu.physical_address_bits);
    }
    serial::puts("\n");

    for (uint16_t i = 0; i < topology->stored_address_overrides(); ++i) {
        const auto& record = topology->address_overrides[i];
        serial::puts("[CPU5] local-apic-address-override");
        hex_field(" offset=0x", record.madt_offset);
        address_field(" address=0x", record.address);
        report_interrupt_issues(record.issues);
        report_physical_width_issue(record.address, cpu.physical_address_bits);
        serial::puts("\n");
    }
    for (uint16_t i = 0; i < topology->stored_io_apics(); ++i) {
        const auto& record = topology->io_apics[i];
        serial::puts("[CPU5] io-apic");
        hex_field(" offset=0x", record.madt_offset);
        hex_field(" id=0x", record.id);
        hex_field(" address=0x", record.address);
        hex_field(" gsi-base=0x", record.gsi_base);
        report_interrupt_issues(record.issues);
        report_physical_width_issue(record.address, cpu.physical_address_bits);
        serial::puts("\n");
    }
    for (uint16_t i = 0; i < topology->stored_source_overrides(); ++i) {
        const auto& record = topology->source_overrides[i];
        serial::puts("[CPU5] interrupt-source-override");
        hex_field(" offset=0x", record.madt_offset);
        hex_field(" bus=0x", record.bus);
        hex_field(" source=0x", record.source);
        hex_field(" gsi=0x", record.gsi);
        hex_field(" flags=0x", record.flags);
        serial::puts(" polarity=");
        serial::puts(name(record.polarity()));
        serial::puts(" trigger=");
        serial::puts(name(record.trigger()));
        report_interrupt_issues(record.issues);
        serial::puts("\n");
    }
}
#endif

} // namespace amd64
} // namespace arch
} // namespace kernel
