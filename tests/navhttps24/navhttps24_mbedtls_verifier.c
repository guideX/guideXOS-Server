#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/oid.h"
#include "mbedtls/platform.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

static int64_t s_test_time;
static uint32_t s_test_rng = 0x24a5c31du;

void *gxos_mbedtls_platform_calloc_uninit(size_t count, size_t size)
{
    return calloc(count, size);
}

void gxos_mbedtls_platform_free_uninit(void *ptr)
{
    free(ptr);
}

int gxos_mbedtls_platform_snprintf_noop(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    int result;
    va_start(args, format);
    result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int gxos_mbedtls_platform_vsnprintf_noop(char *buffer, size_t size, const char *format, va_list args)
{
    return vsnprintf(buffer, size, format, args);
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

void gxos_mbedtls_platform_exit_noop(int status)
{
    (void) status;
}

void mbedtls_platform_zeroize(void *buffer, size_t length)
{
    volatile unsigned char *bytes = (volatile unsigned char *) buffer;
    while (length-- != 0) *bytes++ = 0;
}

psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t *context,
    uint8_t *output, size_t output_size, size_t *output_length)
{
    size_t i;
    (void) context;
    if (output == NULL || output_length == NULL) return PSA_ERROR_INVALID_ARGUMENT;
    for (i = 0; i < output_size; ++i) {
        s_test_rng ^= s_test_rng << 13;
        s_test_rng ^= s_test_rng >> 17;
        s_test_rng ^= s_test_rng << 5;
        output[i] = (uint8_t) s_test_rng;
    }
    *output_length = output_size;
    return PSA_SUCCESS;
}

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

static int parse_file(const char *path, mbedtls_x509_crt *certificate)
{
    FILE *file = fopen(path, "rb");
    long length;
    unsigned char *contents;
    size_t read_length;
    int result;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return -1;
    }
    contents = (unsigned char *) malloc((size_t) length + 1u);
    if (contents == NULL) { fclose(file); return -2; }
    read_length = fread(contents, 1, (size_t) length, file);
    fclose(file);
    if (read_length != (size_t) length) { free(contents); return -3; }
    contents[read_length] = 0;
    result = mbedtls_x509_crt_parse(certificate, contents, read_length + 1u);
    free(contents);
    return result;
}

static int anchor_fingerprint(mbedtls_x509_crt *trust, char output[65])
{
    static const char digits[] = "0123456789abcdef";
    uint8_t digest[32];
    size_t digest_length = 0;
    size_t i;
    if (trust == NULL || trust->raw.p == NULL || trust->raw.len == 0 ||
        psa_hash_compute(PSA_ALG_SHA_256, trust->raw.p, trust->raw.len,
            digest, sizeof(digest), &digest_length) != PSA_SUCCESS || digest_length != sizeof(digest)) {
        return -1;
    }
    for (i = 0; i < sizeof(digest); ++i) {
        output[i * 2u] = digits[digest[i] >> 4];
        output[i * 2u + 1u] = digits[digest[i] & 0x0fu];
    }
    output[64] = '\0';
    return 0;
}

static int verify_case(const char *name, const char *chain_path, const char *anchor_path,
                       const char *hostname, int64_t time, int expect_success,
                       uint32_t required_failure_flags, const char *expected_anchor)
{
    mbedtls_x509_crt chain;
    mbedtls_x509_crt trust;
    uint32_t flags = 0;
    char found_anchor[65] = "";
    int chain_parse;
    int trust_parse;
    int verify_result;
    int passed;

    s_test_time = time;
    mbedtls_x509_crt_init(&chain);
    mbedtls_x509_crt_init(&trust);
    chain_parse = parse_file(chain_path, &chain);
    trust_parse = parse_file(anchor_path, &trust);
    if (chain_parse != 0 || trust_parse != 0 || anchor_fingerprint(&trust, found_anchor) != 0) {
        printf("MBEDTLS_CASE name=%s result=FAIL disposition=parse-rejected chain_parse=%d trust_parse=%d\n",
            name, chain_parse, trust_parse);
        mbedtls_x509_crt_free(&chain);
        mbedtls_x509_crt_free(&trust);
        return 0;
    }

    verify_result = mbedtls_x509_crt_verify_with_profile(
        &chain, &trust, NULL, &mbedtls_x509_crt_profile_default,
        hostname, &flags, NULL, NULL);
    passed = expect_success ? (verify_result == 0 && flags == 0)
                            : (verify_result != 0 || flags != 0);
    if (!expect_success && required_failure_flags != 0 &&
        (flags & required_failure_flags) != required_failure_flags) passed = 0;
    if (expected_anchor != NULL && strcmp(found_anchor, expected_anchor) != 0) passed = 0;
    printf("MBEDTLS_CASE name=%s result=%s verify=%d flags=0x%08" PRIx32
           " required_flags=0x%08" PRIx32 " time=%" PRId64 " anchor_sha256=%s\n",
           name, passed ? "PASS" : "FAIL", verify_result, flags,
           required_failure_flags, time, found_anchor);
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);
    return passed;
}

int main(int argc, char **argv)
{
    const char *valid;
    const char *expired;
    const char *alternate;
    const char *bad_leaf;
    const char *bad_intermediate;
    const char *missing_intermediate;
    const char *g5_root;
    const char *g3_root;
    int64_t valid_time;
    int64_t future_time;
    int passed = 0;
    int total = 0;
    if (argc != 11) {
        fprintf(stderr, "usage: %s valid expired alternate bad-leaf bad-intermediate leaf-only g5-root g3-root valid-time future-time\n", argv[0]);
        return 2;
    }
    valid = argv[1]; expired = argv[2]; alternate = argv[3]; bad_leaf = argv[4];
    bad_intermediate = argv[5]; missing_intermediate = argv[6]; g5_root = argv[7];
    g3_root = argv[8]; valid_time = strtoll(argv[9], NULL, 10); future_time = strtoll(argv[10], NULL, 10);
    if (psa_crypto_init() != PSA_SUCCESS) return 3;
#define RUN_CASE(n, c, a, h, t, ok, f, anchor) do { ++total; passed += verify_case(n,c,a,h,t,ok,f,anchor); } while (0)
    RUN_CASE("valid-exact-g5-anchor-correct-hostname", valid, g5_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", valid_time, 1, 0,
        "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05");
    RUN_CASE("wrong-hostname", valid, g5_root, "wrong.example", valid_time, 0,
        MBEDTLS_X509_BADCERT_CN_MISMATCH, NULL);
    RUN_CASE("expired-public-leaf", expired, g5_root,
        "digicert-tls-ecc-p384-root-g5-expired.chain-demos.digicert.com", valid_time, 0,
        MBEDTLS_X509_BADCERT_EXPIRED, NULL);
    RUN_CASE("future-dated-public-leaf", valid, g5_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", future_time, 0,
        MBEDTLS_X509_BADCERT_FUTURE, NULL);
    RUN_CASE("missing-intermediate", missing_intermediate, g5_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", valid_time, 0,
        MBEDTLS_X509_BADCERT_NOT_TRUSTED, NULL);
    RUN_CASE("modified-leaf-signature-synthetic", bad_leaf, g5_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", valid_time, 0, 0, NULL);
    RUN_CASE("invalid-intermediate-signature-synthetic", bad_intermediate, g5_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", valid_time, 0, 0, NULL);
    RUN_CASE("incorrect-global-g3-anchor-for-self-signed-path", valid, g3_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", valid_time, 0, 0,
        "31ad6648f8104138c738f39ea4320133393e3a18cc02296ef97c2ac9ef6731d0");
    RUN_CASE("cross-signed-alternate-with-exact-g5-trust-anchor", alternate, g5_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", valid_time, 1, 0,
        "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05");
    RUN_CASE("cross-signed-alternate-accepted-with-separate-g3-anchor", alternate, g3_root,
        "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com", valid_time, 1, 0,
        "31ad6648f8104138c738f39ea4320133393e3a18cc02296ef97c2ac9ef6731d0");
#undef RUN_CASE
    printf("NAVHTTPS24_MBEDTLS_RESULT: %s (%d/%d)\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
