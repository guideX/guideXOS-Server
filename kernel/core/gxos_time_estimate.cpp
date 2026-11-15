#include "include/gxos_time_estimate.h"

#include "roughtime_multisource_guest_fixture_keys.h"
#include "../../third_party/libsodium/src/include/sodium/crypto_hash_sha512.h"

namespace gxos {
namespace clockassurance {
namespace {

static TimeEstimateStore s_timeEstimateStore;
static uint8_t s_timeEstimateStoreInitialized;

static const uint32_t kNavhttps15FixturePolicyIdentity = 0x1501U;
static const uint32_t kFixtureOperatorIdentities[3] = { 401U, 402U, 403U };
static const uint32_t kMaximumForwardMovementSeconds = 300U;
static const uint32_t kPitDriftPartsPerMillion = 500U;
static const uint64_t kPartsPerMillion = 1000000ULL;
static void clear_bytes(void* memory, size_t length)
{
    uint8_t* bytes = static_cast<uint8_t*>(memory);
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static bool equal_bytes(const uint8_t* left, const uint8_t* right, size_t length)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

static bool fixture_key_identities(uint8_t identities[3][32])
{
    uint8_t input[33];
    uint8_t digest[64];
    for (uint8_t keyIndex = 0; keyIndex < 3; ++keyIndex) {
        input[0] = 0xff;
        for (uint8_t byte = 0; byte < 32; ++byte) {
            input[byte + 1U] = gxos_roughtime_navhttps15_fixture_keys[keyIndex][byte];
        }
        if (crypto_hash_sha512(digest, input, sizeof(input)) != 0) {
            clear_bytes(input, sizeof(input));
            clear_bytes(digest, sizeof(digest));
            clear_bytes(identities, 3U * 32U);
            return false;
        }
        for (uint8_t byte = 0; byte < 32; ++byte) identities[keyIndex][byte] = digest[byte];
    }
    clear_bytes(input, sizeof(input));
    clear_bytes(digest, sizeof(digest));
    return true;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || left > UINT64_MAX - right) return false;
    *result = left + right;
    return true;
}

static uint64_t ceil_seconds(uint64_t nanoseconds)
{
    const uint64_t whole = nanoseconds / kTimeEstimateNanosecondsPerSecond;
    return whole + (nanoseconds % kTimeEstimateNanosecondsPerSecond != 0 ? 1U : 0U);
}

static uint64_t drift_allowance_seconds(uint64_t elapsedNanoseconds)
{
    const uint64_t wholeSeconds = elapsedNanoseconds / kTimeEstimateNanosecondsPerSecond;
    const uint64_t remainderNs = elapsedNanoseconds % kTimeEstimateNanosecondsPerSecond;
    const uint64_t numerator = wholeSeconds * kPitDriftPartsPerMillion +
        (remainderNs * kPitDriftPartsPerMillion +
            kTimeEstimateNanosecondsPerSecond - 1U) /
                kTimeEstimateNanosecondsPerSecond;
    return (numerator + kPartsPerMillion - 1U) / kPartsPerMillion;
}

static bool valid_configuration(
    const gxos_roughtime_round_configuration& configuration)
{
    if (configuration.policy_identity != kNavhttps15FixturePolicyIdentity ||
        configuration.source_count != GXOS_ROUGHTIME_ROUND_MAX_SOURCES ||
        configuration.quorum != 2 || configuration.reserved != 0 ||
        configuration.maximum_age_seconds != kFixtureTimeEstimateMaximumAgeSeconds ||
        configuration.maximum_source_uncertainty_seconds !=
            kFixtureTimeEstimateMaximumUncertaintySeconds ||
        configuration.maximum_transport_delay_ms != 2000U ||
        configuration.maximum_forward_jump_seconds != kMaximumForwardMovementSeconds) {
        return false;
    }

    for (uint8_t i = 0; i < GXOS_ROUGHTIME_ROUND_MAX_SOURCES; ++i) {
        const gxos_roughtime_round_source& source = configuration.sources[i];
        if (source.server_ipv4 == 0 || source.server_port == 0 || source.reserved != 0 ||
            source.operator_identity != kFixtureOperatorIdentities[i] ||
            !equal_bytes(source.server_public_key, gxos_roughtime_navhttps15_fixture_keys[i], 32)) {
            return false;
        }
        for (uint8_t previous = 0; previous < i; ++previous) {
            const gxos_roughtime_round_source& prior = configuration.sources[previous];
            if (source.server_ipv4 == prior.server_ipv4 &&
                source.server_port == prior.server_port) return false;
        }
    }
    return true;
}

static bool valid_agreement_sources(const gxos_roughtime_round_result& round,
                                    const uint8_t fixtureKeyIds[3][32])
{
    const gxos_roughtime_agreement_result& agreement = round.agreement;
    if (round.status != GXOS_ROUGHTIME_ROUND_EVALUATED ||
        round.attempted_source_count != GXOS_ROUGHTIME_ROUND_MAX_SOURCES ||
        round.verified_response_count > 3 || round.independent_operator_count > 3 ||
        agreement.status != GXOS_ROUGHTIME_AGREEMENT_SATISFIED ||
        agreement.policy_identity != kNavhttps15FixturePolicyIdentity ||
        agreement.profile != GXOS_ROUGHTIME_AGREEMENT_PROFILE_UNREVIEWED_CONFIG ||
        agreement.production_policy_accepted != 0 ||
        agreement.independent_source_count < 2 || agreement.independent_source_count > 3 ||
        agreement.evidence_key_count < 2 || agreement.evidence_key_count > 3 ||
        agreement.independent_source_count != agreement.evidence_key_count ||
        agreement.independent_source_count != round.independent_operator_count) {
        return false;
    }

    uint8_t verifiedCount = 0;
    uint8_t verifiedKeys[3][32] = {};
    uint32_t verifiedOperators[3] = {};
    for (uint8_t i = 0; i < GXOS_ROUGHTIME_ROUND_MAX_SOURCES; ++i) {
        const gxos_roughtime_round_source_result& sourceResult = round.sources[i];
        if (!sourceResult.verified) continue;
        if (sourceResult.status != GXOS_ROUGHTIME_ACQUISITION_SUCCESS ||
            sourceResult.server_port == 0 ||
            !equal_bytes(sourceResult.server_key_identity, fixtureKeyIds[i], 32)) {
            return false;
        }
        verifiedOperators[verifiedCount] = kFixtureOperatorIdentities[i];
        clear_bytes(verifiedKeys[verifiedCount], 32);
        for (uint8_t byte = 0; byte < 32; ++byte) {
            verifiedKeys[verifiedCount][byte] = sourceResult.server_key_identity[byte];
        }
        ++verifiedCount;
    }
    if (verifiedCount != round.verified_response_count) return false;

    for (uint8_t i = 0; i < agreement.independent_source_count; ++i) {
        bool operatorFound = false;
        for (uint8_t j = 0; j < verifiedCount; ++j) {
            if (agreement.operator_identities[i] == verifiedOperators[j]) operatorFound = true;
        }
        if (!operatorFound) return false;
        for (uint8_t previous = 0; previous < i; ++previous) {
            if (agreement.operator_identities[i] == agreement.operator_identities[previous])
                return false;
        }

        bool keyFound = false;
        for (uint8_t j = 0; j < verifiedCount; ++j) {
            if (equal_bytes(agreement.server_key_identities[i], verifiedKeys[j], 32)) keyFound = true;
        }
        if (!keyFound) return false;
        for (uint8_t previous = 0; previous < i; ++previous) {
            if (equal_bytes(agreement.server_key_identities[i],
                            agreement.server_key_identities[previous], 32)) return false;
        }
    }
    return true;
}

static bool agreement_matches_issued_tokens(
    const gxos_roughtime_round_configuration& configuration,
    const gxos_roughtime_round_result& round)
{
    if (round.verified_response_count < 2 ||
        round.verified_response_count > GXOS_ROUGHTIME_ROUND_MAX_SOURCES) return false;

    gxos_roughtime_agreement_policy policy = {};
    policy.policy_identity = configuration.policy_identity;
    policy.profile = GXOS_ROUGHTIME_AGREEMENT_PROFILE_UNREVIEWED_CONFIG;
    policy.source_count = configuration.source_count;
    policy.quorum = configuration.quorum;
    policy.maximum_age_seconds = configuration.maximum_age_seconds;
    policy.maximum_source_uncertainty_seconds =
        configuration.maximum_source_uncertainty_seconds;
    policy.maximum_transport_delay_ms = configuration.maximum_transport_delay_ms;
    policy.maximum_forward_jump_seconds = configuration.maximum_forward_jump_seconds;
    for (uint8_t i = 0; i < configuration.source_count; ++i) {
        policy.sources[i].operator_identity = configuration.sources[i].operator_identity;
        for (uint8_t byte = 0; byte < 32; ++byte) {
            policy.sources[i].server_public_key[byte] =
                configuration.sources[i].server_public_key[byte];
        }
    }

    gxos_roughtime_agreement_result verified = {};
    if (gxos_roughtime_agreement_evaluate(
            &policy, round.evidence, round.verified_response_count,
            round.agreement.evaluated_monotonic_ns, 0, &verified) !=
        GXOS_ROUGHTIME_AGREEMENT_SATISFIED) {
        return false;
    }

    const gxos_roughtime_agreement_result& claimed = round.agreement;
    if (claimed.policy_identity != verified.policy_identity ||
        claimed.profile != verified.profile ||
        claimed.independent_source_count != verified.independent_source_count ||
        claimed.evidence_key_count != verified.evidence_key_count ||
        claimed.production_policy_accepted != verified.production_policy_accepted ||
        claimed.earliest_unix_seconds != verified.earliest_unix_seconds ||
        claimed.latest_unix_seconds != verified.latest_unix_seconds ||
        claimed.estimated_midpoint_unix_seconds != verified.estimated_midpoint_unix_seconds ||
        claimed.evaluated_monotonic_ns != verified.evaluated_monotonic_ns ||
        claimed.maximum_evidence_age_seconds != verified.maximum_evidence_age_seconds ||
        claimed.maximum_uncertainty_seconds != verified.maximum_uncertainty_seconds) {
        return false;
    }
    for (uint8_t i = 0; i < verified.independent_source_count; ++i) {
        if (claimed.operator_identities[i] != verified.operator_identities[i] ||
            !equal_bytes(claimed.server_key_identities[i],
                         verified.server_key_identities[i], 32)) return false;
    }
    return true;
}

static bool same_source_policy(const TimeEstimateInfo& prior,
                               const gxos_roughtime_agreement_result& next)
{
    if (prior.policyIdentity != next.policy_identity ||
        prior.distinctOperatorCount != next.independent_source_count ||
        prior.keyIdentityCount != next.evidence_key_count) return false;
    for (uint8_t i = 0; i < prior.operatorIdentityCount; ++i) {
        bool found = false;
        for (uint8_t j = 0; j < next.independent_source_count; ++j) {
            if (prior.operatorIdentities[i] == next.operator_identities[j]) found = true;
        }
        if (!found) return false;
    }
    for (uint8_t i = 0; i < prior.keyIdentityCount; ++i) {
        bool found = false;
        for (uint8_t j = 0; j < next.evidence_key_count; ++j) {
            if (equal_bytes(prior.serverKeyIdentities[i], next.server_key_identities[j], 32))
                found = true;
        }
        if (!found) return false;
    }
    return true;
}

static TimeEstimateAcceptResult reject_result(TimeEstimateReason reason)
{
    switch (reason) {
    case TimeEstimateReason::NotFixturePolicy: return TimeEstimateAcceptResult::NotFixturePolicy;
    case TimeEstimateReason::InsufficientIndependentSources:
        return TimeEstimateAcceptResult::InsufficientIndependentSources;
    case TimeEstimateReason::ExcessiveUncertainty: return TimeEstimateAcceptResult::ExcessiveUncertainty;
    case TimeEstimateReason::InvalidInterval: return TimeEstimateAcceptResult::InvalidInterval;
    case TimeEstimateReason::Expired: return TimeEstimateAcceptResult::Expired;
    case TimeEstimateReason::CounterOverflow: return TimeEstimateAcceptResult::CounterOverflow;
    case TimeEstimateReason::SourcePolicyChanged: return TimeEstimateAcceptResult::SourcePolicyChanged;
    case TimeEstimateReason::ConflictingEvidence: return TimeEstimateAcceptResult::ConflictingEvidence;
    case TimeEstimateReason::RollbackDetected: return TimeEstimateAcceptResult::RollbackDetected;
    case TimeEstimateReason::MonotonicWentBackwards: return TimeEstimateAcceptResult::RollbackDetected;
    case TimeEstimateReason::ExcessiveForwardMovement:
        return TimeEstimateAcceptResult::ExcessiveForwardMovement;
    case TimeEstimateReason::ClockDiscontinuity: return TimeEstimateAcceptResult::ContinuityFailure;
    default: return TimeEstimateAcceptResult::InvalidRound;
    }
}

} // namespace

void TimeEstimateStore::initialize()
{
    clear_bytes(&m_info, sizeof(m_info));
    m_latchedFailure = 0;
    m_expiryMonotonicNs = 0;
    m_info.state = TimeEstimateState::NoEvidence;
    m_info.scope = TimeEstimateScope::None;
    m_info.reason = TimeEstimateReason::NoAuthenticatedPersistentState;
}

TimeEstimateAcceptResult TimeEstimateStore::accept_fixture_round(
    const gxos_roughtime_round_configuration& configuration,
    const gxos_roughtime_round_result& round,
    uint64_t nowMonotonicNs)
{
    if (m_latchedFailure) {
        return reject_result(m_info.reason == TimeEstimateReason::None
            ? TimeEstimateReason::ClockDiscontinuity : m_info.reason);
    }
    if (m_info.policyIdentity != 0 &&
        configuration.policy_identity != m_info.policyIdentity) {
        invalidate(TimeEstimateReason::SourcePolicyChanged);
        return TimeEstimateAcceptResult::SourcePolicyChanged;
    }
    if (!valid_configuration(configuration)) {
        if (!m_info.available) {
            m_info.state = TimeEstimateState::Rejected;
            m_info.reason = TimeEstimateReason::NotFixturePolicy;
        }
        return TimeEstimateAcceptResult::NotFixturePolicy;
    }
    if (round.status != GXOS_ROUGHTIME_ROUND_EVALUATED) {
        if (!m_info.available) {
            m_info.state = TimeEstimateState::Rejected;
            m_info.reason = TimeEstimateReason::InvalidRound;
        }
        return TimeEstimateAcceptResult::InvalidRound;
    }
    if (round.agreement.independent_source_count < 2 ||
        round.agreement.evidence_key_count < 2 || round.independent_operator_count < 2) {
        if (!m_info.available) {
            m_info.state = TimeEstimateState::Rejected;
            m_info.reason = TimeEstimateReason::InsufficientIndependentSources;
        }
        return TimeEstimateAcceptResult::InsufficientIndependentSources;
    }
    if (round.agreement.status != GXOS_ROUGHTIME_AGREEMENT_SATISFIED) {
        if (!m_info.available) {
            m_info.state = TimeEstimateState::Rejected;
            m_info.reason = TimeEstimateReason::InvalidRound;
        }
        return TimeEstimateAcceptResult::InvalidRound;
    }
    if (round.agreement.maximum_uncertainty_seconds >
        kFixtureTimeEstimateMaximumUncertaintySeconds) {
        if (!m_info.available) {
            m_info.state = TimeEstimateState::Rejected;
            m_info.reason = TimeEstimateReason::ExcessiveUncertainty;
        }
        return TimeEstimateAcceptResult::ExcessiveUncertainty;
    }
    uint8_t fixtureKeyIds[3][32] = {};
    if (!fixture_key_identities(fixtureKeyIds) ||
        !valid_agreement_sources(round, fixtureKeyIds) ||
        !agreement_matches_issued_tokens(configuration, round)) {
        clear_bytes(fixtureKeyIds, sizeof(fixtureKeyIds));
        if (!m_info.available) {
            m_info.state = TimeEstimateState::Rejected;
            m_info.reason = TimeEstimateReason::InvalidRound;
        }
        return TimeEstimateAcceptResult::InvalidRound;
    }
    clear_bytes(fixtureKeyIds, sizeof(fixtureKeyIds));

    const gxos_roughtime_agreement_result& agreement = round.agreement;
    if (agreement.earliest_unix_seconds <
            static_cast<uint64_t>(kPlausibleMinimumUnixSeconds) ||
        agreement.latest_unix_seconds >=
            static_cast<uint64_t>(kPlausibleMaximumUnixSeconds) ||
        agreement.earliest_unix_seconds > agreement.latest_unix_seconds) {
        if (!m_info.available) {
            m_info.state = TimeEstimateState::Rejected;
            m_info.reason = TimeEstimateReason::InvalidInterval;
        }
        return TimeEstimateAcceptResult::InvalidInterval;
    }
    if (nowMonotonicNs < agreement.evaluated_monotonic_ns) {
        m_info.available = 0;
        m_info.agreementSatisfied = 0;
        m_info.state = TimeEstimateState::RollbackSuspected;
        m_info.reason = TimeEstimateReason::MonotonicWentBackwards;
        m_info.currentEarliestUnixSeconds = 0;
        m_info.currentLatestUnixSeconds = 0;
        m_info.remainingFreshnessSeconds = 0;
        m_latchedFailure = 1;
        return TimeEstimateAcceptResult::RollbackDetected;
    }

    if (m_info.policyIdentity != 0 &&
        (m_info.policyIdentity != agreement.policy_identity ||
         m_info.configuredSourceCount != configuration.source_count)) {
        invalidate(TimeEstimateReason::SourcePolicyChanged);
        return TimeEstimateAcceptResult::SourcePolicyChanged;
    }

    if (m_info.available) {
        query(nowMonotonicNs, nullptr);
        if (m_latchedFailure) {
            return reject_result(m_info.reason);
        }
        if (m_info.policyIdentity != agreement.policy_identity ||
            !same_source_policy(m_info, agreement)) {
            invalidate(TimeEstimateReason::SourcePolicyChanged);
            return TimeEstimateAcceptResult::SourcePolicyChanged;
        }
        if (m_info.available) {
            if (agreement.latest_unix_seconds < m_info.currentEarliestUnixSeconds) {
                m_info.available = 0;
                m_info.state = TimeEstimateState::RollbackSuspected;
                m_info.reason = TimeEstimateReason::RollbackDetected;
                m_info.currentEarliestUnixSeconds = 0;
                m_info.currentLatestUnixSeconds = 0;
                m_info.remainingFreshnessSeconds = 0;
                m_latchedFailure = 1;
                return TimeEstimateAcceptResult::RollbackDetected;
            }
            if (agreement.earliest_unix_seconds > m_info.currentLatestUnixSeconds) {
                const uint64_t gap = agreement.earliest_unix_seconds -
                    m_info.currentLatestUnixSeconds;
                m_info.available = 0;
                if (gap > kMaximumForwardMovementSeconds) {
                    m_info.state = TimeEstimateState::ContinuityFailure;
                    m_info.reason = TimeEstimateReason::ExcessiveForwardMovement;
                    m_latchedFailure = 1;
                    m_info.currentEarliestUnixSeconds = 0;
                    m_info.currentLatestUnixSeconds = 0;
                    m_info.remainingFreshnessSeconds = 0;
                    return TimeEstimateAcceptResult::ExcessiveForwardMovement;
                }
                m_info.state = TimeEstimateState::Conflicting;
                m_info.reason = TimeEstimateReason::ConflictingEvidence;
                m_info.currentEarliestUnixSeconds = 0;
                m_info.currentLatestUnixSeconds = 0;
                m_info.remainingFreshnessSeconds = 0;
                return TimeEstimateAcceptResult::ConflictingEvidence;
            }
        }
    }

    const uint64_t remainingAtAnchor =
        kFixtureTimeEstimateMaximumAgeSeconds - agreement.maximum_evidence_age_seconds;
    if (agreement.maximum_evidence_age_seconds >=
        kFixtureTimeEstimateMaximumAgeSeconds || remainingAtAnchor == 0) {
        m_info.available = 0;
        m_info.state = TimeEstimateState::Expired;
        m_info.reason = TimeEstimateReason::Expired;
        m_info.currentEarliestUnixSeconds = 0;
        m_info.currentLatestUnixSeconds = 0;
        m_info.remainingFreshnessSeconds = 0;
        return TimeEstimateAcceptResult::Expired;
    }
    if (agreement.evaluated_monotonic_ns > UINT64_MAX -
        remainingAtAnchor * kTimeEstimateNanosecondsPerSecond) {
        m_info.available = 0;
        m_info.state = TimeEstimateState::Rejected;
        m_info.reason = TimeEstimateReason::CounterOverflow;
        m_info.currentEarliestUnixSeconds = 0;
        m_info.currentLatestUnixSeconds = 0;
        m_info.remainingFreshnessSeconds = 0;
        return TimeEstimateAcceptResult::CounterOverflow;
    }

    clear_bytes(&m_info, sizeof(m_info));
    m_info.state = TimeEstimateState::Fresh;
    m_info.scope = TimeEstimateScope::FixtureOnly;
    m_info.reason = TimeEstimateReason::AcceptedFixtureAgreement;
    m_info.available = 1;
    m_info.agreementSatisfied = 1;
    m_info.configuredSourceCount = configuration.source_count;
    m_info.acceptedSourceCount = agreement.evidence_key_count;
    m_info.distinctOperatorCount = agreement.independent_source_count;
    m_info.operatorIdentityCount = agreement.independent_source_count;
    m_info.keyIdentityCount = agreement.evidence_key_count;
    m_info.productionPolicyAccepted = 0;
    m_info.systemClockChanged = 0;
    m_info.publicHttpsEnabled = 0;
    m_info.policyIdentity = agreement.policy_identity;
    for (uint8_t i = 0; i < agreement.independent_source_count; ++i) {
        m_info.operatorIdentities[i] = agreement.operator_identities[i];
    }
    for (uint8_t i = 0; i < agreement.evidence_key_count; ++i) {
        for (uint8_t byte = 0; byte < 32; ++byte) {
            m_info.serverKeyIdentities[i][byte] = agreement.server_key_identities[i][byte];
        }
    }
    m_info.agreedEarliestUnixSeconds = agreement.earliest_unix_seconds;
    m_info.agreedLatestUnixSeconds = agreement.latest_unix_seconds;
    m_info.currentEarliestUnixSeconds = agreement.earliest_unix_seconds;
    m_info.currentLatestUnixSeconds = agreement.latest_unix_seconds;
    m_info.measurementAnchorMonotonicNs = agreement.evaluated_monotonic_ns;
    m_info.lastObservedMonotonicNs = nowMonotonicNs;
    m_info.maximumAgeSeconds = kFixtureTimeEstimateMaximumAgeSeconds;
    m_info.evidenceAgeAtAnchorSeconds = agreement.maximum_evidence_age_seconds;
    m_info.maximumUncertaintySeconds = agreement.maximum_uncertainty_seconds;
    m_info.currentUncertaintySeconds = agreement.maximum_uncertainty_seconds;
    m_info.remainingFreshnessSeconds = remainingAtAnchor;
    m_expiryMonotonicNs = agreement.evaluated_monotonic_ns +
        remainingAtAnchor * kTimeEstimateNanosecondsPerSecond;

    if (nowMonotonicNs > agreement.evaluated_monotonic_ns) {
        TimeEstimateInfo ignored = {};
        const TimeEstimateReason advanceReason = query(nowMonotonicNs, &ignored);
        if (advanceReason != TimeEstimateReason::AcceptedFixtureAgreement &&
            advanceReason != TimeEstimateReason::None) {
            return reject_result(advanceReason);
        }
    }
    return TimeEstimateAcceptResult::Accepted;
}

TimeEstimateReason TimeEstimateStore::query(uint64_t nowMonotonicNs,
                                            TimeEstimateInfo* output)
{
    if (m_info.available) {
        if (nowMonotonicNs < m_info.measurementAnchorMonotonicNs ||
            nowMonotonicNs < m_info.lastObservedMonotonicNs) {
            m_info.available = 0;
            m_info.state = TimeEstimateState::RollbackSuspected;
            m_info.reason = TimeEstimateReason::MonotonicWentBackwards;
            m_info.currentEarliestUnixSeconds = 0;
            m_info.currentLatestUnixSeconds = 0;
            m_info.remainingFreshnessSeconds = 0;
            m_latchedFailure = 1;
        } else if (nowMonotonicNs >= m_expiryMonotonicNs) {
            m_info.available = 0;
            m_info.state = TimeEstimateState::Expired;
            m_info.reason = TimeEstimateReason::Expired;
            m_info.currentEarliestUnixSeconds = 0;
            m_info.currentLatestUnixSeconds = 0;
            m_info.remainingFreshnessSeconds = 0;
        } else {
            const uint64_t elapsedNs = nowMonotonicNs - m_info.measurementAnchorMonotonicNs;
            const uint64_t ageSinceAnchor = ceil_seconds(elapsedNs);
            const uint64_t totalAge = m_info.evidenceAgeAtAnchorSeconds + ageSinceAnchor;
            if (totalAge >= m_info.maximumAgeSeconds) {
                m_info.available = 0;
                m_info.state = TimeEstimateState::Expired;
                m_info.reason = TimeEstimateReason::Expired;
                m_info.currentEarliestUnixSeconds = 0;
                m_info.currentLatestUnixSeconds = 0;
                m_info.remainingFreshnessSeconds = 0;
            } else {
                const uint64_t drift = drift_allowance_seconds(elapsedNs);
                const uint64_t resolution = elapsedNs == 0 ? 0U : 1U;
                uint64_t uncertainty = 0;
                if (!add_u64(m_info.maximumUncertaintySeconds, drift + resolution,
                             &uncertainty)) {
                    m_info.available = 0;
                    m_info.state = TimeEstimateState::ContinuityFailure;
                    m_info.reason = TimeEstimateReason::CounterOverflow;
                    m_info.currentEarliestUnixSeconds = 0;
                    m_info.currentLatestUnixSeconds = 0;
                    m_info.remainingFreshnessSeconds = 0;
                } else if (uncertainty > kFixtureTimeEstimateMaximumUncertaintySeconds) {
                    m_info.available = 0;
                    m_info.state = TimeEstimateState::Expired;
                    m_info.reason = TimeEstimateReason::UncertaintyLimitExceeded;
                    m_info.currentEarliestUnixSeconds = 0;
                    m_info.currentLatestUnixSeconds = 0;
                    m_info.remainingFreshnessSeconds = 0;
                } else {
                    const uint64_t elapsedFloor =
                        elapsedNs / kTimeEstimateNanosecondsPerSecond;
                    const uint64_t elapsedCeil = ceil_seconds(elapsedNs);
                    const uint64_t lowerAdvance = elapsedFloor > drift + resolution
                        ? elapsedFloor - drift - resolution : 0U;
                    const uint64_t upperAdvance = elapsedCeil + drift + resolution;
                    uint64_t earliest = 0;
                    uint64_t latest = 0;
                    if (!add_u64(m_info.agreedEarliestUnixSeconds, lowerAdvance, &earliest) ||
                        !add_u64(m_info.agreedLatestUnixSeconds, upperAdvance, &latest) ||
                        earliest > latest ||
                        latest >= static_cast<uint64_t>(kPlausibleMaximumUnixSeconds)) {
                        m_info.available = 0;
                        m_info.state = TimeEstimateState::ContinuityFailure;
                        m_info.reason = TimeEstimateReason::CounterOverflow;
                        m_info.currentEarliestUnixSeconds = 0;
                        m_info.currentLatestUnixSeconds = 0;
                        m_info.remainingFreshnessSeconds = 0;
                    } else {
                        m_info.currentEarliestUnixSeconds = earliest;
                        m_info.currentLatestUnixSeconds = latest;
                        m_info.currentUncertaintySeconds = uncertainty;
                        m_info.lastObservedMonotonicNs = nowMonotonicNs;
                        m_info.remainingFreshnessSeconds = m_info.maximumAgeSeconds - totalAge;
                        m_info.state = elapsedNs == 0
                            ? TimeEstimateState::Fresh : TimeEstimateState::Advanced;
                    }
                }
            }
        }
    }

    if (output) *output = m_info;
    return m_info.reason;
}

void TimeEstimateStore::invalidate(TimeEstimateReason reason)
{
    m_info.available = 0;
    m_info.state = TimeEstimateState::Invalidated;
    m_info.reason = reason == TimeEstimateReason::None
        ? TimeEstimateReason::ExplicitInvalidation : reason;
    m_info.agreementSatisfied = 0;
    m_info.currentEarliestUnixSeconds = 0;
    m_info.currentLatestUnixSeconds = 0;
    m_info.remainingFreshnessSeconds = 0;
    if (m_info.reason == TimeEstimateReason::RollbackDetected ||
        m_info.reason == TimeEstimateReason::MonotonicWentBackwards ||
        m_info.reason == TimeEstimateReason::ClockDiscontinuity) {
        m_latchedFailure = 1;
    }
}

void TimeEstimateStore::report_clock_discontinuity()
{
    m_info.available = 0;
    m_info.state = TimeEstimateState::ContinuityFailure;
    m_info.reason = TimeEstimateReason::ClockDiscontinuity;
    m_info.agreementSatisfied = 0;
    m_info.currentEarliestUnixSeconds = 0;
    m_info.currentLatestUnixSeconds = 0;
    m_info.remainingFreshnessSeconds = 0;
    m_latchedFailure = 1;
}

TimeEstimateStore& time_estimate_store()
{
    if (!s_timeEstimateStoreInitialized) {
        s_timeEstimateStore.initialize();
        s_timeEstimateStoreInitialized = 1;
    }
    return s_timeEstimateStore;
}

const char* time_estimate_state_name(TimeEstimateState state)
{
    switch (state) {
    case TimeEstimateState::NoEvidence: return "no-evidence";
    case TimeEstimateState::Fresh: return "fresh";
    case TimeEstimateState::Advanced: return "advanced";
    case TimeEstimateState::Expired: return "expired";
    case TimeEstimateState::Conflicting: return "conflicting";
    case TimeEstimateState::ContinuityFailure: return "continuity-failure";
    case TimeEstimateState::RollbackSuspected: return "rollback-suspected";
    case TimeEstimateState::Invalidated: return "invalidated";
    case TimeEstimateState::Rejected: return "rejected";
    default: return "unknown";
    }
}

const char* time_estimate_scope_name(TimeEstimateScope scope)
{
    switch (scope) {
    case TimeEstimateScope::None: return "none";
    case TimeEstimateScope::FixtureOnly: return "fixture-only";
    case TimeEstimateScope::ProductionApproved: return "production-approved";
    default: return "unknown";
    }
}

const char* time_estimate_reason_name(TimeEstimateReason reason)
{
    switch (reason) {
    case TimeEstimateReason::None: return "none";
    case TimeEstimateReason::NoAuthenticatedPersistentState: return "no-authenticated-persistent-state";
    case TimeEstimateReason::AcceptedFixtureAgreement: return "accepted-fixture-agreement";
    case TimeEstimateReason::InvalidRound: return "agreement-not-satisfied-or-invalid-round";
    case TimeEstimateReason::NotFixturePolicy: return "source-policy-not-pinned-fixture-policy";
    case TimeEstimateReason::InsufficientIndependentSources: return "insufficient-independent-sources";
    case TimeEstimateReason::ExcessiveUncertainty: return "uncertainty-exceeds-fixture-policy";
    case TimeEstimateReason::InvalidInterval: return "invalid-or-out-of-range-interval";
    case TimeEstimateReason::Expired: return "evidence-expired";
    case TimeEstimateReason::CounterOverflow: return "monotonic-or-utc-arithmetic-overflow";
    case TimeEstimateReason::MonotonicWentBackwards: return "monotonic-counter-went-backwards";
    case TimeEstimateReason::SourcePolicyChanged: return "source-policy-changed";
    case TimeEstimateReason::ConflictingEvidence: return "subsequent-evidence-conflicts";
    case TimeEstimateReason::RollbackDetected: return "rollback-suspected";
    case TimeEstimateReason::ExcessiveForwardMovement: return "excessive-forward-movement";
    case TimeEstimateReason::UncertaintyLimitExceeded: return "uncertainty-limit-exceeded";
    case TimeEstimateReason::ClockDiscontinuity: return "monotonic-clock-discontinuity";
    case TimeEstimateReason::ExplicitInvalidation: return "explicitly-invalidated";
    default: return "unknown";
    }
}

const char* time_estimate_accept_result_name(TimeEstimateAcceptResult result)
{
    switch (result) {
    case TimeEstimateAcceptResult::Accepted: return "accepted-fixture-only";
    case TimeEstimateAcceptResult::InvalidRound: return "agreement-not-satisfied-or-invalid-round";
    case TimeEstimateAcceptResult::NotFixturePolicy: return "source-policy-not-pinned-fixture-policy";
    case TimeEstimateAcceptResult::InsufficientIndependentSources: return "insufficient-independent-sources";
    case TimeEstimateAcceptResult::ExcessiveUncertainty: return "uncertainty-exceeds-fixture-policy";
    case TimeEstimateAcceptResult::InvalidInterval: return "invalid-or-out-of-range-interval";
    case TimeEstimateAcceptResult::Expired: return "evidence-expired";
    case TimeEstimateAcceptResult::CounterOverflow: return "monotonic-or-utc-arithmetic-overflow";
    case TimeEstimateAcceptResult::SourcePolicyChanged: return "source-policy-changed";
    case TimeEstimateAcceptResult::ConflictingEvidence: return "subsequent-evidence-conflicts";
    case TimeEstimateAcceptResult::RollbackDetected: return "rollback-suspected";
    case TimeEstimateAcceptResult::ExcessiveForwardMovement: return "excessive-forward-movement";
    case TimeEstimateAcceptResult::ContinuityFailure: return "monotonic-clock-discontinuity";
    default: return "unknown";
    }
}

} // namespace clockassurance
} // namespace gxos
