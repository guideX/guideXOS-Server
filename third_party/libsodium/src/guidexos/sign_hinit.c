/*
 * The function below is copied from libsodium 1.0.22
 * src/libsodium/crypto_sign/ed25519/ref10/sign.c. It initializes the
 * Ed25519 hash context used by the detached verification implementation.
 */
#include "sodium/crypto_hash_sha512.h"
#include "sign_ed25519_ref10.h"

void _crypto_sign_ed25519_ref10_hinit(crypto_hash_sha512_state *hs,
                                      int prehashed)
{
    static const unsigned char DOM2PREFIX[32 + 2] = {
        'S', 'i', 'g', 'E', 'd', '2', '5', '5', '1', '9', ' ',
        'n', 'o', ' ',
        'E', 'd', '2', '5', '5', '1', '9', ' ',
        'c', 'o', 'l', 'l', 'i', 's', 'i', 'o', 'n', 's', 1, 0
    };

    crypto_hash_sha512_init(hs);
    if (prehashed) {
        crypto_hash_sha512_update(hs, DOM2PREFIX, sizeof DOM2PREFIX);
    }
}
