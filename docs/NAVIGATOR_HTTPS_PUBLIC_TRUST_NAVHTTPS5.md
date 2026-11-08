# Navigator HTTPS Public Trust Review — NAVHTTPS5

**Outcome: C — the production trust and clock assessment is complete; the public HTTPS pilot is blocked and was not attempted.** The staged Mozilla/curl candidate remains review-only. No release trust default or physical I219-LM state changed.

## Repository and preserved state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`.
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `9dae1fbb5658df40bc67d240afe7835d9e0f0fc4`.
- Starting worktree: clean. No branch switch, reset, stash, merge, rebase, or clean was performed.
- Reviewed the NAVHTTPS2, NAVHTTPS3, NAVHTTPS4, and NAVHTTPS4R reports.
- I219-LM tracker remains `last_completed=PHASE26`, `next_expected=PHASE27`. No I219-LM code, physical NIC setting, or phase record was changed.

## Preserved NAVHTTPS4R baseline

NAVHTTPS4R remains the accepted guest DNS-to-controlled-HTTPS baseline:

- Real guest UDP DNS query and validated resolver response.
- TCP handoff to the address returned by DNS.
- Controlled `prod.guidexos.test` resolution.
- Verified Mbed TLS session, HTTP 200, and two parsed Navigator document blocks.
- Four required validated HTTPS qualification cases passed.

NAVHTTPS5 did not replace that evidence with static DNS. Its new QEMU clock cases use local controlled fixtures and do not claim on-wire guest DNS. The full NAVHTTPS4R details and retained artifacts are in [NAVHTTPS4R](NAVIGATOR_HTTPS_GUEST_DNS_QUALIFICATION_NAVHTTPS4R.md).

## CA candidate and root-policy assessment

The [candidate PEM](../assets/certs/candidates/mozilla-cacert-2026-11-05.pem) is a copy of curl's CA Extract, converted from Mozilla's certificate store. The [candidate README](../assets/certs/candidates/README.md) records that the downloaded PEM matched curl's published checksum before staging. The [manifest](../assets/certs/candidates/mozilla-cacert-2026-11-05.manifest) records 118 certificates and 200,116 bytes; the PEM SHA-256 is `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`. Its manifest says `bundle_type=production-source`, `production_ready=no`, `test_only=no`, and `rotation_id=mozilla-2026-11-05-review`. The maximum root `notAfter` recorded in that manifest is 2049-01-14T05:52:34Z.

curl states that this PEM conversion does not carry several Firefox/browser CA constraints, including domain constraints. Mozilla's root program also assigns trust bits and maintains restrictions outside the PEM certificates. The Mozilla root list is therefore source material, not a browser-equivalent policy file. The guideXOS validator checks PEM structure and records counts and hashes; it does not review which roots should be trusted for this product or preserve Mozilla's out-of-band trust restrictions. No product-specific root allow/deny review was completed for the 118 entries. See the [curl CA Extract notes](https://curl.se/docs/caextract.html), [Mozilla CA application process](https://wiki.mozilla.org/CA/Application_Process), and [Mozilla included-certificate notes](https://wiki.mozilla.org/CA/Included_Certificates).

The [shipped bundle](../assets/certs/mozilla-cacert-2026-08-13.pem) remains the release default: 121 certificates, 191,850 bytes, SHA-256 `303daa9461b9617eb8e6209b272613fcf2923959ff32e9422eaaae195c55c780`. This phase did not update it or change its existing manifest label. The curl/Mozilla policy-fidelity concern applies to that extracted PEM as well; the existing `production_ready=yes` field is not evidence that browser constraints are enforced by guideXOS.

### Integrity, authenticity, and updates

- The validator passed for the shipped bundle (`shipped-public`, 121 certificates) and the candidate (`production-source`, 118 certificates). The candidate remains `production_ready=no`.
- At runtime, the CA store validates the loaded PEM digest against manifest SHA-256 and fails closed for missing, empty, oversized, malformed, or hash-mismatched trust material. Fixture roots remain in `scripts/fixtures` and are not added to the shipped public bundle.
- The manifest is unsigned. A party able to replace both the PEM and manifest can replace the hash and readiness labels together. The runtime parses `root_count` and `pem_bytes` metadata but does not recount loaded certificates and compare the values.
- Updates are manual: retrieve a dated curl source, verify its published checksum, validate the PEM and manifest, review trust policy, then deliberately update the pinned package and rotation ID. There is no automatic refresh or root-expiration alert. The PEM hash provides integrity consistency, not source authenticity or policy approval.

The candidate is not wired into the pack generator, ramdisk, or runtime trust selection. Its manifest was not promoted. The controlled one-root QEMU trust fixture used SHA-256 `c28c679d134e640c21cb739b617034e996d48a535b97499ffa667e832711e606`, with `production_ready=no`; it remained isolated from public trust.

## Certificate validation behavior

The bare-metal TLS client uses Mbed TLS 4.1.0 with `MBEDTLS_SSL_VERIFY_REQUIRED`, the configured CA chain, and the original URL hostname passed to `mbedtls_ssl_set_hostname`. The client path checks server-auth EKU and key usage; the X.509 verifier checks signatures, certificate validity periods, CA/basic constraints, and path length. A plaintext downgrade is not permitted when HTTPS verification fails.

The vendored Mbed TLS sources expose the name-constraints OID but contain no name-constraints verifier. As a result, guideXOS does not enforce those browser/root-store constraints omitted by curl's PEM conversion. CA bundle parsing and chain verification cannot compensate for this policy gap.

The runtime sets the CA chain's CRL pointer to null in [gxos_tls_foundation.cpp](../gxos_tls_foundation.cpp). It does not fetch or validate CRLs, make OCSP requests, or consume a stapled OCSP response in this client path. Revocation status is therefore not checked. A revoked but otherwise valid certificate can remain acceptable until it expires or another chain check rejects it. That limitation, combined with the missing root-policy constraints, prevents a defensible public trust profile.

## Guest clock and validity-period results

Bare-metal guideXOS reads the CMOS RTC in [kernel/core/time.cpp](../kernel/core/time.cpp), interprets it as UTC, checks stable reads and calendar ranges, and labels an in-range time `Plausible`. The year must be 2024–2100. The kernel does not receive a UEFI `GetTime` value or an authenticated external time source. Hosted Windows time is reported as `Verified`, but that host result does not establish guest time.

The Mbed TLS clock callback in [gxos_tls_foundation.cpp](../gxos_tls_foundation.cpp) returns epoch zero when guideXOS cannot produce a wall-clock value. The production prerequisite gate currently accepts either `Plausible` or `Verified`; a plausible CMOS value is not proof of correct time and can be wrong or rolled back. Do not treat uptime ticks as wall-clock time. The unknown-clock QEMU run showed that the TLS path reports `ClockCallbackUnavailable` and stops before protocol/cipher negotiation, so it does not bypass date checks.

| Case | QEMU guest time / fixture | Evidence | Result |
|---|---|---|---|
| Valid period | `2026-11-08T03:09:23Z`; production-policy fixture is valid 2026-06-01 through 2028-06-01 UTC | All four required `production_validated` HTTPS cases executed and passed. The selected fixture trust was the controlled one-root bundle, not the Mozilla candidate. | Certificate-date path passed; full smoke had separate failures listed below. |
| Expired | Guest time `2026-11-08T03:16:36Z`; fixture expired 2026-06-01 UTC | `policy_validated` and redirect cases returned `CertificateVerifyFailed`; verification flags were `1` (`BADCERT_EXPIRED`). | Target negative assertions passed. Full scenario also reported the unrelated local `tls_smoke` TCP connect failure. |
| Not yet valid | QEMU RTC set to `2026-06-30T12:00:00Z`; fixture `NotBefore` is `2026-07-01T00:00:00Z` | The allowlisted HTTPS case returned `CertificateVerifyFailed`, flags `512` (`BADCERT_FUTURE`), and its negative-case result was `PASS`. | Target negative assertion passed. Full scenario also reported the unrelated local `tls_smoke` TCP connect failure. |
| Unknown/invalid clock | QEMU RTC set to `2000-01-01T00:00:00`; the kernel rejects the year | Clock status `Unavailable`, epoch/UTC unavailable, backend `ClockCallbackUnavailable`; the allowlisted request returned `ClockUnavailable` with no protocol or cipher negotiated. | Fail-closed behavior observed. |
| Production policy with unknown clock | Same invalid RTC; `ProductionValidated` selected | Effective policy `Disabled`; validated HTTPS disabled; TLS TCP attempts `0`, plaintext TCP attempts `0`; blocker: `Wall clock callback is wired, but guideXOS does not currently report a plausible UTC time.` | Public-policy gate prevented transport. |

The date-failure flags distinguish expiration from a future `NotBefore`. The unavailable-clock state is distinct: the runtime refuses to establish TLS rather than substituting an uptime value or disabling validation.

The QEMU runner's whole-smoke status was not green for these runs. The valid case still passed all four required HTTPS assertions, while the broader smoke reported `tls_smoke.tls_status=TcpConnectFailed` and, on that run, `http.case.negotiated_deflate.result=FAIL`. The expired and future fixture runs showed their expected certificate-rejection evidence but also failed the generic local TLS smoke check. The unknown-clock and production-clock runs showed the expected fail-closed evidence; their first wrapper results also exposed stale expected strings for the new clock statuses/blocker. The assertions in [smoke-navigator-kernel.ps1](../scripts/smoke-navigator-kernel.ps1) were aligned to the observed `ClockUnavailable`, `ClockCallbackUnavailable`, and exact blocker output after those runs. Those final assertion-text edits received a PowerShell AST parse check but were not rerun under QEMU.

The AMD64 kernel compiled successfully for the controlled runs; the UEFI loader was unchanged. The valid-policy run `20261107-190549-631130a6` staged `kernel.elf` SHA-256 `923384cbb711c96e2e853aa92278b6dbd9a04a968abdfae57be1e1b28e750d77` and a 64 MiB fixture ramdisk SHA-256 `bbd77aafd1e9d578057a574d16e96f62280b9130627dc4922054e9130a01324d`.

## Hosted regressions and QEMU admission

Hosted protocol and contract tests completed:

| Check | Result |
|---|---:|
| `scripts/smoke-dns-resolution.ps1` | 13 protocol scenarios + 6 controlled-DNS fixture checks passed |
| `scripts/smoke-udp-wire-bounds.ps1` | 6/6 passed |
| `scripts/smoke-navigator-url-resolution.ps1` | 11/11 passed |
| `scripts/test-navigator-https-qualification-contract.ps1` | 7/7 passed, including QAC-blocked classification |
| `scripts/smoke-secure-random-contract.ps1` | Passed |
| CA bundle validator | Shipped and candidate profiles passed |
| `scripts/smoke-navigator-hosted.ps1 -Build` | Build completed; UI smoke failed with 55 checks passed and 230 failed after the Navigator window was not found. The current URL remained `about:navigator` and no toolbar widgets registered. Log: `logs/navigator-hosted-smoke-20261107-184542.log`. |

All completed guest scenarios used the runner's exclusive QAC admission. No other project's QEMU process was stopped. An initial attempt mistakenly included `-GuestDnsQualification`, which also enables the external `example.com` DNS/TCP test. It was cancelled before the guest emitted any Navigator smoke or guest-DNS marker. Its 220-byte capture contains only IPv6 multicast/control traffic; it contains no guest UDP/53 or TCP/443 traffic. That attempt is not counted as a DNS or public-origin test. Later test runs omitted `-GuestDnsQualification`, used only local `10.0.2.2` fixtures, and kept the real-public-HTTPS probe flags off.

## Public pilot disposition and remaining gates

**Public HTTPS pilot: NOT ATTEMPTED / BLOCKED before public-origin transport.** The candidate is not approved as browser-equivalent root policy; the bare-metal clock is merely plausible, not verified; the runtime has no revocation check; and the trust manifest is unsigned. The candidate was not enabled, and no public target was selected or requested. No broad public browsing or release-default change was made.

The smallest safe next phase should:

1. Add a guest time source with explicit provenance and rollback policy, distinguish `Verified` from `Plausible`, require `Verified` for non-smoke/public trust, and keep certificate checking closed when time is unavailable. A UEFI time handoff alone is an input, not authentication; use a protected RTC/rollback design or authenticated time with anti-rollback.
2. Define a guideXOS root policy that retains Mozilla trust metadata/restrictions or a reviewed product-specific allowlist, enforces name constraints, and signs or pins the policy manifest. Recount and compare manifest metadata at runtime.
3. Select and document a revocation policy, including bounded network behavior and failure semantics for CRL/OCSP. Do not describe revocation as checked until implemented and tested.
4. Rerun hosted and controlled QEMU qualification until the whole smoke is green, then run one bounded public HTTPS origin under QAC with valid-chain/hostname/time checks and negative wrong-host, future/expired, downgrade, and response-bound controls.

Keep that public-pilot qualification separate from production browser readiness and keep the I219-LM PHASE27 gate independent. No new TLS engine or browser certificate-policy subsystem was implemented in NAVHTTPS5.

## Git outcome

- Starting HEAD: `9dae1fbb5658df40bc67d240afe7835d9e0f0fc4`.
- This report and its local-QEMU clock-test support are committed on the existing branch.
- No push was performed.
