#include "../kernel/arch/amd64/include/arch/per_cpu.h"

#include <cstdio>
#include <cstdlib>

using namespace guideXOS::acpi;
using namespace kernel::arch::amd64;

namespace {

void check(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

CpuCapabilities capabilities(uint32_t apic_id, CpuTopologySource source = CpuTopologySource::Leaf1F)
{
    CpuCapabilities cpu{};
    cpu.cpuid_available = true;
    cpu.features.nx = CpuidBit::Set;
    cpu.topology = {source, apic_id, 0, 0, 64};
    return cpu;
}

ProcessorInventory fixture()
{
    ProcessorInventory inventory{};
    inventory.status = InventoryStatus::Complete;
    inventory.madt_revision = 5;
    inventory.advertised_count = inventory.stored_count = 4;
    inventory.enabled_count = 2;
    inventory.online_capable_count = 1;
    inventory.records[0] = {0, 1, 1, 44, ProcessorKind::LocalApic, false, false};
    inventory.records[1] = {0x112, 7, 1, 52, ProcessorKind::LocalX2Apic, false, false};
    inventory.records[2] = {0xFF, 9, 2, 68, ProcessorKind::LocalX2Apic, false, false};
    inventory.records[3] = {4, 3, 0, 84, ProcessorKind::LocalApic, false, false};
    return inventory;
}

void expect_fallback(const CpuCapabilities& cpu, const ProcessorInventory* inventory,
                     CpuInventoryMapping mapping, bool id_known)
{
    PerCpuState state;
    check(state.initialize_bootstrap(cpu, inventory), "initialize BSP-only fallback");
    check(state.inventory_mapping() == mapping, "fallback retains mapping disposition");
    check(state.descriptor_count() == 1 && state.online_count() == 0 && state.offline_count() == 1,
          "fallback exposes only the initialized BSP");
    const auto* bsp = state.descriptor(0);
    check(bsp && bsp->bootstrap && bsp->logical_index == 0 && bsp->lifecycle == CpuLifecycle::Initialized &&
          bsp->firmware == FirmwareCpuState::Undiscovered && bsp->apic_id_known == id_known &&
          bsp->apic_id == (id_known ? cpu.topology.initial_apic_id : 0), "fallback BSP identity");
    check(!state.descriptor(1) && !state.capabilities(1) && !state.mark_online(1),
          "fallback hides every firmware slot");
    check(state.mark_online(0) && state.online_count() == 1 && state.offline_count() == 0,
          "fallback preserves BSP execution");
}

void test_mapping()
{
    auto inventory = fixture();
    auto cpu = capabilities(0x112);
    PerCpuState state;
    check(state.initialize_bootstrap(cpu, &inventory), "initialize mixed inventory");
    check(state.inventory_mapping() == CpuInventoryMapping::Mapped && state.descriptor_count() == 4,
          "map all four firmware records");
    inventory.records[1] = {};
    cpu.features.nx = CpuidBit::Clear;
    const uint32_t apic_ids[] = {0x112, 0, 0xFF, 4};
    const uint32_t uids[] = {7, 1, 9, 3};
    const FirmwareCpuState firmware[] = {FirmwareCpuState::Enabled, FirmwareCpuState::Enabled,
                                         FirmwareCpuState::OnlineCapable, FirmwareCpuState::Disabled};
    for (uint32_t index = 0; index < 4; ++index) {
        const auto* descriptor = state.descriptor(index);
        check(descriptor && descriptor->logical_index == index && descriptor->bootstrap == (index == 0) &&
              descriptor->apic_id_known && descriptor->apic_id == apic_ids[index] &&
              descriptor->acpi_uid == uids[index] && descriptor->firmware == firmware[index] &&
              descriptor->lifecycle == (index == 0 ? CpuLifecycle::Initialized : CpuLifecycle::Offline),
              "BSP first, other records in firmware order with independent identity and state");
    }
    check(state.capabilities(0) && state.capabilities(0)->features.nx == CpuidBit::Set &&
          !state.capabilities(1) && !state.capabilities(4), "only the BSP has an owned CPU1 snapshot");
}

void test_bootstrap_identity()
{
    auto inventory = fixture();
    PerCpuState legacy;
    check(legacy.initialize_bootstrap(capabilities(0, CpuTopologySource::LegacyLeaf1), &inventory) &&
          legacy.descriptor(0)->apic_id_known && legacy.descriptor(0)->acpi_uid == 1,
          "unaliased legacy APIC ID zero identifies BSP");
    inventory.records[1].apic_id = 0x100;
    expect_fallback(capabilities(0, CpuTopologySource::LegacyLeaf1), &inventory,
                    CpuInventoryMapping::BootstrapUnmatched, true);
    expect_fallback(capabilities(5), &inventory, CpuInventoryMapping::BootstrapUnmatched, true);
    expect_fallback(capabilities(4, CpuTopologySource::Unavailable), &inventory,
                    CpuInventoryMapping::BootstrapUnmatched, false);
    expect_fallback(capabilities(UINT32_MAX), &inventory, CpuInventoryMapping::BootstrapUnmatched, false);
    expect_fallback(capabilities(UINT8_MAX, CpuTopologySource::LegacyLeaf1), &inventory,
                    CpuInventoryMapping::BootstrapUnmatched, false);

    PerCpuState disabled;
    check(disabled.initialize_bootstrap(capabilities(4), &inventory) && disabled.mark_online(0) &&
          disabled.descriptor(0)->firmware == FirmwareCpuState::Disabled && disabled.online_count() == 1,
          "execution evidence and firmware-enabled state are independent");
    inventory.madt_revision = 4;
    PerCpuState old_madt;
    check(old_madt.initialize_bootstrap(capabilities(0xFF), &inventory) &&
          old_madt.descriptor(0)->firmware == FirmwareCpuState::Disabled &&
          old_madt.descriptor(0)->apic_id == 0xFF, "CPU2 MADT revision policy and full-width APIC ID");
}

void test_invalid_inventory()
{
    const auto cpu = capabilities(0x112);
    const auto invalid = CpuInventoryMapping::Invalid;
    auto inventory = fixture();
    inventory.records[3] = inventory.records[1];
    expect_fallback(cpu, &inventory, invalid, true);
    inventory = fixture();
    inventory.records[3].kind = ProcessorKind::LocalX2Apic;
    inventory.records[3].apic_id = inventory.records[0].apic_id;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory = fixture();
    inventory.records[3].acpi_uid = inventory.records[1].acpi_uid;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory = fixture();
    inventory.records[3].duplicate = true;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory = fixture();
    inventory.records[3].conflicting = true;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory = fixture();
    inventory.records[3].apic_id = UINT8_MAX;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory.records[3].apic_id = 0x100;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory.records[3].apic_id = 4;
    inventory.records[3].acpi_uid = 0x100;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory = fixture();
    inventory.records[2].apic_id = UINT32_MAX;
    expect_fallback(cpu, &inventory, invalid, true);
    inventory = fixture();
    inventory.records[3].kind = static_cast<ProcessorKind>(1);
    expect_fallback(cpu, &inventory, invalid, true);
}

void test_capacity()
{
    ProcessorInventory inventory{};
    inventory.status = InventoryStatus::Complete;
    inventory.advertised_count = inventory.stored_count = inventory.enabled_count = kProcessorCapacity;
    for (uint32_t i = 0; i < kProcessorCapacity; ++i) {
        inventory.records[i] = {0x200 + i, i, 1, 44 + 16 * i, ProcessorKind::LocalX2Apic, false, false};
    }
    const auto cpu = capabilities(inventory.records[kProcessorCapacity - 1].apic_id);
    PerCpuState state;
    check(state.initialize_bootstrap(cpu, &inventory) && state.descriptor_count() == kProcessorCapacity &&
          state.descriptor(0)->apic_id == cpu.topology.initial_apic_id &&
          state.descriptor(kProcessorCapacity - 1)->apic_id == inventory.records[kProcessorCapacity - 2].apic_id &&
          !state.descriptor(kProcessorCapacity) && !state.descriptor(UINT32_MAX), "exact capacity and index bounds");

    ++inventory.advertised_count;
    expect_fallback(cpu, &inventory, CpuInventoryMapping::Invalid, true);
    inventory.status = InventoryStatus::Truncated;
    expect_fallback(cpu, &inventory, CpuInventoryMapping::Truncated, true);
    inventory.status = InventoryStatus::Complete;
    ++inventory.stored_count;
    expect_fallback(cpu, &inventory, CpuInventoryMapping::Invalid, true);
}

void test_lifecycle()
{
    PerCpuState state;
    check(state.descriptor_count() == 0 && state.online_count() == 0 && state.offline_count() == 0 &&
          !state.descriptor(0) && !state.capabilities(0) && !state.mark_online(0), "uninitialized roster");
    const auto inventory = fixture();
    const auto cpu = capabilities(0x112);
    check(state.initialize_bootstrap(cpu, &inventory) && state.online_count() == 0 && state.offline_count() == 4,
          "initialization precedes online accounting");
    check(!state.initialize_bootstrap(capabilities(6), nullptr) && !state.mark_online(1) &&
          !state.mark_online(2) && !state.mark_online(3) && !state.mark_online(UINT32_MAX) &&
          state.online_count() == 0, "reject reinitialization and every non-BSP online transition");
    check(state.mark_online(0) && state.descriptor(0)->lifecycle == CpuLifecycle::Online &&
          state.online_count() == 1 && state.offline_count() == 3, "BSP online transition");
    check(!state.mark_online(0) && !state.initialize_bootstrap(capabilities(6), nullptr) &&
          state.descriptor(0)->apic_id == 0x112 && state.descriptor(1)->lifecycle == CpuLifecycle::Offline &&
          state.descriptor_count() == 4 && state.online_count() == 1, "illegal requests preserve published state");
}

void test_single_processor()
{
    const auto cpu = capabilities(0);
    expect_fallback(cpu, nullptr, CpuInventoryMapping::Unavailable, true);
    expect_fallback(CpuCapabilities{}, nullptr, CpuInventoryMapping::Unavailable, false);
    auto inventory = fixture();
    inventory.status = InventoryStatus::InvalidChecksum;
    expect_fallback(cpu, &inventory, CpuInventoryMapping::Unavailable, true);
    inventory.status = InventoryStatus::Complete;
    inventory.advertised_count = inventory.stored_count = inventory.enabled_count = 0;
    inventory.online_capable_count = 0;
    expect_fallback(cpu, &inventory, CpuInventoryMapping::BootstrapUnmatched, true);
    inventory.advertised_count = inventory.stored_count = inventory.enabled_count = 1;
    PerCpuState state;
    check(state.initialize_bootstrap(cpu, &inventory) && state.inventory_mapping() == CpuInventoryMapping::Mapped &&
          state.mark_online(0) && state.descriptor_count() == 1 && state.online_count() == 1 &&
          state.offline_count() == 0, "one valid firmware record maps to the BSP");
}

void test_boot_publication()
{
    const auto& state = boot_per_cpu_state();
    check(state.descriptor_count() == 0 && state.online_count() == 0, "empty boot roster before entry");
    initialize_boot_per_cpu(nullptr);
    check(state.descriptor_count() == 1 && state.online_count() == 1 && state.offline_count() == 0 &&
          state.inventory_mapping() == CpuInventoryMapping::Unavailable && state.capabilities(0) &&
          state.descriptor(0)->bootstrap && state.descriptor(0)->lifecycle == CpuLifecycle::Online,
          "boot adapter publishes the executing BSP and its snapshot without a handoff");
    initialize_boot_per_cpu(nullptr);
    check(state.descriptor_count() == 1 && state.online_count() == 1, "repeated boot publication is rejected");
}

} // namespace

int main()
{
    test_mapping();
    test_bootstrap_identity();
    test_invalid_inventory();
    test_capacity();
    test_lifecycle();
    test_single_processor();
    test_boot_publication();
    std::puts("PASS: 7/7 AMD64 per-CPU fixture groups");
}
