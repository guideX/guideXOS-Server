#include "gxos_clock_assurance.h"

#include <cassert>

using namespace gxos::clockassurance;

int main()
{
    const int64_t base = 1794272400ll; // 2026-11-10T09:00:00Z

    WithinBootContinuity validRtc;
    assert(validRtc.observe(base, 0) == Observation::Accepted);
    assert(validRtc.observe(base + 10, 10) == Observation::Accepted);
    assert(!validRtc.failed());
    assert(satisfies_requirement(State::Plausible, false));
    assert(!satisfies_requirement(State::Plausible, true));
    assert(check_certificate_time(State::Plausible, false, base, base - 60, base + 60) ==
        CertificateTimeResult::Valid);
    assert(check_certificate_time(State::Plausible, false, base, base + 1, base + 60) ==
        CertificateTimeResult::CertificateNotYetValid);
    assert(check_certificate_time(State::Plausible, false, base, base - 60, base - 1) ==
        CertificateTimeResult::CertificateExpired);
    assert(check_certificate_time(State::Plausible, true, base, base - 60, base + 60) ==
        CertificateTimeResult::InsufficientAssurance);
    assert(check_certificate_time(State::RollbackSuspected, false, base, base - 60, base + 60) ==
        CertificateTimeResult::ClockRollbackSuspected);

    WithinBootContinuity invalidRtc;
    assert(invalidRtc.observe(kPlausibleMinimumUnixSeconds - 1, 0) == Observation::RejectedInvalid);
    assert(invalidRtc.failed());

    WithinBootContinuity backward;
    assert(backward.observe(base, 100) == Observation::Accepted);
    assert(backward.observe(base - 1, 101) == Observation::RejectedRollback);
    assert(backward.failure() == ContinuityFailure::WallClockWentBackwards);
    assert(backward.observe(base, 102) == Observation::RejectedRollback);

    WithinBootContinuity forward;
    assert(forward.observe(base, 100) == Observation::Accepted);
    assert(forward.observe(base + 3600, 101) == Observation::RejectedForwardJump);
    assert(forward.failure() == ContinuityFailure::ExcessiveForwardJump);

    WithinBootContinuity sourceFailure;
    assert(sourceFailure.source_failure() == Observation::RejectedInvalid);
    assert(sourceFailure.failure() == ContinuityFailure::SourceFailure);

    AuthenticatedEvidence evidence = {
        true, true, true, true,
        base - 2, base + 2,
        500, 60
    };
    assert(assess_authenticated_evidence(evidence, 560) == EvidenceResult::Accepted);
    assert(assess_authenticated_evidence(evidence, 561) == EvidenceResult::Expired);
    assert(assess_authenticated_evidence(evidence, 499) == EvidenceResult::MonotonicWentBackwards);
    assert(satisfies_requirement(State::Authenticated, true));

    evidence.metadataPresent = false;
    assert(assess_authenticated_evidence(evidence, 501) == EvidenceResult::InvalidMetadata);
    evidence.metadataPresent = true;
    evidence.sourceIdentityPresent = false;
    assert(assess_authenticated_evidence(evidence, 501) == EvidenceResult::InvalidMetadata);
    evidence.sourceIdentityPresent = true;
    evidence.signatureVerified = false;
    assert(assess_authenticated_evidence(evidence, 501) == EvidenceResult::InvalidSignature);
    evidence.signatureVerified = true;
    evidence.nonceMatched = false;
    assert(assess_authenticated_evidence(evidence, 501) == EvidenceResult::NonceMissingOrReplayed);

    // A new boot has no continuity with the previous tracker and starts only
    // as plausible RTC time; secure cross-boot state is not synthesized.
    WithinBootContinuity afterReboot;
    assert(afterReboot.observe(base + 3600, 0) == Observation::Accepted);
    assert(satisfies_requirement(State::Plausible, false));
    assert(!satisfies_requirement(State::Plausible, true));

    return 0;
}
