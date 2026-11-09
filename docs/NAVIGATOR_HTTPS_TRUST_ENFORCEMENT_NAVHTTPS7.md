# NAVHTTPS7 — Navigator HTTPS Trust Enforcement

**Outcome B — the production trust boundary is stricter and the candidate is accounted for, but no production trust profile is ready.** The normal policy remains disabled. Controlled HTTPS qualification remains an explicit, fixture-only path. No public-origin request was made.

## 1. NAVHTTPS6 starting state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`; development host: `LEON2_RDT` (Windows).
- Required branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- NAVHTTPS7 starting HEAD: `35b08d4e50d8cc39c3e3706677f4071fb329477b`, matching the requested starting commit.
- The NAVHTTPS6 report began at `4536fcec79ea215df6486865fc0b7936539c9681`; its resulting policy was disabled by default, but the packer still accepted a caller override without authenticated release approval.
- The 2026-08-13 shipped bundle retained its historical `production_ready=yes`; the 2026-11-05 curl/Mozilla candidate remained `production_ready=no` and review-only.
- I219-LM remains `last_completed=PHASE26`, `next_expected=PHASE27`. No `.phase`, I219-LM source, or physical NIC settings were changed.

## 2. Trust override and activation audit

The activation path runs from `GXOS_NAVIGATOR_HTTPS_POLICY` through `scripts/generate-wallpaper-pack.ps1`, which writes `/config/navigator/https-policy.txt` and the `HTTPSPOL.TXT` compatibility alias. The ramdisk also stages `/certs/ca-bundle.pem`, `/certs/ca-bundle.manifest`, and `/certs/trust-profile.json`; bare-metal runtime loads these in `gxos_tls_foundation.cpp` and evaluates the selected state in `make_validated_https_policy_info()`.

The packer now checks an override before it creates output. It accepts `disabled`, `local-smoke-only`, `user-trust-dev-mode`, `controlled-qualification`, and `public-pilot-candidate`; it rejects `production-validated` and unknown values. An accepted token is configuration, not proof of approval. The ordinary package policy stays `disabled` when no override is present.

At runtime, `controlled-qualification` requires a matching `fixture-only` profile and works only for the controlled `.guidexos.test` HTTPS fixture lane. `public-pilot-candidate` is always review-only. `ProductionValidated` is inert, and the runtime reports `publicInternetReady=false` and `productionReady=false` for bare-metal trust. The former `/config/navigator/real-public-https-ca-bundle-enabled.txt` / `RPUBCAEN.TXT` signal is no longer read or generated. The separate real-public-probe configuration cannot pass the trust prerequisite while public trust readiness is hard-disabled.

The smoke runner’s critical Name Constraints case was staged, then stopped at QAC admission because another project’s `FONT15R4R2` QEMU process was active. Its admission check occurs before `Start-Process` launches QEMU. No guest from this phase was started.

## 3. Legacy CA-label handling

The historical 2026-08-13 source bytes and tracked compatibility manifest at `out/wallpaper-pack/certs/CABUNDLE.MAN` are preserved. Its `production_ready=yes` value remains available for diagnostics and historical evidence only; neither it nor a matching unsigned PEM hash can authorize public HTTPS. When the packer regenerates a `shipped-public` manifest, the generated value is now `production_ready=no`.

The runtime separately checks PEM structure, manifest hash, manifest byte length, detected PEM block count, and successful parsing. Any unsupported or malformed certificate rejects the complete bundle; runtime no longer keeps a partially parsed CA chain. This distinguishes basic syntax and integrity from provenance, source policy, and authorization.

## 4. Implemented trust-profile contract

The versioned `guidexos.navigator.trust-profile.v1` metadata records profile ID/version/class, bundle identity and SHA-256, source description/date, trust purpose, supported and unsupported restrictions, required verifier capabilities, trust-policy version, clock assurance, revocation state, approval state, and authorization-evidence label. The runtime imposes a 4 KiB metadata cap and binds profile hash and bundle identity to the loaded CA manifest. It requires a complete manifest identity before accepting the profile.

Profiles emitted for fixtures are `fixture-only`; ordinary public-source material is `candidate`. Missing metadata, identity/hash mismatch, unsupported schema/version, or profile-class mismatch blocks the selected policy. A metadata field claiming release approval is not trusted as a signature.

The profile parser checks the presence and basic shape of restriction/capability arrays, but does not interpret their entries. This is safe only because the public runtime activation path remains disabled; semantic policy validation is still a release prerequisite.

## 5. Authentication and provenance limitations

The profile and CA manifest are unsigned. SHA-256 detects a mismatch between files when a trusted expected hash exists; it cannot authenticate a manifest that can be replaced with the bundle, establish who supplied the source, or prove approval. The profile’s `source_checksum_sha256` currently binds to the bundle digest; it is not independent signed provenance.

No release signing key, verifier, verified-boot trust anchor, or key-rotation contract was invented or added. The next release design needs an independently rooted signer and a verifier that authenticates profile identity, policy version, approval state, and bundle hash before use. No private signing key is present in the repository or package.

## 6. Mbed TLS restriction-compatibility matrix

The bundled profile is Mbed TLS 4.1.0 with TF-PSA-Crypto 1.1.0 and the repository’s guideXOS split configuration. Source inspection and an isolated parser harness used that exact configuration. A controlled self-signed CA fixture carrying critical Name Constraints returned a parse error; the same constraint marked noncritical parsed successfully. This matches the behavior discussed in [Mbed TLS issue #8759](https://github.com/Mbed-TLS/mbedtls/issues/8759). The open [PR #11027](https://github.com/Mbed-TLS/mbedtls/pull/11027) targets the upstream development branch and is not part of the bundled 4.1.0 runtime.

| Restriction or behavior | Classification in the shipped runtime | Evidence / limit |
|---|---|---|
| Certificate signatures, CA basic constraints, path length, CA key usage | Enforced | Mbed TLS chain builder checks signatures, `CA`, `keyCertSign`, and path length. |
| Certificate validity dates | Enforced with available wall clock | Bare-metal CMOS time is only `Plausible`, not authenticated; public use remains blocked. |
| Leaf server-auth EKU and TLS key usage | Enforced | The TLS client verifies leaf `serverAuth` EKU and key usage. This does not implement a Mozilla root purpose policy. |
| Requested DNS hostname / leaf SAN | Enforced by TLS hostname verification | It constrains the leaf name to the requested host, not which names a root is authorized to issue. |
| Critical unknown X.509 extension | Explicitly rejected by the parser | Mbed TLS rejects an unrecognized critical extension. The critical Name Constraints fixture exercises this path. |
| Critical Name Constraints | Explicitly rejected, not enforced | The controlled critical fixture fails parsing; this blocks the whole bundle under strict runtime parsing. |
| Noncritical Name Constraints | Not represented or enforced | The controlled noncritical fixture parses; Mbed TLS does not retain or apply its restriction. |
| Certificate policy extension parsing | Partly represented; policy-tree restrictions not enforced | The X.509 parser can parse certificate-policy OIDs, but chain verification does not evaluate a certificate-policy tree. Unsupported critical policy syntax can fail parsing. |
| Mozilla server-auth purpose bits, domain constraints, distrust-after policy | Not represented in the curl PEM/runtime trust decision | These source-level restrictions cannot be reconstructed from the PEM alone. |
| Permitted/excluded DNS subtrees | Unsupported | No Name Constraints representation or path verifier exists in this bundled runtime. |
| Intermediate CA Name Constraints | Unsupported | Unsupported critical forms fail parsing; noncritical forms can be ignored. Strict bundle parsing prevents a filtered partial store but cannot make ignored noncritical restrictions enforceable. |
| Root distrust/revocation overlay | Unverified / unavailable | No signed local deny overlay or online revocation mechanism is active. |

## 7. CA candidate accounting

The dated input is curl’s Mozilla-derived CA Extract: 118 PEM certificates, 200,116 bytes, SHA-256 `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`, source date 2026-11-05, rotation `mozilla-2026-11-05-review`. The checked-in [accounting report](../assets/certs/candidates/mozilla-cacert-2026-11-05.accounting.json) records every certificate by DER SHA-256.

The deterministic policy included **0** roots and excluded **118** because PEM input lacks the required Mozilla purpose/domain/distrust policy and this verifier cannot enforce name constraints or policy processing. Fixture fingerprints were checked and no fixture-root contamination was found. Repeating generation with identical pinned input and toolchain produced byte-identical report SHA-256 `e0ad6623f8b67be3d2cb4908d4a1bfa91bd85b59d9224df40bfc048b036ef96`. The toolchain identity is recorded in the report. Integrity and repeatability do not authenticate source provenance or release approval; the candidate remains disabled and was not promoted.

## 8. Hosted and guest qualification evidence

| Check | Result |
|---|---|
| `scripts/smoke-navigator-hosted.ps1 -Build` | PASS; `NAVIGATOR_SMOKE_RESULT: PASS`; log `logs/navigator-hosted-smoke-20261108-202605.log`. |
| DNS protocol and controlled fixtures | PASS: 13 protocol scenarios + 6 controlled fixture checks. |
| UDP/socket wire bounds | PASS: 6 cases. |
| TCP connect/cleanup lifecycle | PASS; includes IPv4 attribution, failed SYN, pending SYN close, and socket bounds. Existing type-limits warning remains. |
| Navigator URL resolution | PASS: 11 cases. |
| HTTPS qualification contract | PASS: 7 cases, including blocked-QAC classification. |
| Trust-profile contract | PASS: allowed/rejected overrides, version/hash binding, missing metadata, unsupported version, missing evidence, and self-asserted approval. |
| Secure random contract | PASS. |
| Mbed TLS profile verifier | PASS: 4.1.0 / TF-PSA-Crypto 1.1.0 / 3 repository patches. |
| CA validation | PASS for candidate (118 roots, `production_ready=no`), historical shipped source (121 roots, legacy label preserved but newly generated output `no`), and one-root fixture (`test_only=yes`). |
| Candidate policy accounting | PASS: deterministic output, 118 excluded, 0 included, no fixture overlap. |
| Normal wallpaper-pack policy | PASS: generated policy defaults to `disabled`; public-source profile class is `candidate`. |
| Freestanding AMD64 kernel target | PASS: `mingw32-make -C kernel ARCH=amd64 BUILD_DIR=../tmp/navhttps7-kernel-build`; produced `kernel.elf`. |
| Critical Name Constraints guest case | Pending. Runner built and staged the isolated guest, then QAC admission rejected it because PID 26568 owned QEMU for `D:\dev\guideXOSServer_FONT_SUPPORT\...\FONT15R4R2-guest-minimum-size-retry1`. No guest was started by this phase. |

The isolated Mbed TLS parser harness separately confirmed that a valid existing fixture parses, a critical Name Constraints CA fails parsing, and a noncritical Name Constraints CA parses. No public-origin request, public-pilot scenario, or real-public-probe flag was run.

## 9. Guest-time assurance prerequisites

The current guest uses CMOS RTC and reports `Plausible`; this is not authenticated time and has no rollback floor. Existing invalid-clock behavior remains fail-closed. Public certificate validation needs a source policy with authenticated freshness, protected anti-rollback across boots, monotonic within-boot progression, and defined handling for battery loss, stale network time, restored snapshots, and unavailable time service. UEFI time or unauthenticated NTP alone does not provide that assurance.

## 10. Revocation-policy decisions still pending

CRL parsing is not included in the bare-metal profile, Navigator does not fetch or check OCSP, and TLS does not request or consume stapled OCSP. A signed, bounded local distrust overlay is also absent. The release owner must choose the required status policy and its freshness/failure behavior; NAVHTTPS7 does not silently select soft-fail. No revocation code changed.

## 11. Explicit public-pilot readiness gates

Public HTTPS is **NOT READY / NOT ATTEMPTED**. A later pilot requires, at minimum:

- A fingerprint-based root policy derived from authenticated source metadata, with unsupported restrictions rejected or enforced.
- A release-authenticated trust profile and runtime verifier rooted in verified boot/package trust.
- Authenticated fresh guest time and anti-rollback handling.
- A release-owner revocation policy and corresponding bounded implementation.
- A QAC-exclusive controlled guest run covering the runtime profile checks and the existing DNS/TCP/TLS/hostname/no-downgrade contract.
- Separate explicit authorization for one named public origin after all technical gates pass.

The default remains disabled. No public browsing or public pilot is authorized by this change.

## 12. Recommended next milestone

First obtain exclusive QAC admission and run the staged critical Name Constraints negative case plus the controlled HTTPS qualification regression. In parallel, define the release trust anchor and signed profile format, then source machine-readable Mozilla purpose/domain/distrust metadata and a verifier capable of enforcing the selected restrictions. Keep candidate accounting at all-excluded until that policy and verifier are evidenced. Treat authenticated time and revocation as release decisions and prerequisites; retain PHASE27 I219-LM work as an independent hardware gate.
