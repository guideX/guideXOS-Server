#pragma once

#include <stdint.h>

namespace gxos {
namespace clockassurance {

static const int64_t kPlausibleMinimumUnixSeconds = 1704067200ll; // 2024-01-01T00:00:00Z
static const int64_t kPlausibleMaximumUnixSeconds = 4133980800ll; // 2101-01-01T00:00:00Z

enum class State : uint8_t {
    Invalid,
    Plausible,
    Authenticated,
    RollbackSuspected
};

enum class ContinuityFailure : uint8_t {
    None,
    InvalidSample,
    SourceFailure,
    MonotonicWentBackwards,
    WallClockWentBackwards,
    ExcessiveForwardJump
};

enum class Observation : uint8_t {
    Accepted,
    RejectedInvalid,
    RejectedRollback,
    RejectedForwardJump
};

// Compares a wall-clock source against PIT elapsed time during one boot.
// It intentionally has no persistence API: a fresh instance after reboot has
// no evidence about earlier boots and must not claim rollback protection.
class WithinBootContinuity {
public:
    WithinBootContinuity()
        : m_initialized(false), m_failed(false), m_lastWallSeconds(0),
          m_lastMonotonicSeconds(0), m_failure(ContinuityFailure::None)
    {
    }

    Observation observe(int64_t wallSeconds, uint64_t monotonicSeconds)
    {
        if (m_failed) return result_for_failure(m_failure);
        if (wallSeconds < kPlausibleMinimumUnixSeconds ||
            wallSeconds >= kPlausibleMaximumUnixSeconds) {
            return fail(ContinuityFailure::InvalidSample);
        }

        if (!m_initialized) {
            m_initialized = true;
            m_lastWallSeconds = wallSeconds;
            m_lastMonotonicSeconds = monotonicSeconds;
            return Observation::Accepted;
        }

        if (monotonicSeconds < m_lastMonotonicSeconds) {
            return fail(ContinuityFailure::MonotonicWentBackwards);
        }
        if (wallSeconds < m_lastWallSeconds) {
            return fail(ContinuityFailure::WallClockWentBackwards);
        }

        const uint64_t elapsed = monotonicSeconds - m_lastMonotonicSeconds;
        if (elapsed > static_cast<uint64_t>(INT64_MAX) ||
            m_lastWallSeconds > INT64_MAX - static_cast<int64_t>(elapsed)) {
            return fail(ContinuityFailure::InvalidSample);
        }

        const int64_t expectedWallSeconds = m_lastWallSeconds + static_cast<int64_t>(elapsed);
        // PIT is configured for an approximate 100 Hz. Allow 500 ppm drift,
        // plus two seconds for whole-second RTC resolution, bounded at five
        // minutes so a long uptime cannot make arbitrary jumps acceptable.
        uint64_t driftAllowance = elapsed / 2000u;
        if (driftAllowance > 300u) driftAllowance = 300u;
        const int64_t allowance = static_cast<int64_t>(2u + driftAllowance);
        if (wallSeconds > expectedWallSeconds && wallSeconds - expectedWallSeconds > allowance) {
            return fail(ContinuityFailure::ExcessiveForwardJump);
        }
        if (wallSeconds < expectedWallSeconds && expectedWallSeconds - wallSeconds > allowance) {
            return fail(ContinuityFailure::WallClockWentBackwards);
        }

        m_lastWallSeconds = wallSeconds;
        m_lastMonotonicSeconds = monotonicSeconds;
        return Observation::Accepted;
    }

    Observation source_failure()
    {
        return fail(ContinuityFailure::SourceFailure);
    }

    bool failed() const { return m_failed; }
    ContinuityFailure failure() const { return m_failure; }

private:
    Observation fail(ContinuityFailure failure)
    {
        if (!m_failed) {
            m_failed = true;
            m_failure = failure;
        }
        return result_for_failure(m_failure);
    }

    static Observation result_for_failure(ContinuityFailure failure)
    {
        switch (failure) {
        case ContinuityFailure::MonotonicWentBackwards:
        case ContinuityFailure::WallClockWentBackwards:
            return Observation::RejectedRollback;
        case ContinuityFailure::ExcessiveForwardJump:
            return Observation::RejectedForwardJump;
        default:
            return Observation::RejectedInvalid;
        }
    }

    bool m_initialized;
    bool m_failed;
    int64_t m_lastWallSeconds;
    uint64_t m_lastMonotonicSeconds;
    ContinuityFailure m_failure;
};

struct AuthenticatedEvidence {
    bool metadataPresent;
    bool sourceIdentityPresent;
    bool signatureVerified;
    bool nonceMatched;
    int64_t earliestUnixSeconds;
    int64_t latestUnixSeconds;
    uint64_t receivedAtMonotonicSeconds;
    uint64_t maximumAgeSeconds;
};

enum class EvidenceResult : uint8_t {
    Accepted,
    InvalidMetadata,
    InvalidSignature,
    NonceMissingOrReplayed,
    Expired,
    MonotonicWentBackwards
};

// Contract check for a future authenticated time provider. A caller may set
// signatureVerified only after cryptographic verification, and nonceMatched
// only after matching a newly generated request nonce. No current platform
// source calls this helper or promotes the clock to Authenticated.
inline EvidenceResult assess_authenticated_evidence(const AuthenticatedEvidence& evidence,
                                                    uint64_t nowMonotonicSeconds)
{
    if (!evidence.metadataPresent || !evidence.sourceIdentityPresent ||
        evidence.earliestUnixSeconds < kPlausibleMinimumUnixSeconds ||
        evidence.latestUnixSeconds >= kPlausibleMaximumUnixSeconds ||
        evidence.earliestUnixSeconds > evidence.latestUnixSeconds ||
        evidence.maximumAgeSeconds == 0) {
        return EvidenceResult::InvalidMetadata;
    }
    if (!evidence.signatureVerified) return EvidenceResult::InvalidSignature;
    if (!evidence.nonceMatched) return EvidenceResult::NonceMissingOrReplayed;
    if (nowMonotonicSeconds < evidence.receivedAtMonotonicSeconds) {
        return EvidenceResult::MonotonicWentBackwards;
    }
    if (nowMonotonicSeconds - evidence.receivedAtMonotonicSeconds > evidence.maximumAgeSeconds) {
        return EvidenceResult::Expired;
    }
    return EvidenceResult::Accepted;
}

inline bool satisfies_requirement(State state, bool authenticatedRequired)
{
    if (state == State::Authenticated) return true;
    return !authenticatedRequired && state == State::Plausible;
}

enum class CertificateTimeResult : uint8_t {
    Valid,
    ClockInvalid,
    ClockRollbackSuspected,
    InsufficientAssurance,
    CertificateNotYetValid,
    CertificateExpired,
    InvalidCertificateInterval
};

inline CertificateTimeResult check_certificate_time(State state,
                                                     bool authenticatedRequired,
                                                     int64_t nowUnixSeconds,
                                                     int64_t notBeforeUnixSeconds,
                                                     int64_t notAfterUnixSeconds)
{
    if (state == State::RollbackSuspected) return CertificateTimeResult::ClockRollbackSuspected;
    if (state == State::Invalid) return CertificateTimeResult::ClockInvalid;
    if (!satisfies_requirement(state, authenticatedRequired)) {
        return CertificateTimeResult::InsufficientAssurance;
    }
    if (notAfterUnixSeconds <= notBeforeUnixSeconds) {
        return CertificateTimeResult::InvalidCertificateInterval;
    }
    if (nowUnixSeconds < notBeforeUnixSeconds) return CertificateTimeResult::CertificateNotYetValid;
    if (nowUnixSeconds > notAfterUnixSeconds) return CertificateTimeResult::CertificateExpired;
    return CertificateTimeResult::Valid;
}

inline const char* continuity_failure_name(ContinuityFailure failure)
{
    switch (failure) {
    case ContinuityFailure::None: return "none";
    case ContinuityFailure::InvalidSample: return "invalid-rtc-sample";
    case ContinuityFailure::SourceFailure: return "rtc-read-failed";
    case ContinuityFailure::MonotonicWentBackwards: return "monotonic-source-went-backwards";
    case ContinuityFailure::WallClockWentBackwards: return "wall-clock-went-backwards";
    case ContinuityFailure::ExcessiveForwardJump: return "excessive-forward-jump";
    default: return "unknown-clock-failure";
    }
}

} // namespace clockassurance
} // namespace gxos
