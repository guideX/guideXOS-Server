# NAVHTTPS10 — Roughtime Interoperability and Freestanding Ed25519 Assessment

**Outcome B — RFC 10049 v1 interoperability is demonstrated against an independent server. No freestanding Ed25519 backend cleared strict-input qualification, so kernel response verification remains blocked and hosted-only.**

## 1. Repository preflight and preserved state

- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `2676754fc238086061731d61b0c43931f929ebf8` (the requested commit).
- The initial worktree contained the two requested untracked directories, `tmp/navhttps8-kernel-build/` and `tests/__pycache__/`. Both were preserved. No branch switch, rebase, merge, stash, reset, or clean was used.
- I219-LM phase state remains `last_completed=PHASE26`, `next_expected=PHASE27`. No I219-LM source, physical NIC setting, or phase record changed.
- NAVHTTPS7 guest qualification remains **PENDING**. This phase did not request QAC admission or start QEMU.
- NAVHTTPS7–9 reports were reviewed. The hosted NAVHTTPS9 verifier remains accepted only for hosted Python use; no kernel Roughtime capability is inferred.

## 2. Independent RFC 10049 v1 interoperability

RFC 10049 is an Experimental protocol. Its v1 request and response use the `ROUGHTIM` datagram framing, sorted tag maps, Ed25519 signatures, a delegated signing key, a client nonce included in a Merkle tree, and a signed midpoint/radius interval. The protocol authenticates what a server reported after receiving the fresh request; one server does not establish that its clock is correct.

The independent controlled fixture was **Roughenough 2.2.0**, upstream release commit `16012fc1fd3d70ccdffeb8f00794bc124ca9e3d1` (signed GitHub release tag). Its x86-64 Linux server asset SHA-256 was verified as `d4bb29bac6c89363f3f64c2091ecdeb8140665e9ae298385636d706762beecee`. The server ran locally in WSL2 Ubuntu 24.04 on UDP port `15319`; the guideXOS client ran on Windows and sent to the WSL address. This was an independent Rust server implementation, not the Python test server used by NAVHTTPS9.

A fresh 32-byte test-only identity seed was generated for this run, kept outside the repository, and removed from the server's launch directory after qualification. The server-confirmed public key was:

```text
101e9d6505c2011a1f4ba3b6146067fa60495ccaf5cc3da09228a20c0cb01b90
```

This key is test-only and is not a production identity. The expected-key client command was:

```powershell
python -B tools/roughtime_v1.py --server-ip <WSL-IP> --port 15319 `
  --server-key-hex 101e9d6505c2011a1f4ba3b6146067fa60495ccaf5cc3da09228a20c0cb01b90 `
  --timeout 5
```

The direct query exited 0 with protocol version 1, verified root and delegated signatures, matched nonce, verified Merkle inclusion, and returned a bounded time interval. The recorded response had `MIDP=1794426717`, server interval `[1794426712, 1794426722]`, and estimated uncertainty of 5 seconds. The evidence result remained `not-assessed-single-source`, `not-assessed-no-persistent-floor`, and `production_policy_accepted=false`.

A one-message test relay also captured a successful exchange and verified that the upstream response came from the configured Roughenough address. It recorded a 1024-byte request with nonce `ab0a8e539014083d06c8d9b9ad1a08e845f53206c7ddc840349c7de7157cb242`, request SHA-256 `a1a1131451c65c591e0dd2dc77961ba95c24fd8997ccb14f3c8aa0cf5d43cf14`, and 416-byte response SHA-256 `a3a3b10e9addcdbf48dbdfb89df14d6f566a31bbb3d9f93da486d1b8e8cf9395`.

Negative interoperability checks:

- Changing the expected key changed the request's `SRV` identity. Roughenough correctly sent no reply; the client rejected the attempt with `Roughtime UDP request timed out` (exit 2). The hosted suite separately verifies that a response signed under a different long-term key is rejected.
- The relay flipped one bit in the independent response's top-level `SIG`. The client rejected that actual response with `response signature verification failed` (exit 2). The original response SHA-256 was `2ef59f71446cf426cf8f93811ef0736664e013a19a87295acac017f6b7031c75`; the modified response SHA-256 was `251b9a57103b736463bd208ae7396bb3ae3428b0516dc42d9dd43467d02dcb98`.

This demonstrates independent single-server RFC v1 wire interoperability for the hosted client, including delegation, key pinning, nonce binding, signature checks, Merkle inclusion, timestamp, and uncertainty. It does not qualify a freestanding client, a server operator policy, or production time.

## 3. Freestanding Ed25519 dependency assessment

The active dependency profile verifier passes for Mbed TLS **4.1.0**, TF-PSA-Crypto **1.1.0**, and three repository patches. These exact versions and commits are pinned in `guidexos/mbedtls.lock` and `guidexos/mbedtls_profile.json`.

The active TF-PSA headers define EdDSA algorithm and Edwards family identifiers, but `tf-psa-crypto/include/psa/crypto_values.h` explicitly says Mbed TLS does not support Edwards curves yet. The guideXOS crypto configuration requests ECDSA and NIST prime curves, not PureEdDSA/Ed25519. The linked source plan contains ECDH/ECDSA and SHA-512 sources but no Edwards verifier. Header constants are not an implementation.

Monocypher 4.0.3 was assessed as a portable C candidate at upstream commit `ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f`; its source is dual-licensed BSD-2-Clause or CC0-1.0. A compile-and-run negative probe against its Ed25519 API passed an all-zero signature with an all-zero low-order public key (`crypto_ed25519_check` returned 0, meaning accepted). Its verification equation clears the cofactor, so this candidate does not meet this phase's strict public-key validation requirement without an additional, independently reviewed key-validation layer. It was not copied into the repository or linked into guideXOS.

**Backend decision: none selected.** The existing Mbed TLS/TF-PSA profile has no implementation, and the assessed portable C candidate did not meet the strict-input bar. No Ed25519 arithmetic was written from scratch. There is no kernel verification API, known-answer suite, stack measurement, or memory bound for an integrated backend yet. This is the exact blocker to freestanding signature verification.

## 4. Portable verifier status

The NAVHTTPS9 Python verifier remains the only complete response verifier. It checks bounded v1 framing and tag maps, the expected long-term key, delegation validity and signature, delegated response signature, nonce binding, Merkle inclusion, timestamp bounds, radius/uncertainty, response source, monotonic freshness, and sequential interval consistency. It produces evidence but does not set the guideXOS clock.

No C/C++ response parser or kernel time provider was added. The hosted verifier's results remain a differential reference for a later freestanding implementation; they do not establish that the kernel can parse or verify a Roughtime response.

## 5. Clock-assurance integration boundary

NAVHTTPS8 exposes `gxos::clock::AuthenticatedEvidence`, `assess_authenticated_evidence()`, and `GxosClockAssuranceInfo`. The existing evidence helper checks metadata, source identity, signature and nonce results, a bounded Unix interval, and monotonic age. It is a contract check, not a provider or state setter. It does not perform multi-server consistency, enforce an uncertainty policy, establish cross-boot rollback protection, or authorize TLS.

A future provider can submit Roughtime evidence only after it:

1. Verifies the pinned root signature, delegation signature and validity interval, delegated signature, fresh nonce, and Merkle inclusion.
2. Builds a bounded estimate interval from the signed midpoint/radius and measured transport delay; records its uncertainty and monotonic receipt/freshness deadline.
3. Applies a separately approved policy across at least three operator-independent sources, including interval consistency and conflict rejection. A valid single response is not sufficient to promote the clock.
4. Passes the policy-accepted interval to the clock contract, populating source, interval, uncertainty, and freshness fields. The contract should carry or explicitly enforce the uncertainty bound before any status can become `Authenticated`.
5. Preserves the current within-boot continuity tracker and rollback latch. Invalid, stale, inconsistent, or rollback-suspected evidence must remain unusable by TLS.

No current source calls the evidence helper or sets `Authenticated`. The CMOS clock remains `Plausible`; invalid-clock fail-closed behavior and certificate date validation remain intact. No cross-boot rollback protection is claimed, and no kernel clock was changed by this phase.

## 6. Validation results

| Check | Result |
|---|---|
| NAVHTTPS9 hosted Roughtime v1 tests | PASS: 23/23. |
| Clock assurance contract | PASS. |
| HTTPS trust-profile enforcement | PASS. |
| HTTPS qualification outcome contract | PASS: 7 cases. |
| DNS resolution | PASS: 13 protocol scenarios + 6 controlled fixture checks. |
| UDP/socket wire bounds | PASS: 6 cases. |
| Secure-random contract | PASS. |
| Mbed TLS dependency profile | PASS: Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 / 3 patches. |
| AMD64 kernel build | PASS: `mingw32-make -C kernel ARCH=amd64 BUILD_DIR=../tmp/navhttps10-kernel-build`; linked `kernel.elf` (2,767,264 bytes). This build includes the existing ECDH/ECDSA/SHA-512 profile and no Ed25519 verifier. No kernel source changed. |
| QEMU / guest qualification | QEMU not used. NAVHTTPS7 guest acceptance remains PENDING QAC admission. |
| Hosted Navigator/TLS smoke | PASS: `scripts/smoke-navigator-hosted.ps1 -Build`; `NAVIGATOR_SMOKE_RESULT: PASS`, 288/288 checks. The existing hosted Schannel example.com check and controlled localhost HTTPS checks passed. |

The build and hosted tests do not establish kernel Ed25519 verification or guest execution. No backend-specific KATs were added because no candidate cleared backend qualification.

## 7. Existing CSS regression

Keep the intermittent failures visible. The earlier same-day log [`navigator-hosted-smoke-20261111-082640.log`](../logs/navigator-hosted-smoke-20261111-082640.log) failed `CSS inline styles load` and `CSS inline runtime counters`, while inline-polish, inline-1D, style-block, and other Navigator checks passed. After a fresh build, [`navigator-hosted-smoke-20261111-115540.log`](../logs/navigator-hosted-smoke-20261111-115540.log) passed both checks and all 288 smoke assertions.

The assertion named `CSS inline styles load` checks navigation and extracted text markers; it does not assert computed style values. The local smoke-server route contains those markers and inline declarations. The later pass demonstrates run-to-run variation, but does not identify its cause as a harness issue or a CSS baseline issue. No CSS assertions were weakened and no CSS implementation was changed.

## 8. Production and hardware boundaries

- Public bare-metal HTTPS remains disabled. No production CA candidate or Roughtime key was approved.
- The existing hosted smoke did contact `https://example.com/` through Windows Schannel and passed its separate localhost HTTPS fixture checks. This exercised the existing hosted regression path; it did not change bare-metal public trust, the release trust default, or production HTTPS policy.
- A single valid Roughtime response does not transition the clock to `Authenticated` or authorize TLS.
- No insecure time fallback was added; certificate date validation remains enabled.
- NAVHTTPS7 guest qualification is still pending. No QEMU process was started or interrupted.
- I219-LM DMA safety and the `PHASE26` / `PHASE27` tracker remain unchanged.
- Production multi-server acceptance, authenticated key-list distribution, revocation, cross-boot rollback protection, and release trust authorization remain separate gates.

## 9. Next bounded milestone

Select a maintained verifier that rejects non-canonical and weak Ed25519 inputs, or obtain a reviewed backend change in the existing crypto profile. Add RFC 8032 known-answer and negative vectors first, then prove freestanding AMD64 compilation/linkage, stack and memory bounds, and deterministic verification tests. Only after those pass should the smallest bounded Roughtime parser be ported against the hosted differential oracle. Keep the provider diagnostic-only until multi-source policy and the NAVHTTPS8 evidence handoff are implemented and separately guest-qualified.

## References

- [RFC 10049 — Roughtime](https://www.rfc-editor.org/rfc/rfc10049.html)
- [Roughenough 2.2.0 signed release](https://github.com/int08h/roughenough/releases/tag/2.2.0)
- [Roughenough pinned source commit](https://github.com/int08h/roughenough/tree/16012fc1fd3d70ccdffeb8f00794bc124ca9e3d1)
- [Monocypher Ed25519 API and input caveats](https://monocypher.org/manual/ed25519)
- [Mbed TLS TF-PSA EdDSA/Edwards definitions in the pinned dependency](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/v1.1.0/include/psa/crypto_values.h)
- [NAVHTTPS8 clock-assurance contract](NAVIGATOR_HTTPS_TIME_ASSURANCE_NAVHTTPS8.md)
- [NAVHTTPS9 hosted Roughtime verifier](NAVIGATOR_HTTPS_AUTHENTICATED_TIME_NAVHTTPS9.md)
