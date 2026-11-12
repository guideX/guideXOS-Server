# NAVHTTPS11 — Strict Ed25519 Verification and Freestanding Qualification

Date: 2026-11-11
Outcome: **A — selected, strictly validated backend integrated and qualified at kernel-link level.**

## Accepted NAVHTTPS10 baseline

NAVHTTPS10 established independent RFC 10049 v1 interoperability with Roughenough 2.2.0. Its hosted verifier checks the signature, delegation, nonce, Merkle proof, timestamp, and uncertainty; the hosted Roughtime suite passed 23/23. NAVHTTPS10 also recorded a 288/288 hosted Navigator smoke run and a successful AMD64 kernel compile. See [NAVHTTPS10](NAVIGATOR_HTTPS_ROUGHTIME_PORTABLE_NAVHTTPS10.md) and [NAVHTTPS9](NAVIGATOR_HTTPS_AUTHENTICATED_TIME_NAVHTTPS9.md).

At the start of NAVHTTPS11, no Ed25519 implementation was linked into the kernel and no portable kernel Roughtime verifier or authenticated guest clock existed. This phase adds only a verification component. It does not activate Roughtime, change time assurance, enable public HTTPS, or change certificate policy.

## Monocypher probe

The NAVHTTPS10 candidate was Monocypher 4.0.3, commit `ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f`. The probe used its intended EdDSA verification API, `crypto_ed25519_check`, rather than a signing or key-generation routine. Reproduction with an all-zero public key and all-zero signature returned success.

The result follows Monocypher's documented cofactor-cleared verification equation and its invalid-key acceptance convention. The verifier checks the signature equation and canonical scalar constraints, but does not impose the stronger guideXOS requirement that the public key and signature `R` encoding be canonical prime-subgroup points. This probe is evidence of a profile mismatch; it is not evidence of a forgery for ordinary valid public keys. A separate strict point-validation layer could in principle reject these inputs, but that would add curve-validation code and another security boundary to maintain. We therefore did not select this candidate. See the [Monocypher EdDSA manual](https://monocypher.org/manual/eddsa).

## Candidate evaluation

| Candidate and pinned source | Probe result for the guideXOS profile | Maintenance, footprint, license, and integration decision |
| --- | --- | --- |
| Monocypher 4.0.3, `ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f` | RFC signatures work, but the intended verifier accepts the reproduced all-zero weak-key case. Its verification convention does not supply the required prime-subgroup policy. | Upstream provides a portable C release and test/formal-analysis workflows. Compact, but a new strict point-validation layer would add complexity. Rejected for this profile. |
| HACL*, `f557259007db9933442dde10aa995c1b00bd3446` | RFC vector, corrupted signature, wrong key, and all-zero pair behave as expected in the probe. Additional identity-point and order-2 weak-key constructions were accepted, so it does not meet the selected strict profile as called. | Formally verified, portable, and MIT licensed. Its upstream repository describes itself as a research home and points production users to HACL packages. Rejected for this boundary policy without an additional strict validator. |
| libsodium 1.0.22, signed tag `1.0.22-RELEASE`, commit `77e1ce5d6dee871c49ef211222ba18ef0c486bda` | The detached verifier enforces canonical scalar handling, while `crypto_core_ed25519_is_valid_point` rejects noncanonical, off-curve, small-order, and non-prime-subgroup points. Applying the point validator to both public key `A` and signature point `R` yields the selected profile. | Current signed point release at evaluation; the prior 1.0.21 release changelog identifies a security fix in `crypto_core_ed25519_is_valid_point`, which is included in this source line. ISC licensed; a pinned verification-only subset of nine C translation units is sufficient. Selected. |
| Existing Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 profile | The configured profile does not implement Ed25519 verification. | Existing dependencies remain unchanged; no adapter or cryptographic fallback was added. |

The selected 1.0.22 verifier path differs from the previously tested 1.0.21 source only by upstream coverage annotations; the 1.0.22 utility header also contains upstream overflow-safe base64-length arithmetic. The exact 1.0.22 files were pinned and retested. Its release notes describe 1.0.22 as including the 1.0.21-stable changes; that earlier changelog records the point-validator security fix relevant to this profile. The libsodium subset includes upstream ref10 verification, SHA-512, canonical point/scalar routines, and narrow extraction adapters. The curve arithmetic is upstream code; no new elliptic-curve arithmetic was written. Signing, key generation, random-point/scalar APIs, allocator interfaces, and unrelated APIs are excluded. Source files and vectors are SHA-256 pinned and verified before build and test.

## Strict verification contract

The kernel-facing API is in `kernel/core/include/gxos_ed25519.h` and implemented by the pinned profile's guideXOS wrapper. It accepts only a 32-byte public key, a 64-byte signature, and an explicitly sized message of at most 4096 bytes. Null public key or signature pointers fail. A null message pointer is allowed only with a zero length and is mapped to a stable empty byte. The API returns `1` only on success and `0` for every invalid input or signature.

Before detached verification, the wrapper requires both `A` (public key) and `R` (first 32 signature bytes) to pass libsodium's canonical on-curve prime-subgroup point validator. This rejects all-zero encodings, identity and other low-order points, mixed-order points, invalid or noncanonical point encodings, and small-subgroup signature points. The detached verifier additionally rejects a noncanonical scalar `S` and checks the signature equation. This is an explicit pure-Ed25519 profile; cases Wycheproof labels `acceptable` are rejected rather than inherited as successful verification.

## Corpus and portable qualification

The reusable corpus is in `tests/vectors/` and runs through `tests/test_ed25519_strict.py` against both hosted objects and objects compiled with the AMD64 freestanding C flags.

- 238 unique cases per build; all passed in hosted and freestanding-compatible builds (476 total case executions).
- Wycheproof: 117 `valid` accepted, 64 `invalid` rejected, and 29 `acceptable` rejected.
- Six independent positive vectors: three RFC 8032 examples, the RFC SHA-512 example, and signatures over 1023-byte and 4096-byte messages independently generated with Python cryptography 50.0.2.
- Twenty-two direct API cases: 21 negative input/signature cases and one null-pointer empty-message success. Negative coverage includes changed message/signature, wrong valid key, all-zero inputs, identity and order-2 weak keys, invalid/noncanonical `A` and `R`, noncanonical `S`, truncated or oversized key/signature lengths, null pointers, and a 4097-byte message.
- Seven Python `cryptography` reference checks: all six independent signatures and rejection of a corrupted message.
- Corpus and pinned source inventory checks passed. The corpus source commit and generated-vector method are recorded in `tests/vectors/README.md`.

The RFC and Wycheproof vectors are independent of the backend. The reference check uses a general-purpose Ed25519 implementation to confirm valid-vector compatibility and ordinary bad-message rejection; guideXOS intentionally applies a stricter point-acceptance profile to adversarial cases.

## AMD64 kernel build and resources

The actual AMD64 target build completed and linked the selected implementation into both the PE kernel image and converted ELF image. The linked image contains `gxos_ed25519_verify_strict`, libsodium detached verification, and the point validator. The PE import directory has zero RVA and size, and the selected objects' undefined references resolve to linked verifier helpers or guideXOS memory routines. No signing, random-number, allocation, exception, RTTI, or hosted runtime interface is imported. No `crypto_scalarmult` API is exported or linked.

Recorded build measurements:

| Measurement | Result |
| --- | ---: |
| Linked PE kernel image | 2,964,220 bytes |
| Converted ELF kernel image | 2,859,616 bytes |
| Selected backend `.text` and `.text.unlikely` | 49,440 bytes |
| Selected backend `.rdata*` | 34,736 bytes |
| Selected backend `.data` / `.bss` | 0 / 32 bytes |
| Selected backend `.xdata` and `.pdata*` | 2,000 bytes |
| Freestanding object file total | 106,136 bytes |
| Reachable verifier call-chain stack upper bound | 3,824 bytes |
| Largest static frame among compiled backend functions | 4,240 bytes |

GCC `-fstack-usage` and `-fcallgraph-info=su` reports found no dynamic stack frames and no unmeasured reachable callees. The 4,240-byte largest frame belongs to an internal scalar-multiplication helper that is not reachable from the verification entry point; the verifier's measured reachable chain is 3,824 bytes. The AMD64 startup and exception stacks are each 16 KiB. This leaves about 12 KiB below the measured verifier chain for caller frames and other stack use; a future Roughtime caller must include its own locals in its stack budget. The object-file total includes object metadata and is reported separately from section totals and final image sizes.

Build qualification is kernel compilation and linkage only. No QEMU run or guest runtime verification was performed. NAVHTTPS7 guest trust-policy qualification remains pending QAC.

## Hosted regression results

| Regression | Result |
| --- | --- |
| Ed25519 strictness corpus | Pass: 238/238 hosted and 238/238 freestanding-compatible |
| Roughtime v1 verifier | Pass: 23/23 |
| Clock assurance | Pass |
| Trust profile | Pass |
| Secure randomness | Pass |
| DNS | Pass: 13 protocol scenarios and 6 controlled fixture checks |
| UDP/socket | Pass: 6 cases |
| TCP focused IPv4 attribution/connect cleanup | Pass; existing unrelated compiler warning at `tcp.cpp:636` |
| HTTPS qualification contract | Pass: 7 cases |
| Hosted Navigator smoke | 286/288; the two CSS inline-style/runtime-counter checks failed |

The latest NAVHTTPS10 report contains a 288/288 smoke run and records an earlier two-CSS-check variation. This NAVHTTPS11 run reproduced the same two checks failing. No CSS code was changed; the variation remains unresolved and is recorded without expanding the phase into CSS work.

## Production boundary and next milestone

No public HTTPS policy was enabled, no public CA candidate was approved, no certificate checks were relaxed, and no TLS verification path was redirected. CMOS time was not promoted to authenticated time, Roughtime was not activated in the kernel, no cross-boot rollback resistance is claimed, and physical I219-LM DMA handling was not modified. Existing ECDH, ECDSA, SHA-512, and Mbed TLS functionality remain on their existing paths.

Recommended NAVHTTPS12 scope: add a bounded kernel-side Roughtime response/signature verification test using a captured deterministic fixture and differential comparison against the accepted hosted RFC 10049 verifier. Keep it diagnostic-only: no network client, clock promotion, public HTTPS activation, or trust-policy change. Track guest execution separately under QAC and NAVHTTPS7.

## Primary references

- [RFC 8032 — Edwards-Curve Digital Signature Algorithm (EdDSA)](https://www.rfc-editor.org/rfc/rfc8032.html)
- [libsodium 1.0.22 signed release](https://github.com/jedisct1/libsodium/releases/tag/1.0.22-RELEASE)
- [libsodium 1.0.22 release changelog](https://github.com/jedisct1/libsodium/blob/77e1ce5d6dee871c49ef211222ba18ef0c486bda/ChangeLog)
- [libsodium point validation at the pinned commit](https://github.com/jedisct1/libsodium/blob/77e1ce5d6dee871c49ef211222ba18ef0c486bda/src/libsodium/crypto_core/ed25519/core_ed25519.c)
- [libsodium detached Ed25519 verification at the pinned commit](https://github.com/jedisct1/libsodium/blob/77e1ce5d6dee871c49ef211222ba18ef0c486bda/src/libsodium/crypto_sign/ed25519/ref10/open.c)
- [C2SP Wycheproof Ed25519 corpus at the pinned commit](https://github.com/C2SP/wycheproof/blob/65ca494727a52944c204441c860b039e6786b99b/testvectors_v1/ed25519_test.json)
- [HACL* project status and production package guidance](https://github.com/hacl-star/hacl-star)
- [Monocypher project test and audit guidance](https://github.com/LoupVaillant/monocypher)
- [Mbed TLS TF-PSA-Crypto 1.1.0 algorithm identifiers](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/v1.1.0/include/psa/crypto_values.h)
- [GCC stack and callgraph reporting options](https://gcc.gnu.org/onlinedocs/gcc-14.4.0/gcc/Developer-Options.html)
