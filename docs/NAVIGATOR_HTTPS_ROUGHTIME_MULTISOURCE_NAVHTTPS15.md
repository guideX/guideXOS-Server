# NAVHTTPS15 — Live Multi-Source Roughtime Acquisition and Agreement

## 1. Result

**Outcome A.** The bounded acquisition coordinator, two-of-three policy, diagnostic command, hosted fixture coverage, and AMD64 builds are complete. The corrected AMD64 guest passed NAVHTTPS13 Test A and NAVHTTPS15 Test B: three signed UDP responses were verified from three distinct simulated operator IDs and the existing agreement evaluator returned `agreement-satisfied`. The guest explicitly reported `production_policy_accepted=0`, `trusted_time=not-accepted`, `clock_unchanged=yes`, and `public_https=disabled`.

NAVHTTPS7 Test C was attempted after A/B under the same QAC reservation, but remains incomplete. `production_trust_profile_missing` emitted `[NAVIGATOR-SMOKE] result=FAIL`, `production_trust_profile_unsupported_version` emitted `result=PASS`, and the third scenario was blocked when another project's QEMU became active. That QEMU belonged to `D:\dev\guideXOSServer_SECURITY_SETTINGS`; it was left untouched. See the exact admission error in the NAVHTTPS7 log below. This separate acceptance remains pending.

Starting HEAD: `b6d1b040722eab73e74e2dfb0306475d90856f33`.

The NAVHTTPS12–14 reports and the NAVHTTPS14 64/64 Roughtime baseline were reviewed. Existing strict Ed25519 verification, bounded RFC 10049 v1 parsing, secure nonce generation, UDP acquisition, opaque evidence tokens, and the NAVHTTPS14 evaluator remain the active implementations. I219-LM remains **PHASE26 complete / PHASE27 next**.

## 2. Acquisition round

`kernel::roughtime::acquire_agreement_round` collects three configured sources sequentially and submits only NAVHTTPS14 opaque evidence tokens to the existing evaluator. The fixed round accepts exactly three configured sources and a quorum of two. The configuration is restricted to the `UNREVIEWED_CONFIG` profile; it cannot designate itself production-approved.

Each source carries an IPv4 endpoint, a nonzero operator ID, and a 32-byte pinned server key. The implementation rejects empty identities, duplicate endpoints, duplicate server keys, and configurations with fewer than two distinct operators. Duplicate operator IDs are allowed, but the evaluator counts them once. Operator identity is independent of server-key identity.

The normal build has no embedded source list. The explicit diagnostic command is:

```text
roughtime-agree <ip1 port1 operator1 key1> <ip2 port2 operator2 key2> <ip3 port3 operator3 key3>
```

It accepts three numeric IPv4 endpoints, nonzero decimal operator IDs, and 64 hexadecimal key characters per source. It prints attempts, verified responses, independent operators, source statuses, agreement classification, interval, midpoint, uncertainty, and freshness. It always reports `trusted_time=not-accepted`, `clock_unchanged=yes`, and `public_https=disabled`.

The command is synchronous and runs only when explicitly entered. The receive loop yields cooperatively to desktop input/render work every 10 PIT ticks while waiting. No background service or clock-setting path was added.

## 3. Bounds, ownership, and timing

- At most **3 configured sources and 3 acquisition results** per round.
- Each source uses one fresh 32-byte kernel secure-random nonce. Entropy failure produces no request.
- Each RFC 10049 request is 1,036 bytes including the packet wrapper; accepted UDP responses are capped at 1,024 bytes.
- The UDP sender makes at most two send attempts with the same bounded request. Each source has a 200-PIT-tick (about 2-second) receive deadline and a 50,000-poll ceiling. The three receive windows total at most about 6 seconds, excluding bounded network/neighbor resolution work.
- The round owns no heap allocations. UDP socket leases and packet/verifier workspaces are cleared or released on success and failure. Only successful acquisition tokens are retained for agreement evaluation.
- The acquisition path records send/receipt PIT timestamps and rejects backwards PIT observations. The round samples one final monotonic evaluation instant after all source attempts.
- NAVHTTPS14 advances each verified interval to that common evaluation instant using its monotonic evidence age and transport-delay bounds, then checks expiry, uncertainty, continuity, arithmetic limits, conflicts, and quorum. It does not average incompatible measurements. Multiple disjoint quorum candidates remain `AMBIGUOUS`.

The evidence lease is 300 seconds. The experimental shell policy uses a maximum source uncertainty of 30 seconds, a 2,000 ms transport-delay limit, and a 300-second forward-jump limit. Coordinator validation also enforces the NAVHTTPS14 policy maxima (300 seconds age, 302 seconds source uncertainty, 2,000 ms delay, and 86,400 seconds forward jump).

During initial guest qualification, a signed response reached the verifier but was rejected because acquisition passed an unset zero expiry to the NAVHTTPS14 token sealer. The sender now computes and overflow-checks the 300-second expiry before sealing. This preserves the opaque-token boundary and leaves the interval evaluation logic unchanged.

## 4. Controlled fixture

The hosted and guest fixtures use three separately keyed test signers. The QEMU guest sends to the host user-network endpoint `10.0.2.2`, using UDP ports 5319–5321 and simulated operator IDs 401–403. Their public-key fingerprints, as printed by the fixture, are:

| Operator ID | UDP port | Test-only server-key ID |
| ---: | ---: | --- |
| 401 | 5319 | `dbedad6ee009db026021697e3cf0edeeb02c286db9f9247827379354be1882ce` |
| 402 | 5320 | `3ca7e5fe43c5599f5a05a822d168543f6cc650cc845c0f557f5f16e0d7105acc` |
| 403 | 5321 | `e37dd63db316eee29113864a7e31663e8b0004c3cbe9a98c962a8f50790548c1` |

Key material is confined to the test sources. The guest fixture signs current host time and records request nonce, endpoint, and packet lengths. It expects two requests on port 5319 (the NAVHTTPS13 single-source check plus the NAVHTTPS15 round) and one on each other port. This is simulated independence: the three signers share one development host and do not prove three real-world operators.

## 5. Hosted qualification

`python -B -m unittest discover -s tests -p 'test_roughtime*.py' -v` passed **80/80**: the preserved 64-test NAVHTTPS14 baseline plus 16 acquisition-round integration scenarios. Coverage includes three agreeing live UDP signers; two agreeing plus timeout; a valid conflicting outlier; one success; duplicate operator and key; invalid signature; incorrect nonce; unexpected source; excess uncertainty; stale evidence; reordered receipt times; network failure; monotonic discontinuity; ambiguous candidate quorums; and bounded expiry/overflow handling in the same acquisition sealer used by the kernel.

The hosted regressions also passed:

| Check | Result |
| --- | --- |
| Strict Ed25519 harness | 238 hosted and 238 freestanding cases; zero failures |
| Pinned libsodium Ed25519 profile | PASS, version 1.0.22; 28 pinned files |
| Clock assurance | PASS |
| Navigator HTTPS trust profile | PASS |
| Secure-random contract | PASS |
| DNS | PASS, 13 protocol and 6 controlled-fixture cases |
| UDP/socket bounds | PASS, 6 cases |
| TCP connect/cleanup and IPv4 attribution | PASS |
| HTTPS qualification contract | PASS, 7 cases |
| Hosted Navigator smoke | PASS on isolated rerun; log `logs/navigator-hosted-smoke-20261114-145010.log` |

One earlier hosted smoke run under concurrent compiler load logged a failure in CSS phases (`logs/navigator-hosted-smoke-20261114-143734.log`). The isolated rerun passed. No CSS files were modified.

No unrelated CSS changes were made. The hosted smoke changed only the tracked recent-app list in `desktop.json`; that generated side effect was restored to its original clean content.

## 6. AMD64 integration and resource accounting

Both the ordinary and guest-qualification AMD64 targets built and linked successfully with `-fstack-usage -fcallgraph-info=su`. The guest build alone enables NAVHTTPS13/NAVHTTPS15 startup hooks and test-only fixture anchors.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ordinary `kernel.elf` | 2,898,160 | `31e22264b5d667c38ac140d677c138f1e538de416cf2952a5c9642c6b81c2c7f` |
| ordinary `kernel.pe` | 3,005,580 | `7eb14237ded534b339022c3910551393367d3929048ba8d96dfc16109bde37d2` |
| guest qualification `kernel.elf` | 2,898,792 | `86cae940175905ebb61e2b672a1598066425b833ce611305ae70d3e00d355707` |
| guest qualification `kernel.pe` | 3,009,385 | `d278b819328bbb5e56b53808d40521f63dfff890eb1d432b71622b0597d499e9` |

The normal ELF is 5,448 bytes larger than the NAVHTTPS14 ELF (2,892,712 bytes); the ordinary PE is 6,652 bytes larger than the NAVHTTPS14 PE (2,998,928 bytes). The linked ELF contains `acquire_agreement_round`, the agreement evaluator, and the UDP dispatch path. The PE import directory, import address table, and delay-import directory are all zero-sized. The target remains freestanding and is built with `-fno-exceptions -fno-rtti -nostdlib -nostdinc++`; no STL or heap use was added.

Measured static frames include 2,656 bytes for shell command dispatch, 976 bytes for the round coordinator, 3,664 bytes for UDP acquisition, 912 bytes for the Roughtime verifier, 2,496 bytes for agreement evaluation, and 96 bytes for cooperative yield (272 bytes for desktop draw). The strict Ed25519 report gives a 3,824-byte reachable crypto call-chain bound and no dynamic frame reports. The receive-and-verify shell path sums to about 12.1 KiB across measured component frames; the separate evaluator path is smaller. The boot assembly reserves a 16 KiB kernel stack and a separate 16 KiB exception stack. No runtime stack high-water watermark was collected; this accounting is static.

Caller-owned bounded storage measured in the hosted ABI is 1,296 bytes for the round result (including three opaque 256-byte tokens and agreement diagnostics), 488 bytes per acquisition result, and 1,216 bytes for the existing parser/verifier workspace. Packet buffers and nonce are part of the measured 3,664-byte `acquire_ipv4` frame.

## 7. Guest qualification status

The first boot attempt used a pre-fix qualification ELF and the repository `OVMF.fd`, which is a variables image rather than QEMU's x86_64 firmware code. That run booted no kernel and produced no packet traffic. The runner was corrected to use QEMU's x86_64 code image and a fresh writable variables copy.

A subsequent diagnostic guest boot verified the NAVHTTPS13A vectors and the corrected unsigned formatter. Its three-source response checks exposed an expiry-order bug: the pre-fix kernel returned `clock-evidence-rejected` after cryptographic verification. The acquisition sealer was fixed and both kernels rebuilt before acceptance. The diagnostic run is retained for history but is not counted as acceptance.

The preserved diagnostic files are under `logs/navhttps15-guest-20261114-142014/`. The serial log SHA-256 is `e3e519208ebe8aee99053ce32235a9671ed1302d984e2cf7ac693000d1675849`; the packet capture SHA-256 is `5425bda8669a2fb88847f5e059a74813c2d679b9c74a59f9b202b9085decd336`; and the fixture log SHA-256 is `434b25e7bfa5a200ac10877bf2859754667aa2bc84e1c9f76b3247f0ebfa0e25`.

The corrected guest ran with a separate staged ESP under the global `Global\guideXOS_QAC_QEMU_EXCLUSIVE` reservation. The fixture received the NAVHTTPS13 control request and the three NAVHTTPS15 source requests, with distinct request nonces and 416-byte signed replies. The serial log shows five NAVHTTPS13A vector cases passing, the unsigned formatter producing `18446744073709551615`, NAVHTTPS13B acquisition success, and NAVHTTPS15 `agreement-satisfied` with attempted=3, verified=3, and independent_operators=3. The PCAP and serial/fixture evidence are retained. The NAVHTTPS15 fixture/guest were stopped and its reservation released before the NAVHTTPS7 filters ran.

The filtered NAVHTTPS7 attempt is recorded in `logs/navhttps15-guest-20261114-143930/navhttps7-trust-scenarios.log`. Its first serial log, `logs/navigator-kernel-smoke-20261114-144000-production_trust_profile_missing.serial.log`, ended with `[NAVIGATOR-SMOKE] result=FAIL`; the unsupported-version scenario log ended with `result=PASS`. The third `production_critical_name_constraints` scenario did not start: its admission check found a live QEMU whose command line pointed to the Security Settings project. The NAVHTTPS15 runner stopped there and released its QAC mutex; it did not stop or modify that external QEMU.

Successful corrected-guest evidence under `logs/navhttps15-guest-20261114-143930/`:

| Artifact | SHA-256 |
| --- | --- |
| `guest-serial.log` | `954b080f9d28e9df437afc6a551cb3e947f46c938ed2c016a1a6f443d062f90f` |
| `guest-network.pcap` | `f836a2ac606b83d70f0ea41b60802bb8e945fea39b4d51c843138bda5b041005` |
| `fixture-server.log` | `f8b12054a15cf21dc2e69b17da75df26dc0224f61fd324a1962352f6312c6bad` |

Acceptance state:

- Test A — PASS on corrected guest ELF: five NAVHTTPS13A vectors and NAVHTTPS13R unsigned max-value formatter.
- Test B — PASS on corrected guest ELF: live UDP, three valid signatures, three simulated operator identities, `agreement-satisfied`; no clock change and public HTTPS disabled.
- Test C — PARTIAL / pending: missing-profile scenario reported `FAIL`, unsupported-version scenario reported `PASS`, critical-name-constraints scenario was blocked by the active Security Settings QEMU.
- QAC cleanup — NAVHTTPS15-owned guest and fixture stopped; its mutex released. No external QEMU was stopped or modified. A separate Security Settings QEMU was still active at the final admission check.

## 8. Security boundaries and NAVHTTPS16 scope

No OS clock is set, no clock assurance is promoted to production `Authenticated`, no public HTTPS or CA candidate is approved, and no production Roughtime server key is embedded. The output is diagnostic evidence only. The agreement policy is explicitly unreviewed, and the fixture does not establish real operator diversity or cross-boot rollback protection.

NAVHTTPS16 should define the production configuration boundary before adopting any source identities: a versioned signed/authorized manifest, a trusted update root, operator-ID auditability, key rotation and revocation, rollback-safe configuration updates, persistent clock-floor requirements, explicit clock authorization, and release review of quorum, uncertainty, freshness, and conflict policies. Real sources must be independently operated and reviewed. Public HTTPS must remain disabled until those gates and the independent NAVHTTPS7 guest acceptance are complete.
