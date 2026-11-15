#pragma once

#include <stdint.h>

#include "gxos_roughtime_round.h"

namespace gxos {
namespace clockassurance {

static const uint64_t kTimeEstimateNanosecondsPerSecond = 1000000000ULL;
static const uint64_t kTimeEstimatePitTickNanoseconds = 10000000ULL;
static const uint32_t kFixtureTimeEstimateMaximumAgeSeconds = 300U;
static const uint32_t kFixtureTimeEstimateMaximumUncertaintySeconds = 30U;
static const uint32_t kControlledFixtureTlsTrustScopeIdentity = 0x1701U;

enum class TimeEstimateState : uint8_t {
    NoEvidence,
    Fresh,
    Advanced,
    Expired,
    Conflicting,
    ContinuityFailure,
    RollbackSuspected,
    Invalidated,
    Rejected
};

enum class TimeEstimateScope : uint8_t {
    None,
    FixtureOnly,
    ProductionApproved
};

enum class TimeEstimateReason : uint8_t {
    None,
    NoAuthenticatedPersistentState,
    AcceptedFixtureAgreement,
    InvalidRound,
    NotFixturePolicy,
    InsufficientIndependentSources,
    ExcessiveUncertainty,
    InvalidInterval,
    Expired,
    CounterOverflow,
    MonotonicWentBackwards,
    SourcePolicyChanged,
    ConflictingEvidence,
    RollbackDetected,
    ExcessiveForwardMovement,
    UncertaintyLimitExceeded,
    ClockDiscontinuity,
    ExplicitInvalidation
};

enum class TimeEstimateAcceptResult : uint8_t {
    Accepted,
    InvalidRound,
    NotFixturePolicy,
    InsufficientIndependentSources,
    ExcessiveUncertainty,
    InvalidInterval,
    Expired,
    CounterOverflow,
    SourcePolicyChanged,
    ConflictingEvidence,
    RollbackDetected,
    ExcessiveForwardMovement,
    ContinuityFailure
};

enum class TlsTimeEstimateResult : uint8_t {
    Ready,
    NoEvidence,
    WrongScope,
    WrongSourcePolicy,
    InvalidInterval,
    ExcessiveUncertainty,
    Expired,
    MonotonicRollback,
    ContinuityFailure,
    LeaseChanged,
    CounterOverflow
};

// Public diagnostic view. It carries an interval and its uncertainty rather
// than exposing a falsely precise point timestamp. Fixture evidence is never
// a production clock classification or a TLS authorization.
struct TimeEstimateInfo {
    TimeEstimateState state;
    TimeEstimateScope scope;
    TimeEstimateReason reason;
    uint8_t available;
    uint8_t agreementSatisfied;
    uint8_t configuredSourceCount;
    uint8_t acceptedSourceCount;
    uint8_t distinctOperatorCount;
    uint8_t operatorIdentityCount;
    uint8_t keyIdentityCount;
    uint8_t productionPolicyAccepted;
    uint8_t systemClockChanged;
    uint8_t publicHttpsEnabled;
    uint32_t policyIdentity;
    uint32_t operatorIdentities[GXOS_ROUGHTIME_ROUND_MAX_SOURCES];
    uint8_t serverKeyIdentities[GXOS_ROUGHTIME_ROUND_MAX_SOURCES][32];
    uint64_t agreedEarliestUnixSeconds;
    uint64_t agreedLatestUnixSeconds;
    uint64_t currentEarliestUnixSeconds;
    uint64_t currentLatestUnixSeconds;
    uint64_t measurementAnchorMonotonicNs;
    uint64_t lastObservedMonotonicNs;
    uint64_t maximumAgeSeconds;
    uint64_t evidenceAgeAtAnchorSeconds;
    uint64_t maximumUncertaintySeconds;
    uint64_t currentUncertaintySeconds;
    uint64_t remainingFreshnessSeconds;
};

// Per-connection lease returned only from the accepted time-estimate store.
// Callers cannot submit UTC bounds to create one; refresh re-reads the store
// and rejects leases invalidated or replaced during certificate verification.
struct TlsTimeEstimateLease {
    TlsTimeEstimateResult result;
    TimeEstimateState state;
    TimeEstimateScope scope;
    TimeEstimateReason rejectionReason;
    uint8_t available;
    uint32_t sourcePolicyIdentity;
    uint32_t trustScopeIdentity;
    uint64_t earliestUnixSeconds;
    uint64_t latestUnixSeconds;
    uint64_t measurementAnchorMonotonicNs;
    uint64_t observedMonotonicNs;
    uint64_t remainingFreshnessSeconds;
    uint64_t uncertaintySeconds;
    uint64_t generation;
};

class TimeEstimateStore {
public:
    // Accept only NAVHTTPS15's bounded, pinned fixture configuration and the
    // corresponding evaluator result. The input must be the output of
    // acquire_agreement_round(); raw timestamps and verifier evidence are not
    // accepted by this interface.
    TimeEstimateAcceptResult accept_fixture_round(
        const gxos_roughtime_round_configuration& configuration,
        const gxos_roughtime_round_result& round,
        uint64_t nowMonotonicNs);

    // Advance the retained interval with bounded monotonic arithmetic. A
    // decreasing counter latches rollback until reboot.
    TimeEstimateReason query(uint64_t nowMonotonicNs, TimeEstimateInfo* output);

    // Issue and refresh a fixture-only lease for the explicitly controlled
    // HTTPS trust scope. Neither operation accepts caller-provided UTC bounds.
    TlsTimeEstimateResult request_tls_lease(uint64_t nowMonotonicNs,
                                            TlsTimeEstimateLease* output);
    TlsTimeEstimateResult refresh_tls_lease(const TlsTimeEstimateLease& lease,
                                            uint64_t nowMonotonicNs,
                                            TlsTimeEstimateLease* output);

    void invalidate(TimeEstimateReason reason);
    void report_clock_discontinuity();

#if defined(GXOS_TIME_ESTIMATE_TESTING)
    // Host harness only. Production callers cannot reset evidence state or
    // clear a latched rollback/continuity failure.
    void initialize_for_test();
#endif

private:
    friend TimeEstimateStore& time_estimate_store();
    // Called once by the singleton accessor from a zero-initialized object.
    void initialize();
    bool advance_generation();
    TimeEstimateInfo m_info;
    uint8_t m_latchedFailure;
    uint64_t m_expiryMonotonicNs;
    uint64_t m_generation;
};

TimeEstimateStore& time_estimate_store();
TlsTimeEstimateResult request_controlled_fixture_tls_time(
    uint64_t nowMonotonicNs, TlsTimeEstimateLease* output);
TlsTimeEstimateResult refresh_controlled_fixture_tls_time(
    const TlsTimeEstimateLease& lease, uint64_t nowMonotonicNs,
    TlsTimeEstimateLease* output);

const char* time_estimate_state_name(TimeEstimateState state);
const char* time_estimate_scope_name(TimeEstimateScope scope);
const char* time_estimate_reason_name(TimeEstimateReason reason);
const char* time_estimate_accept_result_name(TimeEstimateAcceptResult result);
const char* tls_time_estimate_result_name(TlsTimeEstimateResult result);

} // namespace clockassurance
} // namespace gxos
