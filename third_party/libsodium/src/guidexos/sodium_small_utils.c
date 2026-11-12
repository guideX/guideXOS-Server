/*
 * These three utility implementations are copied from libsodium 1.0.22
 * src/libsodium/sodium/utils.c. Keeping only these helpers avoids linking the
 * hosted allocator, randombytes, and misuse-handler APIs.
 */
#include <stddef.h>
#include <stdint.h>

void sodium_memzero(void *const pnt, const size_t len)
{
    volatile unsigned char *volatile pnt_ =
        (volatile unsigned char *volatile) pnt;
    size_t i = (size_t) 0U;

    while (i < len) {
        pnt_[i++] = 0U;
    }
}

int sodium_is_zero(const unsigned char *n, const size_t nlen)
{
    size_t i;
    volatile unsigned char d = 0U;

    for (i = 0U; i < nlen; i++) {
        d |= n[i];
    }
    return 1 & ((d - 1) >> 8);
}

int sodium_memcmp(const void *const b1_, const void *const b2_, size_t len)
{
    const volatile unsigned char *volatile b1 =
        (const volatile unsigned char *volatile) b1_;
    const volatile unsigned char *volatile b2 =
        (const volatile unsigned char *volatile) b2_;
    size_t i;
    volatile unsigned char d = 0U;

    for (i = 0U; i < len; i++) {
        d |= b1[i] ^ b2[i];
    }
    return (1 & ((d - 1) >> 8)) - 1;
}
