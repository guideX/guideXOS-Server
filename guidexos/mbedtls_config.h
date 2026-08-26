#ifndef GUIDEXOS_MBEDTLS_CONFIG_H
#define GUIDEXOS_MBEDTLS_CONFIG_H

/*
 * guideXOS bare-metal Mbed TLS 4.x TLS/X.509 configuration.
 *
 * Cryptography and platform options moved to TF-PSA-Crypto in Mbed TLS 4.x, so
 * those live in guidexos/crypto_config.h instead of this file.
 */

#define MBEDTLS_CONFIG_VERSION 0x04010000

/* Keep the first freestanding bring-up tightly scoped to a client-only path. */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* Limit the first compile pass to certificate parsing and validation plumbing. */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_REMOVE_INFO

/* Prefer a conservative record size for the bounded guideXOS arena. */
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

/* Expose runtime version information for diagnostics once the import is complete. */
#define MBEDTLS_VERSION_C

/* Public Web PKI chains commonly need both ECDSA and RSA certificate support. */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

#endif /* GUIDEXOS_MBEDTLS_CONFIG_H */
