#include "include/arch/bsp_apic_base_probe.h"

namespace kernel {
namespace arch {
namespace amd64 {
namespace {

// One owner for the diagnostic IDT and assembly fixup context. Acquire it
// before the reader can prepare or publish either of those objects.
bool s_probe_in_use;

} // namespace

ApicBaseProbeResult sample_bootstrap_apic_base(const PerCpuState& roster, ApicBaseProbeReader reader)
{
    const auto* bsp = roster.descriptor(0);
    if (!reader || !bsp || !bsp->bootstrap || bsp->logical_index != 0 ||
        bsp->lifecycle != CpuLifecycle::Online || roster.online_count() != 1) {
        return {ApicBaseProbeStatus::InvalidContext, 0};
    }
    const auto* cpu = roster.capabilities(0);
    if (!cpu || !cpu->cpuid_available || cpu->features.long_mode != CpuidBit::Set ||
        cpu->features.apic != CpuidBit::Set || cpu->features.msr != CpuidBit::Set) {
        return {ApicBaseProbeStatus::UnsupportedCpu, 0};
    }
    if (__atomic_exchange_n(&s_probe_in_use, true, __ATOMIC_ACQUIRE)) {
        return {ApicBaseProbeStatus::Busy, 0};
    }

    uint64_t value = 0;
    const auto status = reader(value);
    __atomic_store_n(&s_probe_in_use, false, __ATOMIC_RELEASE);
    return {status, status == ApicBaseProbeStatus::Observed ? value : 0};
}

const char* apic_base_probe_status_name(ApicBaseProbeStatus status)
{
    switch (status) {
    case ApicBaseProbeStatus::Observed: return "observed";
    case ApicBaseProbeStatus::InvalidContext: return "invalid-context";
    case ApicBaseProbeStatus::GeneralProtection: return "recovered-gp";
    case ApicBaseProbeStatus::UnsupportedCpu: return "unsupported-cpu";
    case ApicBaseProbeStatus::Busy: return "busy";
    }
    return "unknown";
}

} // namespace amd64
} // namespace arch
} // namespace kernel
