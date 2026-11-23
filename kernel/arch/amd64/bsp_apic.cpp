#include "include/arch/bsp_apic.h"

#if defined(GXOS_ENABLE_BSP_APIC_DIAGNOSTIC)
#include "include/arch/amd64.h"
#include "../../core/include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace arch {
namespace amd64 {
namespace {

constexpr uint64_t kBsp = 1ULL << 8;
constexpr uint64_t kX2ApicEnable = 1ULL << 10;
constexpr uint64_t kGlobalEnable = 1ULL << 11;
constexpr uint64_t kAddressField = 0x000FFFFFFFFFF000ULL;

ApicBaseValidity base_validity(uint64_t address, uint8_t physical_bits)
{
    if (address == 0 || (address & ~kAddressField) != 0) return ApicBaseValidity::Invalid;
    if (physical_bits < 32 || physical_bits > 52) return ApicBaseValidity::Unknown;
    return (address >> physical_bits) == 0 ? ApicBaseValidity::Valid : ApicBaseValidity::Invalid;
}

ApicComparison compare_identity(const CpuCapabilities& cpu, const CpuDescriptor* bsp, LocalApicMode mode)
{
    if (!bsp || !bsp->bootstrap || bsp->logical_index != 0 || !bsp->apic_id_known || !known_apic_id(cpu)) {
        return ApicComparison::Unavailable;
    }
    if (cpu.topology.source == CpuTopologySource::LegacyLeaf1 &&
        (mode == LocalApicMode::X2Apic || bsp->apic_id >= UINT8_MAX)) {
        return ApicComparison::Insufficient;
    }
    return cpu.topology.initial_apic_id == bsp->apic_id ? ApicComparison::Match : ApicComparison::Mismatch;
}

void observe_base(BspApicObservation& out, const CpuCapabilities& cpu, ApicBaseReader read, void* context)
{
    if (!cpu.cpuid_available) return;
    if (cpu.features.apic == CpuidBit::Clear || cpu.features.msr == CpuidBit::Clear) {
        out.access = ApicBaseAccess::NotEnumerated;
        return;
    }
    uint64_t value = 0;
    if (cpu.features.apic != CpuidBit::Set || cpu.features.msr != CpuidBit::Set ||
        !read || !read(context, value)) return;

    out.access = ApicBaseAccess::Observed;
    out.raw_msr = value;
    out.hardware_bsp = (value & kBsp) != 0;
    out.global_enable = (value & kGlobalEnable) != 0;
    out.x2apic_enable = (value & kX2ApicEnable) != 0;
    out.reported_base = value & kAddressField;

    const uint64_t supported_flags = kBsp | kGlobalEnable |
        (cpu.features.x2apic == CpuidBit::Set ? kX2ApicEnable : 0);
    out.reserved_bits = value & ~(kAddressField | supported_flags);
    out.base_validity = (value & ~(kAddressField | 0xFFFULL)) != 0
        ? ApicBaseValidity::Invalid : base_validity(out.reported_base, cpu.physical_address_bits);
    if (out.reserved_bits != 0 || (out.x2apic_enable && !out.global_enable)) {
        out.mode = LocalApicMode::Invalid;
    } else if (!out.global_enable) {
        out.mode = LocalApicMode::Disabled;
    } else {
        out.mode = out.x2apic_enable ? LocalApicMode::X2Apic : LocalApicMode::XApic;
    }
}

#if defined(GXOS_ENABLE_BSP_APIC_DIAGNOSTIC)
bool read_apic_base(void*, uint64_t& value)
{
    // This executes once in the AMD64 BSP boot path at CPL0. read_msr has
    // no fault fixup, so the caller must establish architectural support.
    value = read_msr(0x1Bu);
    return true;
}

const char* name(CpuidBit value)
{
    switch (value) {
    case CpuidBit::Set: return "1";
    case CpuidBit::Clear: return "0";
    case CpuidBit::Unreported: return "unreported";
    }
    return "unknown";
}

const char* name(ApicBaseAccess access)
{
    switch (access) {
    case ApicBaseAccess::Unavailable: return "unavailable";
    case ApicBaseAccess::NotEnumerated: return "unsupported-by-cpuid";
    case ApicBaseAccess::Observed: return "observed";
    }
    return "unknown";
}

const char* name(LocalApicMode mode)
{
    switch (mode) {
    case LocalApicMode::Unknown: return "unknown";
    case LocalApicMode::Disabled: return "disabled";
    case LocalApicMode::XApic: return "xapic";
    case LocalApicMode::X2Apic: return "x2apic";
    case LocalApicMode::Invalid: return "invalid";
    }
    return "unknown";
}

const char* name(ApicBaseValidity validity)
{
    switch (validity) {
    case ApicBaseValidity::Unknown: return "unverified";
    case ApicBaseValidity::Valid: return "valid";
    case ApicBaseValidity::Invalid: return "invalid";
    }
    return "unknown";
}

const char* name(ApicComparison comparison)
{
    switch (comparison) {
    case ApicComparison::Unavailable: return "unavailable";
    case ApicComparison::Match: return "match";
    case ApicComparison::Mismatch: return "mismatch";
    case ApicComparison::Insufficient: return "insufficient-id-width";
    }
    return "unknown";
}

const char* register_access(const BspApicObservation& observation)
{
    switch (observation.mode) {
    case LocalApicMode::Unknown: return name(observation.access);
    case LocalApicMode::Disabled: return "disabled";
    case LocalApicMode::XApic: return "inaccessible-no-verified-uc-mapping";
    case LocalApicMode::X2Apic: return "deferred-x2apic-msrs";
    case LocalApicMode::Invalid: return "invalid-mode";
    }
    return "unknown";
}
#endif

} // namespace

BspApicObservation characterize_bootstrap_apic(
    const CpuCapabilities& cpu, const CpuDescriptor* bsp,
    const guideXOS::acpi::InterruptTopology* firmware, ApicBaseReader read, void* context)
{
    BspApicObservation out{};
    observe_base(out, cpu, read, context);
    out.roster_comparison = compare_identity(cpu, bsp, out.mode);
    if (firmware) {
        out.madt_base = firmware->effective_local_apic_address();
        out.madt_base_validity = base_validity(out.madt_base, cpu.physical_address_bits);
        if (out.base_validity == ApicBaseValidity::Valid && out.madt_base_validity == ApicBaseValidity::Valid &&
            out.reserved_bits == 0) {
            out.madt_comparison = out.reported_base == out.madt_base ? ApicComparison::Match : ApicComparison::Mismatch;
        }
    }
    return out;
}

#if defined(GXOS_ENABLE_BSP_APIC_DIAGNOSTIC)
void report_bootstrap_apic(const CpuCapabilities& cpu, const PerCpuState& roster,
                           const guideXOS::acpi::InterruptTopology* firmware)
{
    const auto* bsp = roster.descriptor(0);
    const auto observation = characterize_bootstrap_apic(cpu, bsp, firmware, read_apic_base, nullptr);
    serial::puts("[CPU6] cpuid-msr=");
    serial::puts(name(cpu.features.msr));
    serial::puts(" cpuid-apic=");
    serial::puts(name(cpu.features.apic));
    serial::puts(" cpuid-x2apic=");
    serial::puts(name(cpu.features.x2apic));
    serial::puts(" physical-address-bits=0x");
    serial::put_hex8(cpu.physical_address_bits);
    serial::puts(" ia32-apic-base=");
    serial::puts(name(observation.access));
    serial::puts(" mode=");
    serial::puts(name(observation.mode));
    if (observation.access == ApicBaseAccess::Observed) {
        serial::puts(" raw=0x");
        serial::put_hex64(observation.raw_msr);
        serial::puts(" bsp=");
        serial::putc(observation.hardware_bsp ? '1' : '0');
        serial::puts(" global-enable=");
        serial::putc(observation.global_enable ? '1' : '0');
        serial::puts(" x2apic-enable=");
        serial::putc(observation.x2apic_enable ? '1' : '0');
        serial::puts(" reported-base=0x");
        serial::put_hex64(observation.reported_base);
        serial::puts(" base-validity=");
        serial::puts(name(observation.base_validity));
        serial::puts(" reserved-bits=0x");
        serial::put_hex64(observation.reserved_bits);
    }
    serial::puts(" lapic-id-version=");
    serial::puts(register_access(observation));
    serial::puts("\n");

    serial::puts("[CPU6] madt-base=");
    if (firmware) {
        serial::puts(name(observation.madt_base_validity));
        serial::puts(" header=0x");
        serial::put_hex32(firmware->local_apic_address);
        serial::puts(" effective=0x");
        serial::put_hex64(observation.madt_base);
    } else {
        serial::puts("unavailable");
    }
    serial::puts(" msr-comparison=");
    serial::puts(name(observation.madt_comparison));
    serial::puts("\n");

    serial::puts("[CPU6] cpuid-initial-apic-id=");
    if (known_apic_id(cpu)) {
        serial::puts("0x");
        serial::put_hex32(cpu.topology.initial_apic_id);
        serial::puts(cpu.topology.source == CpuTopologySource::LegacyLeaf1 ? " cpuid-id-bits=8" : " cpuid-id-bits=32");
    } else {
        serial::puts("unavailable");
    }
    serial::puts(" roster-bsp-id=");
    if (bsp && bsp->apic_id_known) {
        serial::puts("0x");
        serial::put_hex32(bsp->apic_id);
    } else {
        serial::puts("unavailable");
    }
    serial::puts(" roster-comparison=");
    serial::puts(name(observation.roster_comparison));
    serial::puts(" firmware-mapped=");
    serial::putc(roster.inventory_mapping() == CpuInventoryMapping::Mapped ? '1' : '0');
    serial::puts(" online=0x");
    serial::put_hex32(roster.online_count());
    serial::puts(" offline=0x");
    serial::put_hex32(roster.offline_count());
    serial::puts("\n");
}
#endif

} // namespace amd64
} // namespace arch
} // namespace kernel
