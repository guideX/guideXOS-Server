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

static int64_t s_test_time;
static uint32_t s_test_rng = 0x23a5c31du;

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

static int parse_file(const char *path, mbedtls_x509_crt *certificate)
{
    FILE *file = fopen(path, "rb");
    long length;
    unsigned char *contents;
    size_t read_length;
    int result;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || length > 512 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return -1;
    }
    contents = (unsigned char *) malloc((size_t) length + 1);
    if (contents == NULL) {
        fclose(file);
        return -1;
    }
    read_length = fread(contents, 1, (size_t) length, file);
    fclose(file);
    if (read_length != (size_t) length) {
        free(contents);
        return -1;
    }
    contents[read_length] = 0;
    result = mbedtls_x509_crt_parse(certificate, contents, read_length + 1);
    free(contents);
    return result;
}

static int verify_case(const char *name, const char *chain_path,
                       const char *anchor_path, const char *hostname,
                       int64_t test_time, int expect_success,
                       uint32_t required_failure_flags)
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
    trust_parse = parse_file(anchor_path, &trust);
    if (chain_parse != 0 || trust_parse != 0) {
        printf("MBEDTLS_CASE name=%s result=FAIL disposition=parse-rejected "
               "chain_parse=%d trust_parse=%d\n",
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
        (flags & required_failure_flags) != required_failure_flags) {
        passed = 0;
    }
    printf("MBEDTLS_CASE name=%s result=%s verify=%d flags=0x%08" PRIx32
           " required_flags=0x%08" PRIx32 " time=%" PRId64 "\n",
           name, passed ? "PASS" : "FAIL", verify_result, flags,
           required_failure_flags, test_time);
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);
    return passed;
}

static int verify_eku(const char *name, const char *chain_path,
                      const unsigned char *oid, size_t oid_length,
                      int expect_supported)
{
    mbedtls_x509_crt chain;
    int parse_result;
    int check_result;
    mbedtls_x509_crt_init(&chain);
    parse_result = parse_file(chain_path, &chain);
    if (parse_result != 0) {
        printf("MBEDTLS_CASE name=%s result=FAIL parse=%d\n", name, parse_result);
        mbedtls_x509_crt_free(&chain);
        return 0;
    }
    check_result = mbedtls_x509_crt_check_extended_key_usage(
        &chain, oid, oid_length);
    printf("MBEDTLS_CASE name=%s result=%s eku_check=%d expected_supported=%d\n",
           name, (expect_supported ? check_result == 0 : check_result != 0)
                     ? "PASS" : "FAIL",
           check_result, expect_supported);
    mbedtls_x509_crt_free(&chain);
    return expect_supported ? check_result == 0 : check_result != 0;
}

static int verify_server_auth_eku(const char *chain_path)
{
    const unsigned char *oid = (const unsigned char *) MBEDTLS_OID_SERVER_AUTH;
    return verify_eku("g5-server-auth-purpose", chain_path, oid,
                      sizeof(MBEDTLS_OID_SERVER_AUTH) - 1, 1);
}

int main(int argc, char **argv)
{
    const int64_t valid_time = argc > 10 ? strtoll(argv[10], NULL, 10) : 0;
    const int64_t future_time = argc > 11 ? strtoll(argv[11], NULL, 10) : 0;
    int passed = 1;
    if (argc != 12 || valid_time <= 0 || future_time <= 0) {
        fprintf(stderr, "usage: %s <g5-valid-chain> <g5-expired-chain> "
                "<global-g3-alternate-chain> <candidate-only-cross-chain> "
                "<g5-root> <global-g3-root> <wrong-root> <tampered-leaf-chain> "
                "<tampered-intermediate-chain> <valid-time> <future-time>\n", argv[0]);
        return 2;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        fprintf(stderr, "PSA crypto initialization failed\n");
        return 3;
    }

    passed &= verify_case("g5-valid-chain-correct-hostname", argv[1], argv[5],
                          "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com",
                          valid_time, 1, 0);
    passed &= verify_case("g5-wrong-hostname", argv[1], argv[5],
                          "wrong.example", valid_time, 0,
                          MBEDTLS_X509_BADCERT_CN_MISMATCH);
    passed &= verify_case("g5-expired-public-demo-leaf", argv[2], argv[5],
                          "digicert-tls-ecc-p384-root-g5-expired.chain-demos.digicert.com",
                          valid_time, 0, MBEDTLS_X509_BADCERT_EXPIRED);
    passed &= verify_case("g5-future-dated-public-leaf", argv[1], argv[5],
                          "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com",
                          future_time, 0, MBEDTLS_X509_BADCERT_FUTURE);
    passed &= verify_case("g5-invalid-leaf-signature", argv[8], argv[5],
                          "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com",
                          valid_time, 0, 0);
    passed &= verify_case("g5-invalid-intermediate-signature", argv[9], argv[5],
                          "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com",
                          valid_time, 0, 0);
    passed &= verify_case("g5-wrong-trust-anchor", argv[1], argv[7],
                          "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com",
                          valid_time, 0, 0);
    passed &= verify_server_auth_eku(argv[1]);
    passed &= verify_eku("g5-wrong-trust-purpose-email", argv[1],
                         (const unsigned char *) MBEDTLS_OID_EMAIL_PROTECTION,
                         sizeof(MBEDTLS_OID_EMAIL_PROTECTION) - 1, 0);
    passed &= verify_case("global-g3-cross-signed-alternate-path", argv[3], argv[6],
                          "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com",
                          valid_time, 1, 0);
    passed &= verify_case("cross-signed-path-with-g5-candidate-anchor", argv[4], argv[5],
                          "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com",
                          valid_time, 1, 0);
    mbedtls_psa_crypto_free();
    printf("NAVHTTPS23_MBEDTLS_RESULT: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
