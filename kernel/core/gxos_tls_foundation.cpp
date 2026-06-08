#if defined(GXOS_BARE_METAL)
#define MBEDTLS_CONFIG_FILE "../../guidexos/mbedtls_config.h"
#define TF_PSA_CRYPTO_CONFIG_FILE "../../../guidexos/crypto_config.h"
#endif

#include "../../gxos_tls_foundation.cpp"
