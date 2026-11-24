#include "../kernel/arch/amd64/include/arch/bsp_xapic_admission.h"
#include "../guideXOSBootLoader/amd64_boot_paging.h"
#include "../guideXOSBootLoader/bootinfo_acpi.h"

#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
#include <string>

using namespace kernel::arch::amd64;
using namespace guideXOS::acpi;
using Status = XapicAdmission;

namespace {

constexpr uint64_t kBase = 0xFEE00000;
constexpr uint64_t kBsp = 1ull << 8;
constexpr uint64_t kEnable = 1ull << 11;
constexpr uint64_t kNx = 1ull << 63;
constexpr uint64_t kCacheIndex3 = 3ull << 3;
constexpr uint64_t kPat4KiB = 1ull << 7;
constexpr uint64_t kDefaultPat = 0x0007040600070406;

std::string serial_log;
unsigned cpuid_reads;

CpuCapabilities supported_cpu()
{
    CpuCapabilities cpu{};
    cpu.cpuid_available = true;
    cpu.features.msr = cpu.features.apic = cpu.features.x2apic = CpuidBit::Set;
    cpu.features.long_mode = cpu.features.nx = CpuidBit::Set;
    cpu.physical_address_bits = 39;
    cpu.topology.source = CpuTopologySource::LegacyLeaf1;
    cpu.topology.initial_apic_id = 2;
    return cpu;
}

ProcessorInventory inventory_fixture()
{
    ProcessorInventory inventory{};
    inventory.status = InventoryStatus::Complete;
    inventory.stored_count = inventory.advertised_count = inventory.enabled_count = 2;
    inventory.records[0] = {2, 0, 1, 44, ProcessorKind::LocalApic, false, false};
    inventory.records[1] = {3, 1, 1, 52, ProcessorKind::LocalApic, false, false};
    return inventory;
}

struct Fixture {
    CpuCapabilities cpu = supported_cpu();
    ProcessorInventory inventory = inventory_fixture();
    InterruptTopology firmware{};
    uint64_t msr = kBase | kBsp | kEnable;
    XapicMappingPlan plan{};

    Fixture()
    {
        firmware.local_apic_address = kBase;
        plan.physical_base = kBase;
        plan.bytes = 4096;
        plan.virtual_base = 0xFFFF900000000000;
        plan.active_root = plan.audited_root = 0x4000;
        plan.four_level_root = true;
        plan.ownership = plan.aliases = ApertureCheck::Verified;
        plan.nxe = plan.write_protect = true;
        plan.proposed_leaf = kBase | 1 | kNx | kCacheIndex3;
        plan.cache = {true, kDefaultPat, 0};
        plan.active_table_pages = 605;
        plan.required_new_tables = plan.free_table_frames = 3;
    }

    Status evaluate() const
    {
        PerCpuState roster;
        assert(roster.initialize_bootstrap(cpu, &inventory));
        assert(roster.mark_online(0));
        return evaluate_bsp_xapic_admission(roster, &firmware, &msr, &plan);
    }
};

void processor_and_mode()
{
    Fixture f;
    assert(f.evaluate() == Status::Eligible);
    f.cpu.features.x2apic = CpuidBit::Clear;
    assert(f.evaluate() == Status::Eligible);
    f.cpu.features.long_mode = CpuidBit::Unreported;
    assert(f.evaluate() == Status::UnsupportedCpu);
    f.cpu = supported_cpu();
    f.cpu.features.msr = CpuidBit::Clear;
    assert(f.evaluate() == Status::UnsupportedCpu);
    f.cpu = supported_cpu();
    for (uint64_t flags : {kBsp, kEnable, kBsp | kEnable | 0x400u, kBsp | kEnable | 1u}) {
        f.msr = kBase | flags;
        assert(f.evaluate() == Status::UnsupportedMode);
    }
}

void firmware_and_address_bounds()
{
    Fixture f;
    f.firmware.address_override_count = 1;
    f.firmware.address_overrides[0].address = 0xFEC00000;
    assert(f.evaluate() == Status::BaseMismatch);
    f.firmware.address_overrides[0].address = kBase;
    assert(f.evaluate() == Status::Eligible);
    f.firmware.address_override_count = 2;
    assert(f.evaluate() == Status::InvalidAperture);
    f.firmware.address_override_count = 1;
    f.firmware.address_overrides[0].address = 1ull << 39;
    assert(f.evaluate() == Status::InvalidAperture);
    f.firmware.address_overrides[0].address = kBase + 1;
    assert(f.evaluate() == Status::InvalidAperture);
    f.firmware.address_overrides[0].address = UINT64_MAX & ~4095ull;
    assert(f.evaluate() == Status::InvalidAperture);
    f.firmware.address_overrides[0].address = kBase;
    f.cpu.physical_address_bits = 0;
    assert(f.evaluate() == Status::InvalidAperture);
    f.cpu.physical_address_bits = 32;
    f.msr |= 1ull << 32;
    assert(f.evaluate() == Status::InvalidAperture);
    f.msr = kBsp | kEnable;
    assert(f.evaluate() == Status::InvalidAperture);
    f.msr = kBase | kBsp | kEnable;
    f.firmware.malformed_count = 1;
    assert(f.evaluate() == Status::FirmwareInvalid);
    f.firmware.malformed_count = 0;
    f.firmware.io_apic_count = kIoApicCapacity + 1;
    assert(f.evaluate() == Status::FirmwareInvalid);
    f.firmware.io_apic_count = 0;
    f.inventory.records[0].flags = 0;
    assert(f.evaluate() == Status::FirmwareInvalid);
}

void aperture_and_ownership()
{
    Fixture f;
    f.plan.bytes = UINT64_MAX;
    assert(f.evaluate() == Status::InvalidAperture);
    f.plan.bytes = 8192;
    assert(f.evaluate() == Status::InvalidAperture);
    f.plan.bytes = 4096;
    f.plan.virtual_base |= 1;
    assert(f.evaluate() == Status::InvalidAperture);
    f.plan.virtual_base = UINT64_MAX & ~4095ull;
    assert(f.evaluate() == Status::InvalidAperture);
    f.plan.virtual_base = 0x0000800000000000;
    assert(f.evaluate() == Status::InvalidAperture);
    f = Fixture{};
    f.firmware.io_apic_count = 1;
    f.firmware.io_apics[0].address = kBase + 32;
    assert(f.evaluate() == Status::OwnershipConflict);
    f.firmware.io_apics[0].address = 0xFEC00000;
    f.plan.ownership = ApertureCheck::Conflict;
    assert(f.evaluate() == Status::OwnershipConflict);
    f.plan.ownership = ApertureCheck::Unverified;
    assert(f.evaluate() == Status::OwnershipUnverified);
    f.plan.ownership = ApertureCheck::Verified;
    f.plan.aliases = ApertureCheck::Conflict;
    assert(f.evaluate() == Status::AliasConflict);
    f.plan.aliases = ApertureCheck::Unverified;
    assert(f.evaluate() == Status::AliasUnverified);
}

void root_and_permissions()
{
    Fixture f;
    f.plan.audited_root += 4096;
    assert(f.evaluate() == Status::RootUnverified);
    f.plan.audited_root = f.plan.active_root;
    f.plan.four_level_root = false;
    assert(f.evaluate() == Status::RootUnverified);
    f.plan.four_level_root = true;
    f.plan.nxe = false;
    assert(f.evaluate() == Status::PermissionUnverified);
    f.plan.nxe = true;
    f.plan.write_protect = false;
    assert(f.evaluate() == Status::PermissionUnverified);
    f.plan.write_protect = true;
    const uint64_t leaf = f.plan.proposed_leaf;
    for (uint64_t change : {1ull, 2ull, 4ull, 256ull, 4096ull, kNx}) {
        f.plan.proposed_leaf = leaf ^ change;
        assert(f.evaluate() == Status::PermissionUnverified);
    }
}

void cache_type_and_capacity()
{
    Fixture f;
    f.plan.cache.verified = false;
    assert(f.evaluate() == Status::CacheTypeUnverified);
    f.plan.cache.verified = true;
    f.plan.cache.pat ^= 7ull << 24; // The selected slot is now UC-.
    assert(f.evaluate() == Status::CacheTypeUnverified);
    f.plan.proposed_leaf |= kPat4KiB; // Select slot 7, which remains UC.
    assert(f.evaluate() == Status::Eligible);
    f.plan.cache.mtrr_type = 6; // WB is outside the qualified subset.
    assert(f.evaluate() == Status::CacheTypeUnverified);
    f.plan.cache.mtrr_type = 0;

    const auto limit = guideXOS::amd64_boot_paging::kMaxTablePages;
    f.plan.active_table_pages = limit - 3;
    assert(f.evaluate() == Status::Eligible);
    ++f.plan.active_table_pages;
    assert(f.evaluate() == Status::TableCapacityExceeded);
    f.plan.active_table_pages = limit + 1;
    f.plan.required_new_tables = 0;
    assert(f.evaluate() == Status::TableCapacityExceeded);
    f.plan.active_table_pages = limit;
    assert(f.evaluate() == Status::Eligible);
    f.plan.active_table_pages = limit - 3;
    f.plan.required_new_tables = 3;
    f.plan.free_table_frames = 2;
    const auto original_leaf = f.plan.proposed_leaf;
    assert(f.evaluate() == Status::TableCapacityExceeded);
    assert(f.evaluate() == Status::TableCapacityExceeded);
    assert(f.plan.proposed_leaf == original_leaf && f.plan.free_table_frames == 2);
    ++f.plan.free_table_frames;
    assert(f.evaluate() == Status::Eligible);
}

void fallback_and_repeated_boot()
{
    const Fixture f;
    PerCpuState fallback;
    assert(fallback.initialize_bootstrap(f.cpu, nullptr));
    assert(fallback.mark_online(0));
    assert(!fallback.mark_online(1));
    assert(fallback.online_count() == 1 && fallback.offline_count() == 0);
    assert(evaluate_bsp_xapic_admission(fallback, nullptr, nullptr, nullptr) == Status::FirmwareUnavailable);
    assert(evaluate_bsp_xapic_admission(fallback, &f.firmware, &f.msr, &f.plan) == Status::FirmwareInvalid);

    // The actual boot hook gets a valid handoff but no native MSR or mapping
    // backend. The executable supplies only CPUID and COM1 stubs.
    alignas(4096) guideXOS::BootInfoWithAcpi handoff{};
    handoff.base.Magic = guideXOS::GUIDEXOS_BOOTINFO_MAGIC;
    handoff.base.Version = guideXOS::GUIDEXOS_BOOTINFO_VERSION;
    handoff.base.Size = sizeof(handoff.base);
    handoff.base.BootMode = guideXOS::BootMode::Uefi;
    handoff.acpi.inventory = f.inventory;
    handoff.interrupts.topology = f.firmware;
    guideXOS::seal_bootinfo_acpi(handoff);
    guideXOS::seal_bootinfo_interrupt_topology(handoff);
    const auto* boot_bytes = reinterpret_cast<const unsigned char*>(&handoff.base);
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(handoff.base); i += sizeof(uint32_t)) {
        uint32_t word;
        memcpy(&word, boot_bytes + i, sizeof(word));
        sum += word;
    }
    handoff.base.HeaderChecksum = 0u - sum;
    initialize_boot_per_cpu(&handoff.base);
    const auto& roster = boot_per_cpu_state();
    assert(roster.online_count() == 1 && roster.offline_count() == 1);
    assert(evaluate_bsp_xapic_admission(roster, &f.firmware, &f.msr, nullptr) == Status::OwnershipUnverified);
    const std::string expected = "[CPU7B1] admission=missing-safe-current-boot-apic-base-sample"
        " online=0x00000001 offline=0x00000001 mapping=not-installed controller-access=not-attempted\r\n";
    assert(serial_log == expected && cpuid_reads == 1);
    initialize_boot_per_cpu(nullptr);
    assert(serial_log == expected && cpuid_reads == 2);
    assert(roster.online_count() == 1 && roster.offline_count() == 1);
}

} // namespace

namespace kernel {
namespace arch {
namespace amd64 {
CpuCapabilities read_cpu_capabilities()
{
    ++cpuid_reads;
    return supported_cpu();
}
uint8_t inb(uint16_t port)
{
    assert(port == 0x3FD);
    return 0x20;
}

void outb(uint16_t port, uint8_t value)
{
    assert(port == 0x3F8);
    serial_log += static_cast<char>(value);
}
} // namespace amd64
} // namespace arch
} // namespace kernel

int main()
{
    processor_and_mode();
    firmware_and_address_bounds();
    aperture_and_ownership();
    root_and_permissions();
    cache_type_and_capacity();
    fallback_and_repeated_boot();
    puts("AMD64 BSP xAPIC admission: 6 hosted fixture groups passed");
}
