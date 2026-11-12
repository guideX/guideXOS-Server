# guideXOS libsodium Ed25519 verification subset

This directory contains the verification-only source subset used by the
guideXOS Server AMD64 freestanding kernel. Upstream is libsodium 1.0.22 at
release commit `77e1ce5d6dee871c49ef211222ba18ef0c486bda` (signed tag
`1.0.22-RELEASE`). The upstream ISC license is in `LICENSE`.

The imported upstream files implement ref10 Ed25519 verification, SHA-512,
point arithmetic, canonical scalar handling, and constant-time utility
primitives. The `src/guidexos/` files are narrow adapters based on the same
upstream commit:

- `sign_hinit.c` retains the hash-domain initializer from upstream
  `crypto_sign/ed25519/ref10/sign.c` without importing key generation.
- `point_validate.c` retains upstream
  `crypto_core_ed25519_is_valid_point()` without importing unrelated
  random-point and scalar APIs.
- `sodium_small_utils.c` retains only the zeroization and comparison helpers
  needed by verification.
- `strict_wrapper.c` is guideXOS code. It checks fixed key/signature lengths,
  rejects messages over 4096 bytes, and requires both the public key and
  signature R encoding to be canonical prime-subgroup, non-small-order points
  before detached verification.

The selected upstream verification code is unchanged from the tested 1.0.21
source except for coverage annotations; the 1.0.22 `utils.h` header also
contains upstream overflow-safe base64 length arithmetic. This profile's
verification corpus is rerun against the exact release. The exact vendored
subset is pinned by `guidexos/libsodium_ed25519_sources.mk` and the SHA-256
inventory `guidexos/libsodium_ed25519_sources.sha256`. The build checks those
hashes before compiling. The only hosted-looking abort path
in the general ref10 translation unit is compiled to the compiler's
`__builtin_trap`; that code belongs to an unused point-mapping helper and does
not add a runtime import.

No signing, key generation, entropy, allocator, operating-system, TLS-policy,
or global initialization interfaces are included in the kernel subset.
