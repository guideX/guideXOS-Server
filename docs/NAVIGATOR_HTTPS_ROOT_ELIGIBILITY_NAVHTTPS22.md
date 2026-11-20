# NAVHTTPS22 — Public Root Eligibility Closure and Restricted Trust Candidate

**Outcome B — eligibility advanced, but no defensible candidate is ready.** The NSS cutoff parser defect is fixed, one root now has a fingerprint-keyed evidence record, and the 118-root exclusion accounting is deterministic. DigiCert TLS ECC P384 Root G5 still has unknown Mozilla-specific domain restrictions and unknown program-policy coverage. No root is eligible, no candidate PEM was generated, and public HTTPS remains disabled.

**Repository:** `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
**Branch:** `NAVIGATOR_GENERAL_IMPROVEMENTS`
**Starting HEAD:** `f47d71b636c20a05693c0aa0bc5884ee3df84818`
**Assessment date:** 2026-11-17

## 1. NAVHTTPS21 accepted baseline

NAVHTTPS21 established deterministic NSS/Mozilla/CCADB metadata ingestion, a fail-closed external restriction overlay, removal of the two unmatched Buypass roots, exact-DER distrust-after checks, and rejection of the four SCT-dependent roots. Its tests were 60/60 at acceptance, its distrust-after offline harness passed 12/12, and its ordinary and qualification AMD64 builds passed. The controlled HTTPS guest scenario had previously passed 4/4 assertions; the separate NAVHTTPS21 distrust-after guest scenario was still pending.

NAVHTTPS22 preserves that behavior. It corrects the NSS timestamp parsing defect described in section 2 and adds per-attribute source-coverage validation and an evidence record for one root. The repository still records the physical I219-LM state as `last_completed=PHASE26`, `next_expected=PHASE27`. No CSS, JavaScript, DMA safety, system clock, production time, or revocation behavior was changed.

## 2. Distrust-after reference correction

The NSS snapshot contains four 12-digit `YYMMDDhhmmssZ` server distrust-after values. The importer tried Python's `%Y` parser first; `%Y` accepts under-width input, so it interpreted the years as 2604–2611. NAVHTTPS22 now selects the UTCTime or GeneralizedTime parser by exact input width and rejects malformed lengths. The runtime cutoff constants now agree with the comments in pinned NSS `certdata.txt`:

| Root | Certificate SHA-256 | TLS cutoff UTC |
|---|---|---|
| Izenpe.com | `2530cc8e98321502bad96f9b1fba1b099e2d299e0f4548bb914f363bc0d4531f` | `2026-04-15T23:59:59Z` |
| emSign Root CA - G1 | `40f6af0346a99aa1cd1d555a4e9cce62c7f9634603ee406615833dc8c8d00367` | `2026-11-16T23:59:59Z` |
| DigiCert Trusted Root G4 | `552f7bdcf1a7af9e6ce672017f4f12abf77240c78e761ac203d1d9d20ac89988` | `2026-09-14T23:59:59Z` |
| Autoridad de Certificacion Firmaprofesional CIF A62634068 | `57de0583efd2b26e0361da99da9df4648def7ee8441c3b728afa9bcde0f9b26a` | `2026-08-06T07:01:00Z` |

[Bug 2018158](https://bugzilla.mozilla.org/show_bug.cgi?id=2018158) is an OISTE root transition thread. A Mozilla comment there describes the general TLS distrust-after treatment as checking leaf `notBefore` and rejecting SCT timestamps after the cutoff; that comment is useful context, not a complete specification. [Bug 2089115](https://bugzilla.mozilla.org/show_bug.cgi?id=2089115) is the specific emSign Root CA - G1 change and expressly calls for SCT checking to prevent `notBefore` backdating. The pinned NSS snapshot supplies the exact values. The local Mbed TLS 4.1.0 harness verifies the current callback's behavior and the fail-closed response when SCT checking is unavailable. [Bug 2018979](https://bugzilla.mozilla.org/show_bug.cgi?id=2018979) tracks eMudhra's dual-purpose-root transition and is not the primary source for the semantics.

The correction does not relax policy. The production callback checks the leaf date for each exact restricted root and denies every chain through those roots because the callback cannot verify SCT timestamps. Full TLS distrust-after enforcement remains false for all four roots.

## 3. Full root exclusion-reason accounting

The manifest accounts for all 118 curl comparison roots. The table combines manifest reason counts with direct NSS/CCADB state counts; categories overlap and must not be summed as unique roots.

| Reason or state | Roots | Class | Interpretation |
|---|---:|---|---|
| Mozilla root status `Removed` / `mozilla-root-removed-from-trust-store` | 2 | Hard exclusion | Both are the exact-fingerprint Buypass Class 2 and Class 3 records in CCADB. |
| Exact DER identity mismatch | 0 | Hard exclusion | No same-record DER mismatch was found. Two input certificates have no exact NSS object; they are absent, not mismatched. |
| `certificate-identity-not-verified` | 2 | Missing identity evidence | The two absent NSS objects cannot be trusted by subject or SPKI resemblance. |
| `nss-certificate-record-missing`; `nss-trust-record-missing` | 2 each | Missing NSS records | These are the two unmatched Buypass certificates. |
| NSS server-auth purpose not explicitly trusted (`CKT_NSS_MUST_VERIFY_TRUST`) | 3 | Hard exclusion | QuoVadis Root CA 1 G3, QuoVadis Root CA 3 G3, and Certigna Root CA. The two missing trust records are counted separately. |
| Explicit NSS TLS distrust | 0 | Hard exclusion | No input root has `CKT_NSS_NOT_TRUSTED` in the pinned NSS server-auth record. |
| `server-distrust-after-not-enforced` | 4 | Unsupported functionality | Leaf `notBefore` cutoff handling is present, but full TLS cutoff enforcement remains incomplete. |
| `server-distrust-after-sct-timestamp-not-enforced` | 4 | Unsupported functionality | SCT verification is unavailable, so these paths remain denied even when the leaf date is before the cutoff. |
| `root-specific-external-restrictions-missing` / incomplete | 117 / 1 | Missing source coverage | No root-specific overlay exists for 117 roots; the DigiCert record exists but is incomplete. |
| Domain restriction state unknown | 118 | Missing source coverage | DigiCert is explicitly unknown; the other 117 have no root-specific record. Unknown does not mean unrestricted. |
| Root program-policy state unknown | 118 | Missing policy evidence | The DigiCert record has a specific unresolved coverage reason; the other 117 have no record. |
| Certificate-policy requirement unsupported | 0 | Known technical limitation | No known applicable mandatory certificate-policy requirement was found. Policy status remains unknown for 117 roots and `not-required` only for the scoped DigiCert ordinary non-EV review. |
| `certificate-path-validation-unverified` | 118 | Missing qualification evidence | No candidate-specific path has been qualified through the actual Mbed TLS verifier. |
| `external-policy-snapshot-incomplete` | 118 | Missing source coverage | The available source set is not a complete machine-readable Mozilla restriction registry. |

The manifest now distinguishes a Mozilla `Removed` status from missing NSS identity/trust objects and distinguishes the three matched `CKT_NSS_MUST_VERIFY_TRUST` values from the two absent trust records. The exclusion count for `tls-server-auth-not-explicitly-permitted` is no longer inflated by absent records. Explicit NSS distrust remains zero.

Hard exclusions, unsupported enforcement, and missing evidence are distinct. Removed status and lack of explicit server-auth trust are concrete exclusions. Missing identity/trust records, incomplete policy coverage, and unverified path qualification are missing evidence. Unknown domain or certificate-policy status is not evidence that a restriction exists, and it is not evidence that none exists.

The importer parsed all 118 root certificates for extension facts: none has a root Name Constraints extension or EKU extension; four carry the `anyPolicy` certificatePolicies extension. These DER facts do not establish the absence of Mozilla code-level domain restrictions. There are zero manifest blockers called `unsupported-certificate-policy-requirement`: the detailed DigiCert scope does not require a root certificate-policy OID for ordinary non-EV TLS, while the other 117 roots remain unknown due to missing assessments.

## 4. Shortlist methodology

The inventory contains 118 roots: 116 exact DER matches to NSS and two unmatched, removed Buypass roots. Of the comparison roots, 113 have explicit NSS TLS `CKT_NSS_TRUSTED_DELEGATOR` values, three matched roots have `CKT_NSS_MUST_VERIFY_TRUST`, and two have no NSS identity. Four roots have TLS distrust-after cutoffs and are excluded while SCT enforcement is unavailable.

NAVHTTPS21's three-root starting shortlist was re-screened. Only DigiCert TLS ECC P384 Root G5 advanced to detailed NAVHTTPS22 review. This selection prioritizes exact NSS identity, explicit Websites trust, no cutoff, a single-purpose TLS root, a documented Mozilla inclusion record, and scoped ordinary TLS evaluation.

| Screened root | SHA-256 | NAVHTTPS22 decision |
|---|---|---|
| DigiCert TLS ECC P384 Root G5 | `018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05` | Detailed review. Exact NSS and CCADB identity, `CKT_NSS_TRUSTED_DELEGATOR`, Mozilla Included, no server distrust-after. |
| Telia EC TLS Root CA v3 | `098e08a91dbbf77478b96cceb89b1413a5da37b7c862606a955deb07179f4326` | Not advanced. Mozilla Bug 2075488 reports a CRL signature-profile nonconformance in the Telia EC TLS hierarchy, including a test issuing CA. The report says the issue ended 2026-10-01 and affected zero certificates; the screen-out reflects the lack of a guideXOS revocation engine and the preference for a simpler initial root, not a claim of current noncompliance. |
| GTS Root R4 | `349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d` | Not advanced. The pinned CCADB row lists Server Authentication, Client Authentication, and Secure Email trust bits, adding mixed-purpose review. DigiCert is a more focused TLS-only root for this bounded investigation. |

The two alternatives were screened, not given a full root-specific evidence overlay. They are not declared ineligible on the basis of the limited screen alone; the global source-coverage and path-validation blockers would still apply.

## 5. Source coverage for the detailed root

The one detailed record is keyed by exact DER SHA-256. It has complete source-backed coverage for 9 of 11 tracked attributes. Domain restrictions and broader program-policy compliance remain unknown, so its overall root restriction coverage is incomplete.

| Attribute | Finding for DigiCert TLS ECC P384 Root G5 | Source coverage |
|---|---|---|
| Exact identity | SHA-256 `018e13f0…490c1b05`; exact NSS certificate and trust object match | Complete: pinned NSS, exact CCADB fingerprint, Bug 1764392 |
| Root inclusion | Mozilla Included; current purpose is Websites | Complete: NSS, CCADB, Bug 1764392, and Bug 2031732 transition table |
| TLS trust purpose | NSS `CKT_NSS_TRUSTED_DELEGATOR` | Complete for Mozilla TLS server authentication: NSS and Bug 1764392 |
| Explicit distrust | Not distrusted | Complete for the pinned snapshot: NSS and Bug 2031732 |
| Server distrust-after | None in the pinned NSS TLS trust record | Complete for the pinned snapshot: NSS and Bug 2031732 |
| Email distrust-after | None in the pinned NSS email trust record | Complete for the pinned snapshot: NSS and Bug 2031732 |
| Domain restrictions | Unknown | Incomplete: Additional Trust Changes is best-effort; CCADB and Mozilla policy are not complete fingerprint-keyed restriction registries; Bug 1764392 does not establish absence |
| Root Name Constraints | No extension in the exact DER root certificate | Complete for root-certificate extension presence: exact NSS DER and CCADB; it says nothing about other Mozilla code-level domain rules |
| Certificate policy | Not required for the scoped ordinary non-EV TLS path | Complete for this scope: Mozilla Root Store Policy §§2.1 and 7.1 #3, CCADB EV metadata, Bug 1764397 |
| Issuance evidence | No additional root-specific issuance-proof condition was found for this non-EV scope | Complete only for the reviewed root-specific sources; this is not a claim that guideXOS enforces general CT requirements |
| Program policy | Unknown | Incomplete: current general policy and inclusion evidence do not establish this CA's complete current compliance state |

The CCADB record's aggregate trust bits include Server Authentication and Client Authentication; that aggregate is not used as a Mozilla-only trust decision. The Mozilla TLS trust conclusion comes from the exact NSS record. The root has no Certificate Policies or EKU extension. A separate domain restriction or future policy change is still possible, so neither fact resolves the unknown fields.

The source set is intentionally interpreted by its defined coverage. NSS establishes exact identity, Mozilla membership, trust purpose, and the two distrust-after attributes; the Mozilla Root Store Policy establishes program-wide rules; CCADB supplies exact fingerprint/status/EV cross-checks; Bugzilla supplies selected root-specific history. Mozilla describes Additional Trust Changes as best-efforts explanatory coverage. None of those sources is a complete registry of all per-root custom domain restrictions or a complete per-root compliance database.

## 6. Applicable restrictions and cross-signing

For the scoped DigiCert root, NSS shows Websites trust and no TLS or email distrust-after date. The exact root certificate has no Name Constraints extension. Bug 2031732, whose latest pinned revision is 2026-11-16, lists DigiCert TLS ECC P384 Root G5 as `Websites` / `KEEP`. These sources establish the reviewed facts, not the absence of all possible restrictions. The overlay therefore leaves Mozilla-specific domain restrictions and overall program compliance unknown.

The investigation is keyed to the certificate SHA-256, not subject name or SPKI. A cross-signed certificate with different DER or another anchor does not inherit this root's NSS trust or policy record. NAVHTTPS21's alternate-anchor and cross-signed tests continue to enforce that rule. No DigiCert-rooted production chain was built or validated because the root did not pass the source-coverage gate.

## 7. Certificate-policy capability assessment

The repository pins Mbed TLS 4.1.0. It parses Certificate Policies OIDs, but it does not implement the full RFC 5280 policy tree in this profile: policy mappings, policy constraints, `inhibitAnyPolicy`, and trust-anchor-required policy propagation are not evaluated. Its current extension enforcement support is limited to `anyPolicy`; non-`anyPolicy` values are unsupported, noncritical policy data may be retained without path-policy evaluation, and a critical unsupported extension fails parsing. A basic OID comparison cannot be reported as policy-tree compliance. The NAVHTTPS18 `certificate-policy-tree-not-applied` case preserves this limitation.

DigiCert is assessed only for ordinary TLS server authentication, with EV presentation and EV policy processing excluded. Mozilla Root Store Policy §2.1 applies EV Guidelines to TLS certificates marked EV, and §7.1 #3 asks whether a CA issues EV certificates and, if so, which EV OID applies. Bug 1764397 associates `2.23.140.1.1` with DigiCert G5 EV treatment; it does not make that an ordinary TLS requirement. The pinned CCADB EV OID is therefore not treated as a general path-policy rule. This avoids an unnecessary policy OID check for the scoped profile without claiming RFC 5280 policy-tree support.

If EV or another policy-constrained path is later required, the current verifier is insufficient. No certificate-policy engine was added in NAVHTTPS22. No special root-specific CT issuance-proof rule was identified for DigiCert in the selected sources; general CT enforcement is not claimed by the overlay or by this runtime.

## 8. Candidate eligibility decisions

| Root | Eligibility decision | Decisive blockers |
|---|---|---|
| DigiCert TLS ECC P384 Root G5 | Ineligible; not included | Unknown custom domain restrictions, unknown broader program-policy coverage, incomplete global external-policy snapshot, and path validation not qualified per root |
| Telia EC TLS Root CA v3 | Not advanced to detailed review | Screening preference due CRL/revocation review complexity; no affirmative eligibility decision made |
| GTS Root R4 | Not advanced to detailed review | Mixed-purpose screening preference; no affirmative eligibility decision made |

Across the full inventory, **0 roots are technically eligible**, **0 candidate roots are included**, **0 candidate bundles are generated**, **0 roots are production-approved**, and `public_https_enabled` is `false`. No public root PEM was emitted. Fixture roots remain separate.

## 9. Deterministic manifest hashes

The final manifest was regenerated from the pinned NAVHTTPS21/NAVHTTPS22 source set. NAVHTTPS20's source-lock baseline remains pinned and unchanged.

| Artifact | SHA-256 |
|---|---|
| NAVHTTPS22 trust manifest `assets/certs/candidates/mozilla-nss-2026-11-17.navhttps22-trust-manifest.json` | `b4097df582bb6612ac1e2132f70791bd84e603ed731455d7f18f87f2dccc7b40` |
| Extended source lock | `77f4d7d6ef8d8b4d2577437be3dc51fa15eeba82893b4921481c3c19efb7b483` |
| NAVHTTPS20 source-lock baseline recorded by manifest | `8c18c1a11f1c471a672e48e496fd42690531797cc6a3f5a7e209a85631774095` |
| Root-restrictions overlay | `9676339568a863ee5bc95ab778aedc8646e8ce41040251f323b74d8d40dbb5d9` |
| Distrust-after enforcement policy | `ac05c3786965c910821806d4b0b9c6e0891083384c597dcc9176bccb02279bca` |
| NSS `certdata.txt` | `6d177e33ab54b338edfee24dc3868585833636716a320a725137ab4a15927292` |
| Mozilla Root Store Policy 3.1 | `0e39e12a7251b5011c86cbc83355eb63dd0b82ca33019f5ccafad6cc7ffb59db` |
| Additional Trust Changes snapshot | `4cf63203c5fb8aef744b8c8d148d4bc4cd43c2e37881b20ae03f9c16eed2b60d` |
| CCADB V5 snapshot | `f3b205d590aa5e78d5e79df631b87a331f2e3e1ea990b704c3165fa2627daf36` |

New Bugzilla REST snapshots are pinned in `assets/certs/mozilla/navhttps22-sources/` and bound from the overlay/source lock:

| Snapshot | Revision | SHA-256 |
|---|---|---|
| Bug 1764392 | `2022-07-13T20:54:10Z` | `c13a9ddd1538d1a63c705c58b21c347288fc9eefa080c274a7cf2d627ecfc237` |
| Bug 1764397 | `2022-07-20T17:25:35Z` | `0e5d584e22de1af043b9eee9ad78647f3ff35dbe87bedfe66b81733499d592a6` |
| Bug 2031732 | `2026-11-16T17:05:26Z` | `318d215feb08457afcb4af980224591773040586c3f75488c6098f9c61b662be` |

The manifest records 118 input roots, 116 exact matches, 2 unmatched, 113 TLS trusted-delegator records, 54 NSS must-verify records across the broader NSS snapshot, 0 explicit TLS distrust records, and 0 eligible/candidate/production-approved roots. Repeated import generation is byte-identical and emits no bundle.

## 10. Offline qualification

No root-specific DigiCert chain was proposed, so the selected-root positive-chain and negative-chain matrix was not run against a DigiCert trust anchor. Existing offline verifier corpora cover the relevant primitives with test-only roots; they do not turn those results into candidate-root approval.

| Regression | Result |
|---|---|
| NAVHTTPS20 and NAVHTTPS21 metadata/overlay suites | 65/65 pass after the accounting audit (35 NAVHTTPS20, 30 NAVHTTPS21) |
| NAVHTTPS21 distrust-after verifier | 12/12 pass, including before/equal/after cutoff, missing-SCT fail closed, exact root identity, hostname, expiry, signature, and alternate-anchor behavior |
| NAVHTTPS18 Mbed TLS verifier | 15/15 expected assertions pass: valid fixture chain, wrong root, expired/future dates, wrong hostname, bad signature, Name Constraints, serverAuth EKU, path length, strict source parsing, and documented policy-tree limitation |
| NAVHTTPS19 Name Constraints | 40/40 pass (16 positive, 24 negative; 22 Name Constraints rejections) |
| Trust-profile validation | PASS |
| Mbed TLS dependency profile | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, four patches |
| Roughtime | 96/96 pass |
| Strict Ed25519 | 238/238 hosted and 238/238 freestanding; 117 valid accepted, 64 invalid and 29 acceptable rejected; 7 cryptography reference checks; reachable stack upper bound 3,824 bytes |
| Clock assurance | PASS |
| Secure randomness | PASS |
| DNS | 13 protocol scenarios and 6 controlled loopback fixture checks pass. The fixture timed out in a parallel run and passed on an isolated rerun. |
| UDP/socket bounds | 6/6 pass: 4 checksum and 2 datagram-bound cases |
| IPv4/TCP cleanup | PASS: IPv4 attribution, failed/pending SYN cleanup, socket bounds |
| HTTPS qualification contract | 7/7 cases pass |
| Hosted Navigator smoke | PASS using only local HTTP/HTTPS and controlled fixtures; log `logs/navigator-hosted-smoke-20261117-192628.log` |

All DNS/TCP/HTTPS activity in NAVHTTPS22 tests used loopback fixtures. No public-origin request was made. The hosted smoke used a task-specific fixture directory at `tmp/navhttps22-hosted-fixtures-20261117`; it did not overwrite the existing `navigator-smoke/generated` tree.

## 11. Guest distrust-after status

No QAC controller command or QAC tool is available in this host/tool environment. NAVHTTPS22 did not launch QEMU or attempt guest admission. The NAVHTTPS21 D-A guest Test A/B therefore remains pending. The earlier NAVHTTPS21 controlled HTTPS `production_validated` result remains historical 4/4 evidence; the current source revision was built and hosted-tested, but a current guest run still needs the authorized QAC controller.

## 12. Kernel resources, linkage, and stack

Both builds used fresh isolated directories under `tmp/navhttps22-*`. The ordinary AMD64 build used production defaults; the qualification build included existing HTTPS, Name Constraints, guest-time qualification flags and compiler stack/callgraph reporting.

| Build | ELF size; text/data/bss | PE size | SHA-256 (ELF / PE) |
|---|---|---:|---|
| Ordinary | 2,923,968 bytes; 2,404,969 / 67,552 / 575,822,464 bytes | 3,038,115 bytes | `8ddb8c5ef519f0020bd3496d1cc37c5f61bbe4811fd81a036abe5cd76c5dd61c` / `3b617fa171baf58969534a4f53e1c659630f44d81f249db8bf26e2ca90982d1d` |
| Qualification | 2,950,896 bytes; 2,427,529 / 67,552 / 173,173,376 bytes | 3,064,141 bytes | `125abe5ab69cf7be881ff36a90ffa93325d2b409476d139f8030b7f9f36e2e74` / `1c6a502404119ffaa40784554893cb75c7c031b80485978157cb15a43ec8ad5c` |

The PE import table is empty in both builds, and the ELF relocation table has no records. `nm -u` reports 48 ordinary and 52 qualification undefined symbol-table entries, matching the corresponding NAVHTTPS21 artifacts; these are not new imports or relocation entries. The qualification output contains 166 `.su` stack-usage files and 166 `.ci` callgraph files. The combined TLS verify callback frame is 224 bytes; `mbedtls_ssl_verify_certificate` is also 224 bytes. The callback frame is not an incremental stack-cost estimate. The strict Ed25519 call-chain estimate is 3,824 bytes with no unmeasured callees. Existing compiler warning `kernel/core/tcp.cpp:636` about an always-false `ephemeral > 65535` comparison remains unchanged.

## 13. Production HTTPS readiness

Production readiness remains **not approved**:

- Public HTTPS stays disabled; no roots are production-approved and no candidate PEM exists.
- Root approval, integrity, authenticated-time, and release-owner gates remain mandatory.
- Production authenticated time is not provisioned or activated. Roughtime operators remain fixture-only, with no cross-boot rollback protection.
- No OneCRL/CRL/OCSP revocation policy or source provisioning was added. No implicit revocation soft-fail approval was made.
- No plaintext fallback or system-clock mutation was introduced.
- The runtime continues to deny chains through the four cutoff roots while SCT timestamp verification is absent.
- The I219-LM DMA safety state was not touched.
- The detailed DigiCert record remains blocked by unknown domain restrictions, unknown program-policy coverage, and unverified root-specific path validation.

The Mozilla Root Program is the owner needed to supply authoritative per-root domain-coverage evidence or a complete source that establishes coverage. The guideXOS security/release owner must decide production revocation, trusted-time provisioning and persistence, trust scope, and release authorization.

## 14. Recommended NAVHTTPS23 scope

Keep the current root and public-HTTPS gates in place. Obtain a complete fingerprint-keyed source for DigiCert TLS ECC P384 Root G5's domain restrictions and a current program-compliance assessment, pin and hash the evidence, then update the same overlay and importer. If those sources close all blockers, build an offline chain fixture anchored to the exact root and run the full positive/negative matrix against Mbed TLS 4.1.0 before considering a disabled candidate. Do not infer permission from the current `Websites` trust bit or from a blank source field.

Separately, run the pending D-A guest Test A/B through QAC when the controller is available, preserving the positive controlled HTTPS regression. Continue to deny the four D-A roots unless real SCT signature and timestamp enforcement is implemented. Production public HTTPS remains outside scope until authenticated time with cross-boot protection, a revocation decision and source, and release-owner authorization are completed.

## 15. NAVHTTPS22 audit follow-up

This task specified expected starting HEAD `f47d71b636c20a05693c0aa0bc5884ee3df84818`. Repository preflight instead found HEAD `bf86e24003239797e5cf23cac263e36d759f85d5`, which is already the NAVHTTPS22 implementation commit directly on top of that expected revision. The branch was `NAVIGATOR_GENERAL_IMPROVEMENTS`, two commits ahead of origin, and the tracked worktree was clean. Existing untracked build outputs, caches, logs, fixtures, and qualification artifacts were preserved. No reset, clean, stash, rebase, merge, branch switch, push, or I219-LM operation was performed.

The follow-up corrected the exclusion accounting so CCADB `Removed` status, missing NSS trust records, and matched NSS `CKT_NSS_MUST_VERIFY_TRUST` values are separately counted. It changed the importer, its regression tests, and this report only; runtime validation code and trust defaults did not change. The manifest was regenerated from the same pinned inputs and now hashes to `b4097df582bb6612ac1e2132f70791bd84e603ed731455d7f18f87f2dccc7b40`. It still reports 118 inputs, 0 eligible roots, 0 candidates, 0 production-approved roots, and public HTTPS disabled.

Fresh follow-up checks on 2026-11-19:

- Importer and restriction suites: 65/65 pass.
- Deterministic manifest generation: output hash matches the committed artifact.
- NAVHTTPS21 distrust-after Mbed TLS harness: 12/12 pass.
- NAVHTTPS18 verifier nested in the NAVHTTPS19 run: 15/15 pass.
- NAVHTTPS19 Name Constraints corpus: 40/40 pass (16 positive, 24 negative).
- Trust-profile validation: pass; HTTPS qualification contract: 7/7 pass.
- Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 profile and pinned libsodium 1.0.22 profile: pass.
- Roughtime hosted and portable suites: 96/96 pass.
- Strict Ed25519: 238/238 hosted and 238/238 freestanding; 7 reference checks; 3,824-byte reachable stack upper bound.
- Clock assurance: pass.
- Secure-random contract: pass.
- DNS: 13 protocol scenarios and 6 controlled fixture checks pass.
- UDP/socket bounds: 6/6 pass.
- IPv4/TCP cleanup: pass for attribution, failed/pending SYN cleanup, and socket bounds.
- Hosted Navigator smoke using local HTTP/HTTPS fixtures: pass (`logs/navigator-hosted-smoke-20261119-194113.log`).

The existing NAVHTTPS22 ordinary and qualification AMD64 build artifacts remain the outputs recorded in section 12; this accounting-only follow-up does not change kernel code. No QAC controller was available, so the guest distrust-after scenario remains pending. I219-LM remains `last_completed=PHASE26`, `next_expected=PHASE27`.

## References

- [NSS `certdata.txt`, pinned commit](https://github.com/nss-dev/nss/blob/8c60e757c30b1b239bae46532b4f6d4ebb1e1cad/lib/ckfw/builtins/certdata.txt)
- [Mozilla Root Store Policy 3.1, pinned commit](https://github.com/mozilla/pkipolicy/blob/bb613bb30b8d85706bac7ecdd877d19b50efe4e7/rootstore/policy.md)
- [Mozilla Additional Trust Changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes)
- [CCADB V5 certificate records](https://ccadb.my.salesforce-sites.com/ccadb/AllCertificateRecordsCSVFormatV5)
- [Bug 1764392 — DigiCert G5 NSS inclusion and trust flags](https://bugzilla.mozilla.org/show_bug.cgi?id=1764392)
- [Bug 1764397 — DigiCert G5 EV treatment and OID](https://bugzilla.mozilla.org/show_bug.cgi?id=1764397)
- [Bug 2031732 — DigiCert dual-purpose root transition plan](https://bugzilla.mozilla.org/show_bug.cgi?id=2031732)
- [Bug 2075488 — Telia EC CRL signature-profile incident](https://bugzilla.mozilla.org/show_bug.cgi?id=2075488)
- [Bug 2018158 — OISTE transition comment on TLS distrust-after checks](https://bugzilla.mozilla.org/show_bug.cgi?id=2018158)
- [Bug 2089115 — emSign TLS cutoff and SCT backdating protection](https://bugzilla.mozilla.org/show_bug.cgi?id=2089115)
- [Bug 2018979 — eMudhra transition tracker](https://bugzilla.mozilla.org/show_bug.cgi?id=2018979)
