#include "gxos_ed25519.h"
#include "sodium/crypto_core_ed25519.h"
#include "sodium/crypto_sign_ed25519.h"

int gxos_ed25519_verify_strict(const uint8_t *public_key,
                               size_t public_key_len,
                               const uint8_t *signature,
                               size_t signature_len,
                               const uint8_t *message,
                               size_t message_len)
{
    static const uint8_t empty_message = 0;

    if (public_key == NULL || signature == NULL ||
        public_key_len != GXOS_ED25519_PUBLIC_KEY_BYTES ||
        signature_len != GXOS_ED25519_SIGNATURE_BYTES ||
        message_len > GXOS_ED25519_MAX_MESSAGE_BYTES ||
        (message == NULL && message_len != 0U)) {
        return 0;
    }

    if (crypto_core_ed25519_is_valid_point(public_key) != 1 ||
        crypto_core_ed25519_is_valid_point(signature) != 1) {
        return 0;
    }

    if (message == NULL) {
        message = &empty_message;
    }

    return crypto_sign_ed25519_verify_detached(
               signature, message, (unsigned long long) message_len,
               public_key) == 0 ? 1 : 0;
}
