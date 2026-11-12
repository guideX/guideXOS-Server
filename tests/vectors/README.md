# Ed25519 strict verification vectors

`wycheproof_ed25519_test.json` is the C2SP Project Wycheproof Ed25519 suite at
commit `65ca494727a52944c204441c860b039e6786b99b`. The upstream Apache 2.0
license is included as `LICENSE-wycheproof.txt`. It contains 210 vectors:
117 `valid`, 64 `invalid`, and 29 `acceptable`. The guideXOS strict profile
accepts only `valid`; both `invalid` and `acceptable` are expected to reject.

`ed25519_independent.json` contains RFC 8032 Appendix A vectors and two
independently generated pure Ed25519 signatures over 1023-byte and 4096-byte
messages. Those boundary signatures were generated with Python cryptography
50.0.2. The temporary seed was not retained. The corpus is checked by
`tests/test_ed25519_strict.py` against hosted objects and the same source built
with the AMD64 freestanding compiler flags.

The SHA-256 inventory in `ed25519_vectors.sha256` pins these files. This suite
uses a strict prime-subgroup acceptance profile: weak, low-order, mixed-order,
invalid, and noncanonical public-key or R encodings are rejected even when a
different Ed25519 verification convention marks a case acceptable.
