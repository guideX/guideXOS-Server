#include "../kernel/arch/amd64/include/arch/bsp_apic.h"

#include <assert.h>
#include <initializer_list>
#include <stdio.h>

using namespace kernel::arch::amd64;
using namespace guideXOS::acpi;

namespace {

constexpr uint64_t kBase = 0xFEE00000;
constexpr uint64_t kBsp = 0x100;
constexpr uint64_t kX2Apic = 0x400;
constexpr uint64_t kEnable = 0x800;

struct MockMsr {
    uint64_t bits;
    bool available = true;
    unsigned calls = 0;
};

bool read_mock(void* context, uint64_t& bits)
{
    auto& mock = *static_cast<MockMsr*>(context);
    ++mock.calls;
    bits = mock.bits;
    return mock.available;
}

CpuCapabilities cpu_fixture()
{
    CpuCapabilities cpu{};
    cpu.cpuid_available = true;
    cpu.features.msr = CpuidBit::Set;
    cpu.features.apic = CpuidBit::Set;
    cpu.features.x2apic = CpuidBit::Set;
    cpu.physical_address_bits = 39;
    cpu.topology.source = CpuTopologySource::Leaf0B;
    cpu.topology.initial_apic_id = 2;
    return cpu;
}

BspApicObservation sample(const CpuCapabilities& cpu, uint64_t value,
                          const CpuDescriptor* bsp = nullptr, const InterruptTopology* madt = nullptr)
{
    MockMsr mock{value};
    const auto observation = characterize_bootstrap_apic(cpu, bsp, madt, read_mock, &mock);
    assert(mock.calls == 1 && observation.access == ApicBaseAccess::Observed);
    assert(observation.raw_msr == value);
    return observation;
}

void mode_and_flags()
{
    auto cpu = cpu_fixture();
    const auto xapic = sample(cpu, kBase | kBsp | kEnable);
    assert(xapic.mode == LocalApicMode::XApic);
    assert(xapic.reported_base == kBase && xapic.base_validity == ApicBaseValidity::Valid);
    assert(xapic.reserved_bits == 0 && xapic.hardware_bsp && xapic.global_enable && !xapic.x2apic_enable);

    const auto x2apic = sample(cpu, kBase | kEnable | kX2Apic);
    assert(x2apic.mode == LocalApicMode::X2Apic);
    assert(!x2apic.hardware_bsp && x2apic.global_enable && x2apic.x2apic_enable);

    const auto disabled = sample(cpu, kBase | kBsp);
    assert(disabled.mode == LocalApicMode::Disabled);
    assert(disabled.hardware_bsp && !disabled.global_enable && !disabled.x2apic_enable);

    cpu.features.x2apic = CpuidBit::Clear;
    assert(sample(cpu, kBase | kEnable).mode == LocalApicMode::XApic);
}

void invalid_encodings()
{
    auto cpu = cpu_fixture();
    assert(sample(cpu, kBase | kX2Apic).mode == LocalApicMode::Invalid);
    const auto reserved = sample(cpu, kBase | kEnable | 0x201);
    assert(reserved.mode == LocalApicMode::Invalid && reserved.reserved_bits == 0x201);
    assert(reserved.reported_base == kBase);
    for (const auto feature : {CpuidBit::Clear, CpuidBit::Unreported}) {
        cpu.features.x2apic = feature;
        const auto unsupported = sample(cpu, kBase | kEnable | kX2Apic);
        assert(unsupported.mode == LocalApicMode::Invalid && unsupported.reserved_bits == kX2Apic);
    }
}

void address_bounds()
{
    auto cpu = cpu_fixture();
    assert(sample(cpu, kEnable).base_validity == ApicBaseValidity::Invalid);
    const uint64_t above_4g = 0x1FEE00000ULL;
    const auto wide = sample(cpu, above_4g | kEnable);
    assert(wide.reported_base == above_4g && wide.base_validity == ApicBaseValidity::Valid);
    const uint64_t first_outside = 1ULL << 39;
    const auto outside = sample(cpu, first_outside | kBase | kEnable);
    assert(outside.reported_base == (first_outside | kBase) && outside.base_validity == ApicBaseValidity::Invalid);

    cpu.physical_address_bits = 52;
    const uint64_t last_page = (1ULL << 52) - 0x1000;
    assert(sample(cpu, last_page | kEnable).base_validity == ApicBaseValidity::Valid);
    const auto high_reserved = sample(cpu, (1ULL << 63) | kBase | kEnable);
    assert(high_reserved.base_validity == ApicBaseValidity::Invalid);
    assert(high_reserved.reserved_bits == (1ULL << 63) && high_reserved.reported_base == kBase);
    for (const unsigned width : {0u, 31u, 53u, 64u}) {
        cpu.physical_address_bits = static_cast<uint8_t>(width);
        assert(sample(cpu, kBase | kEnable).base_validity == ApicBaseValidity::Unknown);
    }
}

void firmware_comparison()
{
    const auto cpu = cpu_fixture();
    InterruptTopology madt{};
    madt.local_apic_address = static_cast<uint32_t>(kBase);
    assert(sample(cpu, kBase | kEnable, nullptr, &madt).madt_comparison == ApicComparison::Match);
    assert(sample(cpu, kBase + 0x1000, nullptr, &madt).madt_comparison == ApicComparison::Mismatch);

    madt.address_override_count = 1;
    madt.address_overrides[0].address = 0x1FEE00000ULL;
    const auto override_match = sample(cpu, madt.address_overrides[0].address, nullptr, &madt);
    assert(override_match.madt_base == madt.address_overrides[0].address);
    assert(override_match.madt_comparison == ApicComparison::Match);
    assert(sample(cpu, kBase, nullptr, &madt).madt_comparison == ApicComparison::Mismatch);

    madt.address_overrides[0].address |= 1;
    const auto unaligned = sample(cpu, kBase, nullptr, &madt);
    assert(unaligned.madt_base_validity == ApicBaseValidity::Invalid);
    assert(unaligned.madt_comparison == ApicComparison::Unavailable);
    madt.address_overrides[0].address = 1ULL << 39;
    assert(sample(cpu, kBase, nullptr, &madt).madt_base_validity == ApicBaseValidity::Invalid);
    madt.address_override_count = 2;
    assert(sample(cpu, kBase, nullptr, &madt).madt_base == 0);
    madt.address_override_count = 0;
    madt.malformed_count = 1;
    assert(sample(cpu, kBase, nullptr, &madt).madt_comparison == ApicComparison::Unavailable);
    assert(sample(cpu, kBase).madt_base_validity == ApicBaseValidity::Unknown);
}

void roster_comparison()
{
    auto cpu = cpu_fixture();
    PerCpuState state;
    ProcessorInventory inventory{};
    inventory.status = InventoryStatus::Complete;
    inventory.stored_count = inventory.advertised_count = 2;
    inventory.records[0].kind = inventory.records[1].kind = ProcessorKind::LocalApic;
    inventory.records[0].apic_id = 3;
    inventory.records[1].apic_id = cpu.topology.initial_apic_id;
    inventory.records[1].acpi_uid = 1;
    inventory.records[0].flags = inventory.records[1].flags = 1;
    assert(state.initialize_bootstrap(cpu, &inventory) && state.mark_online(0));
    assert(state.inventory_mapping() == CpuInventoryMapping::Mapped);
    assert(sample(cpu, kBase | kEnable, state.descriptor(0)).roster_comparison == ApicComparison::Match);
    assert(sample(cpu, kBase | kEnable, state.descriptor(1)).roster_comparison == ApicComparison::Unavailable);

    const uint64_t x2apic = kBase | kEnable | kX2Apic;
    auto bsp = *state.descriptor(0);
    bsp.apic_id = 0x102;
    assert(sample(cpu, x2apic, &bsp).roster_comparison == ApicComparison::Mismatch);
    cpu.topology.initial_apic_id = bsp.apic_id;
    assert(sample(cpu, x2apic, &bsp).roster_comparison == ApicComparison::Match);
    cpu.topology.source = CpuTopologySource::LegacyLeaf1;
    cpu.topology.initial_apic_id = 2;
    assert(sample(cpu, x2apic, &bsp).roster_comparison == ApicComparison::Insufficient);
    bsp.apic_id = 2;
    assert(sample(cpu, x2apic, &bsp).roster_comparison == ApicComparison::Insufficient);
    assert(sample(cpu, kBase | kEnable, &bsp).roster_comparison == ApicComparison::Match);
    cpu.topology.initial_apic_id = UINT8_MAX;
    assert(sample(cpu, kBase | kEnable, &bsp).roster_comparison == ApicComparison::Unavailable);
    assert(state.online_count() == 1 && state.offline_count() == 1 && !state.mark_online(1));
}

void unavailable_and_fallback()
{
    MockMsr mock{kBase | kEnable, false};
    auto cpu = cpu_fixture();
    InterruptTopology madt{};
    madt.local_apic_address = static_cast<uint32_t>(kBase);
    PerCpuState fallback;
    assert(fallback.initialize_bootstrap(cpu, nullptr) && fallback.mark_online(0));
    const auto* bsp = fallback.descriptor(0);
    const auto missing = characterize_bootstrap_apic(cpu, bsp, &madt, read_mock, &mock);
    assert(mock.calls == 1 && missing.access == ApicBaseAccess::Unavailable);
    assert(missing.mode == LocalApicMode::Unknown && missing.raw_msr == 0);
    assert(missing.madt_base_validity == ApicBaseValidity::Valid && missing.madt_comparison == ApicComparison::Unavailable);
    assert(missing.roster_comparison == ApicComparison::Match);
    assert(fallback.inventory_mapping() == CpuInventoryMapping::Unavailable && fallback.online_count() == 1);
    assert(characterize_bootstrap_apic(cpu, bsp, nullptr, nullptr, nullptr).access == ApicBaseAccess::Unavailable);

    cpu.features.apic = CpuidBit::Clear;
    assert(characterize_bootstrap_apic(cpu, bsp, &madt, read_mock, &mock).access == ApicBaseAccess::NotEnumerated);
    cpu.features.apic = CpuidBit::Set;
    cpu.features.msr = CpuidBit::Clear;
    assert(characterize_bootstrap_apic(cpu, bsp, nullptr, read_mock, &mock).access == ApicBaseAccess::NotEnumerated);
    cpu.features.msr = CpuidBit::Unreported;
    assert(characterize_bootstrap_apic(cpu, bsp, nullptr, read_mock, &mock).access == ApicBaseAccess::Unavailable);
    cpu.cpuid_available = false;
    cpu.features.msr = CpuidBit::Set;
    assert(characterize_bootstrap_apic(cpu, bsp, nullptr, read_mock, &mock).access == ApicBaseAccess::Unavailable);
    assert(mock.calls == 1);
}

} // namespace

int main()
{
    mode_and_flags();
    invalid_encodings();
    address_bounds();
    firmware_comparison();
    roster_comparison();
    unavailable_and_fallback();
    puts("AMD64 BSP APIC: 6 fixture groups passed");
}
