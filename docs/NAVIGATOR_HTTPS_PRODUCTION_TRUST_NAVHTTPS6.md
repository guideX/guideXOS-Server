# NAVHTTPS6 — Navigator HTTPS Production Trust Foundation

**Outcome B — the trust policy and bounded pilot path are documented, but no suitable production profile is ready.** The 2026-08-13 Mozilla/curl bundle remains physically packaged with its legacy `production_ready=yes` label, but the normal HTTPS policy is now `disabled`. The separate 2026-11-05 candidate remains review-only with `production_ready=no`. The public HTTPS pilot was not run and public browsing was not enabled.

## 1. Preflight and preserved baseline

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Development host: `LEON2_RDT` — HP ProDesk 600 G6 Microtower, Windows host.
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `4536fcec79ea215df6486865fc0b7936539c9681` (the requested HEAD).
- Starting worktree: clean. The branch was already two commits ahead of its upstream tracking ref; this phase did not change branch topology or push.
- I219-LM state remains `last_completed=PHASE26`, `next_expected=PHASE27`. No I219-LM code, physical NIC setting, or phase record was changed.

The accepted [NAVHTTPS5R3 report](NAVIGATOR_HTTPS_REGRESSION_NAVHTTPS5R3.md) remains the transport baseline. Its controlled guest run completed DNS resolution, TCP establishment, TLS certificate rejection/verification, and all four HTTPS policy cases. The historical generic pre-SYN TCP failure did not reproduce; its cause remains inconclusive and is not reopened here. NAVHTTPS4R's real guest DNS-to-controlled-HTTPS result remains preserved separately. No public-origin request was made in NAVHTTPS6.

## 2. CA source, trust purpose, and proposed profile

The staged [candidate PEM](../assets/certs/candidates/mozilla-cacert-2026-11-05.pem) is curl's CA Extract, converted from Mozilla's root store. Its source date is 2026-11-05; it has 118 certificates and 200,116 bytes. The candidate PEM SHA-256 is `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`. Its manifest is `bundle_type=production-source`, `production_ready=no`, `test_only=no`, `rotation_id=mozilla-2026-11-05-review`. The repository README records that the downloaded PEM matched curl's published checksum before staging.

curl documents that the extract is under the Mozilla Public License 2.0, checks for source changes daily, and produces a new conversion when its source or conversion script changes. Its documentation also warns that the PEM conversion omits Firefox/browser restrictions such as CA domain constraints. Mozilla's root program has trust-purpose bits and additional restrictions that are not all represented in the PEM or even in `certdata.txt`; Mozilla's additional-trust page documents distrust-after rules and other policy outside the certificate bytes. See [curl CA Extract](https://curl.se/docs/caextract.html), [Mozilla included certificates](https://wiki.mozilla.org/CA/Included_Certificates), and [Mozilla additional trust changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes).

For a future Navigator public-origin profile, the proposed policy is:

- Include only roots explicitly authorized for TLS server authentication by the source trust metadata and by a guideXOS-reviewed allowlist. Identify each included and excluded anchor by a stable certificate fingerprint, not by subject text.
- Carry forward Mozilla distrust, purpose, domain, issuance-date, and other restrictions. Reject a root if its constraints cannot be represented and enforced. Do not turn missing metadata into unrestricted trust.
- Exclude roots with no server-auth purpose, explicit distrust, ambiguous trust state, malformed CA properties, or a restriction unsupported by the guideXOS verifier. Keep any review exceptions in versioned policy metadata with a reason and owner.
- Continue enforcing path signatures, CA/basic constraints, path length, leaf server-auth EKU/key usage, validity dates, and the original URL hostname. A PEM list by itself does not prove those policy decisions.

This bounded profile cannot be built from the staged PEM with the current verifier. guideXOS imports Mbed TLS 4.1.0 with certificate parsing and verification, but the vendored X.509 code has no name-constraints verifier. An OID definition is present; parsing/enforcing the constraint is not. A restriction omitted by curl cannot be reconstructed from the PEM. Mbed TLS documents its X.509 verification API and has an open project issue describing the missing name-constraints handling: [Mbed TLS X.509 API](https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-4.1.0/include/mbedtls/x509_crt.h), [name-constraints issue](https://github.com/Mbed-TLS/mbedtls/issues/8759).

**Disposition:** the 118-root extract remains review-only source material; no bounded production candidate was prepared. No subset was selected because there is no machine-readable restriction source plus verifier capable of proving that the subset preserves the source policy.

## 3. Reproducibility, authenticity, and update handling

The repository's `scripts/validate-navigator-ca-bundle.ps1` parses PEM blocks and emits counts, dates, source label, rotation ID, and a SHA-256 manifest. Its output is deterministic when `-GeneratedUtc`, source description, rotation ID, and input bytes are fixed. It does not retrieve curl material, run Mozilla-to-PEM conversion, preserve Mozilla trust bits, curate roots, or authenticate a manifest. The current candidate's bytes are pinned by the recorded hash; its generation inputs do not include a pinned conversion-script revision.

Future candidate regeneration should use this controlled sequence:

1. Fetch a dated curl extract over HTTPS and verify the checksum published for that exact source version. Record the source date, source URL, checksum source, conversion-script revision, and license. Treat the checksum as corruption detection unless an independently trusted release signature or repository pin authenticates it.
2. Obtain Mozilla trust-purpose and restriction metadata for the same source revision. Produce a deterministic, fingerprint-based allowlist and denylist. Do not copy every PEM entry automatically.
3. Serialize selected certificates in a stable order and LF format. Run the repository validator with fixed `GeneratedUtc`, `SourceDescription`, and `RotationId`; compare output hashes and counts to the reviewed input manifest.
4. Keep the bundle and policy manifest versioned together. A release signer must authenticate the manifest from a key rooted in a verified boot or package-signing chain. The current v0.1 manifest is unsigned; a PEM SHA-256 only detects a mismatch against that manifest and does not authenticate the manifest or the policy labels.
5. Stage updates as a new immutable version, validate it before activation, and retain the last accepted bundle/manifest pair for rollback. Activate atomically; never merge controlled fixture certificates into public trust. Review source distrust or critical security changes promptly; otherwise a monthly source check is proportionate for a bounded preview.

The current runtime compares the loaded PEM hash with manifest `sha256`, but does not cryptographically authenticate the manifest and does not compare manifest `pem_bytes` and `root_count` with the loaded bytes and parser result. Runtime parser fallback can also skip unsupported certificates. These are unresolved production-policy gaps, not capabilities claimed by this phase. A signed policy format and strict metadata/parser accounting are prerequisites before the manifest's `production_ready` field can safely authorize public trust.

## 4. Guest clock assurance

| State | Current meaning | Certificate-time policy |
|---|---|---|
| `Unavailable` | RTC read is unstable, invalid, outside the accepted calendar range, or unsupported. TLS callback cannot provide wall-clock time. | Fail closed. Do not substitute uptime or skip date checks. This behavior is retained. |
| `Plausible` | A stable CMOS RTC value passes calendar/range checks and is interpreted as UTC. It has no authenticated source or rollback evidence. | Suitable only for controlled fixture tests that deliberately set the guest RTC. It is not trusted public certificate time. |
| Policy-accepted time | A future state that requires a documented source policy, freshness evidence, and rollback handling. No bare-metal guideXOS source currently provides this state. | Required before public-origin certificate validity can be accepted. |

The guest currently reads CMOS RTC only. It does not consume UEFI `GetTime`, persist a rollback floor, or acquire authenticated time. UEFI time would still be an input, not proof of authenticity. An unauthenticated NTP response can be spoofed and cannot establish anti-rollback. The hosted `Verified` label means Windows system UTC returned a date within the application's plausible range; it is not a cryptographic attestation and does not establish guest time.

The minimum defensible public policy is: require an authenticated, fresh time source at boot; reject large rollback against a protected monotonic floor; keep a monotonic clock for within-boot progression; and fail certificate validation closed until initial time is accepted. Possible infrastructure is a protected RTC plus TPM-backed rollback state, or authenticated time such as NTS with signed/fresh bootstrap evidence plus persistent anti-rollback. It needs a defined recovery path for battery loss, restored snapshots, stale time, and unavailable network. Authenticated NTP alone is insufficient if an attacker can roll the clock back across boots.

No clock code changed. Existing `Plausible` remains accurately classified as plausibility only; it does not make the guest ready for public trust. The production pilot remains blocked until this separate time source and policy exist.

## 5. Revocation assessment and preview proposal

The active guideXOS Mbed TLS profile includes certificate parsing but not `MBEDTLS_X509_CRL_PARSE_C`; the build plan omits `x509_crl.c`, and the TLS session currently passes a null CRL pointer to `mbedtls_ssl_conf_ca_chain`. Although Mbed TLS has optional CRL parsing and verification APIs, Navigator does not load or enforce CRLs. The client path does not request, fetch, parse, or verify OCSP responses, and it does not consume stapled status. No revocation check is claimed.

| Mechanism | Feasibility here | Current behavior and limitation |
|---|---|---|
| CRL | Mbed TLS can support local CRL parsing/chain checks when configured and linked. | Not configured, packaged, loaded, or attached. Missing status has no fail-closed semantics. Large/fresh CRL distribution would need bounded size, signature, freshness, and rollback checks. |
| OCSP | Requires a bounded HTTP(S) client path, response parser, responder authorization, nonce/freshness rules, and cache policy. | No Navigator OCSP client or verifier is present. |
| Stapled OCSP | Requires TLS status-request negotiation and authenticated parsing of the handshake response. | The client does not request or consume stapled status. |
| Local distrust/revocation data | A signed, versioned, size-bounded deny overlay can reject known bad roots/intermediates or leaf serials. | No such runtime overlay exists. Root distrust is a CA-policy change; leaf revocation is a separate certificate-status problem. |

Preview policy proposal: for a separately approved, single-origin technical preview, distribute a signed local deny overlay and reject every listed root, intermediate, or leaf. Do not claim full leaf revocation coverage. A soft-fail for unavailable online status should be considered only for that short, origin-allowlisted preview, with explicit release-owner acceptance of the stale/revoked-leaf risk, a fixed expiry, and a rollback/stop procedure. Production use should require a stronger status policy or an explicit, separately reviewed certificate-lifetime and revocation-risk decision. Without that owner decision, no pilot should start.

## 6. Trust configuration contract

| Trust domain | Material and selection | Required behavior |
|---|---|---|
| Controlled test HTTPS | One-root fixtures under `scripts/fixtures`; local smoke or user-dev mode selected explicitly. | `test_only=yes`; fixture roots remain separate from public-root paths and bundles. Limit targets to controlled `guidexos.test` names. Never infer public readiness from a fixture. |
| Existing shipped public-root bundle | `assets/certs/mozilla-cacert-2026-08-13.pem`, 121 roots, SHA-256 `303daa9461b9617eb8e6209b272613fcf2923959ff32e9422eaaae195c55c780`; its packaged manifest says `production_ready=yes`. | Still present at `/certs/ca-bundle.pem` for compatibility and review. Normal package policy is `disabled`, so it does not authorize public HTTPS. The unsigned legacy readiness label is not approval evidence. |
| Newer disabled review candidate | `assets/certs/candidates/mozilla-cacert-2026-11-05.pem`, manifest says `production_ready=no`. | Review and validator input only. It is not wired into the wallpaper pack, ramdisk, or runtime trust selection. No automatic promotion or fallback. |
| Future bounded public-origin pilot | A new signed policy profile, authenticated time, supported root constraints, a selected allowlisted hostname, and a separate explicit pilot selection. | Permit only the reviewed origin(s), preserve hostname/date/chain validation, block numeric-IP substitution and plaintext fallback, cap response/time/redirect work, and expose all selected policy IDs and artifact hashes. |
| Future production/public preview | Separately reviewed, signed root-policy and revocation/time contracts, release-signed artifacts, update/rollback ownership, and release-owner approval. | No generic public navigation until every security gate is supported. Unknown or unsupported schema, invalid signature, missing/corrupt material, identity mismatch, expiry, or parser skip fails closed. Never fall back to a different unverified public root set. |

Current explicit selection and runtime diagnostics expose the CA path, source, manifest type/hash/readiness, HTTPS policy file/source/state, clock status/backend, and TLS outcome. Existing qualification attestation records kernel/ramdisk hashes, PEM and manifest hashes, trust labels, and the HTTPS policy hash. These values provide traceability, not authenticity. Before production use, the attestation and runtime contract also need an authenticated trust-profile ID, policy-signature result/key ID, parser counts, clock source/acceptance state, revocation data version/freshness, and an explicit no-fallback result.

The current `production_ready` field is an unsigned manifest label. It must not be treated as CA approval. NAVHTTPS6 did not promote the candidate, change its `production_ready=no` value, or select production trust. The previously generated normal ramdisk policy was `production-validated`; NAVHTTPS6 changes the normal package generator to stage `disabled` unless `GXOS_NAVIGATOR_HTTPS_POLICY` explicitly supplies a policy. That override remains available and does not itself enforce release-owner authorization or a reviewed trust profile; this phase changes the normal default only. A routine package build therefore no longer selects arbitrary-origin HTTPS based on the legacy unsigned shipped-root label.

## 7. Hosted implementation and regression results

No TLS transport, browser, clock, or root-store implementation changed. One packaging default changed: a normal build now writes the explicit `disabled` HTTPS policy instead of selecting `ProductionValidated`. Existing controls already provide bounded PEM loading, Mbed TLS chain and hostname verification, leaf server-auth checks, certificate date checks, secure-random gating, invalid-clock fail-closed behavior, explicit policy selection, and no plaintext fallback after HTTPS validation failure. The uncovered root-policy, signature, count, clock-authentication, and revocation gaps are documented rather than hidden behind a candidate promotion.

| Check | Result |
|---|---|
| `scripts/smoke-navigator-hosted.ps1 -Build` | PASS; `NAVIGATOR_SMOKE_RESULT: PASS`. Hosted path uses Schannel and Windows system trust. Smoke log SHA-256: `8ba344fb5cc19697952a1994c892df20d7f8e77850b13109f8f270e7610f7050`. Runner does not emit a numeric assertion count. |
| `scripts/smoke-dns-resolution.ps1` | PASS: 13 DNS protocol scenarios and 6 controlled fixture checks. |
| `scripts/smoke-tcp-connect-cleanup.ps1` | PASS: IPv4 send-stage attribution and TCP cleanup/socket-bound cases. Existing compiler warning: `ephemeral > 65535` is always false for its type. |
| `scripts/smoke-udp-wire-bounds.ps1` | PASS: 6 cases. |
| `scripts/smoke-navigator-url-resolution.ps1` | PASS: 11 URL resolution cases. |
| `scripts/test-navigator-https-qualification-contract.ps1` | PASS: 7 qualification outcome cases, including QAC-blocked classification. |
| `scripts/smoke-secure-random-contract.ps1` | PASS. |
| `scripts/validate-navigator-ca-bundle.ps1` against the staged curl candidate | PASS: 118 PEM certificates, SHA matches the pinned candidate, `production_ready=no`, `test_only=no`. Generated manifest was placed in the system temp directory. |
| CA validator against smoke fixture | PASS: one root, `production_ready=no`, `test_only=yes`; fixture remained separate. |
| `scripts/verify-mbedtls-profile.ps1` | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches. |
| `scripts/generate-wallpaper-pack.ps1` normal build | PASS: with no explicit policy, both output policy filenames contain `disabled`; rebuilt `ESP/ramdisk.img` while retaining the staged Developer Studio and PacMan packages. The shipped CA manifest still reports the legacy label `production_ready=yes`, but the disabled policy keeps it inactive. |

No QEMU run was needed for this documentation/readiness phase. No real public-origin probe, public-host compatibility corpus, public-pilot scenario, or public browsing was run. The hosted smoke changed only `desktop.json`'s recent-items list; the change was inspected and restored. `git diff --check` passed after all report and implementation changes.

## 8. Unresolved security risks

1. The curl/Mozilla PEM omits policy constraints that the guideXOS Mbed TLS build cannot enforce.
2. The manifest is unsigned; a PEM and manifest can be replaced together. Runtime does not compare `pem_bytes` or `root_count` with the loaded/parser counts and can report a filtered parse.
3. The bare-metal RTC is unauthenticated and rollbackable. Current status `Plausible` is not sufficient evidence for public certificate dates.
4. No CRL, OCSP, stapled OCSP, or signed local deny overlay is active. A valid but revoked leaf may be accepted if other checks pass.
5. The current trust-profile manifest format has no authenticated policy identity or signature-verification result. A manifest `production_ready=yes` is not itself proof of review or provenance.
6. NAVHTTPS4R proves DNS-to-controlled-HTTPS behavior, not a public guest-origin route. Actual public DNS, target policy, and public-origin TLS remain unqualified.

## 9. Public HTTPS pilot readiness

| Prerequisite | Status | Evidence or required work |
|---|---|---|
| NAVHTTPS5R3 controlled DNS/TCP/TLS/HTTP baseline | Satisfied | Preserved report; generic TCP failure remains non-reproduced/inconclusive. |
| Guest DNS to a controlled HTTPS target | Satisfied | NAVHTTPS4R baseline. |
| Guest DNS to the chosen public FQDN and TCP to the returned address | Missing | Must be captured in a later QAC-exclusive guest run; no public target was probed in NAVHTTPS6. |
| Candidate provenance and integrity hash | Partially satisfied | Dated curl candidate and matching published checksum recorded; checksum and manifest do not authenticate an approved policy. |
| Bounded public root profile with source restrictions | Missing | Need a reviewed source-metadata allowlist and enforcement of Mozilla/browser restrictions. |
| Authenticated trust metadata and no-fallback runtime contract | Missing | Need signed policy/manifest support and runtime validation; current v0.1 manifest is unsigned and counters are not reconciled. |
| Hostname, chain, EKU, expiry/future checks and no plaintext fallback | Satisfied for current controlled validation path | Existing tests cover correct/wrong host, valid/expired/future fixtures, and downgrade rejection. Public policy still depends on unresolved trust/time gates. |
| Guest certificate-time assurance | Missing | CMOS yields only `Plausible`; no authenticated freshness or rollback floor exists. |
| Revocation policy | Requires release-owner decision | No CRL/OCSP/stapled processing. Decide strict failure or approve a short preview soft-fail risk with signed local deny data. |
| Bounded response and network handling | Partially satisfied | Existing Navigator limits include 32 KiB headers, 256 KiB encoded body, 5 redirects, and 5-second connect/read timeouts. Exercise those limits against the selected origin in the later pilot. |
| Pilot allowlist, expiry, cleanup, and evidence capture | Missing | Select one hostname and time window; capture serial/pcap/attestation; prove fixture shutdown, QAC release, and removal of temporary trust/target files. |
| Separate pilot authorization | Requires release-owner decision | A later request must authorize one bounded qualification after the technical gates above are satisfied. |

**Disposition: NOT READY / NOT ATTEMPTED.** A bounded public-origin pilot is not recommended yet. A later authorization should not override the missing technical prerequisites.

## 10. Recommended NAVHTTPS7 scope

1. Select the root-policy source and implement a small fingerprint allowlist/denylist schema that preserves server-auth purpose and distrust metadata. Reject unsupported constraints rather than dropping them.
2. Add authenticated policy metadata rooted in the release/verified-boot trust chain. At runtime, compare manifest bytes, PEM bytes, PEM block count, parsed certificate count, and signed policy IDs; reject parser skips for production.
3. Choose and document the guest time source, freshness, anti-rollback state, boot uncertainty, and recovery. Keep public certificate verification blocked until the guest can report a policy-accepted time state.
4. Obtain a release-owner revocation decision. If preview soft-fail is approved, implement only the bounded status/deny mechanism required for the selected scope and report its limits accurately.
5. Re-run hosted regressions. Then use QAC-exclusive admission for one controlled guest negative-policy pass and, only after all prerequisites and owner decisions are closed, request a separate bounded public-origin qualification. Keep the public-browsing default disabled.

No NAVHTTPS6 change authorizes publication or public preview release.
