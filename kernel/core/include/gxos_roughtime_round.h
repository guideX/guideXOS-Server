#pragma once

#include <stdint.h>

#include "gxos_roughtime_acquisition.h"

#define GXOS_ROUGHTIME_ROUND_MAX_SOURCES 3U
#define GXOS_ROUGHTIME_ROUND_MAX_REQUESTS 3U

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gxos_roughtime_round_source {
    uint32_t server_ipv4;
    uint16_t server_port;
    uint16_t reserved;
    uint32_t operator_identity;
    uint8_t server_public_key[32];
} gxos_roughtime_round_source;

// Experimental, caller-supplied configuration. The implementation fixes the
// policy profile to UNREVIEWED_CONFIG and the quorum to two of three.
typedef struct gxos_roughtime_round_configuration {
    uint32_t policy_identity;
    uint8_t source_count;
    uint8_t quorum;
    uint16_t reserved;
    uint32_t maximum_age_seconds;
    uint32_t maximum_source_uncertainty_seconds;
    uint32_t maximum_transport_delay_ms;
    uint32_t maximum_forward_jump_seconds;
    gxos_roughtime_round_source sources[GXOS_ROUGHTIME_ROUND_MAX_SOURCES];
} gxos_roughtime_round_configuration;

typedef enum gxos_roughtime_round_status {
    GXOS_ROUGHTIME_ROUND_EVALUATED = 0,
    GXOS_ROUGHTIME_ROUND_INVALID_CONFIGURATION = 1,
    GXOS_ROUGHTIME_ROUND_BUSY = 2,
    GXOS_ROUGHTIME_ROUND_MONOTONIC_FAILURE = 3
} gxos_roughtime_round_status;

typedef struct gxos_roughtime_round_source_result {
    gxos_roughtime_acquisition_status status;
    uint32_t server_ipv4;
    uint16_t server_port;
    uint16_t request_bytes;
    uint16_t response_bytes;
    uint8_t verified;
    uint8_t reserved[3];
    uint8_t server_key_identity[32];
} gxos_roughtime_round_source_result;

typedef struct gxos_roughtime_round_result {
    gxos_roughtime_round_status status;
    uint8_t attempted_source_count;
    uint8_t verified_response_count;
    uint8_t independent_operator_count;
    uint8_t reserved;
    gxos_roughtime_round_source_result sources[GXOS_ROUGHTIME_ROUND_MAX_SOURCES];
    gxos_roughtime_verified_evidence evidence[GXOS_ROUGHTIME_ROUND_MAX_SOURCES];
    gxos_roughtime_agreement_result agreement;
} gxos_roughtime_round_result;

const char* gxos_roughtime_round_status_name(gxos_roughtime_round_status status);

#ifdef __cplusplus
}

namespace kernel {
namespace roughtime {

// Performs at most three independent 1036-byte requests. Each source has the
// existing finite acquisition deadline; evidence is evaluated once, at the
// monotonic instant sampled after the final source attempt.
gxos_roughtime_round_status acquire_agreement_round(
    const gxos_roughtime_round_configuration* configuration,
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_round_result* result);

} // namespace roughtime
} // namespace kernel

#if defined(GXOS_ROUGHTIME_ROUND_TESTING)
typedef gxos_roughtime_acquisition_status (*gxos_roughtime_round_test_provider_fn)(
    void* context,
    const gxos_roughtime_round_source* source,
    gxos_roughtime_v1_acquisition_result* result);

extern "C" gxos_roughtime_round_status gxos_roughtime_round_test_run(
    const gxos_roughtime_round_configuration* configuration,
    uint64_t now_monotonic_ns,
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_round_result* result,
    gxos_roughtime_round_test_provider_fn provider,
    void* provider_context);
#endif
#endif
