#include "include/gxos_roughtime_acquisition.h"

#include "../../third_party/libsodium/src/include/sodium/crypto_hash_sha512.h"

namespace {

static const uint8_t kPacketMagic[8] = { 'R', 'O', 'U', 'G', 'H', 'T', 'I', 'M' };
static const size_t kRoughtimeMessageBytes = 1024U;
static const size_t kPacketHeaderBytes = 12U;
static const size_t kRequestHeaderBytes = 40U;

static void clear_bytes(uint8_t* output, size_t length)
{
    if (!output) return;
    for (size_t i = 0; i < length; ++i) output[i] = 0;
}

static void copy_bytes(uint8_t* output, const uint8_t* input, size_t length)
{
    for (size_t i = 0; i < length; ++i) output[i] = input[i];
}

static void write_u32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

} // namespace

extern "C" gxos_roughtime_acquisition_status gxos_roughtime_v1_build_request(
    const uint8_t nonce[32],
    const uint8_t server_public_key[32],
    uint8_t* request_packet,
    size_t request_packet_capacity,
    size_t* request_packet_length)
{
    if (request_packet_length) *request_packet_length = 0;
    if (!nonce || !server_public_key || !request_packet || !request_packet_length) {
        return GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
    }
    if (request_packet_capacity < GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES) {
        return GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
    }

    // RFC 10049 sorts tags by their little-endian uint32 value:
    // VER, SRV, NONC, TYPE, ZZZZ. Four offsets and five tags use 36 bytes;
    // the message header is 40 bytes, leaving 984 bytes for values.
    static const uint32_t kOffsets[4] = { 4U, 36U, 68U, 72U };
    static const uint8_t kTags[20] = {
        'V', 'E', 'R', 0,
        'S', 'R', 'V', 0,
        'N', 'O', 'N', 'C',
        'T', 'Y', 'P', 'E',
        'Z', 'Z', 'Z', 'Z'
    };
    uint8_t identity_input[33];
    uint8_t digest[64];
    identity_input[0] = 0xff;
    copy_bytes(identity_input + 1, server_public_key, 32);
    if (crypto_hash_sha512(digest, identity_input, sizeof(identity_input)) != 0) {
        clear_bytes(identity_input, sizeof(identity_input));
        clear_bytes(digest, sizeof(digest));
        return GXOS_ROUGHTIME_ACQUISITION_CRYPTO_FAILURE;
    }

    clear_bytes(request_packet, GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES);
    copy_bytes(request_packet, kPacketMagic, sizeof(kPacketMagic));
    write_u32(request_packet + 8, static_cast<uint32_t>(kRoughtimeMessageBytes));

    uint8_t* message = request_packet + kPacketHeaderBytes;
    write_u32(message, 5U);
    for (size_t i = 0; i < 4; ++i) write_u32(message + 4U + 4U * i, kOffsets[i]);
    copy_bytes(message + 20U, kTags, sizeof(kTags));

    uint8_t* values = message + kRequestHeaderBytes;
    write_u32(values, 1U);
    copy_bytes(values + 4U, digest, 32U);
    copy_bytes(values + 36U, nonce, 32U);
    write_u32(values + 68U, 0U);
    // ZZZZ is already all-zero and fills the message to the RFC-recommended
    // minimum of 1024 bytes. Its value has a four-byte aligned length.

    clear_bytes(identity_input, sizeof(identity_input));
    clear_bytes(digest, sizeof(digest));
    *request_packet_length = GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES;
    return GXOS_ROUGHTIME_ACQUISITION_SUCCESS;
}

extern "C" gxos_roughtime_acquisition_status gxos_roughtime_v1_build_request_with_random(
    gxos_roughtime_random_fill_fn random_fill,
    void* random_context,
    const uint8_t server_public_key[32],
    uint8_t nonce_out[32],
    uint8_t* request_packet,
    size_t request_packet_capacity,
    size_t* request_packet_length)
{
    if (request_packet_length) *request_packet_length = 0;
    if (nonce_out) clear_bytes(nonce_out, 32U);
    if (request_packet && request_packet_capacity >= GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES) {
        clear_bytes(request_packet, GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES);
    }
    if (!random_fill || !server_public_key || !nonce_out || !request_packet ||
        !request_packet_length || request_packet_capacity < GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES) {
        return GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
    }
    if (random_fill(random_context, nonce_out, 32U) != 1) {
        clear_bytes(nonce_out, 32U);
        clear_bytes(request_packet, GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES);
        return GXOS_ROUGHTIME_ACQUISITION_RANDOM_FAILURE;
    }
    if (gxos_roughtime_v1_build_request(nonce_out, server_public_key,
            request_packet, request_packet_capacity, request_packet_length) !=
        GXOS_ROUGHTIME_ACQUISITION_SUCCESS) {
        clear_bytes(nonce_out, 32U);
        clear_bytes(request_packet, GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES);
        *request_packet_length = 0;
        return GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
    }
    return GXOS_ROUGHTIME_ACQUISITION_SUCCESS;
}

extern "C" gxos_roughtime_acquisition_status gxos_roughtime_v1_dispatch_response(
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
    gxos_roughtime_v1_evidence* result)
{
    if (!result) return GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
    clear_bytes(reinterpret_cast<uint8_t*>(result), sizeof(*result));
    result->status = GXOS_ROUGHTIME_V1_INVALID_ARGUMENT;

    if (!response_packet || !request_packet || !expected_nonce || !server_public_key ||
        !workspace || request_packet_length > GXOS_ROUGHTIME_V1_MAX_REQUEST_PACKET_BYTES) {
        return GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
    }
    if (actual_source_ipv4 != expected_source_ipv4 || actual_source_port != expected_source_port) {
        return GXOS_ROUGHTIME_ACQUISITION_UNEXPECTED_SOURCE;
    }
    if (response_packet_length > GXOS_ROUGHTIME_V1_MAX_UDP_RESPONSE_BYTES) {
        return GXOS_ROUGHTIME_ACQUISITION_RESPONSE_TOO_LARGE;
    }

    const gxos_roughtime_v1_status verify_status = gxos_roughtime_v1_verify(
        response_packet, response_packet_length,
        request_packet, request_packet_length,
        expected_nonce, server_public_key,
        sent_monotonic_ns, received_monotonic_ns,
        workspace, result);
    return verify_status == GXOS_ROUGHTIME_V1_VERIFIED
        ? GXOS_ROUGHTIME_ACQUISITION_SUCCESS
        : GXOS_ROUGHTIME_ACQUISITION_VERIFIER_REJECTED;
}

extern "C" const char* gxos_roughtime_acquisition_status_name(
    gxos_roughtime_acquisition_status status)
{
    switch (status) {
    case GXOS_ROUGHTIME_ACQUISITION_SUCCESS: return "success";
    case GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT: return "invalid-argument";
    case GXOS_ROUGHTIME_ACQUISITION_RANDOM_FAILURE: return "secure-random-failure";
    case GXOS_ROUGHTIME_ACQUISITION_NETWORK_UNAVAILABLE: return "network-unavailable";
    case GXOS_ROUGHTIME_ACQUISITION_SOCKET_FAILURE: return "socket-failure";
    case GXOS_ROUGHTIME_ACQUISITION_SEND_FAILURE: return "send-failure";
    case GXOS_ROUGHTIME_ACQUISITION_TIMEOUT: return "timeout";
    case GXOS_ROUGHTIME_ACQUISITION_UNEXPECTED_SOURCE: return "unexpected-source";
    case GXOS_ROUGHTIME_ACQUISITION_RESPONSE_TOO_LARGE: return "response-too-large";
    case GXOS_ROUGHTIME_ACQUISITION_VERIFIER_REJECTED: return "verifier-rejected";
    case GXOS_ROUGHTIME_ACQUISITION_CLOCK_EVIDENCE_REJECTED: return "clock-evidence-rejected";
    case GXOS_ROUGHTIME_ACQUISITION_CRYPTO_FAILURE: return "crypto-failure";
    case GXOS_ROUGHTIME_ACQUISITION_MONOTONIC_DISCONTINUITY: return "monotonic-discontinuity";
    default: return "unknown";
    }
}
