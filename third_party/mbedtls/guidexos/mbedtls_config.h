#ifndef GUIDEXOS_MBEDTLS_CONFIG_H
#define GUIDEXOS_MBEDTLS_CONFIG_H

/*
 * guideXOS bare-metal Mbed TLS scaffold configuration.
 *
 * This header documents the intended minimal feature set for the first
 * freestanding bring-up. It is safe to keep in-tree before the official
 * upstream source drop exists; the Navigator backend will continue to report
 * that Mbed TLS source import is missing until the official tree is added
 * under third_party/mbedtls.
 */

/* No OS entropy, sockets, or filesystem helpers inside Mbed TLS. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_CTR_DRBG_C

/* Bounded memory strategy for freestanding use. */
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

/* guideXOS must provide a trusted wall-clock hook when the source is wired. */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_TIME_ALT

/* TLS client and certificate parsing only. */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PEM_PARSE_C

/* Minimal parsing and crypto support for common public Web PKI chains. */
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_GCM_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C

/* Keep optional test helpers, server mode, and filesystem-bound features off. */

#endif /* GUIDEXOS_MBEDTLS_CONFIG_H */
