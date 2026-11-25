#include "include/arch/bsp_xapic_admission.h"
#include "include/arch/bsp_apic.h"
#include "../../../guideXOSBootLoader/amd64_boot_paging.h"

#if defined(GXOS_ENABLE_BSP_XAPIC_ADMISSION_DIAGNOSTIC)
#include "../../core/include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace arch {
namespace amd64 {
namespace {

using namespace guideXOS::amd64_boot_paging;
using Status = XapicAdmission;

constexpr uint64_t kPresent = 1ull << 0;
constexpr uint64_t kPwt = 1ull << 3;
constexpr uint64_t kPcd = 1ull << 4;
constexpr uint64_t kPat4KiB = 1ull << 7;
constexpr uint64_t kNx = 1ull << 63;
constexpr uint64_t kKernelCanonicalBase = 0xFFFF800000000000ull;
constexpr uint8_t kStrongUc = 0;

bool valid_aperture(uint64_t base, uint64_t bytes, uint8_t physical_bits)
{
    if (base == 0 || (base & kPageMask) != 0 || bytes != kPageBytes ||
        physical_bits < 32 || physical_bits > 52) return false;
    const uint64_t limit = 1ull << physical_bits;
    return base < limit && bytes <= limit - base;
}

bool copy_apic_base_sample(void* context, uint64_t& out)
{
    out = *static_cast<const uint64_t*>(context);
    return true;
}

bool is_strong_uc(const XapicCacheEvidence& cache, uint64_t leaf)
{
    // CPU7B1 qualifies only the shared Intel/AMD UC + UC case. The index
    // comes from the actual proposed leaf. UC- and unproven inputs fail closed.
    const uint64_t index = ((leaf & kPat4KiB) >> 5) | ((leaf & (kPcd | kPwt)) >> 3);
    const uint8_t pat_type = static_cast<uint8_t>(cache.pat >> (8 * index));
    return cache.verified && cache.mtrr_type == kStrongUc && pat_type == kStrongUc;
}

Status evaluate_plan(const CpuCapabilities& cpu, uint64_t base, const XapicMappingPlan& plan)
{
    if (plan.physical_base != base || !valid_aperture(base, plan.bytes, cpu.physical_address_bits) ||
        plan.virtual_base < kKernelCanonicalBase || (plan.virtual_base & kPageMask) != 0 ||
        plan.virtual_base > UINT64_MAX - plan.bytes) return Status::InvalidAperture;
    if (plan.ownership == ApertureCheck::Conflict) return Status::OwnershipConflict;
    if (plan.ownership != ApertureCheck::Verified) return Status::OwnershipUnverified;
    if (plan.aliases == ApertureCheck::Conflict) return Status::AliasConflict;
    if (plan.aliases != ApertureCheck::Verified) return Status::AliasUnverified;
    if (!plan.four_level_root || plan.active_root != plan.audited_root ||
        plan.active_root >= kIdentityLimit ||
        !valid_aperture(plan.active_root, kPageBytes, cpu.physical_address_bits) ||
        plan.active_root == base || plan.active_table_pages == 0) return Status::RootUnverified;

    const uint64_t required_leaf = base | kPresent | kNx;
    const uint64_t cache_bits = kPat4KiB | kPcd | kPwt;
    if (!plan.nxe || cpu.features.nx != CpuidBit::Set || !plan.write_protect ||
        (plan.proposed_leaf & ~cache_bits) != required_leaf) return Status::PermissionUnverified;
    if (!is_strong_uc(plan.cache, plan.proposed_leaf)) return Status::CacheTypeUnverified;

    if (plan.active_table_pages > kMaxTablePages ||
        plan.required_new_tables > 3 || plan.required_new_tables > plan.free_table_frames ||
        plan.required_new_tables > kMaxTablePages - plan.active_table_pages) {
        return Status::TableCapacityExceeded;
    }
    return Status::Eligible;
}

} // namespace

XapicAdmission evaluate_bsp_xapic_admission(
    const PerCpuState& roster, const guideXOS::acpi::InterruptTopology* firmware,
    const uint64_t* apic_base_msr, const XapicMappingPlan* plan)
{
    const auto* cpu = roster.capabilities(0);
    if (!cpu || !cpu->cpuid_available || cpu->features.long_mode != CpuidBit::Set ||
        cpu->features.apic != CpuidBit::Set || cpu->features.msr != CpuidBit::Set) {
        return Status::UnsupportedCpu;
    }
    if (!firmware) return Status::FirmwareUnavailable;
    const auto* bsp = roster.descriptor(0);
    if (firmware->truncated() || firmware->malformed_count != 0 ||
        roster.inventory_mapping() != CpuInventoryMapping::Mapped ||
        roster.online_count() != 1 || !bsp || bsp->firmware != FirmwareCpuState::Enabled ||
        !bsp->apic_id_known || bsp->apic_id >= UINT8_MAX) return Status::FirmwareInvalid;

    const uint64_t base = firmware->effective_local_apic_address();
    if (!valid_aperture(base, kPageBytes, cpu->physical_address_bits)) return Status::InvalidAperture;
    for (uint16_t i = 0; i < firmware->io_apic_count; ++i) {
        if ((firmware->io_apics[i].address & ~kPageMask) == base) return Status::OwnershipConflict;
    }

    if (!apic_base_msr) return Status::MissingApicBaseSample;
    uint64_t sample = *apic_base_msr;
    const auto observation = characterize_bootstrap_apic(*cpu, bsp, firmware, copy_apic_base_sample, &sample);
    if (observation.mode != LocalApicMode::XApic || !observation.hardware_bsp ||
        observation.roster_comparison != ApicComparison::Match) return Status::UnsupportedMode;
    if (observation.base_validity != ApicBaseValidity::Valid) return Status::InvalidAperture;
    if (observation.madt_comparison != ApicComparison::Match) return Status::BaseMismatch;
    if (!plan) return Status::OwnershipUnverified;
    return evaluate_plan(*cpu, base, *plan);
}

const char* xapic_admission_name(XapicAdmission admission)
{
    switch (admission) {
    case Status::UnsupportedCpu: return "unsupported-cpu";
    case Status::FirmwareUnavailable: return "firmware-unavailable";
    case Status::FirmwareInvalid: return "firmware-invalid";
    case Status::InvalidAperture: return "invalid-aperture";
    case Status::MissingApicBaseSample: return "missing-safe-current-boot-apic-base-sample";
    case Status::UnsupportedMode: return "unsupported-apic-mode-or-bsp";
    case Status::BaseMismatch: return "firmware-msr-base-mismatch";
    case Status::OwnershipUnverified: return "ownership-unverified";
    case Status::OwnershipConflict: return "ownership-conflict";
    case Status::AliasUnverified: return "alias-unverified";
    case Status::AliasConflict: return "alias-conflict";
    case Status::RootUnverified: return "kernel-root-unverified";
    case Status::PermissionUnverified: return "permissions-unverified";
    case Status::CacheTypeUnverified: return "effective-uc-unverified";
    case Status::TableCapacityExceeded: return "page-table-capacity-exceeded";
    case Status::Eligible: return "plan-eligible";
    }
    return "unknown";
}

#if defined(GXOS_ENABLE_BSP_XAPIC_ADMISSION_DIAGNOSTIC)
void report_bsp_xapic_admission(const PerCpuState& roster,
                               const guideXOS::acpi::InterruptTopology* firmware,
                               const uint64_t* apic_base_msr)
{
    const auto status = evaluate_bsp_xapic_admission(roster, firmware, apic_base_msr, nullptr);
    serial::puts("[CPU7B1] admission=");
    serial::puts(xapic_admission_name(status));
    serial::puts(" online=0x");
    serial::put_hex32(roster.online_count());
    serial::puts(" offline=0x");
    serial::put_hex32(roster.offline_count());
    serial::puts(" mapping=not-installed controller-access=not-attempted\n");
}
#endif

} // namespace amd64
} // namespace arch
} // namespace kernel
