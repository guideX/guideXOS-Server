#ifndef GUIDEXOS_TF_PSA_CRYPTO_CONFIG_H
#define GUIDEXOS_TF_PSA_CRYPTO_CONFIG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void* gxos_mbedtls_platform_calloc_uninit(size_t nmemb, size_t size);
void gxos_mbedtls_platform_free_uninit(void* ptr);
int gxos_mbedtls_platform_snprintf_noop(char* s, size_t n, const char* format, ...);
int gxos_mbedtls_platform_vsnprintf_noop(char* s, size_t n, const char* format, va_list args);
int gxos_mbedtls_platform_fprintf_noop(FILE* stream, const char* format, ...);
void gxos_mbedtls_platform_exit_noop(int status);
int64_t gxos_mbedtls_time_callback(int64_t* timer);

#ifdef __cplusplus
}
#endif

/*
 * guideXOS bare-metal Mbed TLS 4.x TF-PSA-Crypto configuration.
 *
 * This keeps the freestanding client focused on TLS 1.2 verification while
 * retaining the public-web ECDHE groups needed by ordinary HTTPS endpoints.
 */

#define TF_PSA_CRYPTO_CONFIG_VERSION 0x01010000

#define MBEDTLS_PLATFORM_STD_CALLOC gxos_mbedtls_platform_calloc_uninit
#define MBEDTLS_PLATFORM_STD_FREE gxos_mbedtls_platform_free_uninit
#define MBEDTLS_PLATFORM_STD_SNPRINTF gxos_mbedtls_platform_snprintf_noop
#define MBEDTLS_PLATFORM_STD_VSNPRINTF gxos_mbedtls_platform_vsnprintf_noop
#define MBEDTLS_PLATFORM_STD_FPRINTF gxos_mbedtls_platform_fprintf_noop
#define MBEDTLS_PLATFORM_STD_EXIT gxos_mbedtls_platform_exit_noop
#define MBEDTLS_PLATFORM_STD_TIME gxos_mbedtls_time_callback
#define MBEDTLS_PLATFORM_MS_TIME_TYPE_MACRO int64_t

#define PSA_WANT_ALG_ECDH 1
#define PSA_WANT_ALG_ECDSA 1
#define PSA_WANT_ALG_GCM 1
#define PSA_WANT_ALG_RSA_OAEP 1
#define PSA_WANT_ALG_RSA_PKCS1V15_SIGN 1
#define PSA_WANT_ALG_RSA_PSS 1
#define PSA_WANT_ALG_SHA_256 1
#define PSA_WANT_ALG_SHA_384 1
#define PSA_WANT_ALG_SHA_512 1
#define PSA_WANT_ALG_TLS12_PRF 1

#define PSA_WANT_ECC_SECP_R1_256 1
#define PSA_WANT_ECC_SECP_R1_384 1

/*
 * The bundled p256-m driver accelerates only secp256r1.  P-384 remains on the
 * builtin PSA driver so public chains can use either curve without routing
 * an unsupported size into the P-256 accelerator.  The narrowly-scoped
 * guideXOS opt-in below is paired with dispatch tests and bounded tracing;
 * it does not enable any additional curve or algorithm family.
 */

#define MBEDTLS_PSA_P256M_DRIVER_ENABLED 1
#define GUIDEXOS_MBEDTLS_ALLOW_PARTIAL_ECC_TLS 1

/* The custom TF-PSA profile replaces the upstream crypto_config.h, so carry
 * over the bounded NIST prime reductions needed by the software P-384 path.
 * Without this, P-384 falls back to generic MPI division during every field
 * operation and can appear to hang while parsing a production trust bundle. */
#define MBEDTLS_ECP_NIST_OPTIM 1

#define PSA_WANT_KEY_TYPE_AES 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_DERIVE 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT 1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY 1
#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_BASIC 1
#define PSA_WANT_KEY_TYPE_RSA_PUBLIC_KEY 1

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_EXIT_ALT
#define MBEDTLS_PLATFORM_FPRINTF_ALT
#define MBEDTLS_PLATFORM_GMTIME_R_ALT
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_PLATFORM_TIME_ALT
#define MBEDTLS_PLATFORM_ZEROIZE_ALT
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C
#define MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_CRYPTO_CLIENT
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG

/* guideXOS provides its own fail-closed wall-clock integration. */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE

/* X.509 parsing still depends on these support layers in the split config. */
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

#endif /* GUIDEXOS_TF_PSA_CRYPTO_CONFIG_H */

