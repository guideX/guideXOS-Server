#pragma once

#include "gxos_roughtime_agreement.h"

// Internal bridge for the trusted UDP acquisition implementation. Call only
// immediately after gxos_roughtime_v1_dispatch_response returns success with
// that acquisition's secure-random request and PIT timestamps. It validates
// the clock-evidence contract, derives the bounded expiry, and seals the token.
namespace kernel {
namespace roughtime {
namespace agreement_internal {

bool seal_acquisition_evidence(
    const gxos_roughtime_v1_evidence& verified,
    uint8_t* clock_evidence_contract_valid,
    uint64_t* expires_monotonic_ns,
    gxos_roughtime_verified_evidence* token);

} // namespace agreement_internal
} // namespace roughtime
} // namespace kernel
