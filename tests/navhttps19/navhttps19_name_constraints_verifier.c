#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/platform.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

static int64_t s_test_time = 1781481600; /* 2026-06-15 UTC */
static uint32_t s_test_rng = 0x19a5c31du;

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

static int join_path(char *output, size_t output_size,
                     const char *directory, const char *name)
{
    int written = snprintf(output, output_size, "%s/%s", directory, name);
    return written >= 0 && (size_t) written < output_size ? 0 : -1;
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

static int run_case(const char *directory, const char *case_name,
                    const char *chain_name, const char *root_name,
                    const char *hostname, char expectation,
                    unsigned *positive_count, unsigned *negative_count,
                    unsigned *name_constraint_count)
{
    char chain_path[1024];
    char root_path[1024];
    mbedtls_x509_crt chain;
    mbedtls_x509_crt trust;
    uint32_t flags = 0;
    int chain_parse;
    int trust_parse;
    int verify_result = -1;
    int passed = 0;

    if (join_path(chain_path, sizeof(chain_path), directory, chain_name) != 0 ||
        join_path(root_path, sizeof(root_path), directory, root_name) != 0) {
        fprintf(stderr, "fixture path is too long for case %s\n", case_name);
        return 0;
    }

    mbedtls_x509_crt_init(&chain);
    mbedtls_x509_crt_init(&trust);
    chain_parse = parse_file(chain_path, &chain);
    trust_parse = parse_file(root_path, &trust);
    if (chain_parse == 0 && trust_parse == 0 && expectation != 'P') {
        verify_result = mbedtls_x509_crt_verify_with_profile(
            &chain, &trust, NULL, &mbedtls_x509_crt_profile_default,
            strcmp(hostname, "-") == 0 ? NULL : hostname,
            &flags, NULL, NULL);
    }

    switch (expectation) {
        case 'A':
            passed = chain_parse == 0 && trust_parse == 0 &&
                     verify_result == 0 && flags == 0;
            (*positive_count)++;
            break;
        case 'N':
            passed = chain_parse == 0 && trust_parse == 0 &&
                     (verify_result != 0 || flags != 0);
            (*negative_count)++;
            break;
        case 'C':
            passed = chain_parse == 0 && trust_parse == 0 &&
                     verify_result != 0 &&
                     flags == MBEDTLS_X509_BADCERT_NAME_CONSTRAINTS;
            (*negative_count)++;
            (*name_constraint_count)++;
            break;
        case 'P':
            passed = chain_parse != 0 && trust_parse == 0;
            (*negative_count)++;
            break;
        default:
            fprintf(stderr, "unknown expectation '%c' for case %s\n",
                    expectation, case_name);
            break;
    }

    printf("MBEDTLS_CASE name=%s result=%s verify=%d flags=0x%08" PRIx32
           " chain_parse=%d trust_parse=%d expectation=%c\n",
           case_name, passed ? "PASS" : "FAIL", verify_result, flags,
           chain_parse, trust_parse, expectation);
    mbedtls_x509_crt_free(&chain);
    mbedtls_x509_crt_free(&trust);
    return passed;
}

int main(int argc, char **argv)
{
    FILE *manifest;
    char line[1024];
    unsigned positive_count = 0;
    unsigned negative_count = 0;
    unsigned name_constraint_count = 0;
    unsigned case_count = 0;
    unsigned failed_count = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <fixture-directory> <cases.tsv>\n", argv[0]);
        return 2;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        fprintf(stderr, "PSA crypto initialization failed\n");
        return 3;
    }
    manifest = fopen(argv[2], "r");
    if (manifest == NULL) {
        fprintf(stderr, "failed to open case manifest: %s\n", argv[2]);
        mbedtls_psa_crypto_free();
        return 4;
    }

    while (fgets(line, sizeof(line), manifest) != NULL) {
        char case_name[96];
        char chain_name[128];
        char root_name[128];
        char hostname[256];
        char expectation;
        int consumed = 0;
        int scanned;
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\r') {
            continue;
        }
        scanned = sscanf(line, "%95[^|]|%127[^|]|%127[^|]|%255[^|]|%c%n",
                         case_name, chain_name, root_name, hostname,
                         &expectation, &consumed);
        if (scanned != 5 || consumed == 0 ||
            (line[consumed] != '\n' && line[consumed] != '\r' &&
             line[consumed] != '\0')) {
            fprintf(stderr, "invalid test manifest line %u\n", case_count + 1);
            failed_count++;
            continue;
        }
        case_count++;
        if (!run_case(argv[1], case_name, chain_name, root_name,
                      hostname, expectation, &positive_count,
                      &negative_count, &name_constraint_count)) {
            failed_count++;
        }
    }
    fclose(manifest);
    mbedtls_psa_crypto_free();
    printf("NAVHTTPS19_MBEDTLS_SUMMARY total=%u positive=%u negative=%u "
           "name_constraint_rejections=%u failed=%u\n",
           case_count, positive_count, negative_count,
           name_constraint_count, failed_count);
    printf("NAVHTTPS19_MBEDTLS_RESULT: %s\n",
           case_count != 0 && failed_count == 0 ? "PASS" : "FAIL");
    return case_count != 0 && failed_count == 0 ? 0 : 1;
}
