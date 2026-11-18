# NAVHTTPS21 — Mozilla Trust Restrictions and Distrust-After Enforcement

**Outcome B — enforcement foundation advanced.** Exact NSS server-auth distrust-after dates are pinned, and the runtime checks the leaf `notBefore` portion. Mozilla also requires rejecting SCT timestamps after a TLS distrust-after date; Navigator does not verify SCT timestamps, so the runtime now fails closed for every chain through one of the four affected roots. Full allow/deny semantics remain **partial**, and those roots remain ineligible. The authoritative sources available here also do not give a complete fingerprint-keyed restriction assessment for any candidate root. There are zero eligible roots, no public candidate PEM, and public HTTPS remains disabled.

**NAVHTTPS22 audit amendment:** NAVHTTPS21's importer tried Python's `%Y` parser before `%y` on NSS's 12-digit `YYMMDDhhmmssZ` values. Because `%Y` accepts under-width input, the four 2026 values were recorded as years 2604–2611. The four runtime paths were denied regardless because SCT validation is unavailable, but the cutoff metadata and leaf-date comparisons were incorrect. NAVHTTPS22 changes the parser to select formats by exact width and updates the runtime table. The table below now shows the dates printed in pinned NSS `certdata.txt` comments. The citation to Bug 2018158 is contextual: it is an OISTE transition thread whose comment describes the general TLS distrust-after check (`notBefore` plus SCT timestamps), not a complete specification. Bug 2089115 is the root-specific emSign change that explicitly applies SCT checking. NAVHTTPS22 verifies the interpretation against pinned NSS values and the exercised fail-closed Mbed TLS callback. Current NAVHTTPS22 eligibility decisions and hashes are in [`NAVIGATOR_HTTPS_ROOT_ELIGIBILITY_NAVHTTPS22.md`](NAVIGATOR_HTTPS_ROOT_ELIGIBILITY_NAVHTTPS22.md).

## Accepted baseline and repository state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- NAVHTTPS21 starting HEAD: `0d9e92ee1975439f8ebcb50a379f9a68deee7f2d`
- Physical I219-LM state preserved: `last_completed=PHASE26`, `next_expected=PHASE27`.
- NAVHTTPS20's source lock was not changed. Its NSS certdata commit remains `8c60e757c30b1b239bae46532b4f6d4ebb1e1cad` (SHA-256 `6d177e33ab54b338edfee24dc3868585833636716a320a725137ab4a15927292`); its Mozilla policy commit remains `bb613bb30b8d85706bac7ecdd877d19b50efe4e7`.
- The accepted NAVHTTPS20 comparison examined 118 curl roots: 116 exact DER matches, 2 unmatched entries, 113 TLS trusted-delegator records, 54 must-verify records, and 4 matched server distrust-after records. The prior candidate and public approval counts were zero.
- Existing untracked outputs, logs, fixtures, caches, and CSS/compositor evidence were preserved. New NAVHTTPS21 build and test outputs use new, isolated `tmp/navhttps21-*` paths.

## Sources and restriction-data coverage

The NAVHTTPS21 extension lock pins the unchanged NAVHTTPS20 source lock and every new snapshot by SHA-256. The NAVHTTPS20 source pins and hashes remain the baseline of record.

| Source | Revision and integrity | Authority, use, and limits |
|---|---|---|
| [NSS built-in root store](https://github.com/nss-dev/nss/blob/8c60e757c30b1b239bae46532b4f6d4ebb1e1cad/lib/ckfw/builtins/certdata.txt) | Commit `8c60e757c30b1b239bae46532b4f6d4ebb1e1cad`; SHA-256 `6d177e33ab54b338edfee24dc3868585833636716a320a725137ab4a15927292`; MPL-2.0 | Exact DER identities, Mozilla CA policy membership, NSS purpose trust values, and root-level server/email distrust-after values. The [NSS Root Store guide](https://firefox-source-docs.mozilla.org/security/nss/runbooks/rootstore.html) identifies this source as NSS root-store data. |
| [Mozilla Root Store Policy](https://github.com/mozilla/pkipolicy/blob/bb613bb30b8d85706bac7ecdd877d19b50efe4e7/rootstore/policy.md) | Commit `bb613bb30b8d85706bac7ecdd877d19b50efe4e7`; SHA-256 `0e39e12a7251b5011c86cbc83355eb63dd0b82ca33019f5ccafad6cc7ffb59db`; policy 3.1, effective 2026-07-01 | Program-wide requirements, not a complete root-fingerprint restriction or “no restrictions” registry. License recorded in NAVHTTPS20 metadata as CC-BY-SA-3.0-or-later. |
| [Mozilla Additional Trust Changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes) | Retrieved 2026-11-16; SHA-256 `4cf63203c5fb8aef744b8c8d148d4bc4cd43c2e37881b20ae03f9c16eed2b60d`; CC-BY-SA-3.0-or-later | Mozilla-maintained best-efforts explanations and examples. The page is not machine-readable or complete per root. |
| [CCADB V5 all-certificate records](https://ccadb.my.salesforce-sites.com/ccadb/AllCertificateRecordsCSVFormatV5) | Snapshot retrieved 2026-11-17; 11,126,356 bytes; SHA-256 `f3b205d590aa5e78d5e79df631b87a331f2e3e1ea990b704c3165fa2627daf36`; 10,405 distinct fingerprint entries parsed | Exact fingerprint, record type, Mozilla status, aggregate trust-bit, and EV metadata cross-check. Data is under CDLA-2.0-Permissive with CCADB attribution. Aggregate trust bits are not a Mozilla-only trust decision. Blank fields and absent rows do not mean unrestricted. See [CCADB resources](https://www.ccadb.org/resources) and [CCADB data usage](https://www.ccadb.org/rootstores/usage). |
| [curl CA Extract](https://curl.se/docs/caextract.html) | Unchanged NAVHTTPS20 comparison input dated 2026-11-05; SHA-256 `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285` | Comparison set only. It was not refreshed to improve matching and is not treated as a complete Mozilla trust decision. |

The NAVHTTPS21 source lock is SHA-256 `68bc26b5a0e97080df6bd61169b1f7eacc5b51988c61641f3d9aa6b665d5dabb`. The structured root-restrictions overlay is SHA-256 `25339dd599e85905547096805b873209f76d137ddd24d5a1d703b4c2c6ff12f6`; it is explicitly `coverage_status: incomplete` and currently has no per-root records. A missing record means **unknown**, not “no restriction.” The distrust-after policy is schema v2, SHA-256 `36127d88adc1c7bd1868969420c88a5ca93780c646838442cfeb35570fe591ef`, and records `partial-notbefore-only`.

Overlay v2 requires exact SHA-256 keys, source IDs bound to pinned source hashes, effective dates, purpose, explicit distrust state, separate TLS/email cutoffs, domain restrictions, Name Constraints, certificate-policy requirements, issuance evidence, program-policy state, and supported enforcement declarations. It rejects malformed values, invalid UTC timestamps, unknown states, unresolved provenance, contradictions, and hash tampering. It distinguishes `none`, `unknown`, `unsupported`, and an applicable restriction. Neither CCADB blank fields nor text in an untrusted overlay can create eligibility.

## Unmatched root investigation

The two unmatched entries are the 2026-11-05 curl CA Extract certificates below. Their certificate bytes parse correctly; neither exact DER nor the same SPKI appears in the pinned NSS certificate and trust objects. The exact CCADB SHA-256 records identify both as Mozilla **Removed**. No cross-signed replacement with either exact identity or SPKI was found in the pinned NSS set. The discrepancy is a source-set difference, not a PEM extraction/parser defect. They remain excluded; the CCADB snapshot does not establish whether a replacement certificate outside this pinned set is currently used.

| Original source label | SHA-256 / SHA-1 | Expected NSS result | CCADB V5 exact record |
|---|---|---|---|
| Buypass Class 2 Root CA | `9a114025197c5bb95d94e63d55cd43790847b646b23cdf11ada4a00eff15fb48` / `490a7574de870a47fe58eef6c76bebc60b124099` | No certificate or trust record; no same-SPKI certificate | Mozilla Status `Removed`; exact SHA-256 match |
| Buypass Class 3 Root CA | `edf7ebbca27a2a384d387b7d4010c666e2edb4843e4c29b4ae1d5b9332e6b24d` / `dafaf7fa6684ec068f1450bdc7c281a5bca96457` | No certificate or trust record; no same-SPKI certificate | Mozilla Status `Removed`; exact SHA-256 match |

`CKT_NSS_TRUSTED_DELEGATOR` is affirmative trust for its specific NSS purpose. `CKT_NSS_MUST_VERIFY_TRUST` is a distinct value and does not grant generic TLS root eligibility. The counts 113 and 54 are NSS trust-value accounting; neither count is an eligibility count. Exact DER, Mozilla policy membership, TLS purpose, restrictions, path compatibility, and authorization remain separate gates.

## Distrust-after semantics and runtime enforcement

Mozilla/NSS `CKA_NSS_SERVER_DISTRUST_AFTER` includes an additional TLS issuance-evidence check. Mozilla's [Bug 2018979](https://bugzilla.mozilla.org/show_bug.cgi?id=2018979) states that TLS enforcement checks the end-entity `notBefore` **and rejects SCT timestamps after the cutoff**. Navigator currently implements only `reject iff leaf.notBefore > cutoff`; equality is accepted. The cutoff is normalized to UTC at second precision and is not compared with the present wall clock. There is no SCT parser, SCT signature verifier, CT log state, or access to verified SCT timestamps in the current Mbed TLS callback, so a backdated `notBefore` cannot satisfy the full TLS rule by itself. Existing Mbed TLS validity checks and NAVHTTPS17's controlled authenticated-time lease continue to assess ordinary certificate validity separately.

The enforcement table is keyed only by exact certificate DER SHA-256. It contains the four matched NSS TLS roots:

| Root | SHA-256 | TLS `notBefore` cutoff UTC |
|---|---|---|
| Izenpe.com | `2530cc8e98321502bad96f9b1fba1b099e2d299e0f4548bb914f363bc0d4531f` | `2026-04-15T23:59:59Z` |
| emSign Root CA - G1 | `40f6af0346a99aa1cd1d555a4e9cce62c7f9634603ee406615833dc8c8d00367` | `2026-11-16T23:59:59Z` |
| DigiCert Trusted Root G4 | `552f7bdcf1a7af9e6ce672017f4f12abf77240c78e761ac203d1d9d20ac89988` | `2026-09-14T23:59:59Z` |
| Autoridad de Certificacion Firmaprofesional CIF A62634068 | `57de0583efd2b26e0361da99da9df4648def7ee8441c3b728afa9bcde0f9b26a` | `2026-08-06T07:01:00Z` |

The existing Mbed TLS verify callback now hashes each CA certificate in the verified chain with PSA SHA-256, records any exact restricted identity, and checks the leaf date when depth zero is visited. Mbed TLS 4.1.0's root-to-leaf callback order is verified in the offline corpus. Because the callback cannot access verified SCT timestamps, it also adds `MBEDTLS_X509_BADCERT_OTHER` to every chain that reaches one of the four exact roots, including certificates before or equal to cutoff. It does not clear Mbed TLS signature, time, hostname, EKU, or Name Constraints flags. No current wall-clock comparison was added, and the existing verifier remains the only chain verifier. The importer reports full TLS distrust-after as false and blocks affected roots until SCT timestamp enforcement exists.

Cross-signed certificates and alternate anchors are not equated by subject or SPKI. A path that contains the exact restricted DER identity is subject to its cutoff, including a path with a cross-signed intermediate. A distinct cross-signed DER identity or a separately trusted alternate anchor does not inherit that NSS trust object's record. Its own exact trust identity and restrictions must be assessed. A required issuance-proof condition such as CT is not inferred absent from `notBefore`; the overlay marks it separately, and current code has no CT verifier, so such roots remain ineligible.

### Certificate-policy compatibility

Mbed TLS 4.1.0 parses `certificatePolicies` OIDs into the certificate structure. Its source states that only `anyPolicy` is currently supported for enforcement; non-`anyPolicy` values produce `MBEDTLS_ERR_X509_FEATURE_UNAVAILABLE`, and a noncritical extension may be retained without policy-tree evaluation. A critical unsupported policy extension fails parsing. There is no RFC 5280 policy-tree processing for policy mappings, policy constraints, `inhibitAnyPolicy`, or trust-anchor-required policy propagation in this profile. Therefore a single OID comparison would not justify a general path-policy claim.

No complete authoritative per-root record found here establishes a required policy OID for any shortlisted root. The importer nevertheless treats `required`, `unsupported`, and `unknown` policy states as blockers; it does not allow an overlay to claim an unimplemented policy-tree or CT enforcement mechanism. NAVHTTPS18's `certificate-policy-tree-not-applied` case preserves the known Mbed TLS limitation. No policy engine was added because no candidate-specific requirement can be supported and independently qualified from these sources.

## Root shortlist and candidate generation

The shortlist is limited to three exact DER matches with explicit NSS TLS trusted-delegator values and CCADB `Mozilla Status: Included`. Their certificates have no Name Constraints, Certificate Policies, or EKU extensions, and NSS reports no server distrust-after date. These are screening facts only; they do not establish the absence of external Mozilla restrictions or prove a path.

| Shortlist root | SHA-256 | Reason for investigation | Eligibility blocker |
|---|---|---|---|
| Telia EC TLS Root CA v3 | `098e08a91dbbf77478b96cceb89b1413a5da37b7c862606a955deb07179f4326` | Exact DER, NSS TLS trusted delegator, CCADB Included, no cutoff or certificate-level NC/policy/EKU extension | Incomplete per-root restriction coverage; no candidate-specific path fixture/verification |
| GTS Root R4 | `349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d` | Same objective screening results; no certificate-level NC/policy/EKU extension | Incomplete per-root restriction coverage; no candidate-specific path fixture/verification |
| DigiCert TLS ECC P384 Root G5 | `018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05` | Same objective screening results; no certificate-level NC/policy/EKU extension | Incomplete per-root restriction coverage; no candidate-specific path fixture/verification. CCADB EV OIDs, if present, describe EV metadata and do not establish a general required-policy path rule. |

Each is `external_restrictions: unknown`, `certificate_policy_status: unknown`, and `path_validation_status: unverified-by-mbedtls-per-root`. Thus zero roots pass. Deterministic manifest accounting SHA-256 is `9851430f7e0cfde3e224edcd6243d2ce90a8187623bbf9b9f48660cd13aa7e7a` at `assets/certs/candidates/mozilla-nss-2026-11-13.navhttps21-trust-manifest.json`. It records 118 input roots, 116 exact matches, 2 unmatched, 113 TLS trusted, 54 must-verify, 0 eligible, 0 candidate-included, 0 production-approved, and `public_https_enabled: false`. Candidate bundle generation is false; no public candidate PEM was emitted. Fixture roots remain separate. The four cutoff records distinguish `notBefore` checking from full TLS enforcement; each reports SCT enforcement false.

## Qualification evidence

| Qualification | Result |
|---|---|
| NAVHTTPS20 importer baseline | 35/35 pass |
| NAVHTTPS21 overlay/importer tests | 25/25 pass after fail-closed and SCT-enforcement reporting changes (combined importer count 60) |
| NAVHTTPS21 offline Mbed TLS cutoff harness | 12/12 pass for the isolated cutoff helper, missing-SCT fail-closed behavior, and path/hostname/signature/expiry cases. This harness injects a test policy into Mbed TLS; production callback behavior denies the four exact roots. SCT timestamp parsing/signature validation is not implemented or tested. |
| NAVHTTPS18 offline Mbed TLS verifier | 15/15 expected assertions pass, including wrong root, validity dates, host, signature, EKU, noncritical Name Constraints bypass, policy-tree limitation, and empty public candidate handling |
| NAVHTTPS19 Name Constraints corpus | 40/40 pass (16 positive, 24 negative, 22 Name Constraints rejections); original NAVHTTPS18 bypass regression remains included |
| Roughtime hosted and portable | 96/96 pass |
| Ed25519 hosted and freestanding | 238/238 in each mode; 7 independent cryptography reference checks; 3,824-byte reachable verifier stack upper bound, no unmeasured callees |
| Clock assurance | PASS |
| Secure randomness | PASS |
| DNS | 19 scenarios/checks pass: 13 protocol plus 6 controlled fixture |
| UDP/socket bounds | 6 pass: 4 checksum plus 2 datagram bounds |
| IPv4/TCP | IPv4 attribution and TCP cleanup pass; existing compiler warning at `kernel/core/tcp.cpp:636` about an always-false `ephemeral > 65535` comparison |
| HTTPS trust profile and policy overrides | PASS |
| HTTPS qualification contract | 7/7 cases pass |
| Mbed TLS / TF-PSA-Crypto profile | PASS; Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 |
| Hosted Navigator smoke | PASS using local HTTP/HTTPS and controlled fixtures; log `logs/navigator-hosted-smoke-20261117-071507.log` |
| Ordinary AMD64 build | PASS; ELF 2,923,968 bytes (text/data/bss 2,404,969 / 67,552 / 575,822,464), PE 3,038,115 bytes; empty PE import table. PE SHA-256 `f591337713f249b1e3326cb224fecf884f4ec19d3b00aed01913cbc1c67273d9`; ELF SHA-256 `d905fdfd679dc804b2dbb5ebfcbb87a9db173029fc1e434e49124eccc2adbe9e`. |
| Qualification AMD64 build | PASS; ELF 2,950,896 bytes (text/data/bss 2,427,529 / 67,552 / 173,173,376), PE 3,064,141 bytes; empty PE import table; 166 stack-usage and 166 callgraph records; TLS callback frame 224 bytes (combined callback, not incremental cost). PE SHA-256 `63923e503a99d90c9d1c82a8aacccfb3a8fe663ef78bcd8240e3831aa152eb9c`; ELF SHA-256 `90a413ed354d0a389034db93429df0c74eb4e7066feb74af0743318fd4ccb11c`. |
| QAC guest regression | `production_validated` passed one scenario with 4/4 HTTPS assertions, including NAVHTTPS17 controlled-time checks. The next scenario was stopped by QAC exclusivity after another project's QEMU was detected. NAVHTTPS21 D-A guest Test A/B and NAVHTTPS19 guest Test C remain pending; the completed guest binary predates the final fail-closed SCT callback and did not exercise D-A. |

NAVHTTPS18/19 and NAVHTTPS21 are offline and use local test roots. No public-origin DNS/TCP/TLS request was made. NAVHTTPS17's pre-existing guest evidence is documented in [`NAVIGATOR_HTTPS_TLS_TIME_INTEGRATION_NAVHTTPS17.md`](NAVIGATOR_HTTPS_TLS_TIME_INTEGRATION_NAVHTTPS17.md). Guest regressions cover the existing controlled Name Constraints and time paths; they do not exercise NAVHTTPS21's distrust-after callback. Offline proof is not a production approval.

## Limits, readiness, and next scope

| Requirement | NAVHTTPS21 status |
|---|---|
| Exact root identity, NSS TLS purpose, source integrity | **PASS** — per-root identities and purpose values are exact and hash-pinned. |
| Two unmatched comparison roots | **Resolved as source-set discrepancy; excluded** — CCADB says Mozilla Removed; neither exact cert nor same SPKI is in the pinned NSS set. |
| Four server distrust-after records | **Partial: leaf `notBefore` checked; full TLS SCT rule unsupported** — runtime denies every chain through these roots, and all four remain blocked from eligibility. |
| External per-root restriction coverage | **BLOCKED** — Mozilla's Additional Trust Changes page is best-efforts; CCADB does not provide a complete runtime restriction registry. |
| Certificate policy | **BLOCKED when required or unknown** — Mbed TLS does not implement RFC 5280 policy-tree processing. |
| Name Constraints | **Enforcement retained and NAVHTTPS19 corpus passes** — root-applicable policy mapping is still incomplete. |
| Candidate PEM / eligible roots | **0 / 0** — no defensible root candidate. |
| Public HTTPS and production approval | **Disabled / 0** — no activation or release approval. |
| Authenticated production time, cross-boot protection, revocation | **Separate production blockers** — NAVHTTPS17 remains fixture-only; no OneCRL/CRL/OCSP policy was added or selected. |
| Public-origin qualification | **Not run** — no public-origin request was authorized. |

Recommended NAVHTTPS22 scope: implement and verify Mozilla-compatible SCT timestamp validation for TLS distrust-after (including the relevant SCT sources and signatures), identify complete authoritative per-root restriction coverage, and add a test-only controlled guest chain through the production callback. Re-run ordinary and QAC builds and the offline corpus. Keep roots with TLS distrust-after dates excluded until the full rule has a real enforcement path and exact source binding.

## References

- [NSS Root Store guide](https://firefox-source-docs.mozilla.org/security/nss/runbooks/rootstore.html)
- [Mozilla Bug 2018158 — OISTE transition thread with a contextual comment describing TLS `notBefore` and SCT checks](https://bugzilla.mozilla.org/show_bug.cgi?id=2018158)
- [Mozilla Bug 2089115 — emSign Root CA - G1 cutoff and SCT backdating protection](https://bugzilla.mozilla.org/show_bug.cgi?id=2089115)
- [Mozilla Bug 2018979 — eMudhra transition tracking](https://bugzilla.mozilla.org/show_bug.cgi?id=2018979) (context; not the primary semantics source)
- [Mozilla Additional Trust Changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes)
- [Mozilla Root Store Policy 3.1 source](https://github.com/mozilla/pkipolicy/blob/bb613bb30b8d85706bac7ecdd877d19b50efe4e7/rootstore/policy.md)
- [CCADB resources](https://www.ccadb.org/resources) and [data usage terms](https://www.ccadb.org/rootstores/usage)
