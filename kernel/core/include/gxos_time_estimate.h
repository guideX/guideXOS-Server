#pragma once

#include <stdint.h>

#include "gxos_roughtime_round.h"

namespace gxos {
namespace clockassurance {

static const uint64_t kTimeEstimateNanosecondsPerSecond = 1000000000ULL;
static const uint64_t kTimeEstimatePitTickNanoseconds = 10000000ULL;
static const uint32_t kFixtureTimeEstimateMaximumAgeSeconds = 300U;
static const uint32_t kFixtureTimeEstimateMaximumUncertaintySeconds = 30U;

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

class TimeEstimateStore {
public:
    // Call once for a zero-initialized store. Kept explicit so the kernel's
    // singleton has no dynamic initializer or C++ runtime dependency.
    void initialize();

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

    void invalidate(TimeEstimateReason reason);
    void report_clock_discontinuity();

private:
    TimeEstimateInfo m_info;
    uint8_t m_latchedFailure;
    uint64_t m_expiryMonotonicNs;
};

TimeEstimateStore& time_estimate_store();

const char* time_estimate_state_name(TimeEstimateState state);
const char* time_estimate_scope_name(TimeEstimateScope scope);
const char* time_estimate_reason_name(TimeEstimateReason reason);
const char* time_estimate_accept_result_name(TimeEstimateAcceptResult result);

} // namespace clockassurance
} // namespace gxos
