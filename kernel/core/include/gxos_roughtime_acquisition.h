#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gxos_roughtime_v1.h"

#define GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES 1036U
#define GXOS_ROUGHTIME_V1_MAX_UDP_RESPONSE_BYTES 1024U
#define GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS 300U

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gxos_roughtime_acquisition_status {
    GXOS_ROUGHTIME_ACQUISITION_SUCCESS = 0,
    GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT = 1,
    GXOS_ROUGHTIME_ACQUISITION_RANDOM_FAILURE = 2,
    GXOS_ROUGHTIME_ACQUISITION_NETWORK_UNAVAILABLE = 3,
    GXOS_ROUGHTIME_ACQUISITION_SOCKET_FAILURE = 4,
    GXOS_ROUGHTIME_ACQUISITION_SEND_FAILURE = 5,
    GXOS_ROUGHTIME_ACQUISITION_TIMEOUT = 6,
    GXOS_ROUGHTIME_ACQUISITION_UNEXPECTED_SOURCE = 7,
    GXOS_ROUGHTIME_ACQUISITION_RESPONSE_TOO_LARGE = 8,
    GXOS_ROUGHTIME_ACQUISITION_VERIFIER_REJECTED = 9,
    GXOS_ROUGHTIME_ACQUISITION_CLOCK_EVIDENCE_REJECTED = 10,
    GXOS_ROUGHTIME_ACQUISITION_CRYPTO_FAILURE = 11
} gxos_roughtime_acquisition_status;

typedef int (*gxos_roughtime_random_fill_fn)(void* context,
                                             uint8_t* output,
                                             size_t output_length);

typedef struct gxos_roughtime_v1_acquisition_result {
    gxos_roughtime_acquisition_status status;
    gxos_roughtime_v1_evidence evidence;
    uint32_t server_ipv4;
    uint16_t server_port;
    uint16_t request_bytes;
    uint16_t response_bytes;
    uint8_t clock_evidence_contract_valid;
    uint64_t evidence_expires_monotonic_ns;
} gxos_roughtime_v1_acquisition_result;

// Build a complete RFC 10049 v1 UDP request. The message body is 1024 bytes;
// the 12-byte Roughtime packet wrapper makes the UDP payload 1036 bytes.
gxos_roughtime_acquisition_status gxos_roughtime_v1_build_request(
    const uint8_t nonce[32],
    const uint8_t server_public_key[32],
    uint8_t* request_packet,
    size_t request_packet_capacity,
    size_t* request_packet_length);

// Obtain the nonce from the supplied secure-random provider. A failure clears
// outputs and returns without producing a request.
gxos_roughtime_acquisition_status gxos_roughtime_v1_build_request_with_random(
    gxos_roughtime_random_fill_fn random_fill,
    void* random_context,
    const uint8_t server_public_key[32],
    uint8_t nonce_out[32],
    uint8_t* request_packet,
    size_t request_packet_capacity,
    size_t* request_packet_length);

// Reject a mismatched datagram source before passing the complete packet to
// the NAVHTTPS12 verifier. Evidence is committed only on full verification.
gxos_roughtime_acquisition_status gxos_roughtime_v1_dispatch_response(
    const uint8_t* response_packet,
    size_t response_packet_length,
    uint32_t actual_source_ipv4,
    uint16_t actual_source_port,
    uint32_t expected_source_ipv4,
    uint16_t expected_source_port,
    const uint8_t* request_packet,
    size_t request_packet_length,
    const uint8_t expected_nonce[32],
    const uint8_t server_public_key[32],
    uint64_t sent_monotonic_ns,
    uint64_t received_monotonic_ns,
    gxos_roughtime_v1_workspace* workspace,
    gxos_roughtime_v1_evidence* result);

const char* gxos_roughtime_acquisition_status_name(
    gxos_roughtime_acquisition_status status);

#ifdef __cplusplus
}

namespace kernel {
namespace roughtime {

gxos_roughtime_acquisition_status acquire_ipv4(
    uint32_t server_ipv4,
    uint16_t server_port,
    const uint8_t server_public_key[32],
    gxos_roughtime_v1_acquisition_result* result);

} // namespace roughtime
} // namespace kernel
#endif
