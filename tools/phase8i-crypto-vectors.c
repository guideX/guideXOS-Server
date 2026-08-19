/*
 * Phase 8I deterministic ECDSA verification probe.
 *
 * The vectors are the RFC 4754 P-256/P-384 vectors already carried by the
 * pinned TF-PSA-Crypto test suite.  This probe intentionally exercises the
 * public PSA verify-hash API: P-256 must select the guideXOS accelerator and
 * P-384 must take the built-in fallback exactly once.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psa/crypto.h"
#include "psa/crypto_extra.h"

void mbedtls_platform_zeroize(void *buf, size_t len)
{
    volatile uint8_t *bytes = (volatile uint8_t *) buf;
    while (len-- != 0) {
        *bytes++ = 0;
    }
}

psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t *context,
    uint8_t *output, size_t output_size, size_t *output_length)
{
    (void) context;
    (void) output;
    (void) output_size;
    if (output_length != NULL) {
        *output_length = 0;
    }
    return PSA_ERROR_NOT_SUPPORTED;
}

void *gxos_mbedtls_platform_calloc_uninit(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void gxos_mbedtls_platform_free_uninit(void *ptr)
{
    free(ptr);
}

int gxos_mbedtls_platform_vsnprintf_noop(char *s, size_t n,
                                         const char *format, va_list args)
{
    return vsnprintf(s, n, format, args);
}

int gxos_mbedtls_platform_snprintf_noop(char *s, size_t n,
                                        const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = gxos_mbedtls_platform_vsnprintf_noop(s, n, format, args);
    va_end(args);
    return result;
}

int gxos_mbedtls_platform_fprintf_noop(FILE *stream, const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

void gxos_mbedtls_platform_exit_noop(int status)
{
    exit(status);
}

int64_t gxos_mbedtls_time_callback(int64_t *timer)
{
    if (timer != NULL) {
        *timer = 0;
    }
    return 0;
}

typedef struct {
    const char *name;
    size_t coordinate_size;
    psa_ecc_family_t family;
    size_t bits;
    psa_algorithm_t hash_alg;
    const char *x_hex;
    const char *y_hex;
    const char *r_hex;
    const char *s_hex;
    const char *hash_hex;
} ecdsa_vector_t;

static const ecdsa_vector_t vectors[] = {
    {
        "P-256 RFC4754 SHA-256",
        32,
        PSA_ECC_FAMILY_SECP_R1,
        256,
        PSA_ALG_SHA_256,
        "2442A5CC0ECD015FA3CA31DC8E2BBC70BF42D60CBCA20085E0822CB04235E970",
        "6FC98BD7E50211A4A27102FA3549DF79EBCB4BF246B80945CDDFE7D509BBFD7D",
        "CB28E0999B9C7715FD0A80D8E47A77079716CBBF917DD72E97566EA1C066957C",
        "86FA3BB4E26CAD5BF90B7F81899256CE7594BB1EA0C89212748BFF3B3D5B0315",
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
    },
    {
        "P-384 RFC4754 SHA-384",
        48,
        PSA_ECC_FAMILY_SECP_R1,
        384,
        PSA_ALG_SHA_384,
        "96281BF8DD5E0525CA049C048D345D3082968D10FEDF5C5ACA0C64E6465A97EA5CE10C9DFEC21797415710721F437922",
        "447688BA94708EB6E2E4D59F6AB6D7EDFF9301D249FE49C33096655F5D502FAD3D383B91C5E7EDAA2B714CC99D5743CA",
        "FB017B914E29149432D8BAC29A514640B46F53DDAB2C69948084E2930F1C8F7E08E07C9C63F2D21A07DCB56A6AF56EB3",
        "B263A1305E057F984D38726A1B46874109F417BCA112674C528262A40A629AF1CBB9F516CE0FA7D2FF630863A00E8B9F",
        "CB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1631A8B605A43FF5BED8086072BA1E7CC2358BAECA134C825A7",
    },
};

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static int hex_decode(const char *hex, uint8_t *output, size_t output_size)
{
    size_t length = strlen(hex);
    size_t i;

    if (length != output_size * 2) return -1;
    for (i = 0; i < output_size; ++i) {
        int high = hex_nibble(hex[i * 2]);
        int low = hex_nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return -1;
        output[i] = (uint8_t) ((high << 4) | low);
    }
    return 0;
}

static psa_status_t verify_vector(const ecdsa_vector_t *vector,
                                  int alter_signature)
{
    uint8_t public_key[1 + 2 * 48];
    uint8_t signature[2 * 48];
    uint8_t hash[48];
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = 0;
    psa_status_t status;

    memset(public_key, 0, sizeof(public_key));
    memset(signature, 0, sizeof(signature));
    memset(hash, 0, sizeof(hash));
    public_key[0] = 0x04;

    if (hex_decode(vector->x_hex, &public_key[1], vector->coordinate_size) != 0 ||
        hex_decode(vector->y_hex, &public_key[1 + vector->coordinate_size],
                   vector->coordinate_size) != 0 ||
        hex_decode(vector->r_hex, signature, vector->coordinate_size) != 0 ||
        hex_decode(vector->s_hex, &signature[vector->coordinate_size],
                   vector->coordinate_size) != 0 ||
        hex_decode(vector->hash_hex, hash, vector->coordinate_size) != 0) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    if (alter_signature) {
        signature[2 * vector->coordinate_size - 1] ^= 0x01;
    }

    psa_set_key_type(&attributes,
                     PSA_KEY_TYPE_ECC_PUBLIC_KEY(vector->family));
    psa_set_key_bits(&attributes, vector->bits);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA_ANY);
    status = psa_import_key(&attributes, public_key,
                            1 + 2 * vector->coordinate_size, &key);
    if (status == PSA_SUCCESS) {
        status = psa_verify_hash(key, PSA_ALG_ECDSA_ANY, hash,
                                 vector->coordinate_size, signature,
                                 2 * vector->coordinate_size);
    }
    if (key != 0) {
        psa_destroy_key(key);
    }
    psa_reset_key_attributes(&attributes);
    return status;
}

int main(void)
{
    size_t i;
    psa_status_t status;
    int failures = 0;

    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        fprintf(stderr, "PSA_INIT status=%" PRId32 "\n", (int32_t) status);
        return 1;
    }

    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        status = verify_vector(&vectors[i], 0);
        printf("VECTOR name=%s bits=%zu altered=0 status=%" PRId32 " expected=0\n",
               vectors[i].name, vectors[i].bits, (int32_t) status);
        if (status != PSA_SUCCESS) failures++;

        status = verify_vector(&vectors[i], 1);
        printf("VECTOR name=%s bits=%zu altered=1 status=%" PRId32
               " expected=%" PRId32 "\n",
               vectors[i].name, vectors[i].bits, (int32_t) status,
               (int32_t) PSA_ERROR_INVALID_SIGNATURE);
        if (status != PSA_ERROR_INVALID_SIGNATURE) failures++;
    }

    printf("PHASE8I_CRYPTO_VECTORS=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
