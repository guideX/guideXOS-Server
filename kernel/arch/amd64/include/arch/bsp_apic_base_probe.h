#pragma once

#include "per_cpu.h"

namespace kernel {
namespace arch {
namespace amd64 {

// The first three values are also the return codes of bsp_apic_base_fixup.asm.
enum class ApicBaseProbeStatus : uint32_t {
    Observed = 0, InvalidContext = 1, GeneralProtection = 2, UnsupportedCpu, Busy
};

struct ApicBaseProbeResult {
    ApicBaseProbeStatus status;
    uint64_t value;

    const uint64_t* observation() const {
        return status == ApicBaseProbeStatus::Observed ? &value : nullptr;
    }
};

// A reader checks the native entry context before executing the fixed read.
using ApicBaseProbeReader = ApicBaseProbeStatus (*)(uint64_t& value);
ApicBaseProbeResult sample_bootstrap_apic_base(const PerCpuState& roster, ApicBaseProbeReader reader);
const char* apic_base_probe_status_name(ApicBaseProbeStatus status);

} // namespace amd64
} // namespace arch
} // namespace kernel
