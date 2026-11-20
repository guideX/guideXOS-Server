# NAVHTTPS23 — Authoritative Root Coverage and First Candidate Qualification

**Outcome A — First public-root candidate qualified for scoped ordinary TLS.** DigiCert TLS ECC P384 Root G5 has complete source coverage for the selected-root, ordinary server-authentication scope, is compatible with the enforced restrictions in the active Mbed TLS profile, and passes reproducible candidate-specific offline qualification. The candidate is disabled and unapproved. This does not enable public HTTPS or approve production trust.

**Repository:** D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS
**Branch:** NAVIGATOR_GENERAL_IMPROVEMENTS
**Starting HEAD:** b2236cd01c09f8dc51412340d3efafa06357e9e6
**Assessment date:** 2026-11-19

## 1. Accepted baseline and preflight

The actual ancestry is b2236cd0 (NAVHTTPS22 accounting correction), parent bf86e240 (NAVHTTPS22 implementation), then f47d71b6 (NAVHTTPS21). NAVHTTPS22 therefore began on top of the earlier NAVHTTPS22 implementation, as requested.

NAVHTTPS22's accepted baseline is retained: corrected exclusion accounting for 118 roots; the two Mozilla-Removed Buypass roots; zero exact-DER mismatches; three matched NSS CKT_NSS_MUST_VERIFY_TRUST records; no explicit NSS TLS distrust records; 65/65 importer and restriction tests; 12/12 distrust-after tests; NAVHTTPS18 15/15; NAVHTTPS19 40/40; Roughtime 96/96; hosted and freestanding Ed25519 238/238 each; and successful ordinary and qualification AMD64 builds. The NAVHTTPS21 distrust-after guest scenario was still pending.

Preflight found a clean tracked worktree at the expected starting HEAD and 82 previously reported untracked status entries. All those files and the artifacts generated during NAVHTTPS23 were preserved. The hosted smoke run changed one recent-item entry in desktop.json; because that file was clean at preflight, the incidental change was restored. No App Model, I219-LM, DMA, or bare-metal ABI change is part of this work.

The I219-LM state remains:

    last_completed=PHASE26
    next_expected=PHASE27

## 2. Exact selected root and Mozilla trust facts

| Attribute | NAVHTTPS23 evidence |
|---|---|
| Certificate | DigiCert TLS ECC P384 Root G5 |
| Subject | CN=DigiCert TLS ECC P384 Root G5,O=DigiCert\, Inc.,C=US |
| SHA-256 of DER | 018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05 |
| NSS identity and purpose | Exact DER identity; active NSS server-auth record is CKT_NSS_TRUSTED_DELEGATOR. |
| Current Mozilla inclusion | CCADB V5 row dated 2026-11-17 says Mozilla Status=Included; current TLS Server Authentication community report dated 2026-11-19 has the same exact certificate identity. |
| Explicit distrust and cutoff | No explicit NSS TLS distrust and no TLS or email distrust-after cutoff for this root. |
| Root constraints | No X.509 Name Constraints extension; no exact G5 match in the pinned NSS imposed-constraints table. |

Mozilla [Bug 2031732](https://bugzilla.mozilla.org/show_bug.cgi?id=2031732) records DigiCert's transition-plan row for this root as Websites | 018E…C1B05 | KEEP. That is corroborating transition context only. The active NSS server-auth trust record and CCADB inclusion record establish current trust; the CA's requested transition status is not treated as the trust decision.

## 3. Mozilla domain-restriction coverage

NAVHTTPS22 had nine of eleven candidate attributes supported and left Mozilla-specific domain restrictions and the broader program-policy/certificate-policy questions unresolved. NAVHTTPS23 resolved the selected root's remaining scope without inferring unrestricted status from a missing record or a single boolean.

| Source or enforcement point | Pinned record and coverage | Finding for this exact root |
|---|---|---|
| [CCADB TLS Server Authentication community report](https://ccadb.my.salesforce-sites.com/ccadb/Report?Name=TLSServerAuthenticationCSV) | Snapshot 2026-11-19; exact fingerprint row; Mozilla Constrained=false. | CCADB defines this field as a boolean indicating whether constraint information exists for the root/program record. It is not itself a domain list and was not used alone to assert no restriction. |
| CCADB V5 record | Snapshot 2026-11-17; exact root fingerprint; Mozilla Included; Technically Constrained=false. | Confirms inclusion and cross-checks certificate metadata; the technically-constrained boolean alone is not the restriction determination. |
| [Mozilla Additional Trust Changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes) | Existing NAVHTTPS20 snapshot dated 2026-11-16. Mozilla describes this page as best-efforts coverage, not a complete machine-readable restriction registry. | No root-specific restriction entry was identified, but silence on this page is not treated as affirmative evidence by itself. |
| [NSS imposed Name Constraints source](https://github.com/nss-dev/nss/blob/8c60e757c30b1b239bae46532b4f6d4ebb1e1cad/lib/certdb/genname.c) | Pinned genname.c revision 8c60e757c30b1b239bae46532b4f6d4ebb1e1cad; the built-in table has the two entries ANSSI and TUBITAK1. | Neither exact entry matches G5. The table is the exhaustive NSS source table at that revision. |
| [Gecko issuer validation source](https://hg.mozilla.org/mozilla-central/file/28b8992494cac70d7abafa3ed120411de6d44dc0/security/certverifier/NSSCertDBTrustDomain.cpp) | Pinned NSSCertDBTrustDomain.cpp revision 28b8992494cac70d7abafa3ed120411de6d44dc0 calls CERT_GetImposedNameConstraints while finding issuers. | Confirms that the NSS table is on the browser issuer-validation path, rather than an unused data table. |
| Root DER | Exact selected certificate parsed from the pinned candidate fixture and CCADB certificate record. | Root Name Constraints extension is absent. |
| Root-specific Mozilla history | Bugzilla 1764392 and 2031732 snapshots already pinned in NAVHTTPS22. | Confirms identity, initial Websites-purpose context, and transition history; neither is treated as a complete restriction registry. |

**Determination:** no selected-root-specific Mozilla domain restriction was found in the covered per-root records or the enforced NSS/Gecko imposed-constraints mechanism. For this selected G5 root and ordinary TLS scope, domain restriction coverage is complete and the status is “none identified.” The conclusion is not based solely on Mozilla Constrained=false, the transition thread, or the Additional Trust Changes page. Changes after the pinned snapshots require a refresh before any production approval.

The existing NAVHTTPS19 Mbed TLS implementation enforces DNS, IP, email, and directoryName constraints and rejects unsupported applicable constrained-name forms. Since no applicable domain constraint is attached to this root in the reviewed sources, no root-specific constraint needs to be bypassed or weakened for the candidate.

## 4. Certificate-policy applicability

The [Mozilla Root Store Policy 3.1](https://www.mozilla.org/en-US/about/governance/policies/security-group/certs/policy/) is effective 2026-07-01. Its CA-program requirements include compliance with the TLS Baseline Requirements and, where certificates are marked for EV, the EV Guidelines. Its inclusion application asks whether a CA issues EV certificates and requests the EV policy OID for that separate purpose. This is not a requirement that every ordinary TLS server-authentication path build a full RFC 5280 certificate-policy tree.

The pinned Firefox verifier source separates EV policy authorization through GetKnownEVPolicies from ordinary TLS server validation, which uses CertPolicyId::anyPolicy. Pinned Mozilla PSM EV mapping source maps G5 to EV OID 2.23.140.1.1. The distinction is therefore explicit: this candidate supports ordinary TLS server authentication only. It does not claim EV validation, an EV indicator, policy mappings, or full policy-tree semantics.

Mbed TLS 4.1.0 parses certificate policy OIDs but does not implement full RFC 5280 policy-tree processing. That missing behavior is not required for this scoped ordinary server-authentication decision. A CA's program-wide operational obligations remain under the Mozilla program and are not an independent audit performed by guideXOS. Current audit metadata in the CCADB V5 snapshot has standard, NetSec, TLS BR, and TLS EVG statement dates of 2025-11-21; audited period 2024-09-01 through 2025-08-31; and CP/CPS effective date 2026-09-03. This metadata is not a finding of current operational compliance; refresh it before production review.

**Determination:** certificate-policy status is not-required for ordinary TLS in this qualification. EV and any path requiring full policy-tree processing remain unsupported and excluded.

## 5. Attribute coverage, enforcement, and eligibility

The generated overlay is schema guidexos.mozilla-root-restrictions.v2, has coverage_status=complete, and uses coverage_scope=selected-root-set. It contains exactly the G5 SHA-256 fingerprint. It does not cover the other 117 roots; their status remains unknown.

| Attribute | Evidence status | Required behavior and enforcement | Eligibility consequence |
|---|---|---|---|
| Certificate identity | Complete | Exact DER fingerprint matched to NSS and CCADB. | Pass |
| Root inclusion | Complete | Active NSS TLS trust and CCADB Mozilla Included record. | Pass |
| Trust purpose | Complete | NSS CKT_NSS_TRUSTED_DELEGATOR for TLS server authentication. Candidate manifest scope is TLS server authentication. | Pass |
| Explicit distrust | Complete; not distrusted | Exact NSS trust record contains no explicit TLS distrust. | Pass |
| TLS/email distrust-after | Complete; none for G5 | Exact NSS records checked; existing callback still denies roots when SCT evidence is unavailable. | Pass for this root |
| Domain restrictions | Complete for G5; none identified | Exact CCADB records plus pinned NSS table and Gecko enforcement hook; Additional Trust Changes is interpreted as best-efforts only. | Pass |
| Name Constraints | Complete for G5; none on root | Root DER has no Name Constraints extension; shared NAVHTTPS19 verifier enforces supported forms and fails closed on unsupported applicable forms. | Pass |
| Certificate policy | Complete for ordinary TLS; not required | Ordinary server-auth branch does not require a policy tree; EV is separately mapped and disabled. | Pass for ordinary TLS; EV excluded |
| Issuance evidence | Complete for this scope; no extra root-specific requirement identified | Current included TLS root and representative CA-disclosed server chains; no extra issuance restriction was found for ordinary TLS in the scoped sources. | Pass |
| Program policy | Complete for current root-specific state; ongoing CA compliance not re-audited | Current Mozilla inclusion, trust purpose, Root Store Policy 3.1, and root-specific transition evidence reviewed. | Pass for technical candidate; production refresh still required |
| Candidate path validation | Verified by Mbed TLS 4.1.0 | Actual mbedtls_x509_crt_verify_with_profile with active guideXOS configs; qualification report bound by SHA-256. | Pass |

The 118-root input inventory remains diagnostic metadata; the generated public candidate bundle contains only G5. The importer reports full-inventory external-policy coverage as false, 117 other roots as unknown, one technically eligible root, one candidate root, zero production-approved roots, and public_https_enabled=false.

## 6. Pinned source and artifact hashes

The NAVHTTPS23 source lock extends, rather than replaces, the accepted NAVHTTPS20–22 snapshots. Hashes below are SHA-256 of the exact local bytes.

| Source snapshot | Revision or effective date | SHA-256 |
|---|---|---|
| NSS certdata.txt | NSS 8c60e757c30b1b239bae46532b4f6d4ebb1e1cad | 6d177e33ab54b338edfee24dc3868585833636716a320a725137ab4a15927292 |
| Mozilla Root Store Policy 3.1 | Commit bb613bb30b8d85706bac7ecdd877d19b50efe4e7; effective 2026-07-01 | 0e39e12a7251b5011c86cbc83355eb63dd0b82ca33019f5ccafad6cc7ffb59db |
| Additional Trust Changes | Existing snapshot 2026-11-16 | 4cf63203c5fb8aef744b8c8d148d4bc4cd43c2e37881b20ae03f9c16eed2b60d |
| CCADB V5 | Snapshot 2026-11-17 | f3b205d590aa5e78d5e79df631b87a331f2e3e1ea990b704c3165fa2627daf36 |
| CCADB TLS Server Authentication community report | Snapshot 2026-11-19 | 4ad8918077f77a8b1c651b3a711a32d658d610a475d4116cc8b5c08114f6253e |
| CCADB resources documentation | Snapshot 2026-11-19 | 92273cd0fdd6e595a72ed42762022ee8fdfaf7ffc29e436d648ccbd04942f7f8 |
| NSS genname.c imposed-constraints table | Commit 8c60e757c30b1b239bae46532b4f6d4ebb1e1cad | 1fd0bff1c9cfd59a3ebadbcf5bd60fa5555e29ba68a0190eda0bcf401b80e75d |
| Gecko NSSCertDBTrustDomain.cpp | Commit 28b8992494cac70d7abafa3ed120411de6d44dc0 | 42d13febe01938e87d2ece06916192f217edc9d216cbf485d5c977dcd75455ff |
| Gecko CertVerifier.cpp | Commit 28b8992494cac70d7abafa3ed120411de6d44dc0 | 53cf6f8c999a310d4e91314568a1d08d6cdddffc552ed89cfb8105a9cee4d6d3 |
| Gecko EV mapping source/header | Commit 28b8992494cac70d7abafa3ed120411de6d44dc0 | 3a737949ca1c47f477b8bce5e0129334bf00fb0e1b5a435fbba01be77fde3c13 / 0798a969bfd1d51b25910377ae01e29e4b4c939c46cd56c5be526e414a954432 |
| Bugzilla 2031732 | Last change 2026-11-16T17:05:26Z | 318d215feb08457afcb4af980224591773040586c3f75488c6098f9c61b662be |

Key generated evidence hashes:

| Artifact | SHA-256 |
|---|---|
| NAVHTTPS23 source lock | 31add38b4446f26b3a533cb45645ee40d725ab97af1d9d856e9c7ef4406f9c87 |
| Selected-root restriction overlay | 01046c433ab7b70ca8fd518c6f7d544b631321479c3e1c3b3b338ca6dfebf08d |
| DigiCert demo fixture manifest | 3215615455a63fb795c4dbdd260ceaec1072a906f53450dd52ae9ea60be7c410 |
| Mbed TLS qualification report | ce00af0c1a65ad89786d1dc52ea04a8f5e83fdd96d284874e880f98dd0c0d67d |
| NAVHTTPS23 trust manifest | 8fb3889404f5c632f67f51ef517ff025d8e49b2e9f0625ccd0fe2fa160be6790 |
| Candidate PEM | 05161ad2ac04a0df956ef803e127aa877cc5131e0a727ed8e5de43f02e8868c4 |
| Candidate metadata sidecar | e54283023e73f1b7fd02d3b3f81be2ecf4d3810322e315b4a66b43c2794660e1 |

The verifier is the repository-pinned Mbed TLS 4.1.0 revision 0fe989b6b514192783c469039edd325fd0989806. Its qualification report records hashes for x509_crt.c, the active Mbed TLS and crypto configs, C verifier harness, CMake file, fixture preparer, and offline runner. The fixture manifest binds the certificate PEMs individually.

## 7. Representative paths and cross-signing

Fixtures were captured on 2026-11-19 from DigiCert's CCADB-disclosed demo endpoints using OpenSSL s_client -showcerts; the certificate bytes and chain PEMs are hashed in tests/fixtures/navhttps23/manifest.json. No public leaf or hierarchy was fabricated. Altered-signature PEMs are clearly identified negative controls.

The captured valid path includes leaf 4398421de2131accaf446b61dea44bd9814ed65cd540ecca07afcaba45490b18, issuing CA 97a073957c5812725757672f222690655a1407d71011edfc8b2fadc5128911da, and self-signed G5 root 018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05. The demo also offers a G5 cross-certificate under DigiCert Global Root G3, fingerprint 3142f99d358d9b500e5cdd2dff8f719a8ccecf589652334848522cca807d0807, with Global Root G3 fingerprint 31ad6648f8104138c738f39ea4320133393e3a18cc02296ef97c2ac9ef6731d0.

Mbed TLS accepted the captured self-signed G5 path with the candidate root, accepted the cross-signed G5 path with the G5 candidate anchor, and accepted the Global G3 alternate path when Global G3 was separately configured as the anchor. The candidate bundle itself contains only G5. The alternate-anchor test demonstrates path behavior; it does not add Global G3 to the candidate. These tests do not claim qualification of all possible chain-building permutations or roots.

## 8. Candidate-specific verifier tests

The offline harness directly calls the active Mbed TLS 4.1.0 mbedtls_x509_crt_verify_with_profile verifier with guideXOS configs. All 11 candidate-specific cases passed:

| Case | Expected verifier result | Result |
|---|---|---|
| Valid representative chain and correct DNS hostname | Accept | PASS |
| Incorrect hostname | Reject for CN/hostname mismatch | PASS |
| Expired CA-disclosed demo leaf | Reject as expired | PASS |
| Future-dated leaf | Reject as not yet valid | PASS |
| Tampered leaf signature | Reject signature | PASS |
| Wrong trust anchor | Reject chain | PASS |
| Tampered intermediate signature | Reject signature | PASS |
| TLS server-auth purpose | Accept | PASS |
| Email-only trust purpose | Reject | PASS |
| G5 cross-signed alternate path under Global G3 | Accept only with separately supplied G3 anchor | PASS |
| Cross-signed path with G5 candidate anchor | Accept | PASS |

Coverage for the full requested assertion set is split between this candidate test and the existing shared suites:

| Requested behavior | Evidence |
|---|---|
| Valid chain, correct and incorrect hostnames, expired/future dates, invalid signature, wrong anchor, invalid intermediate, and trust purpose | Candidate-specific Mbed TLS cases above |
| Name Constraints violation and unsupported constrained-name forms | NAVHTTPS19 Mbed TLS corpus: 40/40; 16 positive and 24 negative, including 22 constraint rejections. Unsupported applicable forms fail closed. The actual G5 demo chain has no such constraint, so no synthetic G5-constrained chain is claimed. |
| Applicable certificate-policy violation | Not applicable to ordinary TLS server authentication. EV/policy-tree validation is excluded; no policy-tree support is claimed. |
| Missing/tampered metadata and unsupported manifest | NAVHTTPS23 importer tests bind selected-root/source and verifier evidence hashes, reject tampering and unsupported evidence; existing NAVHTTPS21 trust-profile suite covers missing approval and unsupported trust profile data. |
| Wrong TLS time-assurance scope and unauthorized activation | Existing trust-profile and HTTPS qualification-contract suites reject those cases. |
| Cross-signed or alternate path | Two explicit candidate-specific cross-path cases above. |
| No plaintext downgrade | HTTPS qualification contract and hosted local-fixture smoke. |

The test leaf has serverAuth and clientAuth EKUs and cannot represent a client-only negative chain; wrong-purpose rejection is exercised using Email purpose instead. The capture's EV policy OID is not interpreted as EV acceptance. No EV indicator is produced.

## 9. Candidate artifacts and readiness states

Generated artifacts:

- assets/certs/candidates/mozilla-nss-2026-11-19.navhttps23-trust-manifest.json
- assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.pem
- assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.metadata.json
- assets/certs/mozilla/navhttps23-sources/root-restrictions-overlay.json
- assets/certs/mozilla/navhttps23-sources/source-lock.json
- tests/fixtures/navhttps23/

The candidate PEM contains exactly one certificate, G5. Fixture roots are not included. The sidecar and manifest explicitly record candidate_enabled=false, production_approved=false, public_https_enabled=false, and candidate mode disabled-offline-only.

| Readiness state | Result |
|---|---|
| Source coverage complete | Yes, for the one selected root and ordinary TLS scope; full 118-root inventory is not complete. |
| Technically eligible | 1 root |
| Candidate roots | 1 root |
| Offline qualified | Yes, 11/11 candidate-specific cases plus shared regression coverage |
| Guest qualified | No; NAVHTTPS21 distrust-after guest D-A scenario remains pending |
| Production approved | 0 roots |
| Public HTTPS enabled | No |

The guest D-A scenario remains pending because the QAC admission tool was unavailable on this host. No QEMU instance was launched and no guest result is inferred.

## 10. Regression, build, and resource evidence

Executed results:

| Suite | Result |
|---|---|
| NAVHTTPS20–23 importer, restriction, and candidate Python tests | 76/76 passed (35 + 30 + 11) |
| NAVHTTPS23 actual Mbed TLS candidate harness | 11/11 passed |
| NAVHTTPS21 distrust-after offline harness | 12/12 passed |
| NAVHTTPS18 offline TLS verifier | 15/15 passed |
| NAVHTTPS19 Name Constraints | 40/40 passed; 16 positive, 24 negative, 22 constraint rejections |
| Navigator HTTPS trust profile | Passed |
| HTTPS qualification contract | 7/7 passed |
| Roughtime | 96/96 passed |
| Ed25519 | 238/238 hosted and 238/238 freestanding; 7 Python reference checks; 3,824-byte static stack-chain upper bound |
| Mbed TLS profile | Pinned Mbed TLS 4.1.0 and TF-PSA Crypto 1.1.0 verified; 4 patches |
| Libsodium profile | Pinned libsodium 1.0.22; 28 files verified |
| Clock assurance and secure randomness | Both passed |
| DNS | Passed: 13 protocol and 6 controlled-fixture checks |
| UDP/socket bounds | 6 passed: 4 checksum and 2 bounds checks |
| TCP connect cleanup | Passed; existing compiler warning at kernel/core/tcp.cpp:636 says the comparison is always false |
| Hosted Navigator smoke | Passed with local HTTP/HTTPS fixtures only; no public origin |

No new ordinary or qualification AMD64 kernel build was run. The NAVHTTPS23 changes are restricted to source metadata import, pinned evidence, candidate generation, offline fixtures, and a host-side verifier harness; no production runtime verifier or trust-profile implementation changed, so the previous kernel artifacts are not invalidated by this work. The NAVHTTPS22 AMD64 build passes are historical evidence from their recorded source revision, not current-build acceptance. The NAVHTTPS23 host build output is in tmp/navhttps23-mbedtls-offline/.

## 11. Remaining production gates

The candidate is not production-approved. Before any production approval or public HTTPS enablement:

- Recheck current Mozilla, NSS, CCADB, and CA operational/audit status and bind any changed records to new snapshots.
- Complete the existing NAVHTTPS21 distrust-after guest scenario under exclusive QAC admission.
- Provision production trusted time and establish the required cross-boot rollback assurance.
- Define and implement a production certificate revocation policy and enforcement behavior. No revocation soft-fail choice is made here.
- Obtain release-owner approval and complete the remaining runtime/release qualification gates.

No public HTTPS activation, production root approval, Roughtime trust promotion, ordinary system clock change, Name Constraints weakening, or I219-LM change was made.

## 12. NAVHTTPS24 recommendation

Keep the root scope at one. The smallest next engineering action is to run the already-prepared NAVHTTPS21 distrust-after D-A guest scenario when QAC admission is available, under exclusive ownership, and record the result without changing the phase tracker. Then refresh current external source/audit records before a separate production review. Do not expand automatically to the remaining 117 roots or implement EV policy-tree processing in NAVHTTPS24.
