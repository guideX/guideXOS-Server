# NAVHTTPS12 — Freestanding Roughtime v1 Verification

Date: 2026-11-12; branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`; starting HEAD: `7d810dde2117a59a789a110d0cd0408ab57d9124`
Outcome: **A — the bounded RFC 10049 v1 verifier is implemented, differentially qualified, and linked into the AMD64 kernel. Guest execution remains pending because QEMU/QAC is unavailable in this environment.**

## 1. Starting state and dependencies

The starting worktree had no tracked edits. The preexisting untracked `tests/__pycache__/` and `tmp/navhttps8-kernel-build/` directories were preserved. No branch change, reset, stash, rebase, merge, push, or I219-LM operation was performed.

NAVHTTPS9–11 were reviewed. The accepted Ed25519 profile remains libsodium 1.0.22, source commit `77e1ce5d6dee871c49ef211222ba18ef0c486bda`, with the existing strict wrapper and source inventory verification. `scripts/verify-libsodium-ed25519-profile.ps1` passed. The signed `1.0.22-RELEASE` tag authenticates the pinned commit; the commit itself is not described as signed. No cryptographic implementation or input policy was changed.

## 2. Portable parser and verification API

`kernel/core/include/gxos_roughtime_v1.h` exposes a C-compatible API implemented in `kernel/core/gxos_roughtime_v1.cpp`. It verifies one complete response against the exact request packet, expected 32-byte nonce, and pinned long-term server key. It uses the NAVHTTPS11 strict Ed25519 API for both signatures and the pinned SHA-512 implementation for key identity, request leaf, and Merkle nodes.

The implementation has no heap allocation, recursive parsing, global clock access, or clock-setting behavior. The caller owns the packet, key, result, and 1,216-byte scratch workspace. The evidence result is 200 bytes. Parser work is capped at 576 operations, maps at 16 pairs, nesting at three maps, response packets at 1,024 bytes, request packets at 1,036 bytes, and Merkle paths at 32 hashes. The RFC recommends UDP request messages of at least 1,024 bytes; the local request bound allows that message size plus the 12-byte wrapper. Response packets retain a separate 1,024-byte total bound. The verifier also rejects a response packet larger than its request, as required by the RFC.

Evidence is staged privately and copied to the caller only after all structural, signature, nonce, delegation, time, and Merkle checks pass. On rejection the result contains only a status and zero evidence fields. Scratch storage is cleared after each verification attempt. `production_policy_accepted` is always zero.

## 3. RFC 10049 compatibility decisions

The parser checks `ROUGHTIM` framing and exact encoded packet length, then validates little-endian map lengths, offsets, and tags. Tags must use the canonical uppercase ASCII form with optional right-side NUL padding, be strictly sorted by their little-endian tag value, and not repeat. Field offsets must be in range, ordered, and four-byte aligned. Known fields receive exact-length checks. Unknown syntactically valid tags are ignored as RFC 10049 requires; they do not bypass checks on required fields.

The request is checked for v1 support, type zero, the expected nonce, a 32-byte server identity, and zero-filled `ZZZZ` padding. The response must have type one, the same nonce, a bounded proof, and the required signed-response and certificate fields. The server identity is the first 32 bytes of `SHA-512(0xff || server_public_key)` and must match the request's `SRV` field.

The root key verifies the delegation using `Roughtime v1 delegation signature\0`. The delegated key verifies the signed response using `Roughtime v1 response signature\0`; the C string's terminating NUL is included in each transcript. The delegation's `MINT` and `MAXT` bounds must be ordered and contain the signed midpoint. The verifier accepts only protocol version 1, a nonzero radius, and a midpoint/radius interval within the supported 2024–2100 range. The 300-second radius ceiling and two-second maximum measured transport delay are bounded local acceptance policy, consistent with the hosted verifier.

The Merkle leaf is the first 32 bytes of `SHA-512(0x00 || complete_request_packet)`. Each path level hashes `0x01 || left || right`, truncates to 32 bytes, and selects sibling order from the corresponding least-significant-first index bit. The final node must equal the signed root; impossible high index bits are rejected. These rules follow [RFC 10049](https://www.rfc-editor.org/rfc/rfc10049.html). The implementation is intentionally a bounded verifier for the required v1 request/response subset, not a generic unbounded map library.

The existing Python reference had treated 1,024 bytes as the complete request packet limit. RFC 10049 recommends a UDP request message of at least 1,024 bytes; packet framing adds 12 bytes. The 1,036-byte request limit is a local finite bound that permits that full message size, not an RFC maximum. The 1,024-byte response cap is also a local bound. `tools/roughtime_v1.py` now applies the separate request limit, and a 1,036-byte request packet is covered by both implementations.

## 4. Independent fixture provenance

`tests/vectors/roughtime_roughenough_2_2_0.json` contains an actual response captured from an independently implemented Roughenough 2.2.0 server at upstream commit `16012fc1fd3d70ccdffeb8f00794bc124ca9e3d1`. The downloaded server archive SHA-256 was checked against the upstream release sidecar: `d4bb29bac6c89363f3f64c2091ecdeb8140665e9ae298385636d706762beecee`. The fixture is immutable and its JSON file digest is pinned in `tests/vectors/roughtime_v1_fixtures.sha256`.

The captured request is 1,024 bytes (SHA-256 `923fe7d85db83b4f2c6f6139bf9c95623b00b3603dec52e1084c7ef2a6888ff5`); the response is 416 bytes (SHA-256 `7ad184d649ac1ec4738a5aa88327d9bb4c6edafb2247b39a7076890b19e29cd9`). Its public key is `3b6a27bcceb6a42d62a3a8d02a6f0d73653215771de243a63ac048a18b59da29`, signed midpoint is `1794491571`, and radius is five seconds. Roughenough ran with its documented test-only all-zero seed option; no private key is included in the repository. This proves implementation interoperability for a recorded exchange, not freshness of a later measurement or trustworthiness of a production server.

## 5. Differential and malformed-input results

`scripts/test-roughtime-v1.ps1` now runs both the NAVHTTPS9 Python tests and the portable verifier tests. The latest run passed **40/40 unittest cases**: 23 reference tests and 17 portable tests. The portable tests compile the parser and the exact pinned Ed25519/SHA-512 sources into a temporary hosted library, then compare it with `tools/roughtime_v1.py` for controlled and independent valid fixtures.

For accepted responses, the tests compare status, server identity, delegation/signature/nonce/Merkle flags, request identity, midpoint and radius, interval, uncertainty, and monotonic receipt/delay evidence. Rejections are checked on the same delivered response for a wrong expected key, so key rejection is not inferred from a network timeout. Unknown valid extension tags are accepted by both implementations.

Negative cases cover corrupted response and delegation signatures, invalid delegation bounds, nonce mismatch and replay under another nonce, incorrect Merkle path/index, missing required fields, duplicate and unsorted tags, invalid tag padding, misaligned offsets, nonzero request padding, truncation, oversized frames, maximum-size request handling, timestamp/arithmetic bounds, excessive radius, unsupported version, limited buffers, and reuse after rejection. The suite also compares 96 single-bit response mutations and 96 deterministic malformed frames. Failed verification leaves every evidence field zero, including after workspace reuse.

## 6. AMD64 integration and resources

The actual kernel target was built with:

```text
mingw32-make -C kernel ARCH=amd64 BUILD_DIR=../tmp/navhttps12-kernel-build EXTRA_CFLAGS='-fstack-usage -fcallgraph-info=su' -j2
```

The command completed and linked `kernel.pe` and `kernel.elf`. The final images contain `gxos_roughtime_v1_verify`, `gxos_ed25519_verify_strict`, and `crypto_hash_sha512`. The PE import directory has zero RVA and size; no new dynamic import was introduced. The parser compiles without exceptions, RTTI, or hosted-library dependencies. Its symbols resolve in the final kernel link.

| Measurement | NAVHTTPS11 baseline | NAVHTTPS12 result |
| --- | ---: | ---: |
| PE kernel image | 2,964,220 bytes | 2,971,154 bytes (+6,934) |
| ELF kernel image | 2,859,616 bytes | 2,864,760 bytes (+5,144) |
| Parser object `.text` | — | 5,184 bytes |
| Parser object `.rdata*` | — | 240 bytes |
| Parser object `.data` / `.bss` | — | 0 / 0 bytes |
| Parser object `.xdata` / `.pdata` | — | 84 / 84 bytes |
| Parser object file size | — | 7,810 bytes on disk |
| Parser API frame | — | 912 bytes |
| Signature bridge frame | — | 80 bytes |
| Strict Ed25519 reachable call chain | 3,824 bytes | 3,824 bytes |
| Combined parser-to-Ed25519 call chain | — | 4,816 bytes |
| Caller workspace / result | — | 1,216 / 200 bytes |

The 4,816-byte combined call chain adds the parser API frame and signature bridge to NAVHTTPS11's measured strict-verifier chain. It excludes the caller's frame. If both workspace and result are allocated on the caller's stack, add 1,416 bytes, for 6,232 bytes plus the caller's own frame and locals. The existing startup and exception stacks are each 16 KiB. This is static build evidence, not a guest runtime measurement. The 1,216-byte workspace is zeroed by the API and includes transcript, digest, and Merkle scratch space.

## 7. Guest qualification

No QEMU process or `qemu-system-x86_64` executable was available on the host or PATH, so no QAC guest execution was attempted and no other project's reservation was touched. The independent valid fixture and negative cases ran in the portable hosted harness; AMD64 kernel compilation/linkage passed. Guest execution of the linked kernel verifier remains pending. NAVHTTPS7 guest trust enforcement remains a separate pending acceptance gate.

## 8. Clock and production boundary

The result exposes the pinned server identity, signature/nonce/Merkle status, signed midpoint and radius, bounded interval and uncertainty, request/nonce identity, and caller-supplied monotonic send/receive times. This is compatible evidence material for a future NAVHTTPS8 clock-assurance adapter, but this phase does not call `assess_authenticated_evidence()`, change the `Plausible` CMOS classification, set an operating-system clock, or produce an `Authenticated` clock state. A single response is not approved production time.

Public HTTPS remains disabled. No public CA candidate, Roughtime key set, multi-server quorum, freshness policy, or clock-source policy was approved. No certificate verification behavior, NAVHTTPS7 state, or physical I219-LM behavior was changed. I219-LM remains at `last_completed=PHASE26`, `next_expected=PHASE27`.

## 9. Regression results and CSS status

| Regression | Result |
| --- | --- |
| NAVHTTPS9 plus portable Roughtime | PASS, 40/40 unittest cases |
| NAVHTTPS11 strict Ed25519 corpus | PASS, 238/238 hosted and 238/238 freestanding-compatible |
| Clock-assurance contract | PASS |
| Trust-profile enforcement | PASS |
| Secure-random contract | PASS |
| DNS | PASS, 13 protocol scenarios and 6 controlled-fixture checks |
| UDP/socket | PASS, 6 cases |
| TCP connect/cleanup | PASS; the existing unrelated warning at `tcp.cpp:636` remains |
| HTTPS qualification contract | PASS, 7 cases |
| Hosted Navigator smoke | PASS; `NAVIGATOR_SMOKE_RESULT: PASS` in `logs/navigator-hosted-smoke-20261112-060518.log` |

Both `CSS inline styles load` and `CSS inline runtime counters` passed in the latest smoke run. The intermittent two-check failures recorded in NAVHTTPS9–11 remain unresolved across runs; this pass does not establish their cause or close that regression. No CSS code or assertion was changed. Controlled DNS-to-HTTPS behavior passed its existing checks.

## 10. Recommended NAVHTTPS13

Prioritize exclusive QAC guest execution of the linked verifier with the immutable Roughenough response and deterministic wrong-key, signature, nonce, and Merkle failures. Keep the run bounded and diagnostic, then close NAVHTTPS7's separate guest trust-policy cases under the same reservation only if the combined run preserves each test's evidence and scope. Do not add live UDP acquisition or clock promotion to that qualification step. A later milestone can design a clock-assurance adapter and multi-source policy after guest results, operator/key lifecycle, quorum, uncertainty, and freshness requirements receive separate review.

## References

- [RFC 10049 — Roughtime](https://www.rfc-editor.org/rfc/rfc10049.html)
- [Roughenough 2.2.0 release](https://github.com/int08h/roughenough/releases/tag/2.2.0)
- [Roughenough pinned source commit](https://github.com/int08h/roughenough/tree/16012fc1fd3d70ccdffeb8f00794bc124ca9e3d1)
- [libsodium 1.0.22 signed release tag](https://github.com/jedisct1/libsodium/releases/tag/1.0.22-RELEASE)
- [NAVHTTPS8 clock-assurance contract](NAVIGATOR_HTTPS_TIME_ASSURANCE_NAVHTTPS8.md)
- [NAVHTTPS9 hosted verifier](NAVIGATOR_HTTPS_AUTHENTICATED_TIME_NAVHTTPS9.md)
- [NAVHTTPS10 interoperability assessment](NAVIGATOR_HTTPS_ROUGHTIME_PORTABLE_NAVHTTPS10.md)
- [NAVHTTPS11 strict Ed25519 backend](NAVIGATOR_HTTPS_ED25519_NAVHTTPS11.md)
