/*
 * This is the crypto_core_ed25519_is_valid_point() implementation from
 * libsodium 1.0.22 src/libsodium/crypto_core/ed25519/core_ed25519.c. It is
 * isolated here to avoid linking unrelated random-point/scalar APIs into the
 * freestanding verifier subset.
 */
#include "sodium/private/ed25519_ref10.h"

int crypto_core_ed25519_is_valid_point(const unsigned char *p)
{
    ge25519_p3 p_p3;

    if (ge25519_is_canonical(p) == 0 ||
        ge25519_has_small_order(p) != 0 ||
        ge25519_frombytes(&p_p3, p) != 0 ||
        ge25519_is_on_curve(&p_p3) == 0 ||
        ge25519_is_on_main_subgroup(&p_p3) == 0) {
        return 0;
    }
    return 1;
}
