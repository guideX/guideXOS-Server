#ifndef GXOS_TLS_DISTRUST_AFTER_H
#define GXOS_TLS_DISTRUST_AFTER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GxosTlsDistrustAfterPolicy {
    const char* certificateSha256LowerHex;
    int64_t cutoffUnixSeconds;
} GxosTlsDistrustAfterPolicy;

/*
 * Exact certificate DER SHA-256 identities and TLS cutoffs from pinned NSS
 * certdata.txt. Subject names and SPKI matches are deliberately insufficient.
 */
static const GxosTlsDistrustAfterPolicy GXOS_TLS_MOZILLA_DISTRUST_AFTER_POLICIES[] = {
    { "2530cc8e98321502bad96f9b1fba1b099e2d299e0f4548bb914f363bc0d4531f", INT64_C(20007475199) },
    { "40f6af0346a99aa1cd1d555a4e9cce62c7f9634603ee406615833dc8c8d00367", INT64_C(20228486399) },
    { "552f7bdcf1a7af9e6ce672017f4f12abf77240c78e761ac203d1d9d20ac89988", INT64_C(20165241599) },
    { "57de0583efd2b26e0361da99da9df4648def7ee8441c3b728afa9bcde0f9b26a", INT64_C(20146928400) },
};

#define GXOS_TLS_MOZILLA_DISTRUST_AFTER_POLICY_COUNT \
    (sizeof(GXOS_TLS_MOZILLA_DISTRUST_AFTER_POLICIES) / sizeof(GXOS_TLS_MOZILLA_DISTRUST_AFTER_POLICIES[0]))

static inline const GxosTlsDistrustAfterPolicy*
gxos_tls_distrust_after_policy_in_set(
    const char* fingerprintLowerHex,
    const GxosTlsDistrustAfterPolicy* policies,
    size_t policyCount)
{
    size_t index;
    if (fingerprintLowerHex == NULL || policies == NULL || strlen(fingerprintLowerHex) != 64u) return NULL;
    for (index = 0; index < policyCount; ++index) {
        if (strcmp(fingerprintLowerHex,
                   policies[index].certificateSha256LowerHex) == 0) {
            return &policies[index];
        }
    }
    return NULL;
}

static inline const GxosTlsDistrustAfterPolicy*
gxos_tls_mozilla_distrust_after_policy_for_fingerprint(const char* fingerprintLowerHex)
{
    return gxos_tls_distrust_after_policy_in_set(
        fingerprintLowerHex,
        GXOS_TLS_MOZILLA_DISTRUST_AFTER_POLICIES,
        GXOS_TLS_MOZILLA_DISTRUST_AFTER_POLICY_COUNT);
}

/* Leaf notBefore component: reject issuance strictly later than cutoff. */
static inline int gxos_tls_mozilla_distrust_after_rejects(
    int64_t leafNotBeforeUnixSeconds,
    int64_t cutoffUnixSeconds)
{
    return leafNotBeforeUnixSeconds > cutoffUnixSeconds;
}

/*
 * A TLS distrust-after decision is incomplete unless the required SCT
 * timestamp check was performed. Callers without verified SCT evidence must
 * reject the restricted path, even when leaf notBefore is before the cutoff.
 */
static inline int gxos_tls_mozilla_tls_distrust_after_rejects(
    int64_t leafNotBeforeUnixSeconds,
    int64_t cutoffUnixSeconds,
    int sctTimestampCheckEnforced)
{
    return !sctTimestampCheckEnforced ||
        gxos_tls_mozilla_distrust_after_rejects(
            leafNotBeforeUnixSeconds, cutoffUnixSeconds);
}

#ifdef __cplusplus
}
#endif

#endif /* GXOS_TLS_DISTRUST_AFTER_H */
