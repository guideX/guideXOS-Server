#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gxos_roughtime_v1.h"

#ifndef __cplusplus
#error "The opaque NAVHTTPS14 evidence-token API requires C++ access control."
#endif

#define GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES 8U
#define GXOS_ROUGHTIME_AGREEMENT_TOKEN_BYTES 256U
#define GXOS_ROUGHTIME_AGREEMENT_MAX_POLICY_AGE_SECONDS GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS
#define GXOS_ROUGHTIME_AGREEMENT_MAX_FORWARD_JUMP_SECONDS 86400U

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gxos_roughtime_agreement_profile {
    GXOS_ROUGHTIME_AGREEMENT_PROFILE_FIXTURE_ONLY = 1,
    GXOS_ROUGHTIME_AGREEMENT_PROFILE_UNREVIEWED_CONFIG = 2
} gxos_roughtime_agreement_profile;

typedef enum gxos_roughtime_agreement_status {
    GXOS_ROUGHTIME_AGREEMENT_SATISFIED = 0,
    GXOS_ROUGHTIME_AGREEMENT_INSUFFICIENT_SOURCES = 1,
    GXOS_ROUGHTIME_AGREEMENT_INCONSISTENT_SOURCES = 2,
    GXOS_ROUGHTIME_AGREEMENT_STALE_EVIDENCE = 3,
    GXOS_ROUGHTIME_AGREEMENT_EXCESSIVE_UNCERTAINTY = 4,
    GXOS_ROUGHTIME_AGREEMENT_INVALID_SOURCE_POLICY = 5,
    GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE = 6,
    GXOS_ROUGHTIME_AGREEMENT_UNRECOGNIZED_SOURCE = 7,
    GXOS_ROUGHTIME_AGREEMENT_DUPLICATE_RESPONSE = 8,
    GXOS_ROUGHTIME_AGREEMENT_AMBIGUOUS = 9,
    GXOS_ROUGHTIME_AGREEMENT_CLOCK_ROLLBACK = 10,
    GXOS_ROUGHTIME_AGREEMENT_FORWARD_JUMP = 11,
    GXOS_ROUGHTIME_AGREEMENT_CONTINUITY_FAILURE = 12,
    GXOS_ROUGHTIME_AGREEMENT_VERIFIER_REJECTED = 13
} gxos_roughtime_agreement_status;

typedef struct gxos_roughtime_agreement_source {
    uint8_t server_public_key[32];
    uint32_t operator_identity;
} gxos_roughtime_agreement_source;

typedef struct gxos_roughtime_agreement_policy {
    uint32_t policy_identity;
    gxos_roughtime_agreement_profile profile;
    uint8_t source_count;
    uint8_t quorum;
    uint16_t reserved;
    uint32_t maximum_age_seconds;
    uint32_t maximum_source_uncertainty_seconds;
    uint32_t maximum_transport_delay_ms;
    uint32_t maximum_forward_jump_seconds;
    gxos_roughtime_agreement_source sources[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES];
} gxos_roughtime_agreement_policy;

// Opaque, fixed-size verifier-issued token. Callers may copy tokens but must
// not construct or modify their storage. Only the issue function below can
// create an accepted token; agreement evaluation has no raw-evidence input.
typedef struct gxos_roughtime_verified_evidence {
#ifdef __cplusplus
private:
    friend struct GxosRoughtimeAgreementAccess;
#endif
    uint8_t opaque[GXOS_ROUGHTIME_AGREEMENT_TOKEN_BYTES];
} gxos_roughtime_verified_evidence;

// Caller-owned, within-one-boot continuity state. Start each boot with a
// zero-initialized object. It contains no persistent or cross-boot trust.
typedef struct gxos_roughtime_agreement_continuity {
    uint8_t initialized;
    uint8_t failed;
    uint8_t failure_status;
    uint8_t reserved[5];
    uint64_t evaluated_monotonic_ns;
    uint64_t earliest_unix_seconds;
    uint64_t latest_unix_seconds;
} gxos_roughtime_agreement_continuity;

typedef struct gxos_roughtime_agreement_result {
    gxos_roughtime_agreement_status status;
    uint32_t policy_identity;
    gxos_roughtime_agreement_profile profile;
    uint8_t independent_source_count;
    uint8_t evidence_key_count;
    uint8_t continuity_checked;
    uint8_t production_policy_accepted;
    uint8_t reserved[2];
    uint64_t earliest_unix_seconds;
    uint64_t latest_unix_seconds;
    uint64_t estimated_midpoint_unix_seconds;
    uint64_t evaluated_monotonic_ns;
    uint64_t maximum_evidence_age_seconds;
    uint64_t maximum_uncertainty_seconds;
    uint32_t operator_identities[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES];
    uint8_t server_key_identities[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES][32];
} gxos_roughtime_agreement_result;

// Raw-packet token issuance is compiled only into the hosted test harness.
// Production tokens are attached by the secure-random/PIT UDP acquisition path.
#if defined(GXOS_ROUGHTIME_AGREEMENT_TESTING)
gxos_roughtime_agreement_status gxos_roughtime_agreement_issue_evidence(
    const uint8_t* response_packet,
    size_t response_packet_length,
    const uint8_t* request_packet,
    size_t request_packet_length,
    const uint8_t expected_nonce[32],
    const uint8_t server_public_key[32],
    uint64_t sent_monotonic_ns,
    uint64_t received_monotonic_ns,
    gxos_roughtime_v1_workspace* verifier_workspace,
    gxos_roughtime_verified_evidence* result);
#endif

// Evaluate at most eight verifier-issued observations. now_monotonic_ns must
// come from the same boot's monotonic source used for the request timestamps.
// If continuity is non-null, accepted results update its caller-owned state;
// rollback and forward-jump failures latch for the remainder of that state.
gxos_roughtime_agreement_status gxos_roughtime_agreement_evaluate(
    const gxos_roughtime_agreement_policy* policy,
    const gxos_roughtime_verified_evidence* evidence,
    size_t evidence_count,
    uint64_t now_monotonic_ns,
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_agreement_result* result);

const char* gxos_roughtime_agreement_status_name(
    gxos_roughtime_agreement_status status);

#ifdef __cplusplus
}

#include "../../../gxos_clock_assurance.h"

namespace gxos {
namespace roughtime {

// Data-only handoff for NAVHTTPS8. It never changes clock assurance state.
gxos::clockassurance::AgreementEvidence to_clock_assurance_evidence(
    const gxos_roughtime_agreement_result& result,
    uint64_t maximum_age_seconds);

} // namespace roughtime
} // namespace gxos
#endif
