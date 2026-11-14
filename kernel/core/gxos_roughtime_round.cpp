#include "include/gxos_roughtime_round.h"

#if !defined(GXOS_ROUGHTIME_ROUND_TESTING)
#include "include/kernel/pit.h"
#endif

namespace {

static const uint64_t kPitTickNanoseconds = 10000000ULL;
static bool s_round_active = false;

#if !defined(GXOS_ROUGHTIME_ROUND_TESTING)
static bool monotonic_now(uint64_t* now_ns);
#endif

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

static bool token_is_nonzero(const gxos_roughtime_verified_evidence& token)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&token);
    uint8_t aggregate = 0;
    for (size_t i = 0; i < sizeof(token); ++i) aggregate |= bytes[i];
    return aggregate != 0;
}

class RoundLease {
public:
    RoundLease() : m_acquired(!s_round_active)
    {
        if (m_acquired) s_round_active = true;
    }
    ~RoundLease() { if (m_acquired) s_round_active = false; }
    bool acquired() const { return m_acquired; }

private:
    bool m_acquired;
};

typedef gxos_roughtime_acquisition_status (*SourceProvider)(
    void*, const gxos_roughtime_round_source*, gxos_roughtime_v1_acquisition_result*);

static bool build_policy(const gxos_roughtime_round_configuration& configuration,
                         gxos_roughtime_agreement_policy* policy,
                         uint8_t* distinct_operator_count)
{
    if (!policy || !distinct_operator_count || configuration.policy_identity == 0 ||
        configuration.source_count != GXOS_ROUGHTIME_ROUND_MAX_SOURCES ||
        configuration.quorum != 2 || configuration.reserved != 0 ||
        configuration.maximum_age_seconds == 0 ||
        configuration.maximum_age_seconds > GXOS_ROUGHTIME_AGREEMENT_MAX_POLICY_AGE_SECONDS ||
        configuration.maximum_source_uncertainty_seconds == 0 ||
        configuration.maximum_source_uncertainty_seconds > 302U ||
        configuration.maximum_transport_delay_ms == 0 ||
        configuration.maximum_transport_delay_ms > 2000U ||
        configuration.maximum_forward_jump_seconds == 0 ||
        configuration.maximum_forward_jump_seconds >
            GXOS_ROUGHTIME_AGREEMENT_MAX_FORWARD_JUMP_SECONDS) {
        return false;
    }

    clear_bytes(policy, sizeof(*policy));
    policy->policy_identity = configuration.policy_identity;
    policy->profile = GXOS_ROUGHTIME_AGREEMENT_PROFILE_UNREVIEWED_CONFIG;
    policy->source_count = configuration.source_count;
    policy->quorum = configuration.quorum;
    policy->maximum_age_seconds = configuration.maximum_age_seconds;
    policy->maximum_source_uncertainty_seconds =
        configuration.maximum_source_uncertainty_seconds;
    policy->maximum_transport_delay_ms = configuration.maximum_transport_delay_ms;
    policy->maximum_forward_jump_seconds = configuration.maximum_forward_jump_seconds;

    uint32_t operators[GXOS_ROUGHTIME_ROUND_MAX_SOURCES] = {};
    uint8_t operator_count = 0;
    for (uint8_t i = 0; i < configuration.source_count; ++i) {
        const gxos_roughtime_round_source& source = configuration.sources[i];
        if (source.server_ipv4 == 0 || source.server_port == 0 ||
            source.operator_identity == 0 || source.reserved != 0) {
            return false;
        }
        for (uint8_t previous = 0; previous < i; ++previous) {
            const gxos_roughtime_round_source& prior = configuration.sources[previous];
            if (source.server_ipv4 == prior.server_ipv4 &&
                source.server_port == prior.server_port) {
                return false;
            }
            if (equal_bytes(source.server_public_key, prior.server_public_key, 32)) {
                return false;
            }
        }

        policy->sources[i].operator_identity = source.operator_identity;
        for (size_t byte = 0; byte < 32; ++byte) {
            policy->sources[i].server_public_key[byte] = source.server_public_key[byte];
        }
        bool operator_seen = false;
        for (uint8_t previous = 0; previous < operator_count; ++previous) {
            if (operators[previous] == source.operator_identity) operator_seen = true;
        }
        if (!operator_seen) operators[operator_count++] = source.operator_identity;
    }
    if (operator_count < configuration.quorum) return false;
    *distinct_operator_count = operator_count;
    return true;
}

static gxos_roughtime_round_status run_round(
    const gxos_roughtime_round_configuration* configuration,
    uint64_t now_monotonic_ns,
    bool sample_now_after_acquisition,
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_round_result* result,
    SourceProvider provider,
    void* provider_context)
{
    if (!result) return GXOS_ROUGHTIME_ROUND_INVALID_CONFIGURATION;
    clear_bytes(result, sizeof(*result));
    result->status = GXOS_ROUGHTIME_ROUND_INVALID_CONFIGURATION;
    result->agreement.status = GXOS_ROUGHTIME_AGREEMENT_INVALID_SOURCE_POLICY;
    if (!configuration || !provider) return result->status;

    gxos_roughtime_agreement_policy policy = {};
    uint8_t configured_operator_count = 0;
    if (!build_policy(*configuration, &policy, &configured_operator_count)) {
        return result->status;
    }

    RoundLease lease;
    if (!lease.acquired()) {
        result->status = GXOS_ROUGHTIME_ROUND_BUSY;
        return result->status;
    }

    uint8_t evidence_count = 0;
    for (uint8_t i = 0; i < configuration->source_count; ++i) {
        const gxos_roughtime_round_source& source = configuration->sources[i];
        gxos_roughtime_round_source_result& diagnostic = result->sources[i];
        diagnostic.status = GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
        diagnostic.server_ipv4 = source.server_ipv4;
        diagnostic.server_port = source.server_port;
        ++result->attempted_source_count;

        gxos_roughtime_v1_acquisition_result acquired = {};
        const gxos_roughtime_acquisition_status status = provider(
            provider_context, &source, &acquired);
        diagnostic.status = status;
        if (status != GXOS_ROUGHTIME_ACQUISITION_SUCCESS) {
            clear_bytes(&acquired, sizeof(acquired));
            continue;
        }

        if (!acquired.clock_evidence_contract_valid ||
            acquired.request_bytes != GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES ||
            acquired.response_bytes == 0 ||
            acquired.response_bytes > GXOS_ROUGHTIME_V1_MAX_UDP_RESPONSE_BYTES ||
            !token_is_nonzero(acquired.agreement_evidence)) {
            diagnostic.status = GXOS_ROUGHTIME_ACQUISITION_CLOCK_EVIDENCE_REJECTED;
            clear_bytes(&acquired, sizeof(acquired));
            continue;
        }

        diagnostic.status = GXOS_ROUGHTIME_ACQUISITION_SUCCESS;
        diagnostic.verified = 1;
        diagnostic.request_bytes = acquired.request_bytes;
        diagnostic.response_bytes = acquired.response_bytes;
        for (size_t byte = 0; byte < 32; ++byte) {
            diagnostic.server_key_identity[byte] =
                acquired.evidence.server_key_identity[byte];
        }
        clear_bytes(&result->evidence[evidence_count],
                    sizeof(result->evidence[evidence_count]));
        for (size_t byte = 0; byte < sizeof(acquired.agreement_evidence); ++byte) {
            reinterpret_cast<uint8_t*>(&result->evidence[evidence_count])[byte] =
                reinterpret_cast<const uint8_t*>(&acquired.agreement_evidence)[byte];
        }
        ++evidence_count;
        ++result->verified_response_count;
        clear_bytes(&acquired, sizeof(acquired));
    }

    uint32_t verified_operators[GXOS_ROUGHTIME_ROUND_MAX_SOURCES] = {};
    uint8_t verified_operator_count = 0;
    for (uint8_t i = 0; i < configuration->source_count; ++i) {
        if (!result->sources[i].verified) continue;
        const uint32_t operator_identity = configuration->sources[i].operator_identity;
        bool seen = false;
        for (uint8_t j = 0; j < verified_operator_count; ++j) {
            if (verified_operators[j] == operator_identity) seen = true;
        }
        if (!seen) verified_operators[verified_operator_count++] = operator_identity;
    }
    result->independent_operator_count = verified_operator_count;

#if !defined(GXOS_ROUGHTIME_ROUND_TESTING)
    if (sample_now_after_acquisition && !monotonic_now(&now_monotonic_ns)) {
        result->status = GXOS_ROUGHTIME_ROUND_MONOTONIC_FAILURE;
        result->agreement.status = GXOS_ROUGHTIME_AGREEMENT_INVALID_EVIDENCE;
        return result->status;
    }
#else
    (void)sample_now_after_acquisition;
#endif

    result->agreement.status = gxos_roughtime_agreement_evaluate(
        &policy, result->evidence, evidence_count, now_monotonic_ns,
        continuity, &result->agreement);
    result->status = GXOS_ROUGHTIME_ROUND_EVALUATED;
    (void)configured_operator_count;
    return result->status;
}

#if !defined(GXOS_ROUGHTIME_ROUND_TESTING)
static gxos_roughtime_acquisition_status acquire_source(
    void*, const gxos_roughtime_round_source* source,
    gxos_roughtime_v1_acquisition_result* result)
{
    return kernel::roughtime::acquire_ipv4(source->server_ipv4, source->server_port,
                                            source->server_public_key, result);
}

static bool monotonic_now(uint64_t* now_ns)
{
    if (!now_ns) return false;
    const uint64_t ticks = kernel::pit::ticks();
    if (ticks > (~static_cast<uint64_t>(0) / kPitTickNanoseconds)) return false;
    *now_ns = ticks * kPitTickNanoseconds;
    return true;
}
#endif

} // namespace

extern "C" const char* gxos_roughtime_round_status_name(
    gxos_roughtime_round_status status)
{
    switch (status) {
    case GXOS_ROUGHTIME_ROUND_EVALUATED: return "evaluated";
    case GXOS_ROUGHTIME_ROUND_INVALID_CONFIGURATION: return "invalid-configuration";
    case GXOS_ROUGHTIME_ROUND_BUSY: return "round-already-active";
    case GXOS_ROUGHTIME_ROUND_MONOTONIC_FAILURE: return "monotonic-clock-failure";
    default: return "unknown-round-status";
    }
}

#if !defined(GXOS_ROUGHTIME_ROUND_TESTING)
namespace kernel {
namespace roughtime {

gxos_roughtime_round_status acquire_agreement_round(
    const gxos_roughtime_round_configuration* configuration,
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_round_result* result)
{
    return run_round(configuration, 0, true, continuity, result, acquire_source, nullptr);
}

} // namespace roughtime
} // namespace kernel
#endif

#if defined(GXOS_ROUGHTIME_ROUND_TESTING)
extern "C" gxos_roughtime_round_status gxos_roughtime_round_test_run(
    const gxos_roughtime_round_configuration* configuration,
    uint64_t now_monotonic_ns,
    gxos_roughtime_agreement_continuity* continuity,
    gxos_roughtime_round_result* result,
    gxos_roughtime_round_test_provider_fn provider,
    void* provider_context)
{
    return run_round(configuration, now_monotonic_ns, false, continuity, result,
                     provider, provider_context);
}
#endif
