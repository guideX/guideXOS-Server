#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kernel/core/include/gxos_tls_distrust_after.h"
#include "mbedtls/platform.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

static int64_t s_test_time = INT64_C(1781481600); /* 2026-06-15 UTC */
static uint32_t s_test_rng = 0x21d157a9u;
static int s_sctTimestampCheckEnforced = 1;

void mbedtls_platform_zeroize(void *buffer, size_t length)
{
    volatile unsigned char *bytes = (volatile unsigned char *) buffer;
    while (length-- != 0) *bytes++ = 0;
}

psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t *context,
    uint8_t *output, size_t output_size, size_t *output_length)
{
    size_t index;
    (void) context;
    if (output == NULL || output_length == NULL) return PSA_ERROR_INVALID_ARGUMENT;
    for (index = 0; index < output_size; ++index) {
        s_test_rng ^= s_test_rng << 13;
        s_test_rng ^= s_test_rng >> 17;
        s_test_rng ^= s_test_rng << 5;
        output[index] = (uint8_t) s_test_rng;
    }
    *output_length = output_size;
    return PSA_SUCCESS;
}

void *gxos_mbedtls_platform_calloc_uninit(size_t count, size_t size) { return calloc(count, size); }
void gxos_mbedtls_platform_free_uninit(void *ptr) { free(ptr); }
int gxos_mbedtls_platform_vsnprintf_noop(char *buffer, size_t size, const char *format, va_list args)
{ return vsnprintf(buffer, size, format, args); }
int gxos_mbedtls_platform_snprintf_noop(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    int result;
    va_start(args, format);
    result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}
int gxos_mbedtls_platform_fprintf_noop(FILE *stream, const char *format, ...)
{
    va_list args;
    int result;
    va_start(args, format);
    result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}
void gxos_mbedtls_platform_exit_noop(int status) { (void) status; }
int64_t gxos_mbedtls_time_callback(int64_t *timer)
{
    if (timer != NULL) *timer = s_test_time;
    return s_test_time;
}
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *value, struct tm *result)
{
    time_t seconds;
    struct tm *shared;
    if (value == NULL || result == NULL) return NULL;
    seconds = (time_t) *value;
    shared = gmtime(&seconds);
    if (shared == NULL) return NULL;
    *result = *shared;
    return result;
}

typedef struct VerifyContext {
    const GxosTlsDistrustAfterPolicy *policies;
    size_t policyCount;
    int64_t cutoff;
    int sctTimestampCheckEnforced;
    int restrictedCertificateSeen;
    int restrictionSeenBeforeLeaf;
    int leafSeen;
    int distrustRejected;
    int callbackDepthOrderValid;
    int lastDepth;
    int callbackCount;
} VerifyContext;

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    int era;
    unsigned yoe, doy, doe;
    year -= month <= 2;
    era = (year >= 0 ? year : year - 399) / 400;
    yoe = (unsigned) (year - era * 400);
    doy = (153u * (month + (month > 2 ? (unsigned) -3 : 9u)) + 2u) / 5u + day - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t) era * 146097 + (int64_t) doe - 719468;
}

static int64_t certificate_not_before(const mbedtls_x509_crt *certificate)
{
    const mbedtls_x509_time *time;
    int64_t days;
    if (certificate == NULL) return INT64_MIN;
    time = &certificate->valid_from;
    if (time->year < 1 || time->year > 9999 || time->mon < 1 || time->mon > 12 ||
        time->day < 1 || time->day > 31 || time->hour < 0 || time->hour > 23 ||
        time->min < 0 || time->min > 59 || time->sec < 0 || time->sec > 59) return INT64_MIN;
    days = days_from_civil(time->year, (unsigned) time->mon, (unsigned) time->day);
    return days * INT64_C(86400) + (int64_t) time->hour * 3600 + (int64_t) time->min * 60 + time->sec;
}

static int distrust_after_verify_callback(void *opaque, mbedtls_x509_crt *certificate,
                                          int depth, uint32_t *flags)
{
    VerifyContext *context = (VerifyContext *) opaque;
    uint8_t fingerprint[32];
    size_t fingerprintLength = 0;
    char fingerprintHex[65];
    const GxosTlsDistrustAfterPolicy *policy;
    if (context == NULL || certificate == NULL || flags == NULL) return 0;
    if (context->callbackCount != 0 && depth > context->lastDepth) context->callbackDepthOrderValid = 0;
    context->lastDepth = depth;
    ++context->callbackCount;
    if (depth > 0 && certificate->raw.p != NULL && certificate->raw.len != 0) {
        if (psa_hash_compute(PSA_ALG_SHA_256, certificate->raw.p, certificate->raw.len,
                             fingerprint, sizeof(fingerprint), &fingerprintLength) != PSA_SUCCESS ||
            fingerprintLength != sizeof(fingerprint)) {
            *flags |= MBEDTLS_X509_BADCERT_OTHER;
            return 0;
        }
        for (size_t i = 0; i < sizeof(fingerprint); ++i) {
            static const char digits[] = "0123456789abcdef";
            fingerprintHex[i * 2] = digits[fingerprint[i] >> 4];
            fingerprintHex[i * 2 + 1] = digits[fingerprint[i] & 15];
        }
        fingerprintHex[64] = '\0';
        policy = gxos_tls_distrust_after_policy_in_set(
            fingerprintHex, context->policies, context->policyCount);
        if (policy != NULL) {
            if (!context->restrictedCertificateSeen || policy->cutoffUnixSeconds < context->cutoff) {
                context->cutoff = policy->cutoffUnixSeconds;
            }
            context->restrictedCertificateSeen = 1;
        }
    }
    if (depth == 0) {
        int64_t issuedAt = certificate_not_before(certificate);
        context->leafSeen = 1;
        context->restrictionSeenBeforeLeaf = context->restrictedCertificateSeen;
        if (context->restrictedCertificateSeen &&
            gxos_tls_mozilla_tls_distrust_after_rejects(
                issuedAt, context->cutoff, context->sctTimestampCheckEnforced)) {
            *flags |= MBEDTLS_X509_BADCERT_OTHER;
            context->distrustRejected = 1;
        }
    }
    return 0;
}

static int read_file(const char *path, unsigned char **contents, size_t *length)
{
    FILE *file = fopen(path, "rb");
    long fileLength;
    unsigned char *buffer;
    size_t readLength;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (fileLength = ftell(file)) <= 0 || fileLength > 512 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return -1;
    }
    buffer = (unsigned char *) malloc((size_t) fileLength + 1);
    if (buffer == NULL) { fclose(file); return -1; }
    readLength = fread(buffer, 1, (size_t) fileLength, file);
    fclose(file);
    if (readLength != (size_t) fileLength) { free(buffer); return -1; }
    buffer[readLength] = 0;
    *contents = buffer;
    *length = readLength + 1;
    return 0;
}

static int parse_file(const char *path, mbedtls_x509_crt *certificate)
{
    unsigned char *contents = NULL;
    size_t length = 0;
    int result;
    if (read_file(path, &contents, &length) != 0) return -1;
    result = mbedtls_x509_crt_parse(certificate, contents, length);
    free(contents);
    return result;
}

static int root_fingerprint(mbedtls_x509_crt *root, char output[65])
{
    uint8_t digest[32];
    size_t length = 0;
    static const char digits[] = "0123456789abcdef";
    if (root == NULL || root->raw.p == NULL ||
        psa_hash_compute(PSA_ALG_SHA_256, root->raw.p, root->raw.len,
                         digest, sizeof(digest), &length) != PSA_SUCCESS || length != sizeof(digest)) return -1;
    for (size_t i = 0; i < sizeof(digest); ++i) {
        output[i * 2] = digits[digest[i] >> 4];
        output[i * 2 + 1] = digits[digest[i] & 15];
    }
    output[64] = '\0';
    return 0;
}

static int run_case(const char *name, const char *chainPath, const char *rootPath,
                    const char *hostname, int64_t currentTime, const char *policyFingerprint,
                    int64_t cutoff, size_t policyCount, int expectSuccess,
                    uint32_t requiredFlags, int expectDistrust, const char *anchorFingerprint)
{
    mbedtls_x509_crt chain, trust;
    GxosTlsDistrustAfterPolicy policy = { policyFingerprint, cutoff };
    VerifyContext context = { 0 };
    uint32_t flags = 0;
    int chainParse, trustParse, verifyResult = -1, passed = 0;
    char foundRoot[65];
    mbedtls_x509_crt_init(&chain);
    mbedtls_x509_crt_init(&trust);
    context.policies = &policy;
    context.policyCount = policyCount;
    context.sctTimestampCheckEnforced = s_sctTimestampCheckEnforced;
    context.callbackDepthOrderValid = 1;
    s_test_time = currentTime;
    chainParse = parse_file(chainPath, &chain);
    trustParse = parse_file(rootPath, &trust);
    if (chainParse == 0 && trustParse == 0 && root_fingerprint(&trust, foundRoot) == 0) {
        if (anchorFingerprint != NULL && strcmp(foundRoot, anchorFingerprint) != 0) {
            fprintf(stderr, "fixture trust anchor fingerprint differs from the pinned manifest\n");
        } else {
            verifyResult = mbedtls_x509_crt_verify_with_profile(
                &chain, &trust, NULL, &mbedtls_x509_crt_profile_default,
                hostname, &flags, distrust_after_verify_callback, &context);
        }
    }
    if (expectSuccess) passed = chainParse == 0 && trustParse == 0 && verifyResult == 0 && flags == 0;
    else passed = chainParse == 0 && trustParse == 0 && (verifyResult != 0 || flags != 0);
    if ((flags & requiredFlags) != requiredFlags) passed = 0;
    if (context.distrustRejected != expectDistrust) passed = 0;
    if (policyCount != 0 && expectDistrust && !context.restrictionSeenBeforeLeaf) passed = 0;
    if (!context.callbackDepthOrderValid) passed = 0;
    printf("NAVHTTPS21_CASE name=%s result=%s verify=%d flags=0x%08" PRIx32
           " root_seen=%d leaf_seen=%d restriction_before_leaf=%d distrust_rejected=%d\n",
           name, passed ? "PASS" : "FAIL", verifyResult, flags,
           context.restrictedCertificateSeen, context.leafSeen,
           context.restrictionSeenBeforeLeaf, context.distrustRejected);
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);
    return passed;
}

int main(int argc, char **argv)
{
    mbedtls_x509_crt chain;
    mbedtls_x509_crt trust;
    char rootFingerprint[65];
    char alternateRootAFingerprint[65];
    char alternateRootBFingerprint[65];
    int64_t leafNotBefore;
    int64_t alternateLeafNotBefore;
    int failed = 0;
    int64_t anchorTime = INT64_C(1781481600); /* before the fixture leaf expires */
    if (argc != 9) {
        fprintf(stderr, "usage: %s <chain.pem> <root.pem> <tampered-chain.pem> <expected-root-sha256> "
                "<alternate-chain-a.pem> <alternate-root-a.pem> "
                "<alternate-chain-b.pem> <alternate-root-b.pem>\n", argv[0]);
        return 2;
    }
    if (psa_crypto_init() != PSA_SUCCESS) return 3;
    mbedtls_x509_crt_init(&chain);
    mbedtls_x509_crt_init(&trust);
    if (parse_file(argv[1], &chain) != 0 || parse_file(argv[2], &trust) != 0 ||
        root_fingerprint(&trust, rootFingerprint) != 0 || strcmp(rootFingerprint, argv[4]) != 0) return 4;
    leafNotBefore = certificate_not_before(&chain);
    if (leafNotBefore == INT64_MIN) return 5;
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);

    if (parse_file(argv[5], &chain) != 0 || parse_file(argv[6], &trust) != 0 ||
        root_fingerprint(&trust, alternateRootAFingerprint) != 0) return 6;
    alternateLeafNotBefore = certificate_not_before(&chain);
    if (alternateLeafNotBefore == INT64_MIN) return 7;
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);
    if (parse_file(argv[7], &chain) != 0 || parse_file(argv[8], &trust) != 0 ||
        root_fingerprint(&trust, alternateRootBFingerprint) != 0) return 8;
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);

    failed += !run_case("issued-before-cutoff", argv[1], argv[2], "test.dextercd.com", anchorTime,
                        rootFingerprint, leafNotBefore + 1, 1, 1, 0, 0, argv[4]);
    failed += !run_case("issued-exactly-at-cutoff", argv[1], argv[2], "test.dextercd.com", anchorTime,
                        rootFingerprint, leafNotBefore, 1, 1, 0, 0, argv[4]);
    failed += !run_case("issued-after-cutoff", argv[1], argv[2], "test.dextercd.com", anchorTime,
                        rootFingerprint, leafNotBefore - 1, 1, 0, MBEDTLS_X509_BADCERT_OTHER, 1, argv[4]);
    s_sctTimestampCheckEnforced = 0;
    failed += !run_case("missing-sct-enforcement-fails-closed-before-cutoff", argv[1], argv[2],
                        "test.dextercd.com", anchorTime, rootFingerprint, leafNotBefore + 1,
                        1, 0, MBEDTLS_X509_BADCERT_OTHER, 1, argv[4]);
    s_sctTimestampCheckEnforced = 1;
    failed += !run_case("no-applicable-cutoff", argv[1], argv[2], "test.dextercd.com", anchorTime,
                        rootFingerprint, leafNotBefore - 1, 0, 1, 0, 0, argv[4]);
    rootFingerprint[0] = rootFingerprint[0] == '0' ? '1' : '0';
    failed += !run_case("different-exact-root-identity", argv[1], argv[2], "test.dextercd.com", anchorTime,
                        rootFingerprint, leafNotBefore - 1, 1, 1, 0, 0, argv[4]);
    failed += !run_case("wrong-hostname-preserves-verifier-failure", argv[1], argv[2], "wrong.example.invalid", anchorTime,
                        argv[4], leafNotBefore + 1, 1, 0, MBEDTLS_X509_BADCERT_CN_MISMATCH, 0, argv[4]);
    failed += !run_case("expired-leaf-preserves-verifier-failure", argv[1], argv[2], "test.dextercd.com",
                        INT64_C(1798761600), argv[4], leafNotBefore + 1, 1, 0,
                        MBEDTLS_X509_BADCERT_EXPIRED, 0, argv[4]);
    failed += !run_case("cross-signed-path-restriction", argv[1], argv[2], "test.dextercd.com", anchorTime,
                        argv[4], leafNotBefore - 1, 1, 0, MBEDTLS_X509_BADCERT_OTHER, 1, argv[4]);
    failed += !run_case("invalid-signature-preserves-verifier-failure", argv[3], argv[2], "test.dextercd.com",
                        anchorTime, argv[4], leafNotBefore + 1, 1, 0, 0, 0, argv[4]);
    failed += !run_case("restricted-cross-signed-path-is-rejected", argv[5], argv[6], "alternate.test",
                        anchorTime, alternateRootAFingerprint, alternateLeafNotBefore - 1, 1, 0,
                        MBEDTLS_X509_BADCERT_OTHER, 1, alternateRootAFingerprint);
    failed += !run_case("alternate-anchor-does-not-inherit-root-a-cutoff", argv[7], argv[8], "alternate.test",
                        anchorTime, alternateRootAFingerprint, alternateLeafNotBefore - 1, 1, 1,
                        0, 0, alternateRootBFingerprint);

    printf("NAVHTTPS21_DISTRUST_AFTER_RESULT: %s cases=12 failures=%d\n",
           failed == 0 ? "PASS" : "FAIL", failed);
    mbedtls_psa_crypto_free();
    return failed == 0 ? 0 : 1;
}
