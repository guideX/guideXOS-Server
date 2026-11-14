#pragma once

#include "gxos_roughtime_agreement.h"

// Internal bridge for the trusted UDP acquisition implementation. Call only
// immediately after gxos_roughtime_v1_dispatch_response returns success with
// the secure-random request and PIT timestamps from that acquisition.
namespace kernel {
namespace roughtime {
namespace agreement_internal {

bool seal_acquisition_evidence(
    const gxos_roughtime_v1_evidence& verified,
    uint64_t expires_monotonic_ns,
    gxos_roughtime_verified_evidence* token);

} // namespace agreement_internal
} // namespace roughtime
} // namespace kernel
