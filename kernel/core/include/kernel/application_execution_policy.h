#ifndef KERNEL_APPLICATION_EXECUTION_POLICY_H
#define KERNEL_APPLICATION_EXECUTION_POLICY_H

#include <stdint.h>

namespace kernel {
namespace application_policy {

static const uint16_t kApplicationIdentityVersion = 1u;
static const uint16_t kPublisherIdentityVersion = 1u;
static const uint16_t kEvaluationInputVersion = 1u;
static const uint16_t kEvaluationResultVersion = 1u;
static const uint32_t kAppIdCapacity = 80u;
static const uint32_t kPackageVersionCapacity = 32u;
static const uint32_t kArchitectureCapacity = 16u;
static const uint32_t kPublisherDisplayNameCapacity = 96u;

enum class SignatureStatus : uint8_t {
    Unknown,
    Unsigned,
    Verified,
    Invalid,
    VerificationUnavailable
};

enum class RevocationStatus : uint8_t {
    Unknown,
    NotRevoked,
    Revoked
};

enum class TrustClassification : uint8_t {
    UnknownOrUnverifiable,
    TrustedApplication,
    TrustedPublisher,
    // Block classifications are applied after administrator-local policy.
    // Higher-priority platform/catalog blocks use authoritativeBlock.
    ExplicitlyBlockedApplication,
    ExplicitlyBlockedPublisher,
    SignedUnknownPublisher,
    UnsignedApplication
};

// Display text is descriptive only. A publisher is authoritative only when
// its public-key identity and verified signature status are supplied by a
// trusted verifier; a name such as "guideXOS" conveys no signing authority.
struct PublisherIdentityV1 {
    uint16_t version;
    SignatureStatus signatureStatus;
    RevocationStatus revocationStatus;
    uint8_t hasPublicKeyIdentity;
    uint8_t reserved;
    char displayName[kPublisherDisplayNameCapacity];
    uint8_t publicKeyIdentitySha256[32];
};

// The App Model ID is the canonical application key. Package version and
// digest are optional until a trusted source supplies them independently.
struct ApplicationIdentityV1 {
    uint16_t version;
    uint8_t hasExecutableDigest;
    uint8_t reserved;
    char appId[kAppIdCapacity];
    char packageVersion[kPackageVersionCapacity];
    char architecture[kArchitectureCapacity];
    uint8_t executableSha256[32];
    PublisherIdentityV1 publisher;
};

enum class RuleEffect : uint8_t {
    None,
    Allow,
    Block,
    RequireAdministrator
};

struct EvaluationInputV1 {
    uint16_t version;
    uint8_t mandatoryReleaseRejected;
    uint8_t authoritativeBlock;
    RuleEffect administratorLocalRule;
    RuleEffect unknownApplicationRule;
    TrustClassification trust;
    uint8_t trustedApplicationDigestMatch;
    uint8_t administratorAuthorized;
    uint8_t reserved;
};

enum class PolicyResult : uint8_t {
    Allowed,
    Blocked,
    RequiresAdministrator,
    UnknownTrust,
    VerificationUnavailable,
    InvalidApplicationIdentity,
    ReleaseProfileRejected,
    InvalidPolicyInput
};

struct EvaluationResultV1 {
    uint16_t version;
    PolicyResult result;
    TrustClassification trust;
    uint8_t launchAllowed;
    uint8_t publisherCryptographicallyVerified;
    uint8_t reserved[2];

    bool allowed() const { return launchAllowed != 0u; }
};

inline bool boundedText(const char* value, uint32_t capacity, bool required)
{
    if (!value || capacity == 0u) return false;
    for (uint32_t i = 0u; i < capacity; ++i) {
        if (value[i] == '\0') return !required || i != 0u;
        const uint8_t ch = static_cast<uint8_t>(value[i]);
        if (ch < 0x20u || ch == 0x7Fu) return false;
    }
    return false;
}

inline bool validCanonicalAppId(const char* value)
{
    // App Model IDs are exact, case-sensitive identities today. Match the
    // shared model's bounded printable-text contract instead of imposing a
    // second grammar that could reject an existing registered identity.
    return boundedText(value, kAppIdCapacity, true);
}

inline bool validApplicationIdentity(const ApplicationIdentityV1& identity)
{
    if (identity.version != kApplicationIdentityVersion ||
        !validCanonicalAppId(identity.appId) ||
        !boundedText(identity.architecture, kArchitectureCapacity, true) ||
        !boundedText(identity.packageVersion, kPackageVersionCapacity, false) ||
        identity.hasExecutableDigest > 1u) return false;
    if (identity.reserved != 0u ||
        identity.publisher.version != kPublisherIdentityVersion ||
        !boundedText(identity.publisher.displayName,
            kPublisherDisplayNameCapacity, false) ||
        identity.publisher.hasPublicKeyIdentity > 1u ||
        identity.publisher.reserved != 0u) return false;
    return true;
}

inline EvaluationResultV1 result(PolicyResult decision,
                                 TrustClassification trust,
                                 bool allowed,
                                 bool verifiedPublisher = false)
{
    EvaluationResultV1 output{};
    output.version = kEvaluationResultVersion;
    output.result = decision;
    output.trust = trust;
    output.launchAllowed = allowed ? 1u : 0u;
    output.publisherCryptographicallyVerified = verifiedPublisher ? 1u : 0u;
    return output;
}

// The evaluation order is intentional and stable:
// release restrictions, authority blocks/revocations, administrator policy,
// verified application/publisher trust, then unknown/unsigned policy.
// Evaluation itself does not mint authority; callers must obtain all inputs
// from trusted kernel providers.
inline EvaluationResultV1 evaluate(const ApplicationIdentityV1& identity,
                                   const EvaluationInputV1& input)
{
    const TrustClassification trust = input.trust;
    if (!validApplicationIdentity(identity))
        return result(PolicyResult::InvalidApplicationIdentity, trust, false);
    if (input.version != kEvaluationInputVersion ||
        static_cast<uint8_t>(input.administratorLocalRule) >
            static_cast<uint8_t>(RuleEffect::RequireAdministrator) ||
        static_cast<uint8_t>(input.unknownApplicationRule) >
            static_cast<uint8_t>(RuleEffect::RequireAdministrator) ||
        static_cast<uint8_t>(input.trust) >
            static_cast<uint8_t>(TrustClassification::UnsignedApplication) ||
        static_cast<uint8_t>(identity.publisher.signatureStatus) >
            static_cast<uint8_t>(SignatureStatus::VerificationUnavailable) ||
        static_cast<uint8_t>(identity.publisher.revocationStatus) >
            static_cast<uint8_t>(RevocationStatus::Revoked) ||
        input.mandatoryReleaseRejected > 1u || input.authoritativeBlock > 1u ||
        input.trustedApplicationDigestMatch > 1u ||
        input.administratorAuthorized > 1u || input.reserved != 0u)
        return result(PolicyResult::InvalidPolicyInput, trust, false);
    if (input.mandatoryReleaseRejected)
        return result(PolicyResult::ReleaseProfileRejected, trust, false);
    if (input.authoritativeBlock ||
        identity.publisher.revocationStatus == RevocationStatus::Revoked)
        return result(PolicyResult::Blocked, trust, false);

    if (input.administratorLocalRule == RuleEffect::Block)
        return result(PolicyResult::Blocked, trust, false);
    if (input.administratorLocalRule == RuleEffect::RequireAdministrator &&
        !input.administratorAuthorized)
        return result(PolicyResult::RequiresAdministrator, trust, false);
    if (input.administratorLocalRule == RuleEffect::Allow ||
        (input.administratorLocalRule == RuleEffect::RequireAdministrator &&
         input.administratorAuthorized))
        return result(PolicyResult::Allowed, trust, true);

    if (trust == TrustClassification::ExplicitlyBlockedApplication ||
        trust == TrustClassification::ExplicitlyBlockedPublisher)
        return result(PolicyResult::Blocked, trust, false);

    const bool verifiedPublisher =
        identity.publisher.signatureStatus == SignatureStatus::Verified &&
        identity.publisher.hasPublicKeyIdentity != 0u &&
        identity.publisher.revocationStatus == RevocationStatus::NotRevoked;
    if (trust == TrustClassification::TrustedApplication &&
        input.trustedApplicationDigestMatch != 0u && identity.hasExecutableDigest != 0u)
        return result(PolicyResult::Allowed, trust, true, false);
    if (verifiedPublisher && trust == TrustClassification::TrustedPublisher)
        return result(PolicyResult::Allowed, trust, true, true);

    if (identity.publisher.signatureStatus == SignatureStatus::Invalid)
        return result(PolicyResult::Blocked, trust, false);

    if (input.unknownApplicationRule == RuleEffect::Block)
        return result(PolicyResult::Blocked, trust, false);
    if (input.unknownApplicationRule == RuleEffect::RequireAdministrator &&
        !input.administratorAuthorized)
        return result(PolicyResult::RequiresAdministrator, trust, false);
    if (input.unknownApplicationRule == RuleEffect::Allow ||
        (input.unknownApplicationRule == RuleEffect::RequireAdministrator &&
         input.administratorAuthorized))
        return result(PolicyResult::Allowed, trust, true);

    const bool unavailable =
        identity.publisher.signatureStatus == SignatureStatus::VerificationUnavailable ||
        identity.publisher.signatureStatus == SignatureStatus::Unknown;
    return result(unavailable ? PolicyResult::VerificationUnavailable
                              : PolicyResult::UnknownTrust,
        trust, true);
}

inline const char* resultName(PolicyResult value)
{
    switch (value) {
        case PolicyResult::Allowed: return "Allowed";
        case PolicyResult::Blocked: return "Blocked";
        case PolicyResult::RequiresAdministrator: return "RequiresAdministrator";
        case PolicyResult::UnknownTrust: return "UnknownTrust";
        case PolicyResult::VerificationUnavailable: return "VerificationUnavailable";
        case PolicyResult::InvalidApplicationIdentity: return "InvalidApplicationIdentity";
        case PolicyResult::ReleaseProfileRejected: return "ReleaseProfileRejected";
        case PolicyResult::InvalidPolicyInput: return "InvalidPolicyInput";
        default: return "UnknownPolicyResult";
    }
}

} // namespace application_policy
} // namespace kernel

#endif // KERNEL_APPLICATION_EXECUTION_POLICY_H
