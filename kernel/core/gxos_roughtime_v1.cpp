#include "gxos_roughtime_v1.h"

#include "gxos_ed25519.h"
#include "../../third_party/libsodium/src/include/sodium/crypto_hash_sha512.h"

namespace {

static const uint8_t kPacketMagic[8] = { 'R', 'O', 'U', 'G', 'H', 'T', 'I', 'M' };
static const uint8_t kResponseContext[] = "Roughtime v1 response signature";
static const uint8_t kDelegationContext[] = "Roughtime v1 delegation signature";
static const uint64_t kNanosecondsPerSecond = 1000000000ULL;
static const uint64_t kMaximumTransportDelayNs = 2000000000ULL;

#define GXRT_TAG4(a, b, c, d) \
    (static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | \
     (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24))
#define GXRT_TAG3(a, b, c) GXRT_TAG4(a, b, c, 0)

static const uint32_t kTagCert = GXRT_TAG4('C', 'E', 'R', 'T');
static const uint32_t kTagDele = GXRT_TAG4('D', 'E', 'L', 'E');
static const uint32_t kTagIndx = GXRT_TAG4('I', 'N', 'D', 'X');
static const uint32_t kTagMaxT = GXRT_TAG4('M', 'A', 'X', 'T');
static const uint32_t kTagMinT = GXRT_TAG4('M', 'I', 'N', 'T');
static const uint32_t kTagMidp = GXRT_TAG4('M', 'I', 'D', 'P');
static const uint32_t kTagNonc = GXRT_TAG4('N', 'O', 'N', 'C');
static const uint32_t kTagPath = GXRT_TAG4('P', 'A', 'T', 'H');
static const uint32_t kTagPubk = GXRT_TAG4('P', 'U', 'B', 'K');
static const uint32_t kTagRadi = GXRT_TAG4('R', 'A', 'D', 'I');
static const uint32_t kTagRoot = GXRT_TAG4('R', 'O', 'O', 'T');
static const uint32_t kTagSrv = GXRT_TAG3('S', 'R', 'V');
static const uint32_t kTagSig = GXRT_TAG3('S', 'I', 'G');
static const uint32_t kTagSrep = GXRT_TAG4('S', 'R', 'E', 'P');
static const uint32_t kTagType = GXRT_TAG4('T', 'Y', 'P', 'E');
static const uint32_t kTagVer = GXRT_TAG3('V', 'E', 'R');
static const uint32_t kTagVers = GXRT_TAG4('V', 'E', 'R', 'S');
static const uint32_t kTagZzzz = GXRT_TAG4('Z', 'Z', 'Z', 'Z');

struct MapView {
    const uint8_t* bytes;
    size_t length;
    size_t values_offset;
    size_t values_length;
    uint32_t count;
};

struct FieldView {
    const uint8_t* bytes;
    size_t length;
};

static void clear_bytes(uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static void copy_bytes(uint8_t* destination, const uint8_t* source, size_t length)
{
    for (size_t i = 0; i < length; ++i) destination[i] = source[i];
}

static uint32_t read_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

static uint64_t read_u64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(read_u32(bytes)) |
           (static_cast<uint64_t>(read_u32(bytes + 4)) << 32);
}

static bool valid_tag_encoding(uint32_t tag)
{
    bool saw_padding = false;
    uint32_t letters = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint8_t value = static_cast<uint8_t>(tag >> (8U * i));
        if (value == 0) {
            saw_padding = true;
        } else {
            if (saw_padding || value < static_cast<uint8_t>('A') || value > static_cast<uint8_t>('Z')) {
                return false;
            }
            ++letters;
        }
    }
    return letters >= 1 && letters <= 4;
}

static bool spend_operation(uint32_t* operations, uint32_t amount = 1)
{
    if (*operations > GXOS_ROUGHTIME_V1_MAX_PARSE_OPERATIONS - amount) return false;
    *operations += amount;
    return true;
}

static gxos_roughtime_v1_status parse_map(const uint8_t* bytes,
                                          size_t length,
                                          uint32_t* operations,
                                          MapView* map)
{
    if (!bytes || !map || length < 4) return GXOS_ROUGHTIME_V1_INVALID_MAP;
    const uint32_t count = read_u32(bytes);
    if (count == 0 || count > GXOS_ROUGHTIME_V1_MAX_MAP_PAIRS) {
        return GXOS_ROUGHTIME_V1_INVALID_MAP;
    }

    const size_t offset_bytes = 4U * static_cast<size_t>(count - 1U);
    const size_t tag_bytes = 4U * static_cast<size_t>(count);
    if (offset_bytes > length - 4U || tag_bytes > length - 4U - offset_bytes) {
        return GXOS_ROUGHTIME_V1_INVALID_MAP;
    }
    const size_t values_offset = 4U + offset_bytes + tag_bytes;
    const size_t values_length = length - values_offset;

    uint32_t previous_tag = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!spend_operation(operations)) return GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
        const size_t tag_offset = 4U + offset_bytes + 4U * static_cast<size_t>(i);
        const uint32_t tag = read_u32(bytes + tag_offset);
        if (!valid_tag_encoding(tag) || (i != 0 && tag <= previous_tag)) {
            return GXOS_ROUGHTIME_V1_INVALID_MAP;
        }
        previous_tag = tag;
        if (i != 0) {
            const uint32_t value_offset = read_u32(bytes + 4U + 4U * static_cast<size_t>(i - 1U));
            if ((value_offset & 3U) != 0 || value_offset > values_length) {
                return GXOS_ROUGHTIME_V1_INVALID_MAP;
            }
            if (i > 1) {
                const uint32_t previous_offset = read_u32(bytes + 4U + 4U * static_cast<size_t>(i - 2U));
                if (value_offset < previous_offset) return GXOS_ROUGHTIME_V1_INVALID_MAP;
            }
        }
    }

    map->bytes = bytes;
    map->length = length;
    map->values_offset = values_offset;
    map->values_length = values_length;
    map->count = count;
    return GXOS_ROUGHTIME_V1_VERIFIED;
}

static bool map_find(const MapView* map,
                     uint32_t tag,
                     uint32_t* operations,
                     FieldView* field)
{
    const size_t offset_bytes = 4U * static_cast<size_t>(map->count - 1U);
    const size_t tags_offset = 4U + offset_bytes;
    for (uint32_t i = 0; i < map->count; ++i) {
        if (!spend_operation(operations)) return false;
        const uint32_t found_tag = read_u32(map->bytes + tags_offset + 4U * static_cast<size_t>(i));
        if (found_tag != tag) continue;
        const size_t begin = i == 0
            ? 0
            : static_cast<size_t>(read_u32(map->bytes + 4U + 4U * static_cast<size_t>(i - 1U)));
        const size_t end = i + 1U == map->count
            ? map->values_length
            : static_cast<size_t>(read_u32(map->bytes + 4U + 4U * static_cast<size_t>(i)));
        if (end < begin || end > map->values_length) return false;
        field->bytes = map->bytes + map->values_offset + begin;
        field->length = end - begin;
        return true;
    }
    return false;
}

static gxos_roughtime_v1_status require_field(const MapView* map,
                                               uint32_t tag,
                                               uint32_t* operations,
                                               FieldView* field)
{
    return map_find(map, tag, operations, field)
        ? GXOS_ROUGHTIME_V1_VERIFIED
        : GXOS_ROUGHTIME_V1_MISSING_FIELD;
}

static gxos_roughtime_v1_status unpack_u32(const FieldView* field, uint32_t* value)
{
    if (field->length != 4) return GXOS_ROUGHTIME_V1_INVALID_MAP;
    *value = read_u32(field->bytes);
    return GXOS_ROUGHTIME_V1_VERIFIED;
}

static gxos_roughtime_v1_status unpack_u64(const FieldView* field, uint64_t* value)
{
    if (field->length != 8) return GXOS_ROUGHTIME_V1_INVALID_MAP;
    *value = read_u64(field->bytes);
    return GXOS_ROUGHTIME_V1_VERIFIED;
}

static gxos_roughtime_v1_status check_version_list(const FieldView* field,
                                                    uint32_t required_version,
                                                    bool must_be_single_version,
                                                    uint32_t* operations)
{
    if (field->length == 0 || (field->length & 3U) != 0 || field->length > 128U) {
        return GXOS_ROUGHTIME_V1_INVALID_MAP;
    }
    const size_t count = field->length / 4U;
    if (count > 32U || (must_be_single_version && count != 1U)) {
        return GXOS_ROUGHTIME_V1_INVALID_MAP;
    }
    uint32_t previous = 0;
    bool found_required = false;
    for (size_t i = 0; i < count; ++i) {
        if (!spend_operation(operations)) return GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
        const uint32_t version = read_u32(field->bytes + 4U * i);
        if (i != 0 && version <= previous) return GXOS_ROUGHTIME_V1_INVALID_MAP;
        previous = version;
        if (version == required_version) found_required = true;
    }
    return found_required ? GXOS_ROUGHTIME_V1_VERIFIED : GXOS_ROUGHTIME_V1_UNSUPPORTED_VERSION;
}

static bool constant_equal(const uint8_t* left, const uint8_t* right, size_t length)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= static_cast<uint8_t>(left[i] ^ right[i]);
    return difference == 0;
}

static gxos_roughtime_v1_status hash512(const uint8_t* input,
                                        size_t input_length,
                                        gxos_roughtime_v1_workspace* workspace)
{
    if (crypto_hash_sha512(workspace->digest, input,
                           static_cast<unsigned long long>(input_length)) != 0) {
        return GXOS_ROUGHTIME_V1_HASH_FAILURE;
    }
    return GXOS_ROUGHTIME_V1_VERIFIED;
}

static gxos_roughtime_v1_status verify_signature(const uint8_t* public_key,
                                                  const uint8_t* signature,
                                                  const uint8_t* context,
                                                  size_t context_length,
                                                  const uint8_t* message,
                                                  size_t message_length,
                                                  gxos_roughtime_v1_workspace* workspace)
{
    if (context_length > sizeof(workspace->transcript) ||
        message_length > sizeof(workspace->transcript) - context_length ||
        context_length + message_length > GXOS_ED25519_MAX_MESSAGE_BYTES) {
        return GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
    }
    copy_bytes(workspace->transcript, context, context_length);
    copy_bytes(workspace->transcript + context_length, message, message_length);
    return gxos_ed25519_verify_strict(
        public_key, GXOS_ED25519_PUBLIC_KEY_BYTES,
        signature, GXOS_ED25519_SIGNATURE_BYTES,
        workspace->transcript, context_length + message_length) == 1
        ? GXOS_ROUGHTIME_V1_VERIFIED
        : GXOS_ROUGHTIME_V1_INVALID_SIGNATURE;
}

static gxos_roughtime_v1_status unwrap_packet(const uint8_t* packet,
                                               size_t packet_length,
                                               size_t maximum_packet_length,
                                               const uint8_t** message,
                                               size_t* message_length)
{
    if (!packet || packet_length < 12U) return GXOS_ROUGHTIME_V1_INVALID_FRAMING;
    if (packet_length > maximum_packet_length) return GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
    if (!constant_equal(packet, kPacketMagic, sizeof(kPacketMagic))) {
        return GXOS_ROUGHTIME_V1_INVALID_FRAMING;
    }
    const uint32_t encoded_length = read_u32(packet + 8);
    if (static_cast<size_t>(encoded_length) != packet_length - 12U) {
        return GXOS_ROUGHTIME_V1_INVALID_FRAMING;
    }
    *message = packet + 12;
    *message_length = packet_length - 12U;
    return GXOS_ROUGHTIME_V1_VERIFIED;
}

static gxos_roughtime_v1_status validate_request(
    const uint8_t* request_packet,
    size_t request_packet_len,
    const uint8_t* expected_nonce,
    const uint8_t* server_public_key,
    uint32_t* operations,
    gxos_roughtime_v1_workspace* workspace,
    uint8_t server_identity[32])
{
    const uint8_t* request_message = 0;
    size_t request_message_length = 0;
    gxos_roughtime_v1_status status = unwrap_packet(
        request_packet, request_packet_len, GXOS_ROUGHTIME_V1_MAX_REQUEST_PACKET_BYTES,
        &request_message, &request_message_length);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    MapView request;
    status = parse_map(request_message, request_message_length, operations, &request);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;

    FieldView ver, nonce, type, server_id, padding;
    if ((status = require_field(&request, kTagVer, operations, &ver)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&request, kTagNonc, operations, &nonce)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&request, kTagType, operations, &type)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&request, kTagSrv, operations, &server_id)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&request, kTagZzzz, operations, &padding)) != GXOS_ROUGHTIME_V1_VERIFIED) {
        return status;
    }
    if (nonce.length != 32 || !constant_equal(nonce.bytes, expected_nonce, 32) ||
        type.length != 4 || read_u32(type.bytes) != 0 || server_id.length != 32) {
        return GXOS_ROUGHTIME_V1_INVALID_REQUEST;
    }
    for (size_t i = 0; i < padding.length; ++i) {
        if (padding.bytes[i] != 0) return GXOS_ROUGHTIME_V1_INVALID_REQUEST;
    }
    status = check_version_list(&ver, 1, false, operations);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;

    workspace->transcript[0] = 0xff;
    copy_bytes(workspace->transcript + 1, server_public_key, 32);
    status = hash512(workspace->transcript, 33, workspace);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    copy_bytes(server_identity, workspace->digest, 32);
    if (!constant_equal(server_id.bytes, server_identity, 32)) return GXOS_ROUGHTIME_V1_KEY_ID_MISMATCH;
    return GXOS_ROUGHTIME_V1_VERIFIED;
}

static gxos_roughtime_v1_status verify_response_inner(
    const uint8_t* response_packet,
    size_t response_packet_len,
    const uint8_t* request_packet,
    size_t request_packet_len,
    const uint8_t* expected_nonce,
    const uint8_t* server_public_key,
    uint64_t sent_monotonic_ns,
    uint64_t received_monotonic_ns,
    uint32_t* operations,
    gxos_roughtime_v1_workspace* workspace,
    gxos_roughtime_v1_evidence* evidence)
{
    if (response_packet_len > request_packet_len) return GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
    if (received_monotonic_ns < sent_monotonic_ns ||
        received_monotonic_ns - sent_monotonic_ns > kMaximumTransportDelayNs) {
        return GXOS_ROUGHTIME_V1_INVALID_TIME;
    }

    gxos_roughtime_v1_status status = validate_request(
        request_packet, request_packet_len, expected_nonce, server_public_key,
        operations, workspace, evidence->server_key_identity);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;

    const uint8_t* response_message = 0;
    size_t response_message_length = 0;
    status = unwrap_packet(response_packet, response_packet_len,
                           GXOS_ROUGHTIME_V1_MAX_RESPONSE_PACKET_BYTES,
                           &response_message, &response_message_length);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    MapView top;
    status = parse_map(response_message, response_message_length, operations, &top);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;

    FieldView response_sig, nonce, type, path, srep_bytes, cert_bytes, index_bytes;
    if ((status = require_field(&top, kTagSig, operations, &response_sig)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&top, kTagNonc, operations, &nonce)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&top, kTagType, operations, &type)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&top, kTagPath, operations, &path)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&top, kTagSrep, operations, &srep_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&top, kTagCert, operations, &cert_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&top, kTagIndx, operations, &index_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED) {
        return status;
    }
    if (response_sig.length != 64 || nonce.length != 32 || type.length != 4 || index_bytes.length != 4) {
        return GXOS_ROUGHTIME_V1_INVALID_MAP;
    }
    if (read_u32(type.bytes) != 1) return GXOS_ROUGHTIME_V1_INVALID_REQUEST;
    if (!constant_equal(nonce.bytes, expected_nonce, 32)) return GXOS_ROUGHTIME_V1_NONCE_MISMATCH;
    if ((path.length % 32U) != 0 || path.length / 32U > GXOS_ROUGHTIME_V1_MAX_PATH_HASHES) {
        return GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
    }
    const uint32_t merkle_index = read_u32(index_bytes.bytes);

    MapView cert;
    status = parse_map(cert_bytes.bytes, cert_bytes.length, operations, &cert);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    FieldView cert_sig, delegation_bytes;
    if ((status = require_field(&cert, kTagSig, operations, &cert_sig)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&cert, kTagDele, operations, &delegation_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED) {
        return status;
    }
    if (cert_sig.length != 64) return GXOS_ROUGHTIME_V1_INVALID_MAP;
    status = verify_signature(server_public_key, cert_sig.bytes,
                              kDelegationContext, sizeof(kDelegationContext),
                              delegation_bytes.bytes, delegation_bytes.length, workspace);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;

    MapView delegation;
    status = parse_map(delegation_bytes.bytes, delegation_bytes.length, operations, &delegation);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    FieldView delegated_key, minimum_time_bytes, maximum_time_bytes;
    if ((status = require_field(&delegation, kTagPubk, operations, &delegated_key)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&delegation, kTagMinT, operations, &minimum_time_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&delegation, kTagMaxT, operations, &maximum_time_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED) {
        return status;
    }
    if (delegated_key.length != 32 || minimum_time_bytes.length != 8 || maximum_time_bytes.length != 8) {
        return GXOS_ROUGHTIME_V1_INVALID_DELEGATION;
    }
    const uint64_t minimum_time = read_u64(minimum_time_bytes.bytes);
    const uint64_t maximum_time = read_u64(maximum_time_bytes.bytes);
    if (minimum_time > maximum_time) return GXOS_ROUGHTIME_V1_INVALID_DELEGATION;

    MapView srep;
    status = parse_map(srep_bytes.bytes, srep_bytes.length, operations, &srep);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    FieldView version_bytes, radius_bytes, midpoint_bytes, versions, root;
    if ((status = require_field(&srep, kTagVer, operations, &version_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&srep, kTagRadi, operations, &radius_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&srep, kTagMidp, operations, &midpoint_bytes)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&srep, kTagVers, operations, &versions)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = require_field(&srep, kTagRoot, operations, &root)) != GXOS_ROUGHTIME_V1_VERIFIED) {
        return status;
    }
    uint32_t protocol_version = 0;
    uint32_t radius = 0;
    uint64_t midpoint = 0;
    if ((status = unpack_u32(&version_bytes, &protocol_version)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = unpack_u32(&radius_bytes, &radius)) != GXOS_ROUGHTIME_V1_VERIFIED ||
        (status = unpack_u64(&midpoint_bytes, &midpoint)) != GXOS_ROUGHTIME_V1_VERIFIED) {
        return status;
    }
    if (protocol_version != 1) return GXOS_ROUGHTIME_V1_UNSUPPORTED_VERSION;
    status = check_version_list(&versions, protocol_version, false, operations);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    if (root.length != 32) return GXOS_ROUGHTIME_V1_INVALID_MAP;
    if (radius == 0) return GXOS_ROUGHTIME_V1_INVALID_TIME;
    if (radius > GXOS_ROUGHTIME_V1_MAX_RADIUS_SECONDS) {
        return GXOS_ROUGHTIME_V1_EXCESSIVE_UNCERTAINTY;
    }
    if (midpoint < radius || midpoint > UINT64_MAX - radius) return GXOS_ROUGHTIME_V1_INVALID_TIME;
    const uint64_t server_earliest = midpoint - radius;
    const uint64_t server_latest = midpoint + radius;
    if (server_earliest < GXOS_ROUGHTIME_V1_TIME_MIN_UNIX_SECONDS ||
        server_latest >= GXOS_ROUGHTIME_V1_TIME_MAX_UNIX_SECONDS ||
        midpoint < minimum_time || midpoint > maximum_time) {
        return midpoint < minimum_time || midpoint > maximum_time
            ? GXOS_ROUGHTIME_V1_INVALID_DELEGATION
            : GXOS_ROUGHTIME_V1_INVALID_TIME;
    }

    status = verify_signature(delegated_key.bytes, response_sig.bytes,
                              kResponseContext, sizeof(kResponseContext),
                              srep_bytes.bytes, srep_bytes.length, workspace);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;

    workspace->transcript[0] = 0;
    copy_bytes(workspace->transcript + 1, request_packet, request_packet_len);
    status = hash512(workspace->transcript, request_packet_len + 1U, workspace);
    if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
    copy_bytes(workspace->merkle_current, workspace->digest, 32);
    copy_bytes(evidence->request_identity, workspace->digest, 32);
    const size_t path_hash_count = path.length / 32U;
    if (path_hash_count < 32U && (merkle_index >> path_hash_count) != 0) {
        return GXOS_ROUGHTIME_V1_INVALID_MERKLE_PROOF;
    }
    for (size_t level = 0; level < path_hash_count; ++level) {
        if (!spend_operation(operations)) return GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
        const uint8_t* sibling = path.bytes + 32U * level;
        workspace->transcript[0] = 1;
        if (((merkle_index >> level) & 1U) != 0) {
            copy_bytes(workspace->transcript + 1, sibling, 32);
            copy_bytes(workspace->transcript + 33, workspace->merkle_current, 32);
        } else {
            copy_bytes(workspace->transcript + 1, workspace->merkle_current, 32);
            copy_bytes(workspace->transcript + 33, sibling, 32);
        }
        status = hash512(workspace->transcript, 65, workspace);
        if (status != GXOS_ROUGHTIME_V1_VERIFIED) return status;
        copy_bytes(workspace->merkle_next, workspace->digest, 32);
        copy_bytes(workspace->merkle_current, workspace->merkle_next, 32);
    }
    if (!constant_equal(workspace->merkle_current, root.bytes, 32)) {
        return GXOS_ROUGHTIME_V1_INVALID_MERKLE_PROOF;
    }

    const uint64_t transport_delay_ns = received_monotonic_ns - sent_monotonic_ns;
    const uint64_t transport_slack_seconds =
        (transport_delay_ns + kNanosecondsPerSecond - 1U) / kNanosecondsPerSecond;
    if (server_latest > UINT64_MAX - transport_slack_seconds) return GXOS_ROUGHTIME_V1_INVALID_TIME;
    const uint64_t latest = server_latest + transport_slack_seconds;
    if (server_earliest < GXOS_ROUGHTIME_V1_TIME_MIN_UNIX_SECONDS ||
        latest >= GXOS_ROUGHTIME_V1_TIME_MAX_UNIX_SECONDS) {
        return GXOS_ROUGHTIME_V1_INVALID_TIME;
    }
    const uint64_t interval_width = latest - server_earliest;

    evidence->protocol_version = protocol_version;
    evidence->radius_seconds = radius;
    evidence->server_midpoint_unix_seconds = midpoint;
    evidence->server_earliest_unix_seconds = server_earliest;
    evidence->server_latest_unix_seconds = server_latest;
    evidence->estimated_midpoint_unix_seconds = server_earliest + interval_width / 2U;
    evidence->earliest_unix_seconds = server_earliest;
    evidence->latest_unix_seconds = latest;
    evidence->estimated_uncertainty_seconds = (interval_width + 1U) / 2U;
    evidence->sent_monotonic_ns = sent_monotonic_ns;
    evidence->received_monotonic_ns = received_monotonic_ns;
    evidence->measured_transport_delay_ns = transport_delay_ns;
    copy_bytes(evidence->request_nonce, expected_nonce, 32);
    evidence->signature_verified = 1;
    evidence->nonce_matched = 1;
    evidence->merkle_proof_verified = 1;
    evidence->production_policy_accepted = 0;
    return GXOS_ROUGHTIME_V1_VERIFIED;
}

} // namespace

extern "C" gxos_roughtime_v1_status gxos_roughtime_v1_verify(
    const uint8_t* response_packet,
    size_t response_packet_len,
    const uint8_t* request_packet,
    size_t request_packet_len,
    const uint8_t expected_nonce[32],
    const uint8_t server_public_key[32],
    uint64_t sent_monotonic_ns,
    uint64_t received_monotonic_ns,
    gxos_roughtime_v1_workspace* workspace,
    gxos_roughtime_v1_evidence* result)
{
    if (!result) return GXOS_ROUGHTIME_V1_INVALID_ARGUMENT;
    clear_bytes(reinterpret_cast<uint8_t*>(result), sizeof(*result));
    result->status = GXOS_ROUGHTIME_V1_INVALID_ARGUMENT;
    if (!response_packet || !request_packet || !expected_nonce || !server_public_key) {
        return result->status;
    }
    if (!workspace) {
        result->status = GXOS_ROUGHTIME_V1_BUFFER_LIMIT;
        return result->status;
    }

    gxos_roughtime_v1_evidence staged;
    clear_bytes(reinterpret_cast<uint8_t*>(&staged), sizeof(staged));
    uint32_t operations = 0;
    const gxos_roughtime_v1_status status = verify_response_inner(
        response_packet, response_packet_len, request_packet, request_packet_len,
        expected_nonce, server_public_key, sent_monotonic_ns, received_monotonic_ns,
        &operations, workspace, &staged);

    clear_bytes(reinterpret_cast<uint8_t*>(workspace), sizeof(*workspace));
    if (status == GXOS_ROUGHTIME_V1_VERIFIED) {
        staged.status = status;
        *result = staged;
    } else {
        result->status = status;
    }
    return status;
}
