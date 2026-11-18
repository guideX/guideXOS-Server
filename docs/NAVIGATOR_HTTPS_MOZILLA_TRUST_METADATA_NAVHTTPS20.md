# NAVHTTPS20 — Mozilla Trust Metadata and Restricted CA Candidate

**Outcome B — metadata foundation established.** The importer is reproducible and fail-closed, but the pinned sources do not contain a complete per-root policy assessment. No public root is technically eligible, no restricted trust PEM was generated, and production approval and public HTTPS remain disabled.

## Repository and accepted baseline

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `7cb73fbd503b98e6497946be8e6a061e6cef98a8`
- I219-LM phase state remains `last_completed=PHASE26`, `next_expected=PHASE27`.
- NAVHTTPS19 remains the accepted verifier baseline: pinned Mbed TLS 4.1.0 (`0fe989b6b514192783c469039edd325fd0989806`) and TF-PSA-Crypto 1.1.0 (`29160dd877d29658279fd683b2ae57b320ddcf09`), 40/40 certificate cases, DNS/IP/email/directoryName Name Constraints enforcement, fail-closed unsupported constrained forms, and the original NAVHTTPS18 noncritical Name Constraints bypass regression.
- NAVHTTPS17 authenticated fixture-time-to-TLS behavior remains intact. No dependency pins or kernel certificate-validation code changed in NAVHTTPS20.

## Authoritative source inventory

All local inputs and the exact incomplete overlay bytes are recorded in [`source-lock.json`](../assets/certs/mozilla/navhttps20-sources/source-lock.json). The `snapshot_date` is 2026-11-16. The importer verifies the overlay hash and confirms the parsed object matches the pinned file before evaluating roots. `.gitattributes` disables line-ending conversion for the pinned source snapshots, PEM inputs, hashed fixture bundles, and generated manifest so their byte hashes remain stable on Windows checkouts.

| Source | Pinned input | Purpose and limits | License recorded |
|---|---|---|---|
| [NSS `certdata.txt`](https://github.com/nss-dev/nss/blob/8c60e757c30b1b239bae46532b4f6d4ebb1e1cad/lib/ckfw/builtins/certdata.txt) | Commit `8c60e757c30b1b239bae46532b4f6d4ebb1e1cad`, 2026-11-13 23:31:24 UTC; SHA-256 `6d177e33ab54b338edfee24dc3868585833636716a320a725137ab4a15927292` | Root DER, NSS trust objects, Mozilla CA policy membership, and root-level server/email distrust-after fields. | MPL-2.0 |
| [Mozilla Root Store Policy](https://github.com/mozilla/pkipolicy/blob/bb613bb30b8d85706bac7ecdd877d19b50efe4e7/rootstore/policy.md) | Commit `bb613bb30b8d85706bac7ecdd877d19b50efe4e7`, 2026-09-23 13:24:54 UTC; SHA-256 `0e39e12a7251b5011c86cbc83355eb63dd0b82ca33019f5ccafad6cc7ffb59db`; version 3.1, effective 2026-07-01 | Root-program policy version and program-level requirements; it is not a complete fingerprint-keyed database of every root's constraints. | CC-BY-SA-3.0-or-later per Mozilla website text licensing; repository metadata reports no SPDX license. |
| [Mozilla Additional Trust Changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes) | Retrieved snapshot 2026-11-16; SHA-256 `4cf63203c5fb8aef744b8c8d148d4bc4cd43c2e37881b20ae03f9c16eed2b60d` | Best-efforts explanatory page covering examples of distrust-after and other trust changes. It is not machine-readable or complete per root. | CC-BY-SA-3.0-or-later |
| [curl CA Extract](https://curl.se/docs/caextract.html) comparison input | `cacert.pem` dated 2026-11-05 04:12:01 UTC; 118 certificates; SHA-256 `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285` | Comparison-only PEM candidate input; curl documents that the PEM omits some browser-specific constraints. | MPL-2.0 |
| [curl converter](https://github.com/curl/curl/blob/a7342f165557f6889e6ba9ba193f0499b5084327/scripts/mk-ca-bundle.pl) | Commit `a7342f165557f6889e6ba9ba193f0499b5084327`, 2026-10-22; SHA-256 `a70b45f335e51177748dbc29b3041964e4921c00554ca389ae05598a6f418659` | Reproducibility/provenance for the comparison bundle. | curl license |

The [NSS Root Store guide](https://firefox-source-docs.mozilla.org/security/nss/runbooks/rootstore.html) identifies `certdata.txt` as NSS root-store data. Mozilla's page also states that EV data is maintained in Firefox PSM and that OneCRL/intermediate distrust is separate. The curl PEM is therefore not treated as a complete Mozilla trust decision. No root restriction is inferred from its subject name or certificate bytes alone.

## Importer and eligibility rules

[`navigator_mozilla_trust_metadata.py`](../scripts/navigator_mozilla_trust_metadata.py) parses supported `CKO_CERTIFICATE` and `CKO_NSS_TRUST` records. It decodes NSS octal DER values, validates embedded SHA-1 and MD5 hashes where present, joins objects by certificate SHA-1 only as an index, then requires exact DER identity and reports the SHA-256 fingerprint. Duplicate or contradictory trust objects, orphan trust records, malformed PEM, hash mismatches, unknown metadata, and unsupported encodings fail closed.

For each of the 118 curl comparison roots, the deterministic manifest records subject and issuer, certificate fingerprints, pinned NSS revision, Mozilla CA policy membership, server-auth purpose and trust value, other NSS purposes (email and code signing) and step-up flag, server/email distrust-after fields, fixture-root checks, missing or unsupported fields, policy overlay state, path-validation state, and stable exclusion reasons. Trust purpose is not conflated: only `CKT_NSS_TRUSTED_DELEGATOR` grants TLS server authentication. `CKT_NSS_NOT_TRUSTED`, `CKT_NSS_MUST_VERIFY_TRUST`, and `CKT_NSS_TRUST_UNKNOWN` are preserved and cannot grant it.

The eligibility function additionally requires exact provenance, Mozilla CA policy membership, no applicable distrust, enforced distrust-after semantics, complete source-bound per-root external policy, enforceable domain restrictions, supported certificate-policy requirements, compatible Name Constraints, and a path verified by the pinned guideXOS Mbed TLS profile. Unsupported, missing, contradictory, or unverified data produces exclusion reasons. The importer writes a candidate PEM only when at least one root passes every gate; it refuses to create one from incomplete metadata. A generated manifest is metadata accounting and is not itself release authorization.

### Implemented versus missing metadata

| Metadata | NAVHTTPS20 handling | Remaining boundary |
|---|---|---|
| Certificate DER and identity | Exact DER match plus embedded hash checks; SHA-256 fingerprint in every report record. | The comparison bundle and NSS source have different root sets; unmatched certificates remain excluded. |
| TLS server-auth trust | Explicitly parses and records the server-auth trust value separately from other purposes. | No candidate from a record that is `MUST_VERIFY`, missing, unknown, or distrusted. |
| Root-level explicit distrust | `CKT_NSS_NOT_TRUSTED` is parsed as a rejection. | Current NSS snapshot has 0 TLS `NOT_TRUSTED` records; this is not a statement about intermediate, leaf, OneCRL, or live revocation status. |
| Root-level distrust-after | Server and email timestamps are normalized to UTC and retained; the comparison boundary is tested. | Enforcement is not integrated per chain/root, so dated roots remain ineligible. |
| Mozilla CA policy membership | `CKA_NSS_MOZILLA_CA_POLICY` is retained. | Membership alone does not satisfy the full Root Store Policy. |
| Domain and root-specific restrictions | Source-bound overlay schema with required completeness fields; absent/incomplete records reject. | The pinned Mozilla wiki snapshot is guidance, not a complete machine-readable per-root registry. Overlay has no records and is marked incomplete. |
| Certificate-policy requirements / EV | Overlay fields and enforcement requirements fail closed unless a required selected policy tree is supported. | Per-root policy OIDs/requirements are not imported from a complete source; EV data is in PSM. |
| Name Constraints | NAVHTTPS19's pinned verifier capability is identified; unsupported constrained forms fail closed. | No complete source mapping establishes which restrictions apply to each candidate root/path. |
| Intermediate and leaf revocation | Kept distinct from root distrust. | OneCRL is not imported; this Navigator path does not consume CRL, OCSP, stapled status, or local deny records. No soft-fail policy is selected. |

## Root accounting and generated artifact

The latest NSS commit restores TLS server-auth trust for DigiCert Trusted Root G4 and supplies a server distrust-after date. The importer recorded it as `2609-01-04T23:59:59Z`; NAVHTTPS22's audit found that this was a timestamp parser defect. The pinned NSS comment identifies the cutoff as `2026-09-14T23:59:59Z`, which NAVHTTPS22 now parses and enforces in the exact-root cutoff table. Four matched comparison roots carry server distrust-after dates: Izenpe.com, emSign Root CA - G1, DigiCert Trusted Root G4, and Autoridad de Certificacion Firmaprofesional CIF A62634068.

| Accounting field | Result |
|---|---:|
| NSS certificate objects / NSS trust objects | 167 / 167 |
| NSS TLS server-auth trusted / must-verify / explicit-distrust / unsupported | 113 / 54 / 0 / 0 |
| curl comparison roots examined | 118 |
| Exact DER identity matches / unmatched comparison entries | 116 / 2 |
| Matched comparison roots with server-auth trusted / must-verify | 113 / 3 |
| Matched roots with server distrust-after dates | 4 |
| Technically eligible / excluded | 0 / 118 |
| Candidate roots included / production-approved | 0 / 0 |
| Public HTTPS enabled | No |

The generated [`mozilla-nss-2026-11-13.navhttps20-trust-manifest.json`](../assets/certs/candidates/mozilla-nss-2026-11-13.navhttps20-trust-manifest.json) is the pinned accounting report. It contains a root-by-root exclusion rationale. All 118 entries are blocked at minimum by `external-policy-snapshot-incomplete`, `root-specific-external-restrictions-missing`, and `certificate-path-validation-unverified`; four also record that server distrust-after is not enforced. The two unmatched entries have no current NSS certificate/trust record. No fixture root was detected in the candidate source input. No restricted public CA PEM was generated.

## Trust-profile integration and regression coverage

The PowerShell trust-profile gate binds a candidate profile to the metadata manifest hash and source bundle hash. It rejects missing metadata, unsupported manifest versions, malformed or changed hashes, incomplete external policy, fixture-root contamination, empty or inconsistent candidate counts, invalid root eligibility records, nonzero production-approved roots, and any manifest claiming public HTTPS is enabled. A technically eligible candidate can validate while `Authorized` remains false; legacy `production_ready=yes` does not bypass the approval state. The NAVHTTPS20 manifest itself remains blocked by incomplete policy, as intended.

Offline coverage combines the new importer and manifest tests with the existing pinned X.509 and policy corpora. It includes exact identity/hash verification, conflicting trust records, unsupported NSS fields, missing/unknown metadata, explicit distrust and distrust-after boundary, modified DER/fingerprints, wrong purpose, incomplete restrictions, policy and Name Constraints enforcement gates, fixture contamination, tampering, hostname/date/signature failures, and deterministic regeneration. The actual Mbed TLS verifier and NAVHTTPS19 enforcement remain qualified by their offline suites; no public origin is required.

| Qualification | Result |
|---|---|
| NAVHTTPS20 metadata importer | 35/35 tests pass |
| Deterministic generation | Two standalone generations byte-identical; manifest SHA-256 `a34bba2ada86bf8a5d86680ac5bfb284e973510e554b8c7fa8dd438132a91e90` |
| NAVHTTPS20 trust-profile PowerShell suite | PASS |
| NAVHTTPS18 Mbed TLS certificate verifier regression | 15/15 assertions pass; valid, unknown root, date, hostname, signature, EKU, strict bundle, noncritical Name Constraints regression, and policy-tree limitation exercised |
| NAVHTTPS19 Name Constraints corpus | 40/40 pass; 16 positive and 24 negative cases, including 22 Name Constraints rejections and 2 malformed parse rejects |
| NAVHTTPS19 controlled AMD64 guest scenarios | 4/4 pass, including the original bypass rejection and NAVHTTPS17 control |
| Roughtime baseline | 96/96 pass |
| Strict Ed25519 | 238 unique cases; hosted 238 and freestanding 238, 0 failures |
| Clock assurance / secure-random contract | PASS / PASS |
| DNS resolution | 13 protocol cases and 6 controlled-fixture checks pass |
| UDP/socket bounds | 6 cases pass: 4 checksum and 2 datagram-bound cases |
| TCP/IPv4 attribution and cleanup | PASS; existing compiler warning at `kernel/core/tcp.cpp:636` about `ephemeral > 65535` |
| HTTPS qualification contract | 7 cases pass |
| Hosted Navigator smoke | PASS; local HTTP/HTTPS only, log `logs/navigator-hosted-smoke-20261116-210507.log` |

The hosted smoke used a fresh temporary fixture directory so the ignored `navigator-smoke/generated/` content was not overwritten. The optional `-PersistentFixtureDir` parameter makes that location explicit. The source change to trust-profile validation is PowerShell release-profile metadata validation; the importer is offline tooling. No production runtime X.509 verifier or kernel source changed. Therefore NAVHTTPS20 did not rebuild AMD64 kernels or rerun QEMU. No public-origin DNS/TCP/TLS request was made. NAVHTTPS19's earlier controlled QEMU evidence remains documented in [`NAVIGATOR_HTTPS_NAME_CONSTRAINTS_NAVHTTPS19.md`](NAVIGATOR_HTTPS_NAME_CONSTRAINTS_NAVHTTPS19.md).

## Production authenticated time and revocation prerequisites

NAVHTTPS17 demonstrates fixture-only authenticated time consumed by TLS; it does not establish production time authority. A pilot still needs authenticated production server identities, secure provisioning of Roughtime public keys, independent real-world operators, a reviewed agreement policy, freshness and uncertainty requirements, cross-boot continuity/rollback safeguards, and production TLS time-lease authorization. Fixture operators and test leases must remain test-only. NAVHTTPS20 did not change the clock or implement a Roughtime service.

NSS root distrust and distrust-after timestamps are not leaf or intermediate revocation. Mozilla's OneCRL and intermediate distrust data are separate, and the current Navigator validation path does not consume CRLs, OCSP, stapled OCSP, or a local certificate denial list. A release owner must decide the revocation policy and accepted residual risk before any pilot; this phase neither chooses soft fail nor claims comprehensive revocation protection.

## Public HTTPS readiness matrix

| Requirement | Status | Evidence / remaining work |
|---|---|---|
| Root certificate provenance | **PASS** | Pinned NSS commit/hash, exact DER matching, SHA-256 fingerprints, and pinned comparison-bundle provenance. Two comparison entries have no current NSS match and are excluded. |
| Server-authentication trust purpose | **PARTIAL** | NSS purpose is imported distinctly and fail-closed; 113 trusted records, 3 comparison roots requiring verification, 2 unmatched. Complete candidate policy is still unavailable. |
| Mozilla distrust metadata | **PARTIAL** | Root TLS trust and four matched distrust-after timestamps are preserved. Enforcement is not per-chain; intermediate/OneCRL and live status are outside this snapshot. |
| Domain restrictions | **BLOCKED** | No complete fingerprint-keyed external restriction registry is available in the pinned overlay. |
| Name Constraints | **PARTIAL** | NAVHTTPS19 verifier enforcement is qualified, but applicable root/path policy is not completely mapped per candidate. |
| Certificate-policy enforcement | **BLOCKED** | No complete per-root policy requirement source and selected-policy path evidence for a candidate. |
| Manifest integrity | **PASS** | Pinned inputs, strict content/hash validation, deterministic output, schema binding, and tamper regressions pass. This does not authenticate release approval. |
| Release-owner authorization | **OWNER DECISION REQUIRED** | Zero roots approved; production approval requires a separate explicit release decision and authenticated approval mechanism. |
| Production authenticated time | **BLOCKED** | NAVHTTPS17 evidence is fixture-only. Production identities, keys, agreement, freshness/uncertainty, and TLS lease policy remain. |
| Production time-source independence | **BLOCKED** | No independently operated production Roughtime sources are provisioned or qualified. |
| Cross-boot protection | **BLOCKED** | Production continuity and rollback protections remain to be defined and qualified. |
| Revocation-policy decision | **OWNER DECISION REQUIRED** | CRL/OCSP/OneCRL/local denial behavior and residual risk have not been selected; no soft-fail behavior is presumed. |
| Public-origin DNS/TCP/TLS readiness | **BLOCKED** | No public-origin request was authorized or made; public HTTPS remains disabled. |
| Actual public HTTPS guest qualification | **BLOCKED** | No public CA candidate exists and no public-origin guest test was authorized. |

## Smallest practical NAVHTTPS21 scope

Resolve the per-root policy data gap for one fingerprint only: identify an authoritative, current Mozilla record set for a candidate root's domain restrictions, distrust, certificate-policy requirements, and program-policy state; bind those records to its exact DER fingerprint; then run that single root through the pinned Mbed TLS profile and existing offline verifier corpus. Keep it excluded unless every required restriction is supported and tested. This phase should not add revocation networking, public-origin requests, or production authorization.

## Source references

- [NSS Root Store guide](https://firefox-source-docs.mozilla.org/security/nss/runbooks/rootstore.html)
- [Mozilla Additional Trust Changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes)
- [Mozilla Root Store Policy 3.1 snapshot](https://github.com/mozilla/pkipolicy/blob/bb613bb30b8d85706bac7ecdd877d19b50efe4e7/rootstore/policy.md)
- [curl CA Extract](https://curl.se/docs/caextract.html)
