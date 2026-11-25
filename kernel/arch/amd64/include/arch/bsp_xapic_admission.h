#pragma once

#include "per_cpu.h"

namespace kernel {
namespace arch {
namespace amd64 {

enum class XapicAdmission : uint8_t {
    UnsupportedCpu, FirmwareUnavailable, FirmwareInvalid, InvalidAperture,
    MissingApicBaseSample, UnsupportedMode, BaseMismatch, OwnershipUnverified,
    OwnershipConflict, AliasUnverified, AliasConflict, RootUnverified,
    PermissionUnverified, CacheTypeUnverified, TableCapacityExceeded, Eligible
};

enum class ApertureCheck : uint8_t { Unverified, Verified, Conflict };

struct XapicCacheEvidence {
    bool verified;
    uint64_t pat;
    // Resolved MTRR type over the entire page, after fixed/variable/default
    // precedence, range validation and conflict checks. Architectural encoding.
    uint8_t mtrr_type;
};

// A trusted producer must bind these facts to the same current-boot BSP,
// physical page and active root. CPU7B1 has no native producer for this plan.
struct XapicMappingPlan {
    uint64_t physical_base;
    uint64_t bytes;
    uint64_t virtual_base;
    // Normalized physical roots with CR3 metadata removed. The audit must
    // establish four-level paging and validate the existing tree and path.
    uint64_t active_root;
    uint64_t audited_root;
    bool four_level_root;
    // Verified ownership requires an exclusive platform MMIO reservation,
    // exclusion from RAM/allocators and no overlapping kernel/device ranges.
    ApertureCheck ownership;
    // Verified requires an empty, reserved virtual slot and no physical aliases
    // (including large pages) across all applicable roots and direct maps.
    ApertureCheck aliases;
    bool nxe;
    bool write_protect;
    uint64_t proposed_leaf;
    XapicCacheEvidence cache;
    // Audit counts the root, all reachable tables and self-map tables against
    // CPU4's budget. Free frames must be eligible for kernel-owned tables.
    uint32_t active_table_pages;
    uint32_t required_new_tables;
    uint32_t free_table_frames;
};

// The optional MSR value must be independently and safely sampled from this
// BSP in the current boot. This evaluator has no hardware or allocator callbacks.
// Eligibility validates a plan against the evidence and conveys no mapping.
XapicAdmission evaluate_bsp_xapic_admission(
    const PerCpuState& roster, const guideXOS::acpi::InterruptTopology* firmware,
    const uint64_t* apic_base_msr, const XapicMappingPlan* plan);

const char* xapic_admission_name(XapicAdmission admission);

#if defined(GXOS_ENABLE_BSP_XAPIC_ADMISSION_DIAGNOSTIC)
void report_bsp_xapic_admission(const PerCpuState& roster,
                               const guideXOS::acpi::InterruptTopology* firmware,
                               const uint64_t* apic_base_msr = nullptr);
#endif

} // namespace amd64
} // namespace arch
} // namespace kernel
