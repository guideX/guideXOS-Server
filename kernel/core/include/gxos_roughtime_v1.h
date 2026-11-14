#pragma once

#include <stddef.h>
#include <stdint.h>

#define GXOS_ROUGHTIME_V1_MAX_RESPONSE_PACKET_BYTES 1024U
#define GXOS_ROUGHTIME_V1_MAX_REQUEST_PACKET_BYTES 1036U
#define GXOS_ROUGHTIME_V1_MAX_MAP_PAIRS 16U
#define GXOS_ROUGHTIME_V1_MAX_PATH_HASHES 32U
#define GXOS_ROUGHTIME_V1_MAX_PARSE_OPERATIONS 576U
#define GXOS_ROUGHTIME_V1_MAX_NESTING_DEPTH 3U
#define GXOS_ROUGHTIME_V1_TRANSCRIPT_BYTES 1088U
#define GXOS_ROUGHTIME_V1_TIME_MIN_UNIX_SECONDS 1704067200ULL
#define GXOS_ROUGHTIME_V1_TIME_MAX_UNIX_SECONDS 4133980800ULL
#define GXOS_ROUGHTIME_V1_MAX_RADIUS_SECONDS 300U
#define GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS 300U

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gxos_roughtime_v1_status {
    GXOS_ROUGHTIME_V1_VERIFIED = 0,
    GXOS_ROUGHTIME_V1_INVALID_ARGUMENT = 1,
    GXOS_ROUGHTIME_V1_BUFFER_LIMIT = 2,
    GXOS_ROUGHTIME_V1_INVALID_FRAMING = 3,
    GXOS_ROUGHTIME_V1_INVALID_MAP = 4,
    GXOS_ROUGHTIME_V1_MISSING_FIELD = 5,
    GXOS_ROUGHTIME_V1_UNSUPPORTED_VERSION = 6,
    GXOS_ROUGHTIME_V1_INVALID_REQUEST = 7,
    GXOS_ROUGHTIME_V1_KEY_ID_MISMATCH = 8,
    GXOS_ROUGHTIME_V1_NONCE_MISMATCH = 9,
    GXOS_ROUGHTIME_V1_INVALID_SIGNATURE = 10,
    GXOS_ROUGHTIME_V1_INVALID_DELEGATION = 11,
    GXOS_ROUGHTIME_V1_INVALID_MERKLE_PROOF = 12,
    GXOS_ROUGHTIME_V1_INVALID_TIME = 13,
    GXOS_ROUGHTIME_V1_EXCESSIVE_UNCERTAINTY = 14,
    GXOS_ROUGHTIME_V1_HASH_FAILURE = 15
} gxos_roughtime_v1_status;

typedef struct gxos_roughtime_v1_workspace {
    uint8_t transcript[GXOS_ROUGHTIME_V1_TRANSCRIPT_BYTES];
    uint8_t digest[64];
    uint8_t merkle_current[32];
    uint8_t merkle_next[32];
} gxos_roughtime_v1_workspace;

// The result is committed only after all structure, signature, nonce, and
// Merkle checks succeed. On rejection, status is set and all evidence fields
// remain zero. This is cryptographic single-response evidence, not clock
// policy approval and not permission to change the operating-system clock.
typedef struct gxos_roughtime_v1_evidence {
    gxos_roughtime_v1_status status;
    uint32_t protocol_version;
    uint32_t radius_seconds;
    uint64_t server_midpoint_unix_seconds;
    uint64_t estimated_midpoint_unix_seconds;
    uint64_t server_earliest_unix_seconds;
    uint64_t server_latest_unix_seconds;
    uint64_t earliest_unix_seconds;
    uint64_t latest_unix_seconds;
    uint64_t estimated_uncertainty_seconds;
    uint64_t sent_monotonic_ns;
    uint64_t received_monotonic_ns;
    uint64_t measured_transport_delay_ns;
    uint8_t server_key_identity[32];
    uint8_t request_nonce[32];
    uint8_t request_identity[32];
    uint8_t signature_verified;
    uint8_t nonce_matched;
    uint8_t merkle_proof_verified;
    uint8_t production_policy_accepted;
} gxos_roughtime_v1_evidence;

// Verify one bounded RFC 10049 v1 response against the exact request packet,
// its expected nonce, and the pinned long-term server public key. The caller
// owns workspace and result storage; they must not overlap each other or the
// packet/key inputs. received_monotonic_ns is supplied by the caller and is
// recorded as evidence metadata only. This function never reads or sets a
// clock. Unknown, syntactically valid tags are ignored as RFC 10049 requires.
gxos_roughtime_v1_status gxos_roughtime_v1_verify(
    const uint8_t* response_packet,
    size_t response_packet_len,
    const uint8_t* request_packet,
    size_t request_packet_len,
    const uint8_t expected_nonce[32],
    const uint8_t server_public_key[32],
    uint64_t sent_monotonic_ns,
    uint64_t received_monotonic_ns,
    gxos_roughtime_v1_workspace* workspace,
    gxos_roughtime_v1_evidence* result);

#ifdef __cplusplus
}
#endif
