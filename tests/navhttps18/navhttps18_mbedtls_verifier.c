#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/platform.h"
#include "mbedtls/oid.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "psa/crypto_extra.h"

static int64_t s_test_time;
static uint32_t s_test_rng = 0x18a5c31du;

void mbedtls_platform_zeroize(void *buffer, size_t length)
{
    volatile unsigned char *bytes = (volatile unsigned char *) buffer;
    while (length-- != 0) {
        *bytes++ = 0;
    }
}

psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t *context,
    uint8_t *output, size_t output_size, size_t *output_length)
{
    size_t index;
    (void) context;
    if (output == NULL || output_length == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    for (index = 0; index < output_size; ++index) {
        s_test_rng ^= s_test_rng << 13;
        s_test_rng ^= s_test_rng >> 17;
        s_test_rng ^= s_test_rng << 5;
        output[index] = (uint8_t) s_test_rng;
    }
    *output_length = output_size;
    return PSA_SUCCESS;
}

void *gxos_mbedtls_platform_calloc_uninit(size_t count, size_t size)
{
    return calloc(count, size);
}

void gxos_mbedtls_platform_free_uninit(void *ptr)
{
    free(ptr);
}

int gxos_mbedtls_platform_vsnprintf_noop(char *buffer, size_t size,
                                         const char *format, va_list args)
{
    return vsnprintf(buffer, size, format, args);
}

int gxos_mbedtls_platform_snprintf_noop(char *buffer, size_t size,
                                        const char *format, ...)
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

void gxos_mbedtls_platform_exit_noop(int status)
{
    (void) status;
}

int64_t gxos_mbedtls_time_callback(int64_t *timer)
{
    if (timer != NULL) {
        *timer = s_test_time;
    }
    return s_test_time;
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *value,
                                    struct tm *result)
{
    time_t seconds;
    struct tm *shared;

    if (value == NULL || result == NULL) {
        return NULL;
    }
    seconds = (time_t) *value;
    shared = gmtime(&seconds);
    if (shared == NULL) {
        return NULL;
    }
    *result = *shared;
    return result;
}

static int read_file(const char *path, unsigned char **contents, size_t *length)
{
    FILE *file = fopen(path, "rb");
    long file_length;
    unsigned char *buffer;
    size_t read_length;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (file_length = ftell(file)) <= 0 || file_length > 512 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return -1;
    }
    buffer = (unsigned char *) malloc((size_t) file_length + 1);
    if (buffer == NULL) {
        fclose(file);
        return -1;
    }
    read_length = fread(buffer, 1, (size_t) file_length, file);
    fclose(file);
    if (read_length != (size_t) file_length) {
        free(buffer);
        return -1;
    }
    buffer[read_length] = 0;
    *contents = buffer;
    *length = read_length + 1;
    return 0;
}

static int parse_file(const char *path, mbedtls_x509_crt *certificate)
{
    unsigned char *contents = NULL;
    size_t length = 0;
    int result;

    if (read_file(path, &contents, &length) != 0) {
        fprintf(stderr, "failed to read certificate fixture: %s\n", path);
        return -1;
    }
    result = mbedtls_x509_crt_parse(certificate, contents, length);
    free(contents);
    return result;
}

static int verify_case(const char *name, const char *chain_path,
                       const char *bundle_path, const char *hostname,
                       int64_t test_time, int expect_success)
{
    mbedtls_x509_crt chain;
    mbedtls_x509_crt trust;
    uint32_t flags = 0;
    int chain_parse;
    int trust_parse;
    int verify_result;
    int passed;

    s_test_time = test_time;
    mbedtls_x509_crt_init(&chain);
    mbedtls_x509_crt_init(&trust);

    chain_parse = parse_file(chain_path, &chain);
    trust_parse = parse_file(bundle_path, &trust);
    if (chain_parse != 0 || trust_parse != 0) {
        passed = !expect_success && (chain_parse != 0 || trust_parse != 0);
        printf("MBEDTLS_CASE name=%s result=%s disposition=parse-rejected "
               "chain_parse=%d trust_parse=%d\n",
               name, passed ? "PASS" : "FAIL", chain_parse, trust_parse);
        mbedtls_x509_crt_free(&chain);
        mbedtls_x509_crt_free(&trust);
        return passed;
    }

    verify_result = mbedtls_x509_crt_verify_with_profile(
        &chain, &trust, NULL, &mbedtls_x509_crt_profile_default,
        hostname, &flags, NULL, NULL);
    passed = expect_success ? (verify_result == 0 && flags == 0)
                            : (verify_result != 0 || flags != 0);
    printf("MBEDTLS_CASE name=%s result=%s verify=%d flags=0x%08" PRIx32
           " chain_parse=%d trust_parse=%d\n",
           name, passed ? "PASS" : "FAIL", verify_result, flags,
           chain_parse, trust_parse);

    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);
    return passed;
}

static int verify_case_without_trust(const char *name, const char *chain_path,
                                     const char *hostname, int64_t test_time)
{
    mbedtls_x509_crt chain;
    mbedtls_x509_crt no_trust;
    uint32_t flags = 0;
    int parse_result;
    int verify_result;

    s_test_time = test_time;
    mbedtls_x509_crt_init(&chain);
    mbedtls_x509_crt_init(&no_trust);
    parse_result = parse_file(chain_path, &chain);
    if (parse_result != 0) {
        printf("MBEDTLS_CASE name=%s result=FAIL disposition=chain-parse "
               "chain_parse=%d\n", name, parse_result);
        mbedtls_x509_crt_free(&chain);
        mbedtls_x509_crt_free(&no_trust);
        return 0;
    }
    verify_result = mbedtls_x509_crt_verify_with_profile(
        &chain, &no_trust, NULL, &mbedtls_x509_crt_profile_default,
        hostname, &flags, NULL, NULL);
    printf("MBEDTLS_CASE name=%s result=%s verify=%d flags=0x%08" PRIx32
           " disposition=empty-candidate-store\n",
           name, (verify_result != 0 || flags != 0) ? "PASS" : "FAIL",
           verify_result, flags);
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&no_trust);
    return verify_result != 0 || flags != 0;
}

static int audit_source_bundle(const char *bundle_path)
{
    mbedtls_x509_crt bundle;
    int parse_result;

    mbedtls_x509_crt_init(&bundle);
    parse_result = parse_file(bundle_path, &bundle);
    printf("MBEDTLS_CASE name=mozilla-source-bundle-strict-parse "
           "result=%s parse_result=%d disposition=%s\n",
           parse_result > 0 ? "PASS" : "FAIL", parse_result,
           parse_result > 0 ? "partial-parse-must-be-rejected" : "all-parsed");
    mbedtls_x509_crt_free(&bundle);
    return parse_result > 0;
}

static int verify_server_auth_eku(const char *name, const char *chain_path,
                                  int expect_server_auth)
{
    mbedtls_x509_crt chain;
    int parse_result;
    int check_result;
    const unsigned char *server_auth = (const unsigned char *) MBEDTLS_OID_SERVER_AUTH;

    mbedtls_x509_crt_init(&chain);
    parse_result = parse_file(chain_path, &chain);
    if (parse_result != 0) {
        printf("MBEDTLS_CASE name=%s result=FAIL disposition=parse-rejected "
               "parse=%d\n", name, parse_result);
        mbedtls_x509_crt_free(&chain);
        return 0;
    }
    check_result = mbedtls_x509_crt_check_extended_key_usage(
        &chain, server_auth, sizeof(MBEDTLS_OID_SERVER_AUTH) - 1);
    printf("MBEDTLS_CASE name=%s result=%s eku_check=%d purpose=tls-server-auth\n",
           name,
           expect_server_auth ? (check_result == 0 ? "PASS" : "FAIL")
                             : (check_result != 0 ? "PASS" : "FAIL"),
           check_result);
    mbedtls_x509_crt_free(&chain);
    return expect_server_auth ? check_result == 0 : check_result != 0;
}

int main(int argc, char **argv)
{
    int result = 1;
    const int64_t future_check_time = 1779926400; /* 2026-05-28 UTC */
    const int64_t valid_check_time = 1781481600; /* 2026-06-15 UTC */
    const int64_t expired_check_time = 1788220800; /* 2026-09-01 UTC */

    if (argc != 17) {
        fprintf(stderr, "usage: %s <public-chain> <public-root> <wrong-root> "
                "<bad-public-chain> <restricted-chain> <restricted-root> "
                "<unknown-critical-chain> <unknown-critical-root> "
                "<critical-nc-chain> <critical-nc-root> <client-eku-chain> "
                "<pathlen-chain> <pathlen-root> <mozilla-source-bundle> "
                "<policy-chain> <policy-root>\n", argv[0]);
        return 2;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        fprintf(stderr, "PSA crypto initialization failed\n");
        return 3;
    }

    result &= verify_case("public-chain-valid-at-fixture-time", argv[1], argv[2],
                          "test.dextercd.com", valid_check_time, 1);
    result &= verify_case("unknown-trust-root", argv[1], argv[3],
                          "test.dextercd.com", valid_check_time, 0);
    result &= verify_case("expired-public-leaf", argv[1], argv[2],
                          "test.dextercd.com", expired_check_time, 0);
    result &= verify_case("future-dated-public-leaf", argv[1], argv[2],
                          "test.dextercd.com", future_check_time, 0);
    result &= verify_case("wrong-hostname", argv[1], argv[2],
                          "wrong.test.dextercd.com", valid_check_time, 0);
    result &= verify_case("invalid-intermediate-signature", argv[4], argv[2],
                          "test.dextercd.com", valid_check_time, 0);
    result &= verify_case("noncritical-name-constraints-ignored", argv[5], argv[6],
                          "www.outside.example", valid_check_time, 1);
    result &= verify_case("unsupported-critical-extension-rejected", argv[7], argv[8],
                          "www.outside.example", valid_check_time, 0);
    result &= verify_case("critical-name-constraints-rejected", argv[9], argv[10],
                          "www.outside.example", valid_check_time, 0);
    result &= verify_server_auth_eku("server-auth-purpose", argv[1], 1);
    result &= verify_server_auth_eku("wrong-trust-purpose-client-only", argv[11], 0);
    result &= verify_case("path-length-zero-enforced", argv[12], argv[13],
                          "www.pathlen.example", valid_check_time, 0);
    result &= verify_case_without_trust("excluded-root-empty-candidate",
                                        argv[1], "test.dextercd.com", valid_check_time);
    result &= audit_source_bundle(argv[14]);
    result &= verify_case("certificate-policy-tree-not-applied", argv[15], argv[16],
                          "www.policy.example", valid_check_time, 1);
    mbedtls_psa_crypto_free();
    printf("NAVHTTPS18_MBEDTLS_RESULT: %s\n", result ? "PASS" : "FAIL");
    return result ? 0 : 1;
}
