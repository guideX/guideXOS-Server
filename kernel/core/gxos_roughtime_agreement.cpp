#include "include/gxos_roughtime_agreement.h"

#include "../../third_party/libsodium/src/include/sodium/crypto_hash_sha512.h"

struct GxosRoughtimeAgreementAccess {
    static uint8_t* storage(gxos_roughtime_verified_evidence* token) { return token->opaque; }
    static const uint8_t* storage(const gxos_roughtime_verified_evidence* token) { return token->opaque; }
};

namespace {

static const uint64_t kNanosecondsPerSecond = 1000000000ULL;
static const uint64_t kNanosecondsPerMillisecond = 1000000ULL;
static const uint64_t kAcquisitionEvidenceLeaseNs =
    static_cast<uint64_t>(GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS) * kNanosecondsPerSecond;
static const uint8_t kTokenMagic[16] = {
    'G','X','O','S','R','T','A','G','R','E','E','1',0,0,0,1
};

struct OperatorMeasurement {
    uint32_t identity;
    uint64_t earliest;
    uint64_t latest;
    uint64_t maximum_age_seconds;
    uint64_t uncertainty_seconds;
    uint8_t present;
    uint8_t usable;
    uint8_t reserved[3];
};

static void clear_bytes(uint8_t* output, size_t length)
{
    if (!output) return;
    for (size_t i = 0; i < length; ++i) output[i] = 0;
}

static void copy_bytes(uint8_t* output, const uint8_t* input, size_t length)
{
    for (size_t i = 0; i < length; ++i) output[i] = input[i];
}

static bool equal_bytes(const uint8_t* left, const uint8_t* right, size_t length)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

static bool all_zero(const uint8_t* bytes, size_t length)
{
    uint8_t value = 0;
    for (size_t i = 0; i < length; ++i) value |= bytes[i];
    return value == 0;
}

static bool hash_key_identity(const uint8_t key[32], uint8_t identity[32])
{
    uint8_t input[33];
    uint8_t digest[64];
    input[0] = 0xff;
    copy_bytes(input + 1, key, 32);
    const int hash_status = crypto_hash_sha512(digest, input, sizeof(input));
    if (hash_status == 0) copy_bytes(identity, digest, 32);
    clear_bytes(input, sizeof(input));
    clear_bytes(digest, sizeof(digest));
    return hash_status == 0;
}

static bool valid_evidence(const gxos_roughtime_v1_evidence& evidence)
{
    if (evidence.status != GXOS_ROUGHTIME_V1_VERIFIED ||
        evidence.protocol_version != 1 || evidence.radius_seconds == 0 ||
        evidence.radius_seconds > GXOS_ROUGHTIME_V1_MAX_RADIUS_SECONDS ||
        evidence.signature_verified != 1 || evidence.nonce_matched != 1 ||
        evidence.merkle_proof_verified != 1 || evidence.production_policy_accepted != 0 ||
        evidence.sent_monotonic_ns > evidence.received_monotonic_ns ||
        evidence.measured_transport_delay_ns !=
            evidence.received_monotonic_ns - evidence.sent_monotonic_ns ||
        evidence.earliest_unix_seconds > evidence.latest_unix_seconds ||
        evidence.server_earliest_unix_seconds > evidence.server_latest_unix_seconds ||
        evidence.earliest_unix_seconds < GXOS_ROUGHTIME_V1_TIME_MIN_UNIX_SECONDS ||
        evidence.latest_unix_seconds >= GXOS_ROUGHTIME_V1_TIME_MAX_UNIX_SECONDS ||
        evidence.estimated_midpoint_unix_seconds < evidence.earliest_unix_seconds ||
        evidence.estimated_midpoint_unix_seconds > evidence.latest_unix_seconds ||
        all_zero(evidence.server_key_identity, 32) ||
        all_zero(evidence.request_identity, 32) ||
        all_zero(evidence.request_nonce, 32)) {
        return false;
    }
    return true;
}

static const size_t kEvidenceOffset = sizeof(kTokenMagic);
static const size_t kExpiryOffset = kEvidenceOffset + sizeof(gxos_roughtime_v1_evidence);
static const size_t kTokenDigestOffset = kExpiryOffset + sizeof(uint64_t);
static const size_t kTokenUsedBytes = kTokenDigestOffset + 32U;
static_assert(kTokenUsedBytes <= GXOS_ROUGHTIME_AGREEMENT_TOKEN_BYTES,
              "opaque verified-evidence token storage is too small");

static void write_u64(uint8_t* output, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) output[i] = static_cast<uint8_t>(value >> (8U * i));
}

static uint64_t read_u64(const uint8_t* input)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(input[i]) << (8U * i);
    return value;
}

static bool valid_expiry(const gxos_roughtime_v1_evidence& evidence,
                         uint64_t expires_monotonic_ns)
{
    return evidence.received_monotonic_ns <= UINT64_MAX - kAcquisitionEvidenceLeaseNs &&
        expires_monotonic_ns == evidence.received_monotonic_ns + kAcquisitionEvidenceLeaseNs;
}

static bool issue_token(const gxos_roughtime_v1_evidence& evidence,
                        uint64_t expires_monotonic_ns,
                        gxos_roughtime_verified_evidence* token)
{
    if (!token || !valid_evidence(evidence) || !valid_expiry(evidence, expires_monotonic_ns))
        return false;
    uint8_t* token_bytes = GxosRoughtimeAgreementAccess::storage(token);
    clear_bytes(token_bytes, GXOS_ROUGHTIME_AGREEMENT_TOKEN_BYTES);
    copy_bytes(token_bytes, kTokenMagic, sizeof(kTokenMagic));
    copy_bytes(token_bytes + kEvidenceOffset,
               reinterpret_cast<const uint8_t*>(&evidence), sizeof(evidence));
    write_u64(token_bytes + kExpiryOffset, expires_monotonic_ns);
    uint8_t digest[64];
    if (crypto_hash_sha512(digest, token_bytes, kTokenDigestOffset) != 0) {
        clear_bytes(token_bytes, GXOS_ROUGHTIME_AGREEMENT_TOKEN_BYTES);
        clear_bytes(digest, sizeof(digest));
        return false;
    }
    copy_bytes(token_bytes + kTokenDigestOffset, digest, 32);
    clear_bytes(digest, sizeof(digest));
    return true;
}

static bool open_token(const gxos_roughtime_verified_evidence& token,
                       gxos_roughtime_v1_evidence* evidence,
                       uint64_t* expires_monotonic_ns)
{
    const uint8_t* token_bytes = GxosRoughtimeAgreementAccess::storage(&token);
    if (!evidence || !expires_monotonic_ns ||
        !equal_bytes(token_bytes, kTokenMagic, sizeof(kTokenMagic))) return false;
    uint8_t digest[64];
    const bool hash_ok = crypto_hash_sha512(digest, token_bytes, kTokenDigestOffset) == 0;
    const bool integrity_ok = hash_ok && equal_bytes(
        digest, token_bytes + kTokenDigestOffset, 32);
    clear_bytes(digest, sizeof(digest));
    if (!integrity_ok) return false;
    copy_bytes(reinterpret_cast<uint8_t*>(evidence), token_bytes + kEvidenceOffset,
               sizeof(*evidence));
    *expires_monotonic_ns = read_u64(token_bytes + kExpiryOffset);
    return valid_evidence(*evidence) && valid_expiry(*evidence, *expires_monotonic_ns);
}

static bool valid_policy(const gxos_roughtime_agreement_policy& policy,
                         uint8_t identities[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES][32],
                         uint8_t* operator_count)
{
    if (policy.policy_identity == 0 ||
        (policy.profile != GXOS_ROUGHTIME_AGREEMENT_PROFILE_FIXTURE_ONLY &&
         policy.profile != GXOS_ROUGHTIME_AGREEMENT_PROFILE_UNREVIEWED_CONFIG) ||
        policy.source_count < 2 ||
        policy.source_count > GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES ||
        policy.maximum_age_seconds == 0 ||
        policy.maximum_age_seconds > GXOS_ROUGHTIME_AGREEMENT_MAX_POLICY_AGE_SECONDS ||
        policy.maximum_source_uncertainty_seconds == 0 ||
        policy.maximum_source_uncertainty_seconds > GXOS_ROUGHTIME_V1_MAX_RADIUS_SECONDS + 2U ||
        policy.maximum_transport_delay_ms > 2000U ||
        policy.maximum_forward_jump_seconds > GXOS_ROUGHTIME_AGREEMENT_MAX_FORWARD_JUMP_SECONDS) {
        return false;
    }

    uint32_t operators[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES] = { 0 };
    uint8_t distinct_operators = 0;
    for (size_t i = 0; i < policy.source_count; ++i) {
        const gxos_roughtime_agreement_source& source = policy.sources[i];
        if (source.operator_identity == 0 || all_zero(source.server_public_key, 32) ||
            !hash_key_identity(source.server_public_key, identities[i])) {
            return false;
        }
        for (size_t previous = 0; previous < i; ++previous) {
            if (equal_bytes(source.server_public_key,
                            policy.sources[previous].server_public_key, 32) ||
                equal_bytes(identities[i], identities[previous], 32)) {
                return false;
            }
        }
        bool operator_seen = false;
        for (size_t previous = 0; previous < distinct_operators; ++previous) {
            if (operators[previous] == source.operator_identity) operator_seen = true;
        }
        if (!operator_seen) operators[distinct_operators++] = source.operator_identity;
    }
    if (policy.quorum < 2 || policy.quorum > distinct_operators) return false;
    *operator_count = distinct_operators;
    return true;
}

static bool same_response(const gxos_roughtime_v1_evidence& left,
                          const gxos_roughtime_v1_evidence& right)
{
    return equal_bytes(left.request_identity, right.request_identity, 32) ||
           equal_bytes(left.request_nonce, right.request_nonce, 32);
}

static uint8_t find_source(const gxos_roughtime_agreement_policy& policy,
                           const uint8_t identities[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES][32],
                           const uint8_t key_identity[32])
{
    for (uint8_t i = 0; i < policy.source_count; ++i) {
        if (equal_bytes(identities[i], key_identity, 32)) return i;
    }
    return 0xffU;
}

static int find_operator(const OperatorMeasurement* operators,
                         uint8_t operator_count,
                         uint32_t identity)
{
    for (uint8_t i = 0; i < operator_count; ++i) {
        if (operators[i].identity == identity) return static_cast<int>(i);
    }
    return -1;
}

static void set_status(gxos_roughtime_agreement_result* result,
                       gxos_roughtime_agreement_status status)
{
    if (result) result->status = status;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t* output)
{
    if (!output || left > UINT64_MAX - right) return false;
    *output = left + right;
    return true;
}

static uint64_t ceil_seconds(uint64_t nanoseconds)
{
    return nanoseconds / kNanosecondsPerSecond +
        ((nanoseconds % kNanosecondsPerSecond) != 0 ? 1U : 0U);
}

static gxos_roughtime_agreement_status fail_continuity(
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_agreement_status failure)
{
    continuity->failed = 1;
    continuity->failure_status = static_cast<uint8_t>(failure);
    return failure;
}

static gxos_roughtime_agreement_status update_continuity(
    const gxos_roughtime_agreement_policy& policy,
    uint64_t now_ns,
    uint64_t earliest,
    uint64_t latest,
    gxos_roughtime_agreement_continuity* continuity,
    uint8_t* checked)
{
    if (!continuity) return GXOS_ROUGHTIME_AGREEMENT_SATISFIED;
    if (continuity->failed) {
        const gxos_roughtime_agreement_status failure =
            continuity->failure_status <= GXOS_ROUGHTIME_AGREEMENT_CONTINUITY_FAILURE
                ? static_cast<gxos_roughtime_agreement_status>(continuity->failure_status)
                : GXOS_ROUGHTIME_AGREEMENT_CONTINUITY_FAILURE;
        return failure;
    }
    if (continuity->initialized > 1) return fail_continuity(
        continuity, GXOS_ROUGHTIME_AGREEMENT_CONTINUITY_FAILURE);
    if (!continuity->initialized) {
        continuity->initialized = 1;
        continuity->evaluated_monotonic_ns = now_ns;
        continuity->earliest_unix_seconds = earliest;
        continuity->latest_unix_seconds = latest;
        *checked = 0;
        return GXOS_ROUGHTIME_AGREEMENT_SATISFIED;
    }

    *checked = 1;
    if (now_ns < continuity->evaluated_monotonic_ns) {
        return fail_continuity(continuity, GXOS_ROUGHTIME_AGREEMENT_CONTINUITY_FAILURE);
    }
    const uint64_t elapsed_ns = now_ns - continuity->evaluated_monotonic_ns;
    const uint64_t elapsed_floor = elapsed_ns / kNanosecondsPerSecond;
    const uint64_t elapsed_ceil = ceil_seconds(elapsed_ns);
    uint64_t expected_earliest = 0;
    uint64_t expected_latest = 0;
    if (!add_u64(continuity->earliest_unix_seconds, elapsed_floor, &expected_earliest) ||
        !add_u64(continuity->latest_unix_seconds, elapsed_ceil, &expected_latest)) {
        return fail_continuity(continuity, GXOS_ROUGHTIME_AGREEMENT_CONTINUITY_FAILURE);
    }
    if (latest < expected_earliest) {
        return fail_continuity(continuity, GXOS_ROUGHTIME_AGREEMENT_CLOCK_ROLLBACK);
    }
    uint64_t forward_limit = 0;
    if (!add_u64(expected_latest, policy.maximum_forward_jump_seconds, &forward_limit)) {
        forward_limit = UINT64_MAX;
    }
    if (earliest > forward_limit) {
        return fail_continuity(continuity, GXOS_ROUGHTIME_AGREEMENT_FORWARD_JUMP);
    }

    continuity->evaluated_monotonic_ns = now_ns;
    continuity->earliest_unix_seconds = earliest;
    continuity->latest_unix_seconds = latest;
    return GXOS_ROUGHTIME_AGREEMENT_SATISFIED;
}

} // namespace

#if defined(GXOS_ROUGHTIME_AGREEMENT_TESTING)
extern "C" gxos_roughtime_agreement_status gxos_roughtime_agreement_issue_evidence(
    const uint8_t* response_packet,
    size_t response_packet_length,
    const uint8_t* request_packet,
    size_t request_packet_length,
    const uint8_t expected_nonce[32],
    const uint8_t server_public_key[32],
    uint64_t sent_monotonic_ns,
    uint64_t received_monotonic_ns,
    gxos_roughtime_v1_workspace* verifier_workspace,
    gxos_roughtime_verified_evidence* result)
{
    if (result) clear_bytes(GxosRoughtimeAgreementAccess::storage(result),
                            GXOS_ROUGHTIME_AGREEMENT_TOKEN_BYTES);
    if (!result || !response_packet || !request_packet || !expected_nonce ||
        !server_public_key || !verifier_workspace || sent_monotonic_ns > received_monotonic_ns) {
        return GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE;
    }
    gxos_roughtime_v1_evidence verified = {};
    const gxos_roughtime_v1_status verify_status = gxos_roughtime_v1_verify(
        response_packet, response_packet_length,
        request_packet, request_packet_length,
        expected_nonce, server_public_key,
        sent_monotonic_ns, received_monotonic_ns,
        verifier_workspace, &verified);
    const bool expiry_fits = verified.received_monotonic_ns <=
        UINT64_MAX - kAcquisitionEvidenceLeaseNs;
    const uint64_t expires_monotonic_ns = expiry_fits
        ? verified.received_monotonic_ns + kAcquisitionEvidenceLeaseNs : 0;
    if (verify_status != GXOS_ROUGHTIME_V1_VERIFIED || !expiry_fits ||
        !issue_token(verified, expires_monotonic_ns, result)) {
        clear_bytes(reinterpret_cast<uint8_t*>(&verified), sizeof(verified));
        return GXOS_ROUGHTIME_AGREEMENT_VERIFIER_REJECTED;
    }
    clear_bytes(reinterpret_cast<uint8_t*>(&verified), sizeof(verified));
    return GXOS_ROUGHTIME_AGREEMENT_SATISFIED;
}
#endif

namespace kernel {
namespace roughtime {
namespace agreement_internal {

bool seal_acquisition_evidence(const gxos_roughtime_v1_evidence& verified,
                               uint8_t* clock_evidence_contract_valid,
                               uint64_t* expires_monotonic_ns,
                               gxos_roughtime_verified_evidence* token)
{
    if (clock_evidence_contract_valid) *clock_evidence_contract_valid = 0;
    if (expires_monotonic_ns) *expires_monotonic_ns = 0;
    if (token) clear_bytes(GxosRoughtimeAgreementAccess::storage(token),
                           GXOS_ROUGHTIME_AGREEMENT_TOKEN_BYTES);
    if (!clock_evidence_contract_valid || !expires_monotonic_ns || !token ||
        !valid_evidence(verified)) {
        return false;
    }

    const gxos::clockassurance::AuthenticatedEvidence clock_evidence = {
        true,
        true,
        verified.signature_verified != 0,
        verified.nonce_matched != 0,
        static_cast<int64_t>(verified.earliest_unix_seconds),
        static_cast<int64_t>(verified.latest_unix_seconds),
        verified.received_monotonic_ns / kNanosecondsPerSecond,
        GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS
    };
    if (gxos::clockassurance::assess_authenticated_evidence(
            clock_evidence, verified.received_monotonic_ns / kNanosecondsPerSecond) !=
        gxos::clockassurance::EvidenceResult::Accepted ||
        verified.received_monotonic_ns > UINT64_MAX - kAcquisitionEvidenceLeaseNs) {
        return false;
    }

    const uint64_t expiry = verified.received_monotonic_ns + kAcquisitionEvidenceLeaseNs;
    if (!issue_token(verified, expiry, token)) return false;
    *clock_evidence_contract_valid = 1;
    *expires_monotonic_ns = expiry;
    return true;
}

} // namespace agreement_internal
} // namespace roughtime
} // namespace kernel

#if defined(GXOS_ROUGHTIME_AGREEMENT_TESTING)
extern "C" int gxos_roughtime_agreement_test_seal_acquisition_evidence(
    const gxos_roughtime_v1_evidence* verified,
    uint8_t* clock_evidence_contract_valid,
    uint64_t* expires_monotonic_ns,
    gxos_roughtime_verified_evidence* token)
{
    return verified && kernel::roughtime::agreement_internal::seal_acquisition_evidence(
        *verified, clock_evidence_contract_valid, expires_monotonic_ns, token) ? 1 : 0;
}
#endif

extern "C" gxos_roughtime_agreement_status gxos_roughtime_agreement_evaluate(
    const gxos_roughtime_agreement_policy* policy,
    const gxos_roughtime_verified_evidence* evidence,
    size_t evidence_count,
    uint64_t now_monotonic_ns,
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_agreement_result* result)
{
    if (!result) return GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE;
    clear_bytes(reinterpret_cast<uint8_t*>(result), sizeof(*result));
    result->status = GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE;
    if (!policy) return result->status;
    result->policy_identity = policy->policy_identity;
    result->profile = policy->profile;
    result->evaluated_monotonic_ns = now_monotonic_ns;

    uint8_t configured_identities[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES][32] = {};
    uint8_t configured_operator_count = 0;
    if (!valid_policy(*policy, configured_identities, &configured_operator_count)) {
        set_status(result, GXOS_ROUGHTIME_AGREEMENT_INVALID_SOURCE_POLICY);
        return result->status;
    }
    if (evidence_count > GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES ||
        (evidence_count != 0 && !evidence)) {
        return result->status;
    }

    gxos_roughtime_v1_evidence opened[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES] = {};
    uint8_t token_qualified[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES] = {};
    OperatorMeasurement operators[GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES] = {};
    uint8_t operator_count = 0;
    bool stale_seen = false;
    bool uncertainty_seen = false;
    bool delay_seen = false;

    for (size_t i = 0; i < evidence_count; ++i) {
        uint64_t expires_monotonic_ns = 0;
        if (!open_token(evidence[i], &opened[i], &expires_monotonic_ns)) {
            set_status(result, GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE);
            return result->status;
        }
        for (size_t previous = 0; previous < i; ++previous) {
            if (same_response(opened[i], opened[previous])) {
                set_status(result, GXOS_ROUGHTIME_AGREEMENT_DUPLICATE_RESPONSE);
                return result->status;
            }
        }

        const uint8_t source_index = find_source(*policy, configured_identities,
                                                  opened[i].server_key_identity);
        if (source_index == 0xffU) {
            set_status(result, GXOS_ROUGHTIME_AGREEMENT_UNRECOGNIZED_SOURCE);
            return result->status;
        }
        const uint64_t received_ns = opened[i].received_monotonic_ns;
        if (now_monotonic_ns < received_ns) {
            set_status(result, GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE);
            return result->status;
        }
        const uint64_t age_ns = now_monotonic_ns - received_ns;
        const uint64_t age_seconds = ceil_seconds(age_ns);
        if (now_monotonic_ns > expires_monotonic_ns ||
            age_seconds > policy->maximum_age_seconds) {
            stale_seen = true;
            continue;
        }
        const uint64_t maximum_delay_ns =
            static_cast<uint64_t>(policy->maximum_transport_delay_ms) * kNanosecondsPerMillisecond;
        if (opened[i].measured_transport_delay_ns > maximum_delay_ns) {
            delay_seen = true;
            continue;
        }

        const uint64_t advance_floor = age_ns / kNanosecondsPerSecond;
        const uint64_t advance_ceil = ceil_seconds(age_ns);
        uint64_t normalized_earliest = 0;
        uint64_t normalized_latest = 0;
        if (!add_u64(opened[i].earliest_unix_seconds, advance_floor, &normalized_earliest) ||
            !add_u64(opened[i].latest_unix_seconds, advance_ceil, &normalized_latest) ||
            normalized_earliest > normalized_latest ||
            normalized_latest >= GXOS_ROUGHTIME_V1_TIME_MAX_UNIX_SECONDS) {
            set_status(result, GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE);
            return result->status;
        }

        const uint32_t source_operator = policy->sources[source_index].operator_identity;
        int group_index = find_operator(operators, operator_count, source_operator);
        if (group_index < 0) {
            group_index = operator_count++;
            operators[group_index].identity = source_operator;
            operators[group_index].earliest = normalized_earliest;
            operators[group_index].latest = normalized_latest;
            operators[group_index].maximum_age_seconds = age_seconds;
            operators[group_index].present = 1;
            operators[group_index].usable = 1;
        } else {
            if (normalized_earliest < operators[group_index].earliest)
                operators[group_index].earliest = normalized_earliest;
            if (normalized_latest > operators[group_index].latest)
                operators[group_index].latest = normalized_latest;
            if (age_seconds > operators[group_index].maximum_age_seconds)
                operators[group_index].maximum_age_seconds = age_seconds;
        }
        token_qualified[i] = 1;
    }

    uint8_t qualified_count = 0;
    for (uint8_t i = 0; i < operator_count; ++i) {
        OperatorMeasurement& group = operators[i];
        const uint64_t width = group.latest - group.earliest;
        group.uncertainty_seconds = width / 2U + (width % 2U);
        if (group.uncertainty_seconds > policy->maximum_source_uncertainty_seconds) {
            uncertainty_seen = true;
            group.usable = 0;
            continue;
        }
        if (qualified_count != i) operators[qualified_count] = group;
        ++qualified_count;
    }
    operator_count = qualified_count;

    if (qualified_count < policy->quorum) {
        gxos_roughtime_agreement_status reason = GXOS_ROUGHTIME_AGREEMENT_INSUFFICIENT_SOURCES;
        if (stale_seen) reason = GXOS_ROUGHTIME_AGREEMENT_STALE_EVIDENCE;
        else if (uncertainty_seen || delay_seen) reason = GXOS_ROUGHTIME_AGREEMENT_EXCESSIVE_UNCERTAINTY;
        set_status(result, reason);
        clear_bytes(reinterpret_cast<uint8_t*>(opened), sizeof(opened));
        return result->status;
    }

    uint64_t all_earliest = 0;
    uint64_t all_latest = UINT64_MAX;
    for (uint8_t i = 0; i < qualified_count; ++i) {
        if (operators[i].earliest > all_earliest) all_earliest = operators[i].earliest;
        if (operators[i].latest < all_latest) all_latest = operators[i].latest;
    }

    if (all_earliest > all_latest) {
        uint32_t candidate_count = 0;
        uint64_t candidate_intersection_earliest = 0;
        uint64_t candidate_intersection_latest = UINT64_MAX;
        const uint16_t limit = static_cast<uint16_t>(1U << qualified_count);
        for (uint16_t mask = 1; mask < limit; ++mask) {
            uint8_t members = 0;
            for (uint8_t bit = 0; bit < qualified_count; ++bit) {
                if ((mask & (1U << bit)) != 0) ++members;
            }
            if (members != policy->quorum) continue;
            uint64_t candidate_earliest = 0;
            uint64_t candidate_latest = UINT64_MAX;
            for (uint8_t bit = 0; bit < qualified_count; ++bit) {
                if ((mask & (1U << bit)) == 0) continue;
                if (operators[bit].earliest > candidate_earliest)
                    candidate_earliest = operators[bit].earliest;
                if (operators[bit].latest < candidate_latest)
                    candidate_latest = operators[bit].latest;
            }
            if (candidate_earliest > candidate_latest) continue;
            ++candidate_count;
            if (candidate_earliest > candidate_intersection_earliest)
                candidate_intersection_earliest = candidate_earliest;
            if (candidate_latest < candidate_intersection_latest)
                candidate_intersection_latest = candidate_latest;
        }
        const gxos_roughtime_agreement_status conflict_status =
            candidate_count > 1 && candidate_intersection_earliest > candidate_intersection_latest
                ? GXOS_ROUGHTIME_AGREEMENT_AMBIGUOUS
                : GXOS_ROUGHTIME_AGREEMENT_INCONSISTENT_SOURCES;
        set_status(result, conflict_status);
        clear_bytes(reinterpret_cast<uint8_t*>(opened), sizeof(opened));
        return result->status;
    }

    result->earliest_unix_seconds = all_earliest;
    result->latest_unix_seconds = all_latest;
    result->estimated_midpoint_unix_seconds = all_earliest + (all_latest - all_earliest) / 2U;
    result->independent_source_count = qualified_count;
    result->evaluated_monotonic_ns = now_monotonic_ns;
    result->policy_identity = policy->policy_identity;
    result->profile = policy->profile;
    result->production_policy_accepted = 0;
    for (uint8_t i = 0; i < qualified_count; ++i) {
        result->operator_identities[i] = operators[i].identity;
        if (operators[i].maximum_age_seconds > result->maximum_evidence_age_seconds)
            result->maximum_evidence_age_seconds = operators[i].maximum_age_seconds;
        if (operators[i].uncertainty_seconds > result->maximum_uncertainty_seconds)
            result->maximum_uncertainty_seconds = operators[i].uncertainty_seconds;
    }
    for (size_t i = 0; i < evidence_count; ++i) {
        if (!token_qualified[i]) continue;
        const uint8_t source_index = find_source(*policy, configured_identities,
                                                  opened[i].server_key_identity);
        if (source_index == 0xffU ||
            find_operator(operators, operator_count,
                          policy->sources[source_index].operator_identity) < 0) {
            continue;
        }
        if (result->evidence_key_count < GXOS_ROUGHTIME_AGREEMENT_MAX_SOURCES) {
            copy_bytes(result->server_key_identities[result->evidence_key_count++],
                       opened[i].server_key_identity, 32);
        }
    }

    const gxos_roughtime_agreement_status continuity_status = update_continuity(
        *policy, now_monotonic_ns, all_earliest, all_latest, continuity,
        &result->continuity_checked);
    if (continuity_status != GXOS_ROUGHTIME_AGREEMENT_SATISFIED) {
        set_status(result, continuity_status);
        clear_bytes(reinterpret_cast<uint8_t*>(opened), sizeof(opened));
        return result->status;
    }

    set_status(result, GXOS_ROUGHTIME_AGREEMENT_SATISFIED);
    clear_bytes(reinterpret_cast<uint8_t*>(opened), sizeof(opened));
    return result->status;
}

extern "C" const char* gxos_roughtime_agreement_status_name(
    gxos_roughtime_agreement_status status)
{
    switch (status) {
    case GXOS_ROUGHTIME_AGREEMENT_SATISFIED: return "agreement-satisfied";
    case GXOS_ROUGHTIME_AGREEMENT_INSUFFICIENT_SOURCES: return "insufficient-sources";
    case GXOS_ROUGHTIME_AGREEMENT_INCONSISTENT_SOURCES: return "inconsistent-sources";
    case GXOS_ROUGHTIME_AGREEMENT_STALE_EVIDENCE: return "stale-evidence";
    case GXOS_ROUGHTIME_AGREEMENT_EXCESSIVE_UNCERTAINTY: return "excessive-uncertainty";
    case GXOS_ROUGHTIME_AGREEMENT_INVALID_SOURCE_POLICY: return "invalid-source-policy";
    case GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE: return "invalid-evidence";
    case GXOS_ROUGHTIME_AGREEMENT_UNRECOGNIZED_SOURCE: return "unrecognized-source";
    case GXOS_ROUGHTIME_AGREEMENT_DUPLICATE_RESPONSE: return "duplicate-response";
    case GXOS_ROUGHTIME_AGREEMENT_AMBIGUOUS: return "ambiguous-agreement";
    case GXOS_ROUGHTIME_AGREEMENT_CLOCK_ROLLBACK: return "clock-rollback";
    case GXOS_ROUGHTIME_AGREEMENT_FORWARD_JUMP: return "unreasonable-forward-jump";
    case GXOS_ROUGHTIME_AGREEMENT_CONTINUITY_FAILURE: return "continuity-failure";
    case GXOS_ROUGHTIME_AGREEMENT_VERIFIER_REJECTED: return "verifier-rejected";
    default: return "unknown-agreement-status";
    }
}

namespace gxos {
namespace roughtime {

gxos::clockassurance::AgreementEvidence to_clock_assurance_evidence(
    const gxos_roughtime_agreement_result& result,
    uint64_t maximum_age_seconds)
{
    gxos::clockassurance::AgreementEvidence evidence = {};
    evidence.metadataPresent = result.status == GXOS_ROUGHTIME_AGREEMENT_SATISFIED &&
        result.policy_identity != 0 && result.earliest_unix_seconds <= result.latest_unix_seconds;
    evidence.agreementSatisfied = result.status == GXOS_ROUGHTIME_AGREEMENT_SATISFIED;
    evidence.policyIdentity = result.policy_identity;
    evidence.independentSourceCount = result.independent_source_count;
    evidence.earliestUnixSeconds = static_cast<int64_t>(result.earliest_unix_seconds);
    evidence.latestUnixSeconds = static_cast<int64_t>(result.latest_unix_seconds);
    evidence.evaluatedAtMonotonicSeconds = result.evaluated_monotonic_ns / kNanosecondsPerSecond;
    evidence.evidenceAgeAtEvaluationSeconds = result.maximum_evidence_age_seconds;
    evidence.maximumAgeSeconds = maximum_age_seconds <= GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS
        ? maximum_age_seconds : GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS;
    evidence.uncertaintySeconds = result.maximum_uncertainty_seconds;
    return evidence;
}

} // namespace roughtime
} // namespace gxos
