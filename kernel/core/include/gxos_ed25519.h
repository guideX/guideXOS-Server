#pragma once

#include <stddef.h>
#include <stdint.h>

#define GXOS_ED25519_PUBLIC_KEY_BYTES 32U
#define GXOS_ED25519_SIGNATURE_BYTES 64U
#define GXOS_ED25519_MAX_MESSAGE_BYTES 4096U

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 only for a valid signature over a canonical prime-subgroup key
// and R point. Invalid lengths, points, messages, and signatures return 0.
int gxos_ed25519_verify_strict(const uint8_t *public_key,
                               size_t public_key_len,
                               const uint8_t *signature,
                               size_t signature_len,
                               const uint8_t *message,
                               size_t message_len);

#ifdef __cplusplus
}
#endif
