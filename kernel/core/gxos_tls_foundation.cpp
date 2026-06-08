#if defined(GXOS_BARE_METAL)
#ifndef MBEDTLS_CONFIG_FILE
#define MBEDTLS_CONFIG_FILE "../../guidexos/mbedtls_config.h"
#endif
#ifndef TF_PSA_CRYPTO_CONFIG_FILE
#define TF_PSA_CRYPTO_CONFIG_FILE "../../../guidexos/crypto_config.h"
#endif
#endif

#include "../../gxos_tls_foundation.cpp"
