# NAVHTTPS17 — TLS Integration with Authenticated Time Estimates

**Outcome A —** the fixture-only estimate is consumed by the real controlled Navigator TLS verifier, and all four guest acceptance cases passed. Hosted checks and ordinary/qualification AMD64 builds pass. The clock remains CMOS-derived and unchanged; public HTTPS remains disabled.

## 1. Mbed TLS time-validation architecture

The kernel uses the repository-pinned Mbed TLS 4.1.0 and TF-PSA Crypto 1.1.0 profile. The profile enables `MBEDTLS_HAVE_TIME_DATE`. `gxos_tls_foundation.cpp` installs the platform time callback backed by the existing CMOS-derived plausible clock; Mbed TLS keeps its native X.509 date checks as part of chain verification. The client also uses `mbedtls_ssl_set_hostname`, the loaded trust store/profile, and the existing Mbed TLS certificate verification path.

The active client path creates a fresh `mbedtls_ssl_context` and configuration for each byte-stream session. The repository does not configure a session cache, session tickets, or TLS session resumption. Certificate decisions therefore are not reused across connections. The controlled fixture adapter uses Mbed TLS's per-SSL `mbedtls_ssl_set_verify` callback; it does not replace `mbedtls_platform_set_time` or clear native verification flags.

## 2. Per-connection integration

Time assurance is required only when the selected and effective HTTPS state is `ControlledQualification`, the trust profile is valid and explicitly `fixture-only`, the bundle hash matches its manifest, and the requested host is `guidexos.test` or a subdomain of `.guidexos.test`. The trust profile must retain its existing controlled-fixture clock-assurance declaration. Other policy states do not receive this fixture lease. Public and production HTTPS policy remains disabled.

Before the controlled handshake, the connection requests a lease from the singleton time-estimate store using the current PIT-derived monotonic counter. It cannot pass UTC bounds into the issuer. The lease carries the lower and upper UTC bounds, fixture scope, pinned source-policy identity, controlled TLS trust-scope identity, monotonic anchor and observation, freshness remaining, uncertainty, continuity state, generation, and a rejection reason. The accepted source-policy identity is the existing NAVHTTPS15 fixture policy (`0x1501`); the TLS scope has a distinct fixed identity (`0x1701`).

The connection installs a certificate callback with its own session context. Mbed TLS calls it for each certificate in the verified path, including trust anchors under the current verifier semantics. For each callback it refreshes the lease from the store and checks that certificate's dates. The callback tracks the tightest `notBefore` and `notAfter` across the path. After a successful handshake, the final check refreshes the same lease again and requires the complete updated uncertainty interval to fit inside those tightest bounds. Evidence replacement, invalidation, rollback, expiry, an invalid scope, or any certificate-time rejection causes fail-closed verification. The adapter adds an Mbed TLS verification error flag; it never clears native flags or overrides chain, hostname, EKU, algorithm, or trust validation.

## 3. Read-only estimate API and interval rule

`TimeEstimateStore::request_tls_lease` issues a per-connection view only from currently accepted store evidence. `refresh_tls_lease` checks the lease generation and re-reads the current estimate so replacement or invalidation during validation is rejected. The public wrappers are named `request_controlled_fixture_tls_time` and `refresh_controlled_fixture_tls_time`. Store initialization is private to the singleton; only the hosted test build exposes a reset helper. No production API accepts caller-created timestamps or can clear latched rollback state.

For a lease interval `[earliest, latest]` and a certificate's inclusive validity range `[notBefore, notAfter]`, acceptance requires:

```text
earliest <= latest
notBefore <= notAfter
notBefore <= earliest <= latest <= notAfter
```

UTC certificate fields are range-checked and converted with bounded Gregorian calendar arithmetic for years 1 through 9999. Malformed intervals, malformed certificate dates, and integer overflow reject. A bound that begins before `notBefore` or ends after `notAfter` rejects, including intervals that straddle either boundary. The lease itself is refused if evidence is missing, stale/expired, too uncertain, outside the fixture scope or source policy, or has monotonic continuity/rollback failure.

## 4. Hosted tests

The final hosted run results are listed after execution. Current completed results before QAC closeout:

| Check | Result |
| --- | --- |
| NAVHTTPS15 Roughtime plus NAVHTTPS16/17 time-estimate and certificate-interval suite | **96/96 PASS** |
| HTTPS qualification contract | **7 cases PASS** |
| Trust-profile enforcement | PASS |
| Clock assurance | PASS |
| Secure-random contract | PASS |
| Mbed TLS profile verifier | PASS: Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, 3 repository patches |
| Hosted Navigator smoke | **288/288 checks PASS** |
| DNS resolution smoke | **13 protocol scenarios + 6 fixtures PASS** |
| UDP wire-bound smoke | **6 cases PASS** |
| TCP connection and cleanup smoke | PASS |

The 96-case Roughtime suite preserves the prior 90-case NAVHTTPS16 set and adds deterministic checks for complete interval containment, both validity boundaries, malformed certificate dates, UTC conversion/leap dates, lease scope/generation/lifetime, rollback, replacement/invalidation, and independent lease contexts. Existing agreement coverage continues to exercise missing authenticated evidence, plausible RTC without accepted evidence, one source without quorum, valid two-of-three agreement, duplicate operators, and stale or excessive-uncertainty evidence. HTTPS/trust contract checks retain wrong-host, invalid-chain, trust-profile, and no-downgrade coverage. A fresh connection context is used for each TLS session; no resumption feature is configured.

## 5. Freestanding build and resource accounting

The ordinary and qualification AMD64 kernel builds both pass with the existing freestanding options and stack/callgraph reports. The qualification build enables the NAVHTTPS13, NAVHTTPS15, NAVHTTPS16, NAVHTTPS17, and Navigator HTTP smoke hooks. Build outputs are retained under `tmp/navhttps17-normal-build-20261114/` and `tmp/navhttps17-qualification-build-20261114/`; the QAC runner also retains its isolated build under `tmp/navhttps17-qac-20261114/`.

| Measurement | Bytes / result | Notes |
| --- | ---: | --- |
| Ordinary ELF / PE | 2,919,176 / 3,032,041 | Freestanding build |
| Qualification ELF / PE | 2,945,200 / 3,054,782 | Adds test-only hooks |
| TLS verification callback frame | 224 | Per certificate callback |
| TLS open byte-stream frame | 576 | Session setup path |
| Time-estimate query frame | 72 | Store read/advancement |
| Lease request / refresh frames | 320 / 352 | Store-derived bounded view |
| `mbedtls_ssl_handshake_step` frame | 80 | Static `.su` measurement |
| Time lease / diagnostic view | about 80 / 216 | Lease stored in the TLS session; diagnostic view is fixed-size |

The Roughtime guest hook retains its round result in the existing fixture integration storage; NAVHTTPS17 does not add a large stack copy of the Roughtime evidence. TLS lease state is connection-owned and released with the Mbed TLS session. The singleton estimate store is fixed static state. These figures come from compiler frame reports and linked image sizes, not runtime stack high-water measurements. The build remains freestanding (`-fno-exceptions -fno-rtti -nostdlib -nostdinc++`). `objdump -p` confirmed the ordinary and qualification PE images have no Import Address Table directory.

## 6. Guest acceptance

Test A — NAVHTTPS16 time estimate: **PASS** on the current qualification image. The hook verified three signed fixture responses from three simulated independent operators, accepted the fixture-only interval, advanced it with PIT monotonic time, and issued the TLS-scoped lease. The no-evidence case ran before acceptance; invalidation passed after the TLS smoke. Serial evidence: `logs/navigator-kernel-smoke-20261114-170924-production_validated.serial.log` (`[NAVHTTPS16-GUEST]`, `[NAVHTTPS17-GUEST]`).

Test B — NAVHTTPS7 missing profile: **PASS**. The current-source QAC run observed the explicit `BlockedPolicy` / `PolicyBlocked` result and zero TCP attempts for the blocked public-pilot fixture. Serial evidence: `tmp/navhttps17-nav7-final-qac-20261114/20261114-172002-50d41a69/logs/navigator-kernel-smoke-20261114-172002-production_trust_profile_missing.serial.log`.

Test C — NAVHTTPS7 critical Name Constraints: **PASS**. The current-source QAC run rejected the critical Name Constraints trust profile during CA parsing, marked the trust store malformed and left HTTPS disabled. Serial evidence: `tmp/navhttps17-nav7-final-qac-20261114/20261114-172002-50d41a69/logs/navigator-kernel-smoke-20261114-172002-production_critical_name_constraints.serial.log`.

Test D — NAVHTTPS17 controlled TLS: **PASS** on the current qualification image. The real controlled HTTPS case returned HTTP 200 with TLS `Success`, `time_assurance_required=yes`, `time_assurance_accepted=yes`, `time_assurance_failed=no`, trust-scope identity 5889, and reason `ready-fixture-only`. The final invalidation marker passed. This independently proves Test A's time-estimate acceptance was consumed by the Navigator TLS callback.

The QAC scripts exited successfully and released their own fixtures and reservations. No QEMU/fixture process or listener remained on the QAC ports after cleanup. Run metadata and serial/build artifacts remain under `tmp/navhttps17-qac-20261114/` and `tmp/navhttps17-nav7-final-qac-20261114/`.

## 7. NAVHTTPS7, NAVHTTPS16, and production status

NAVHTTPS16's hosted estimate and qualification builds remain preserved. Its guest Test A and NAVHTTPS7's missing-profile and critical Name Constraints cases are now closed with current-source QAC serial and fixture evidence. Public HTTPS remains disabled; the controlled fixture policy does not promote fixture evidence to production trust.

The clock classification remains the existing plausible CMOS time plus a fixture-only, within-boot estimate. The estimate does not set CMOS or OS wall time, does not promote production `Authenticated` time, and does not claim cross-boot rollback protection. Public HTTPS is disabled. No public CA candidate or production Roughtime operator key is approved. Production use still requires authorized source provisioning and revocation, an authenticated update root, cross-boot anti-rollback state, reviewed quorum/freshness/uncertainty limits, release-authorized trust-profile constraints, certificate revocation policy, and independent public-HTTPS qualification.

I219-LM remains PHASE26 complete / PHASE27 next; no I219-LM DMA behavior changed.

## 8. Recommended NAVHTTPS18 scope

Keep fixture qualification closed and design the production authorization boundary: authenticated source-manifest format and update root, key lifecycle/revocation, operator independence, protected cross-boot time floor, and the explicit transition from fixture/plausible evidence to production `Authenticated`. Separately define release trust-profile provenance, enforceable root/name-constraint policy, and revocation behavior. Keep public HTTPS disabled until those production prerequisites are reviewed and passed.
