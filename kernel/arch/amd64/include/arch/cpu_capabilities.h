#pragma once

#include <stdint.h>

namespace kernel {
namespace arch {
namespace amd64 {

struct CpuidRegisters {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

// Return false if the requested result is unavailable. The decoder ignores it.
using CpuidReader = bool (*)(void*, uint32_t leaf, uint32_t subleaf, CpuidRegisters&);

enum class CpuidBit : uint8_t { Unreported, Clear, Set };

struct CpuFeatures {
    CpuidBit tsc;
    CpuidBit msr;
    CpuidBit apic;
    CpuidBit sse2;
    CpuidBit pcid;
    CpuidBit x2apic;
    CpuidBit tsc_deadline;
    CpuidBit xsave;
    // CPUID reports CR4.OSXSAVE here. AVX usability also depends on XCR0.
    CpuidBit osxsave;
    CpuidBit avx;
    CpuidBit hypervisor;
    CpuidBit avx2;
    CpuidBit smep;
    CpuidBit smap;
    CpuidBit nx;
    CpuidBit page_1g;
    CpuidBit rdtscp;
    CpuidBit long_mode;
    CpuidBit invariant_tsc;
};

enum class CpuTopologySource : uint8_t {
    Unavailable, Leaf1F, Leaf0B, LegacyLeaf1, AmdLeaf80000026, AmdLeaf8000001E
};

// Hints for the sampled logical processor. No firmware or online CPU list is read.
struct CpuTopologyHint {
    CpuTopologySource source;
    uint32_t initial_apic_id;
    // Structured leaves describe subleaf 0 only. The ID-only sources leave these zero.
    uint8_t level_type;
    uint8_t apic_id_shift;
    // Structured level's EBX count, or legacy HTT-qualified addressable-ID field.
    // Zero means unavailable. Neither field is an active processor count.
    uint16_t logical_count_hint;
};

struct CpuCapabilities {
    bool cpuid_available;
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;  // Zero if the extended range is unavailable.
    char vendor[13];
    char brand[49];               // Empty if the complete brand is unavailable.
    uint8_t physical_address_bits;
    uint8_t linear_address_bits;  // Maximum reported width. Zero means unavailable.
    CpuFeatures features;
    CpuTopologyHint topology;
};

inline bool known_apic_id(const CpuCapabilities& cpu)
{
    const auto& topology = cpu.topology;
    if (!cpu.cpuid_available || topology.source == CpuTopologySource::Unavailable) return false;
    if (topology.source == CpuTopologySource::LegacyLeaf1) return topology.initial_apic_id < UINT8_MAX;
    return topology.initial_apic_id < UINT32_MAX;
}

CpuCapabilities decode_cpu_capabilities(CpuidReader read, void* context);

// CPUID only. For per-CPU results the caller must keep the sample on one CPU.
CpuCapabilities read_cpu_capabilities();

// Narrow CPUID query for the existing NX enablement policies.
bool cpu_supports_nx();

} // namespace amd64
} // namespace arch
} // namespace kernel
