# NAVHTTPS9 — Roughtime Authenticated-Time Foundation

**Outcome B — a bounded hosted RFC 10049 v1 UDP client now produces cryptographically verified, nonce-bound time evidence with explicit uncertainty and monotonic expiry. Kernel integration and production clock authorization remain blocked. Public HTTPS remains disabled.**

## 1. Repository preflight and preserved state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`.
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `7d0f65cf2a326b3b682d166c309014fe9916c47e` (the requested commit).
- Starting worktree: the only untracked path was `tmp/navhttps8-kernel-build/`. It was left in place and not cleaned, reset, stashed, or hidden.
- The branch was already two commits ahead of its upstream. This phase did not switch branches or push.
- I219-LM audit state remains `last_completed=PHASE26`, `next_expected=PHASE27`; no I219 source, hardware, or audit record was changed.
- NAVHTTPS7 guest qualification remains **PENDING**. No QEMU process was started for this phase.

NAVHTTPS6, NAVHTTPS7, and NAVHTTPS8 reports were reviewed. The existing trust profile still requires authenticated, fresh, rollback-protected time for candidate/public trust, and the bare-metal public HTTPS path remains hard-disabled.

## 2. RFC 10049 and interoperability assessment

RFC 10049 (October 2026) is an Experimental protocol, not an Internet Standards Track specification. Version 1 uses an eight-byte `ROUGHTIM` datagram prefix, a little-endian message length, and sorted tag/value maps. UDP requests and responses occupy one datagram. The request advertises v1, includes a fresh 32-byte nonce, identifies the expected long-term Ed25519 key with `SHA-512(0xff || key)[0:32]`, and is padded with zero-valued `ZZZZ` data to 1024 bytes.

A valid response must bind the nonce into the signed Merkle root, carry response type 1, provide a bounded Merkle path and index, and include a signed response with v1, midpoint, nonzero radius, server-version list, and root. The root key signs a delegated key certificate with the `Roughtime v1 delegation signature\0` context; the delegated key signs the response with `Roughtime v1 response signature\0`. Delegated-key `MINT`/`MAXT` must contain the signed midpoint. The Merkle proof hashes the full request datagram and truncates SHA-512 results to 32 bytes. A correct single response proves authenticated provenance and response freshness relative to the nonce. It does not prove the server's clock is correct.

The older Google Roughtime protocol is not wire-compatible with RFC 10049 v1: its published protocol uses a 64-byte nonce, microsecond timestamps, and full 64-byte SHA-512 Merkle nodes. Cloudflare's currently documented implementation supports IETF drafts 08 and 11 and warns that backward compatibility is not guaranteed. The Rust `int08h/roughenough` implementation documents RFC 10049 v1 plus optional legacy draft negotiation. The hosted fixture validates the RFC v1 encoding locally; no independent public server interoperability test was run. Roughtime's name alone is not evidence of wire compatibility.

## 3. Cryptographic and randomness feasibility

The pinned Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 source set includes SHA-512 and `PSA_WANT_ALG_SHA_512` is enabled. The kernel secure-random provider offers bounded RDSEED/RDRAND/virtio-rng sources and fails closed if none can fill the full request. However, the selected TF-PSA source list has no Ed25519/Twisted Edwards verifier implementation. Its public headers define EdDSA identifiers, but the guideXOS build does not compile an implementation or request the corresponding algorithm/key family. Header constants alone do not provide cryptographic capability.

The hosted verifier uses pyca/cryptography 50.0.2 for Ed25519 verification and Python's `hashlib` for SHA-512. Its pinned dependency is isolated in `requirements-roughtime.txt`; it does not alter the kernel cryptography profile. Hosted request nonces use Python's operating-system CSPRNG through `secrets.token_bytes(32)`, with no weak fallback. Deterministic test keys and injected nonce functions exist only in the test fixture.

## 4. Hosted client implementation

`tools/roughtime_v1.py` implements a one-server RFC 10049 v1 UDP measurement. It accepts numeric IPv4 or IPv6 addresses, a port, and an explicitly supplied 32-byte expected public key. It does not perform DNS, update trusted keys, or load an unsigned server list as production authorization.

The client sends one 1024-byte request, uses a finite timeout (default two seconds, hard maximum 30 seconds), and accepts at most a 1024-byte response. It validates the exact response source IP and port, packet wrapper and lengths, map/tag bounds and ordering, required fields, supported versions, both Ed25519 signatures, delegated-key validity, nonce binding, and the bounded Merkle inclusion proof. It closes its UDP socket on every path. Any parse, crypto, source, timeout, randomness, or time-policy failure returns rejection and no evidence.

The command-line output is a JSON evidence record. A valid record is labeled `cryptographically-verified-single-source`; `production_policy_accepted` is always false.

To run it, install the pinned hosted dependency and supply a numeric server address plus the expected public key explicitly:

```powershell
python -m pip install -r requirements-roughtime.txt
python tools/roughtime_v1.py --server-ip <numeric-ip> --port 5319 --server-key-hex <32-byte-public-key-hex>
```

## 5. Signed-response validation evidence

The deterministic hosted suite has **23 passing tests**. It covers a valid signed response from a local controlled UDP fixture; wrong long-term key; response and delegation signature corruption; inverted delegated-key interval; incorrect nonce and replay; invalid Merkle root and index; unsupported version; malformed and truncated packets; excessive radius; unsupported timestamp interval and arithmetic overflow; expired evidence; inconsistent sequential intervals; missing/short secure randomness; timeout; and unexpected response source. Every negative case rejects evidence. Fixture signing keys are test-only and are not trust anchors.

## 6. Time interval, uncertainty, and freshness handling

Each evidence record contains the validated numeric source, long-term key identity, signed midpoint and server interval, interval at response receipt, measured monotonic transport delay, estimated uncertainty, monotonic receipt time, monotonic evidence deadline, signature/nonce/Merkle status, consistency status, and rollback status.

For a signed midpoint `MIDP`, radius `RADI`, and measured request-to-response delay `d`, the hosted result conservatively records the server interval as `[MIDP-RADI, MIDP+RADI]` and expands the upper bound at receipt by `ceil(d)` seconds. Its estimate is the midpoint of that expanded interval and its uncertainty is the rounded-up half-width. Zero or excessive radius, timestamps outside the guideXOS supported range, delay over the configured cap, and overflow/bounds violations reject the evidence. Evidence expires after a monotonic lifetime (300 seconds by default); no wall-clock timer is used for freshness.

The record marks consistency as `not-assessed-single-source` and rollback as `not-assessed-no-persistent-floor`. The client does not set guideXOS clock state, extrapolate uncertainty after receipt, or claim within-boot/cross-boot rollback protection. The existing clock-assurance contract remains unchanged because no kernel provider consumes this hosted record yet.

## 7. Multiple-server assurance and policy

RFC 10049 recommends at least three operational servers run by different parties. Its measurement sequence queries selected servers sequentially, repeats the same order a second time, derives follow-up nonces from the full prior response plus fresh random bytes, and checks all earlier/later response intervals for causal consistency. Violations are cryptographic evidence of server inconsistency; adjudication and key revocation are an ecosystem policy outside the wire protocol.

This milestone implements the RFC pairwise interval consistency predicate and a deterministic conflicting-interval test, but it does not implement the six-query multi-server sequence, malfeasance report transport, server-list updater, or production quorum. A future policy should require at least three distinct operators, release-authenticated pinned public keys with reviewed rotation/revocation, fresh measurements at boot, a maximum radius and transport delay, an acceptable interval spread, and fail-closed behavior on conflict, stale evidence, or unavailable quorum. Distinct keys alone do not prove distinct operators. Without protected persistent state, a fresh quorum is needed after every boot; a signed online measurement cannot establish cross-boot rollback protection by itself.

## 8. Kernel integration boundary

No kernel integration was attempted. The precise blocker is the absence of an Ed25519 verification implementation in the selected freestanding Mbed TLS/TF-PSA source set. SHA-512, secure randomness, and UDP primitives are present, but they are insufficient to authenticate RFC 10049 signatures. Adding a crypto implementation or replacing the approved dependency profile requires a separate reviewed and measured change, including public-key import/verification tests, memory and stack bounds, kernel build qualification, and guest qualification. No unsigned time or unauthenticated fallback was added.

## 9. Navigator HTTPS and trust-profile boundary

Three decisions remain separate:

1. A successful signature, nonce, and Merkle check creates authenticated single-server provenance.
2. A configured source-diversity, uncertainty, freshness, and rollback policy may accept time evidence for a clock decision.
3. The signed trust profile, CA policy, revocation decision, and explicit release authorization govern public HTTPS.

This client reaches only the first decision. A single fixture response cannot satisfy NAVHTTPS7's production clock requirement. The public CA candidate remains unapproved, certificate date checks remain enabled, and bare-metal/public-pilot authorization remains disabled. No trust defaults or public-HTTPS gates changed.

## 10. Hosted regressions and CSS findings

| Suite | Result |
|---|---|
| Roughtime v1 hosted validation | PASS: 23 tests, including local UDP fixture and all listed reject cases. |
| Clock assurance | PASS: deterministic RTC/PIT continuity, rollback, freshness, and certificate-time contract. |
| Trust-profile enforcement | PASS: profile version/hash binding, missing evidence, and non-self-authorizing approval cases. |
| TLS verification | PASS for the hosted Schannel path: trusted response, certificate-validation-enabled contract, hostname diagnostics, and downgrade blocks. This does not qualify the freestanding Mbed TLS guest verifier. |
| Mbed TLS dependency profile | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, and three repository patches. |
| Secure random | PASS: hosted secure-random contract. |
| DNS | PASS: 13 protocol scenarios plus 6 controlled-fixture checks. |
| UDP/socket | PASS: 6 wire-bound cases. |
| TCP cleanup | PASS: failed SYN, pending SYN close, socket bounds, and IPv4 attribution. Existing `ephemeral > 65535` type-limits compiler warning remains. |
| HTTPS qualification contract | PASS: 7 classification cases. |
| Navigator UI smoke | Build PASS; overall smoke **FAIL** on exactly the two CSS checks below. All other smoke checks, including hosted TLS/Schannel and downgrade checks, passed. The built-in hosted Schannel compatibility case also contacted `https://example.com`; it was not a bare-metal public-pilot test. |
| CSS inline-style assertions | **FAIL, unresolved.** `CSS inline styles load` and `CSS inline runtime counters` failed on 2026-11-10 and again on 2026-11-11. An earlier 2026-11-08 run passed both. Only these two checks failed in the current run; related style-block, inline-polish, and inline-1D smoke checks passed. This is an existing intermittent baseline/harness result, not a NAVHTTPS9 CSS change. No CSS code was changed. |

No hosted Mbed TLS certificate-verification runtime suite is present; hosted TLS evidence above uses Windows Schannel. No kernel build was run because no kernel files or dependencies were changed and kernel Ed25519 capability is absent. QEMU usage: none.

## 11. NAVHTTPS7 guest qualification and remaining dependencies

NAVHTTPS7's guest qualification remains **PENDING** after its earlier QAC admission was refused while another project's QEMU process owned the resource. No QAC admission was requested in NAVHTTPS9, no guest case was run, and no pass is inferred. The I219-LM audit still records `last_completed=PHASE26`, `next_expected=PHASE27`.

Remaining security dependencies include a freestanding Ed25519 verifier; independent operator selection and signed key lifecycle; multi-server sequencing and conflict response; an authenticated source policy; a clock provider that safely bridges interval evidence to the existing RTC/PIT continuity contract; a defined maximum uncertainty/freshness policy; a release-authenticated trust profile; revocation policy; and protected persistent state if cross-boot rollback resistance is required.

## 12. Recommended NAVHTTPS10 milestone

Evaluate and integrate a reviewed Ed25519 verifier into the pinned freestanding crypto profile, prove implementation and memory bounds, and add PSA-level RFC 8032 vectors before kernel networking. Then build a bounded kernel response-verification path behind the existing UDP and clock-assurance interfaces, initially in a non-authorizing diagnostic mode. Separately define a release-authenticated list of at least three operator-diverse RFC 10049 v1 keys and a two-pass consistency policy. Keep public HTTPS disabled until the provider is guest-qualified, its uncertainty/freshness policy is approved, and NAVHTTPS7's independent trust-profile and revocation gates are closed.

## References

- [RFC 10049 — Roughtime: A Protocol for Rough Time Synchronization](https://www.rfc-editor.org/rfc/rfc10049.html)
- [Google's older Roughtime protocol](https://roughtime.googlesource.com/roughtime/+/HEAD/PROTOCOL.md)
- [Cloudflare Roughtime implementation and draft-version status](https://github.com/cloudflare/roughtime)
- [Rust `int08h/roughenough` RFC 10049 v1 implementation](https://github.com/int08h/roughenough)
- [pyca/cryptography Ed25519 documentation](https://cryptography.io/en/latest/hazmat/primitives/asymmetric/ed25519/)
- [pyca/cryptography 50.0.2 package metadata](https://pypi.org/project/cryptography/50.0.2/)
