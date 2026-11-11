# NAVHTTPS8 — Guest Time Assurance and TLS Clock Trust Foundation

**Outcome B — a clock-assurance contract and bounded within-boot rollback checks are implemented. Authenticated time has not been achieved, so bare-metal public HTTPS remains disabled and no public-origin request was made.**

## 1. Repository and preserved baseline

- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `5e9053b7cdc5c43c14449dfb365e41d0461f10a3` (the requested HEAD).
- Starting worktree: clean. No branch switch, reset, stash, rebase, clean, merge, or push was performed.
- NAVHTTPS5/6/7 reports were read. NAVHTTPS7's trust-profile enforcement and disabled public bare-metal HTTPS policy remain in place. Its controlled HTTPS evidence remains preserved.
- NAVHTTPS7 guest qualification remains **PENDING**: QAC admission was unavailable in NAVHTTPS7, so its filtered guest case did not run. NAVHTTPS8 does not reinterpret that result as a pass.
- Physical Intel I219-LM PHASE27 safety state was not touched.

## 2. Current clock architecture

The kernel reads the PC-compatible CMOS RTC through ports `0x70/0x71` in `kernel/core/time.cpp`. It waits for RTC update-in-progress to clear, samples the calendar repeatedly for a stable read, handles BCD/binary and 12/24-hour modes, and validates calendar fields. The accepted year range is 2024–2100. The TLS path converts this calendar to Unix seconds and has historically interpreted it as UTC. The RTC interface provides no authenticated provenance or trustworthy timezone metadata; firmware or operating-system convention may instead leave the hardware clock in local time.

The kernel does not call UEFI `GetTime()` and does not receive that value through `BootInfo`. The repository's UEFI runtime-services support is documented as missing. Even a future `GetTime()` call would be another RTC input, not evidence that the RTC value is authentic.

The PIT runs at approximately 100 interrupts per second and exposes a tick count used for uptime and timeouts. It resets on boot, is not a wall clock, and provides no cross-boot continuity. The kernel has no authenticated time client, secure persistent time floor, TPM NV counter, secure-element clock, or rollback-protected UEFI variable path. Ordinary disk files and unsigned firmware variables are mutable and cannot establish anti-rollback.

Timezone preferences in the hosted desktop control display formatting. They do not alter the bare-metal TLS clock, which still consumes the CMOS fields as UTC.

## 3. Clock-assurance states and actual guarantees

The shared clock contract exposes status, source identity, Unix estimate, an optional estimate interval and uncertainty, monotonic freshness deadline, rollback status, and a reason. Current sources report an unknown interval and unknown uncertainty rather than inventing bounds.

| State | Meaning in this build | TLS use |
|---|---|---|
| `Invalid` | RTC/host source failed, returned an invalid date, or made an excessive forward jump. | Fail closed. No usable date is returned to Mbed TLS. |
| `Plausible` | Calendar/range checks passed for CMOS RTC or host OS UTC. Authenticity, quantified uncertainty, and cross-boot rollback protection are absent. | Allowed only by an explicit controlled-fixture profile on bare metal. |
| `Authenticated` | Reserved for a verified fresh provider that supplies source identity, signature/freshness evidence, an estimate interval, uncertainty, and a live monotonic freshness deadline. No current provider sets this state. | Required by any non-fixture trust profile and public trust policy. |
| `RollbackSuspected` | Within-boot RTC/PIT evidence conflicts with the prior wall-clock progression. The state latches until reboot. | TLS time and trust decisions fail closed. |

Hosted Windows system UTC is now labelled `Plausible`, replacing the previous `Verified` label. It is an operating-system supplied value that passes a date range check, not an authenticated guideXOS time assertion. Hosted Schannel continues to use its Windows trust/time behavior as the separate hosted transport path.

## 4. Authenticated-time alternatives

| Candidate | Authentication and freshness | Bootstrap, replay, rollback, and dependencies | Assessment |
|---|---|---|---|
| NTS (RFC 8915) | TLS-authenticated NTS-KE establishes keys; NTS-protected NTP responses provide integrity, request-response binding, and replay protection. | NTS-KE certificate validation itself needs a clock or an explicit bootstrap exception. It needs TLS, DNS/service discovery, UDP NTP, and policy for server keys. NTS alone does not protect a mutable saved time value across reboot. | Good follow-on synchronization protocol after a trusted rough time estimate; not the smallest first-boot bootstrap. The RFC explicitly discusses the certificate-time bootstrap problem. |
| Roughtime (RFC 10049) | A fresh client nonce is signed into the response; the response carries midpoint and radius. Multiple independent pinned servers can expose inconsistent answers. | Uses UDP and pinned Ed25519 public keys rather than TLS/CA validation. Requires secure nonce generation, bounded packet/proof parsing, SHA-512, Ed25519 verification, and a release-authenticated server-key list. A malicious or compromised quorum can still lie; require independent sources and compare intervals. Fresh queries are needed after reboot when no secure persistent state exists. | **Recommended next implementation.** It is designed to provide the rough authenticated time needed to bootstrap certificate validation. Existing UDP and secure-random paths are useful foundations; Ed25519 is present in PSA headers but is not currently selected in the guideXOS crypto configuration, so algorithm availability and RAM cost must be proven before coding the client. |
| Signed nonce-based time service over HTTPS | A signature over a newly generated nonce and a narrow time interval can authenticate a response and reject replay. | HTTPS certificate validation depends on the unknown clock unless a pinned response key or other bootstrap key is used. It adds an HTTP/service parser, signature/key rotation policy, bounded freshness, and secure nonce requirements. | Feasible, but a custom HTTPS API adds more protocol surface than adopting Roughtime for the same bootstrap role. |
| Signed time provisioned during an authenticated update | A release signer can authenticate a timestamp or time floor associated with an update. | A historical signed timestamp proves that an object was timestamped under a policy; it does not prove current time. A fresh nonce or trusted online challenge is still needed. Persistent floors require storage that resists replacement and rollback. | Useful as an additional lower bound or recovery aid after release signing and storage are defined; insufficient alone for current TLS time. |
| Protected persistent last-known time | A signed state record plus protected monotonic counter can detect rollback across boots. | Requires TPM/secure-element support, a driver, atomic update rules, reset/replacement handling, and key lifecycle. The counter is monotonic but is not itself UTC. | Strong complement to authenticated network time, but unavailable in the present kernel. Ordinary disk or unsigned UEFI variables do not qualify. |
| UEFI `GetTime()` / battery-backed RTC | Supplies a platform calendar value and reported RTC capabilities. | No cryptographic source authentication or protection against firmware/RTC manipulation. Runtime services are not currently wired into this kernel. | Helpful as another plausibility source only; it cannot promote a clock to `Authenticated`. |

RFC 3161 time-stamp tokens are also signed evidence, but a token tied to a document hash is historical evidence. RFC 3161's nonce can establish response timeliness for a fresh request; the client still needs a trusted TSA key, bounded response validation, and a policy for how long the resulting estimate remains usable.

## 5. Rollback and freshness limits

The bare-metal clock path now retains a small in-memory tracker for the current boot. It compares each valid RTC sample to PIT-derived elapsed seconds, allows two seconds for RTC granularity plus 500 ppm drift (capped at five minutes), and latches backward movement, monotonic regression, source failure, or an excessive forward jump. Once latched, the TLS clock callback returns unavailable and the failure remains until reboot.

This catches material rollback and large discontinuities observed while the kernel is running. It cannot detect a clock change that occurs between observations and is restored before the next observation. A small forward change within the bounded tolerance can pass. The PIT is approximate and does not establish UTC. A reset/reboot creates a new tracker; CMOS time after boot is again only Plausible. Power loss, battery loss, restored disk snapshots, and RTC changes across reboot have no protected floor. No monotonic timer is claimed as proof of trusted time across boots.

The continuity tracker treats large forward jumps as `Invalid`; backward wall-clock conflicts become `RollbackSuspected`. An RTC read failure also latches `Invalid` for the boot, keeping failure deterministic. The check may reject time after any future suspend/resume path that pauses PIT progression; no ACPI suspend/resume continuity contract exists today.

## 6. TLS and trust-profile contract

The interface in `gxos_tls_prerequisites.h` provides the clock estimate, assurance state, source, interval/uncertainty-known flags, freshness deadline, rollback flag, and diagnostic reason. It does not inject a test clock into the runtime. Deterministic tests use local simulated samples only.

Trust-profile metadata now distinguishes:

- `plausible-allowed-controlled-fixtures-only` for `fixture-only` profiles; and
- `authenticated-fresh-rollback-protected-required` for candidate/release profiles.

The runtime validates that relationship. Controlled `.guidexos.test` qualification retains explicit selection, certificate chain/date/hostname verification, and the fixture-only trust boundary, and can use Plausible time. Candidate/public policy is blocked unless the actual assurance object is Authenticated with a source, bounded interval and uncertainty, an unexpired monotonic freshness deadline, and no rollback conflict. There is no setter or developer configuration field that can create Authenticated state. No platform source currently meets those conditions.

Invalid, missing, or rollback-suspected time causes the wall-clock callback to fail closed and yields explicit diagnostics. Existing Mbed TLS chain, certificate-date, EKU, key-usage, hostname verification, and no-plaintext-fallback behavior were not weakened. Bare-metal public HTTPS remains disabled; no public CA candidate or trust store was approved. No public-origin HTTPS pilot was attempted.

## 7. Implemented improvements

- Added `gxos_clock_assurance.h` with explicit assurance and failure states, a bounded within-boot consistency tracker, authenticated-evidence freshness checks, profile sufficiency, and deterministic certificate-date decision helpers.
- Applied the tracker to the actual CMOS RTC / PIT wall-clock path. A detected rollback or jump invalidates the TLS clock for the rest of the boot.
- Added `GxosClockAssuranceInfo` for source, estimate, optional interval/uncertainty/freshness, rollback state, and reason.
- Reclassified hosted Windows UTC from `Verified` to `Plausible`.
- Bound trust-profile clock requirements to fixture-only versus candidate/public class and surfaced clock assurance details in Navigator diagnostics.
- Kept the existing TLS verifier and fixture qualification contract intact; added deterministic hosted tests without a runtime clock override.

## 8. Hosted qualification

Hosted acceptance and guest acceptance are reported separately. Results for this phase:

| Check | Result |
|---|---|
| Clock assurance contract test | PASS (simulated RTC/PIT continuity, invalid/failure cases, Authenticated evidence expiry/metadata, reboot boundary, certificate date outcomes, and insufficient assurance for public trust) |
| Trust-profile tests | PASS |
| HTTPS qualification outcome contract | PASS (7 cases, including QAC-blocked classification) |
| Mbed TLS import/config profile | PASS (Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches) |
| DNS resolution | PASS (13 protocol scenarios + 6 controlled-fixture checks) |
| UDP/socket wire bounds | PASS (6 cases) |
| TCP connect/cleanup | PASS (failed SYN, pending SYN close, socket bounds; existing type-limits warning) |
| Navigator URL resolution | PASS (11 cases) |
| Secure-random contract | PASS |
| Desktop clock/timezone settings | PASS |
| Hosted Navigator smoke (`smoke-navigator-hosted.ps1 -Build`) | Build PASS; smoke FAIL on two CSS inline-style checks (`CSS inline styles load`, `CSS inline runtime counters`). The clock diagnostics, hosted Schannel readiness, HTTPS handling, and downgrade checks passed in the same run. |
| Freestanding AMD64 kernel build | PASS (`kernel.elf` linked successfully) |

The deterministic clock test covers valid/invalid RTC samples, Plausible-only policy, backward wall-clock movement, excessive forward movement, source failure, authenticated-evidence expiry/replay/missing metadata, reboot without cross-boot continuity, expired/future/valid certificate intervals, and the public-trust rejection of Plausible time. It uses simulated values and does not change host or guest time; no runtime test-clock override was added. Existing hosted checks also confirmed HTTPS downgrade redirects remain blocked. The transport-attempt counters for clock-insufficient TLS failure and the bare-metal no-plaintext-fallback assertion still need exclusive QAC guest evidence.

## 9. Guest qualification status and plan

**NAVHTTPS7 guest qualification: PENDING; NAVHTTPS8 guest qualification: NOT RUN.** QEMU was not started for this phase. The new code was compiled for freestanding AMD64, but no guest result is inferred from compilation or hosted contract tests.

When QAC is available, use its exclusive admission and run only these bounded local-fixture scenarios:

1. Valid RTC with explicitly selected controlled fixture profile: valid fixture handshake and hostname checks pass; public policy remains disabled.
2. Invalid RTC: status `Invalid`, TLS clock callback unavailable, zero TLS/plain TCP attempts for the blocked public-policy case.
3. Runtime RTC moved backward and then held: status latches `RollbackSuspected`; subsequent controlled TLS is blocked until reboot.
4. Excessive forward RTC jump: status latches `Invalid`; no certificate-time bypass occurs.
5. Existing expired and not-yet-valid certificate fixtures: Mbed TLS reports expiration/future flags with date validation enabled.
6. Controlled valid certificate, wrong-host certificate, and downgrade fixture: preserve chain, hostname, and no-plaintext-fallback assertions.
7. Reboot with changed or reset RTC: new boot reports only Plausible (or Invalid); no prior Authenticated status or persistent floor is synthesized.

Use only loopback/QEMU-controlled fixtures, keep public-origin flags off, do not stop another project's QEMU process, and report guest evidence separately from hosted results. This plan does not close NAVHTTPS7's pending qualification until it is actually run.

## 10. Recommended practical next step

Implement a small Roughtime v1 client over the existing UDP path after confirming PSA Ed25519 and SHA-512 support and measured kernel memory limits. Pin a small, release-authenticated set of independent server public keys; generate a fresh secure nonce per query; bound packet, Merkle proof, signature, and retry work; verify each response interval; require agreement from at least two independent sources; and compare the resulting interval with the RTC and PIT progression. Require a fresh quorum at each boot before any public trust decision. Do not fall back to NTP or plausible RTC for public HTTPS when the quorum is unavailable.

Only after that source is implemented and guest-qualified should guideXOS decide whether v0.3 needs TPM/secure-element anti-rollback state for offline continuity. If such storage is absent, require fresh authenticated time on every boot and leave public HTTPS unavailable while offline. Consider NTS as a later synchronization method after the initial rough-time bootstrap.

## 11. Remaining release-owner decisions

- Select the pinned Roughtime operators, quorum, key-update/revocation policy, maximum uncertainty, freshness lifetime, retry budget, and acceptable disagreement response.
- Decide whether public trust requires protected persistent UTC across offline boots or fresh authenticated time at every boot.
- Select TPM/secure-element hardware targets and atomic state format before claiming cross-boot rollback protection.
- Resolve the RTC UTC/local-time convention and timezone handoff for bare metal.
- Close NAVHTTPS7 guest qualification and qualify the NAVHTTPS8 transitions under exclusive QAC admission.
- Separately resolve the public-root policy, trust-profile signature, name-constraint support, and revocation policy already identified by NAVHTTPS7. NAVHTTPS8 does not approve the CA candidate or enable public HTTPS.

## 12. Impact on the eventual public HTTPS pilot

There is no authorization to run a public-origin bare-metal pilot. The guest clock is still only Plausible, the candidate trust profile remains review-only, and NAVHTTPS7 guest qualification remains pending. Public bare-metal HTTPS must remain disabled. The hosted Schannel smoke included its existing public-origin compatibility check to `example.com`; it is not guest or bare-metal pilot evidence. A later pilot must separately show fresh authenticated time, adequate interval/uncertainty, no rollback conflict, valid certificate and hostname checks, approved roots, an accepted revocation policy, no plaintext fallback, bounded responses, and exclusive QAC guest evidence for the selected origin.

## References

- [RFC 10049 — Roughtime: A Protocol for Rough Time Synchronization](https://www.rfc-editor.org/rfc/rfc10049.html)
- [RFC 8915 — Network Time Security for the Network Time Protocol](https://www.rfc-editor.org/rfc/rfc8915.html)
- [RFC 3161 — Internet X.509 Public Key Infrastructure Time-Stamp Protocol](https://datatracker.ietf.org/doc/html/rfc3161)
- [UEFI Specification 2.11 — Time Services](https://uefi.org/sites/default/files/resources/UEFI_Spec_Final_2.11.pdf)
