# NAVHTTPS16 — Authenticated Time-Evidence Integration and Clock Assurance

**Outcome B — hosted implementation and both AMD64 kernel builds pass; guest qualification is pending QAC exclusivity.** This phase adds a bounded, read-only, within-boot time estimate for the already-qualified NAVHTTPS15 fixture agreement. It does not set the CMOS or OS clock, authorize production time, or enable public HTTPS.

## 1. Preserved NAVHTTPS15 baseline

NAVHTTPS15 remains the authority for the three-source UDP acquisition and agreement result. Its corrected guest acquired three signed replies from the 401/402/403 simulated operators, verified all three signatures and nonces, and reported `agreement-satisfied`. The guest left `trusted_time=not-accepted`, the clock unchanged, and public HTTPS disabled. The baseline hosted Roughtime suite passed 80/80 and strict Ed25519 passed 238 hosted and 238 freestanding cases. The complete earlier evidence and artifact hashes remain in [NAVHTTPS15](NAVIGATOR_HTTPS_ROUGHTIME_MULTISOURCE_NAVHTTPS15.md).

The NAVHTTPS15 corrected guest serial, PCAP, and fixture logs were retained at:

| Artifact | SHA-256 |
| --- | --- |
| `logs/navhttps15-guest-20261114-143930/guest-serial.log` | `954b080f9d28e9df437afc6a551cb3e947f46c938ed2c016a1a6f443d062f90f` |
| `logs/navhttps15-guest-20261114-143930/guest-network.pcap` | `f836a2ac606b83d70f0ea41b60802bb8e945fea39b4d51c843138bda5b041005` |
| `logs/navhttps15-guest-20261114-143930/fixture-server.log` | `f8b12054a15cf21dc2e69b17da75df26dc0224f61fd324a1962352f6312c6bad` |

## 2. Read-only estimate contract

`gxos::clockassurance::TimeEstimateInfo` exposes a bounded UTC interval, monotonic measurement anchor and last observation, configured and accepted source counts, distinct operator and key identities, policy identity, age and uncertainty bounds, remaining freshness, scope, state, and a reason. It stores a lower and upper bound instead of treating the evaluator's midpoint as exact time. Its fixed diagnostic view is 216 bytes; the singleton store is statically allocated and has no heap or background-service lifecycle.

The store states are `NoEvidence`, `Fresh`, `Advanced`, `Expired`, `Conflicting`, `ContinuityFailure`, `RollbackSuspected`, `Invalidated`, and `Rejected`. `NoEvidence` starts with `NoAuthenticatedPersistentState`. Every new boot starts in that state because there is no authenticated persistent time floor.

Acceptance is deliberately narrower than general production time: policy identity `0x1501`, exactly three configured endpoints, the pinned NAVHTTPS15 fixture keys, operator IDs 401–403, a two-of-three quorum, 300-second evidence age, 30-second maximum uncertainty, and the existing evaluator's bounded transport and forward-jump limits. The store accepts a round result, not a caller's timestamp. It re-runs `gxos_roughtime_agreement_evaluate()` over the opaque verifier-issued evidence tokens and compares the resulting interval and provenance with the claimed result before retaining it. A copied or caller-edited result cannot change the accepted timestamp interval.

The resulting scope is `FixtureOnly`; `productionPolicyAccepted`, `systemClockChanged`, and `publicHttpsEnabled` remain zero. The existing CMOS `Plausible` classification remains separate and unchanged. There is no API to promote this fixture result to `ProductionApproved` or to write the system clock.

## 3. Advancement and lifecycle

For elapsed monotonic nanoseconds `e`, the store advances the interval conservatively: the lower bound uses whole elapsed seconds minus the 500 ppm drift allowance and PIT resolution allowance; the upper bound uses rounded-up elapsed seconds plus those allowances. The current uncertainty grows by the same drift and one-second resolution allowance after nonzero elapsed time. Arithmetic is overflow-checked; the result expires on the earlier of its bounded evidence lifetime and the uncertainty limit. At 300 seconds of age, the current interval and remaining lifetime are cleared.

A monotonic value below the measurement anchor or last observation invalidates the current interval and latches rollback until reboot. A reported clock discontinuity also latches failure. Subsequent evidence with a lower interval is treated as rollback; evidence too far forward fails continuity; a non-overlapping but allowable disagreement is marked conflicting and made unavailable. A changed policy invalidates old evidence. Explicit invalidation clears availability, current bounds, and remaining lifetime while retaining fixture provenance for diagnostics. A reboot has no accepted evidence because persistent state is not authenticated.

The estimate is diagnostic evidence only. Monotonic advancement does not claim that a single exact Unix timestamp is known, and it does not change CMOS, wall-clock, certificate verification policy, or public HTTPS readiness.

## 4. Diagnostics

The shell exposes:

- `roughtime-fixture-time` — run the pinned three-source fixture acquisition against QEMU user-net host `10.0.2.2`, UDP ports 5319–5321, and retain a qualifying result as fixture-only evidence.
- `roughtime-time` — query and advance the current estimate using PIT monotonic ticks; this command performs no acquisition.

The output includes acceptance scope, source counts, policy, accepted/current intervals, uncertainty, freshness, state/reason, and explicit `system clock changed=no` / `public HTTPS enabled=no` fields. Unsigned 64-bit Unix values use the verified bounded formatter. The existing `roughtime-agree` diagnostic and NAVHTTPS15 evaluator remain intact.

## 5. Hosted verification

| Check | Result |
| --- | --- |
| Roughtime hosted and portable differential suite | **90/90 PASS**, preserving the NAVHTTPS15 80-test baseline plus 10 estimate lifecycle/provenance tests |
| Strict Ed25519 corpus | **238 hosted + 238 freestanding PASS**, zero failures |
| Clock assurance | PASS |
| Navigator HTTPS trust-profile contract | PASS |
| Secure-random contract | PASS |
| DNS resolution | PASS, 13 protocol + 6 controlled-fixture cases |
| UDP/socket wire bounds | PASS, 6 cases |
| TCP cleanup and IPv4 attribution | PASS; the existing `ephemeral > 65535` type-limits warning remains visible |
| HTTPS qualification contract | PASS, 7 cases |
| Hosted Navigator smoke | **288/288 PASS**, `NAVIGATOR_SMOKE_RESULT: PASS`; log `logs/navigator-hosted-smoke-20261114-154051.log` |
| Missing-profile serial reclassification check | PASS: PowerShell parser clean; actual prior serial matches expected fail-closed markers |

The hosted smoke's inline CSS checks passed in this run. Earlier NAVHTTPS15 smoke history still records an intermittent failure in CSS inline-style/runtime-counter checks under concurrent compiler load and an isolated rerun that passed. No CSS or compositor code changed in NAVHTTPS16. The current smoke changed only `desktop.json`'s recent-app list; that generated side effect was restored to the clean preflight value.

The estimate tests cover valid three-source acceptance; one-source and duplicate-operator rejection; unpinned keys; invalid signatures; rejected and ambiguous agreements; caller-edited intervals and monotonic anchors; interval advancement and uncertainty growth; expiry and unsigned-counter boundaries; rollback, discontinuity and excessive forward evidence; conflicting evidence; policy changes; invalidation; reboot without persistent state; and the fixture/production, system-clock, and public-HTTPS separation.

## 6. Freestanding build and resource accounting

The ordinary kernel and the NAVHTTPS13/NAVHTTPS15/NAVHTTPS16 guest-qualification kernel both built and linked as AMD64 PE and ELF artifacts with `-fstack-usage -fcallgraph-info=su`. Both use the existing freestanding flags (`-ffreestanding -fno-exceptions -fno-rtti -nostdlib -nostdinc++`). The new component is statically linked with the pinned libsodium SHA-512 and Roughtime evaluator code; the PE import, import-address-table, and delay-import directories are zero-sized. Linker call graphs include `TimeEstimateStore::accept_fixture_round`, `gxos_roughtime_agreement_evaluate`, `acquire_agreement_round`, `acquire_ipv4`, and `crypto_hash_sha512`.

Measured static frames (normal and qualification builds agree):

| Function or storage | Bytes | Lifetime/path note |
| --- | ---: | --- |
| Shell `execute_command` | 2,688 | Includes its local fixture configuration and 1,296-byte round result |
| `acquire_agreement_round` wrapper | 8 | Calls the coordinator |
| Round coordinator | 976 | Acquires sources, then evaluates agreement after each acquisition returns |
| `acquire_ipv4` | 3,664 | Owns bounded packet, nonce, verifier workspace, and source-result temporaries |
| Roughtime verifier | 912 | Nested during packet verification |
| Strict Ed25519 reachable call-chain bound | 3,824 | From the NAVHTTPS11/15 strict harness; no dynamic frame reports |
| Agreement evaluator | 2,496 | Runs after source acquisition returns; also re-run later during estimate acceptance |
| Estimate acceptance | 960 | Includes bounded result revalidation locals |
| Estimate query | 64 | Read-only interval advancement |
| Qualification `kernel_main` | 656 | Main frame includes the guest integration hook |

These are separate branches, not one stack sum. In the shell acquisition branch, the measured component path is 12,072 bytes: shell dispatch + round wrapper + coordinator + UDP acquisition + Roughtime verification + the nested Ed25519 chain. The small shell character-input frame and any outer event-loop frames are additional. The agreement evaluator runs after UDP acquisition returns; fixture acceptance runs after the whole acquisition/evaluation call returns. The acceptance branch is shell dispatch + 960-byte acceptance + 2,496-byte evaluator (about 6 KiB before outer input frames). In the guest startup hook, the corresponding measured acquisition/crypto peak is 10,040 bytes from the 656-byte main frame through the same coordinator, UDP, verifier, and Ed25519 frames; the later acceptance/revalidation branch is 4,112 bytes. This is static accounting from `.su` and callgraph output, not a runtime stack high-water measurement. The round result is 1,296 bytes; `TimeEstimateInfo` is 216 bytes; the time store is fixed static state and adds no persistent allocation.

## 7. Guest qualification and QAC

**Test A — time-estimate guest integration: pending.** The qualification image is built with the NAVHTTPS16 startup hook. The hook accepts the actual NAVHTTPS15 acquisition-round result, queries its fixture interval, waits 25 PIT ticks, verifies interval advancement and uncertainty, invalidates it, and prints that the system clock is unchanged and public HTTPS remains disabled.

**Test B — missing-profile guest regression: pending.** The prior NAVHTTPS15 serial is not evidence of authorization: it reports selected `ControlledQualification`, effective policy `Disabled`, blocker `Trust profile metadata is missing.`, zero plain/TLS TCP attempts, `BlockedPolicy`, `PolicyBlocked`, and `https.case.policy_validated.result=PASS`. The top-level Navigator run and trust-dependent local TLS smoke report `FAIL` with verify flags `12`, which is fail-closed behavior.

**Test C — critical Name Constraints: pending.** The earlier NAVHTTPS15 run could not start this scenario because another project's QEMU owned the lane. It remains pending until executed.

The image-ready checks found QEMU PID `9240` active for `guideXOSServer_STELLAR_NAVIGATOR` (artifact path under `E:\bkup\artifacts\SN10R21_20261114`); a later check found QEMU PID `30884` active for `guideXOSUEFI_FONT_SUPPORT` (`FONTS8R2/build/guest-run-001`). NAVHTTPS16 did not launch a fixture or QEMU while either process was active and did not stop or modify either process. NAVHTTPS16 held no QAC reservation, and no NAVHTTPS16 fixture process remains. Test A/B/C remain independent pending results until the lane can be acquired.

## 8. NAVHTTPS7 missing-profile root cause

The NAVHTTPS15 `production_trust_profile_missing` scenario reused common smoke assertions that required a successful trust-dependent local TLS smoke, a top-level `result=PASS`, and `tls_smoke.failure.verify_flags=0`. The selected test package was the controlled-qualification HTTPS policy with the normal validated local certificate fixture and `TrustProfileMode=missing`, which intentionally omitted only `trust-profile.json`. The guest loaded `/certs/ca-bundle.pem` and its manifest, matched the manifest SHA-256, parsed the one-root bundle, and reported `production_ready=no`; the absent trust-profile metadata made the effective HTTPS policy `Disabled`. The case was blocked before either plain TCP or TLS TCP, and verification failed closed with flags `12`. The runner therefore classified an intentionally blocked request as a scenario failure. This was stale test expectation/classification, not a production trust bypass, malformed package identity, or fixture authorization.

The checked-in test CA bundle used by the scenario has SHA-256 `c28c679d134e640c21cb739b617034e996d48a535b97499ffa667e832711e606`; the serial independently records the same manifest digest and hash match.

The smallest correction is in `scripts/smoke-navigator-kernel.ps1`: this scenario now expects the observed TLS smoke and overall smoke `FAIL`, requires positive verification-failure flags, and explicitly requires zero TCP attempts plus `BlockedPolicy` / `PolicyBlocked` and the case-level policy `PASS`. The failure markers remain visible and the scenario is still executed. The prior unsupported-version scenario remains accepted. Test C remains pending until QAC runs it.

The serial evidence is `logs/navigator-kernel-smoke-20261114-144000-production_trust_profile_missing.serial.log`; its SHA-256 is `dc0c6e481f306b1c10fe34a2498c5b7d4d9d3426ed834f19aa9c0ee6c2cd9a83`. The original NAVHTTPS15 scenario result and blocked critical-name-constraints result are also documented in the NAVHTTPS15 report.

## 9. Production requirements and NAVHTTPS17 scope

Fixture agreement is not real-world operator independence. Production time still needs reviewed independent sources, an authenticated/versioned source policy with key rotation and revocation, a secure update root, policy-bound quorum/uncertainty/freshness/conflict limits, authenticated cross-boot anti-rollback state, and an explicit reviewed system-clock promotion policy. Public HTTPS additionally remains gated on authenticated fresh time, trust-profile signing and verified-rooted approval, enforceable CA restrictions including Name Constraints, a revocation policy, and successful NAVHTTPS7 QAC regression evidence. No public Roughtime keys, CA candidates, certificate-date rules, or release approvals were added or relaxed here.

Recommended NAVHTTPS17: finish Test B and Test C in the QAC-exclusive lane; specify and verify the signed production source manifest and persistent clock floor; define the only authorized transition from fixture/plausible evidence to production `Authenticated`; and keep public HTTPS disabled until the trust, time, rollback, and revocation acceptance gates all pass. I219-LM remains PHASE26 complete / PHASE27 next; no physical DMA gating changed.

## 10. Final source and artifact hashes

Source hashes:

| Source | SHA-256 |
| --- | --- |
| `kernel/core/gxos_time_estimate.cpp` | `870f9da788a8a8aee17ab2dc0053cc62e8f87b5dfb91db0e9675d1e02c26e3d7` |
| `kernel/core/include/gxos_time_estimate.h` | `1952595681269647cc7725c4d7ceb43692114e2e90b30db334376011723430f4` |
| `kernel/core/main.cpp` | `c96ddb5f6a4f0c393085003b93d5e1cd0e6cb2849b35c494d13873c814a98ae0` |
| `kernel/core/shell.cpp` | `749d175eb260851f18b75f40e0ec0d0fa23d2d20323a807d58bc5e101e424189` |
| `tests/roughtime_time_estimate_test_bridge.cpp` | `6810dcf66791434062b412ef02de0840798568c4a5487716af831061b7659468` |
| `tests/test_roughtime_portable.py` | `d33805ec7d1260624933bbdb4672ad4909d4f14189b5b7e6f0846d2e15d718bc` |
| `scripts/run-roughtime-navhttps15-guest.ps1` | `7e5a9a259c220f7ced57d5975840c771088823621ed9cea2f98ecff39a5df0f3` |
| `scripts/smoke-navigator-kernel.ps1` | `e0c012b7986745c68f5d83e8f02df3e0faee50be616088058e8d2f064112e057` |

Build and qualification artifacts:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ordinary `kernel.elf` | 2,912,832 | `8ca99f2dd924617be3301fbd00d4cd2b89b37b8aadbee4a8175aa734738f2952` |
| ordinary `kernel.pe` | 3,020,805 | `c11551d7291023ee1c1e4a384371121fc8406dcbb9c613fc14d6c5abcc9649df` |
| NAVHTTPS16 qualification `kernel.elf` | 2,913,504 | `cf9cc90f9c047dc61ea60ffb39dc5b91331d7559795db725313ea9912c1c9874` |
| NAVHTTPS16 qualification `kernel.pe` | 3,024,823 | `ee4f823295f48d8fdf5d2c445bb19d841de9b0d417b45772070cc9c2c369d215` |
| Hosted Navigator smoke log | 376,684 | `16120bb65055c9aa0256b6bb517422cf0058c080fea16da5a58a0e2f7250fe5a` |
| NAVHTTPS7 missing-profile serial | 307,414 | `dc0c6e481f306b1c10fe34a2498c5b7d4d9d3426ed834f19aa9c0ee6c2cd9a83` |

The pinned NAVHTTPS15 guest serial, PCAP, and fixture hashes are recorded in Section 1. The validated test CA bundle hash is `c28c679d134e640c21cb739b617034e996d48a535b97499ffa667e832711e606`. The initial branch and commit are recorded by the NAVHTTPS16 task preflight; the final implementation commit is recorded in the completion message.
