# NAVHTTPS18 — Public CA Trust Compatibility and Pilot Preparation

**Outcome C — an evidenced verifier and source-policy blocker prevents a defensible public CA candidate.** The existing public HTTPS policy remains disabled. No public-origin request, pilot, or trust promotion was performed.

## 1. Repository and accepted NAVHTTPS17 baseline

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `18c4cfa66690ee82ea53e75d2b6dfd7671415147` (matched)
- The tracked working tree was clean at start. Pre-existing untracked build and test artifacts were retained.
- I219-LM remains `last_completed=PHASE26`, `next_expected=PHASE27`. No I219-LM source, DMA behavior, physical NIC setting, or phase record changed.

The accepted [NAVHTTPS17 report](NAVIGATOR_HTTPS_TLS_TIME_INTEGRATION_NAVHTTPS17.md) remains closed: 96/96 Roughtime and portable time-estimate checks passed; three signed fixture responses from three simulated operators produced fixture-only agreement and a monotonic UTC estimate; the controlled TLS time lease was consumed by the real Navigator verifier; the controlled HTTPS request returned HTTP 200; and post-handshake invalidation passed. NAVHTTPS7's missing-profile and critical Name Constraints QAC cases also passed. This is fixture evidence, not production time authority.

## 2. Public root-store and verifier inventory

The review input is `assets/certs/candidates/mozilla-cacert-2026-11-05.pem`, a curl CA Extract dated 2026-11-05 and derived from Mozilla's public root store. It contains 118 certificates in 200,116 bytes. Its SHA-256 is `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`. The X.509 bytes expose certificate identity and fields such as subject, issuer, public key, validity, basic constraints, key usage, EKU, and encoded extensions. The accounting record fingerprints each input certificate by DER SHA-256.

The PEM does not carry a complete browser root-program policy. This checkout has no matching machine-readable snapshot of Mozilla's server-authentication trust bits, domain/name restrictions, explicit distrust and distrust-after records, or policy exceptions. curl documents that browser constraints are not all represented in its PEM extract; Mozilla also publishes trust changes outside the certificate bytes ([curl CA Extract](https://curl.se/docs/caextract.html), [Mozilla Included Certificates](https://wiki.mozilla.org/CA/Included_Certificates), [Mozilla Additional Trust Changes](https://wiki.mozilla.org/CA/Additional_Trust_Changes)). A matching, authenticated source-policy dataset was not available, so no root can be selected as an eligible pilot anchor from this PEM alone.

The bare-metal verifier is Mbed TLS 4.1.0 at commit `0fe989b6b514192783c469039edd325fd0989806`, with TF-PSA-Crypto 1.1.0 at `29160dd877d29658279fd683b2ae57b320ddcf09`. The active guideXOS configuration is `guidexos/mbedtls_config.h` plus `guidexos/crypto_config.h`; the pinned profile script confirms three repository patches. Navigator uses Mbed TLS chain, hostname, date, key-usage, and server-auth EKU checks. Trust profile and policy schema versions remain 1. There is no runtime release-signature verifier or independently provisioned release trust key.

The 2026-11-05 input's existing manifest remains `production_ready=no` and review-only. SHA-256 binds a record to bytes but does not authenticate source provenance, Mozilla policy, or release approval.

## 3. Actual verifier compatibility matrix

The offline harness links the pinned Mbed TLS source with the active guideXOS configuration and calls `mbedtls_x509_crt_verify_with_profile`. Its 15 cases exercised real X.509 parsing and verification. TLS server-auth EKU is also checked by the library's client-side TLS certificate-usage path.

| Requirement | Classification | Observed behavior |
|---|---|---|
| Chain signatures and trust-anchor path | **SUPPORTED** | Valid chain accepted; unknown anchor and corrupted intermediate rejected. |
| Certificate validity dates | **SUPPORTED** | Expired and future-dated leaves rejected with date flags. |
| Requested hostname | **SUPPORTED** | Wrong hostname rejected. |
| Basic constraints and path length | **SUPPORTED** | A path-length-zero violation was rejected. |
| Leaf server-auth EKU | **SUPPORTED** | The harness calls Mbed TLS's EKU check used by the TLS client; the client-auth-only leaf was rejected. |
| TLS client key usage | **SUPPORTED** | The pinned TLS client path checks the usage required by its negotiated TLS mode. NAVHTTPS18 did not add a separate negative key-usage fixture. |
| Unknown critical extension | **REJECTED** | Parse fails closed. |
| Critical Name Constraints | **REJECTED** | Parse fails closed. This preserves the accepted NAVHTTPS7 behavior. |
| Noncritical Name Constraints | **MISSING** | Mbed TLS accepted a deliberately out-of-scope DNS name under a CA with noncritical Name Constraints. Do not accept such a chain while this behavior remains. The Mbed TLS project tracks the missing enforcement in [issue #8759](https://github.com/Mbed-TLS/mbedtls/issues/8759). |
| Certificate policy tree / selected policy | **MISSING** | A certificate carrying a policy OID parsed and its chain verified; this profile did not require or enforce a selected policy path. |
| Mozilla root purpose, domain restrictions, explicit distrust, distrust-after rules | **MISSING** | These source-policy records are not in the candidate PEM, and no matching metadata snapshot is staged. |
| Intermediate CA restrictions | **PARTIAL** | Basic constraints and path length are enforced. Name Constraints are not enforced when noncritical. |
| CRL, OCSP, stapled status, local deny overlay | **MISSING** | No revocation status is consumed by this Navigator path. |
| Partial parse of a multi-root input | **REJECTED by guideXOS policy** | Mbed TLS reported 4 parse failures for the 118-certificate source bundle. The guideXOS loader is strict per certificate and rejects a partial bundle; it does not use a partially parsed store. |
| Production-profile approval | **REJECTED / unavailable** | Version and integrity checks do not make an approval self-authenticating. No runtime release-signature verifier or release key is configured. |

Mbed TLS's PEM parse result of 4 is a partial-parse indication, not the total reason for excluding the source set. The policy accounting excludes all 118 roots because source trust restrictions are unavailable and relevant verifier features are missing. The tested ISRG Root X1 is only an offline fixture anchor; its test success does not qualify it for production inclusion.

## 4. Restricted candidate construction and provenance

The deterministic generator was rerun against the pinned input with its exact SHA-256, source identity/date, PowerShell/.NET X.509 toolchain, and smoke-root contamination check. The regenerated JSON is semantically identical to `assets/certs/candidates/mozilla-cacert-2026-11-05.accounting.json`:

| Input certificates | Included | Excluded | Result |
|---:|---:|---:|---|
| 118 | 0 | 118 | `rejected-disabled-no-public-roots-included` |

No restricted public trust PEM was produced. The zero-root accounting object is intentionally not a usable trust bundle. No fixture root entered the source bundle. The accounting fingerprint list provides an exact identity for every excluded certificate.

The blockers are concrete: the candidate lacks source-policy data needed to determine eligible roots and external restrictions, the verifier ignores noncritical Name Constraints, and it does not evaluate a selected certificate-policy tree. Excluding only a root with an obvious certificate-level issue cannot reconstruct Mozilla's omitted purpose, domain, or distrust rules for all possible paths. Importing all roots or treating missing restrictions as unrestricted trust would weaken verification, so no subset was approved or staged.

## 5. Pinned offline public-chain qualification

The positive chain is an archived Let's Encrypt ACME response transcript from a third-party [Gist revision `f79ee0716f2b2d7b7a1e0c106dda6e5999f97c7e`](https://gist.github.com/dextercd/8263747801f74ae065684ac11ae4ef86/f79ee0716f2b2d7b7a1e0c106dda6e5999f97c7e), not an official fixture release or a live origin response. Transcript SHA-256: `eec1e665b56d34b05b7fb047751cd55be232d611073329ba79ea2b321786325f`; chain PEM SHA-256: `1011ab03b96fdba27f4d3aa145b9d31af7273903c4ee2416226cab259272cf7c`. It contains a `test.dextercd.com` leaf valid 2026-05-29 through 2026-08-27, a YR2 intermediate, and a cross-signed intermediate. The ISRG Root X1 test anchor was extracted from the pinned Mozilla input; anchor PEM SHA-256: `22b557a27055b33606b6559f37703928d3e4ad79f110b407d04986e1843543d1`; DER SHA-256: `96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6`. The test clock was fixed at 2026-06-15 UTC.

All 15 Mbed TLS harness assertions passed. A passing assertion on the two missing-functionality cases records the observed behavior; it is not a security pass:

| Case | Verifier result / meaning |
|---|---|
| Valid pinned chain at fixture time | Accepted under the explicit ISRG Root X1 test anchor. |
| Unknown trust root | Rejected. |
| Expired leaf (2026-09-01 fixture time) | Rejected. |
| Future-dated leaf (2026-05-28 fixture time) | Rejected. |
| Wrong hostname | Rejected. |
| Tampered intermediate signature | Rejected. |
| Noncritical Name Constraints violation | **Accepted by Mbed TLS; required enforcement is missing.** |
| Unknown critical extension | Rejected during parsing. |
| Critical Name Constraints | Rejected during parsing. |
| Server-auth EKU | Accepted by the server-auth check. |
| Client-auth-only leaf | Rejected by the server-auth check. |
| Path length zero violation | Rejected. |
| Empty candidate trust store | Rejected. |
| Full Mozilla source bundle | Partial parse result 4; strict policy requires rejection. |
| Certificate-policy chain | Chain accepted without selected policy-tree enforcement. |

Private negative-test keys and chains are generated only in a temporary directory. The host harness supplies a deterministic test-only PSA random callback so PSA can initialize; it performs no TLS handshake or key generation and makes no production-randomness claim. The separate secure-random contract smoke passed. These offline checks do not prove live public HTTPS compatibility, Mozilla policy equivalence, or pilot readiness.

The existing trust-profile, qualification-contract, and clock-assurance suites separately cover missing/unreviewed profile evidence, unsupported versions, bundle integrity mismatch, insufficient public TLS time assurance, disabled prerequisites, and no plaintext downgrade. They do not turn the offline public-chain test into a live guest request.

## 6. Authorization and release-policy boundary

`scripts/navigator_https_trust_profile.ps1` continues to reject environment policy overrides as authorization, enforces schema/profile/policy version 1 and integrity metadata, and returns `Authorized=false` when approval is only a self-asserted label. `production_ready=yes` is not proof of review. The trust-profile regression passed its checks for override rejection, versioning, integrity binding, unsupported version, missing evidence, and non-self-authorizing approval. The seven-case HTTPS qualification contract passed, including disabled prerequisites and failed required-case accounting. No release key or signature was invented or embedded.

Before any public pilot approval, a release owner would need a reviewable package containing:

1. Authenticated source provenance and a matching trust-metadata snapshot, with every proposed root identified by DER fingerprint and its server-auth purpose, domain restrictions, distrust rules, and exceptions recorded.
2. A deterministic candidate transformation, input/output hashes, parser counts, policy-version compatibility, and negative results proving every applicable restriction is enforced or the root/path is excluded.
3. A versioned trust profile and a signature verified at runtime against a release key provisioned outside this repository, with key rotation, revocation, and rollback handling.
4. A separately provisioned production time-source policy and keys, reviewed quorum, freshness and uncertainty bounds, source independence, and cross-boot rollback behavior.
5. An explicit revocation decision and the residual risk accepted for a bounded preview.
6. One hostname, allowed request methods, response/resource caps, pilot window and expiry, evidence capture, cleanup, and a rollback plan.
7. A release-owner signature/authorization over the exact source, profile, build, pilot scope, and expiry. No private authorization key belongs in this repository.

## 7. Authenticated-time production prerequisites

NAVHTTPS17's time-consumer interface is reused; NAVHTTPS18 adds no Roughtime verifier or agreement engine. The accepted guest run proves three signed responses from simulated fixture operators, a fixture-only estimate, within-boot monotonic advancement, and a controlled TLS lease. It does not prove three real independent operators or production key custody.

Production requires release-provisioned operator identities and authenticated server keys, verified endpoint-to-operator binding, independently operated sources, a reviewed quorum/diversity policy, signed key rotation/revocation, accepted UTC interval limits, maximum uncertainty and age, and a protected cross-boot anti-rollback floor. The current fixture policy and keys cannot authorize public origins. The CMOS/OS clock remains unchanged and does not become production-authenticated time.

## 8. Revocation decision record

**Current capability:** no CRL, OCSP, stapled OCSP response, or authenticated local leaf-denial overlay is enforced by this Navigator path. CA distrust in source policy and revocation of an individual leaf are distinct decisions; excluding a root does not establish that a leaf is unrevoked.

**Preview options for the release owner:**

- **Hard fail:** require fresh, authenticated status for the supported certificate path and reject on unavailable, stale, malformed, or revoked status. This can prevent a preview when status infrastructure is unreachable.
- **Bounded soft fail:** allow only an explicitly documented short preview when status is unavailable, while rejecting any authenticated revoked result and consuming a signed local deny overlay. The owner must accept the residual risk that an unreported/recent revocation can be used during the outage window.

Neither mode is implemented or selected here. There is no automatic soft-fail default. A future decision must specify supported mechanisms, freshness, maximum outage window, issuer/path coverage, behavior for unknown status, and the owner accepting the risk.

## 9. Hosted regression results

| Suite | Result |
|---|---|
| Candidate accounting reproduction | PASS; semantic match; 118 input, 0 included, 118 excluded. |
| Mbed TLS public-chain compatibility | PASS; 15/15 assertions, including explicit observations of missing noncritical Name Constraints and policy-tree enforcement. |
| Mbed TLS profile | PASS; 4.1.0, TF-PSA-Crypto 1.1.0, 3 repository patches. |
| Trust profile | PASS; override, version, integrity, missing/unsupported approval evidence. |
| HTTPS qualification contract | PASS; 7 cases. |
| Roughtime/time-estimate | PASS; 96/96 tests. |
| Clock assurance | PASS; public time-assurance and date-boundary checks included. |
| Secure randomness | PASS. |
| DNS | PASS; 13 protocol scenarios + 6 controlled-fixture checks. |
| UDP wire/socket bounds | PASS; 6 cases. |
| TCP connect/cleanup | PASS; IPv4 attribution and connection cleanup cases. Compiler emitted the existing `ephemeral > 65535` always-false warning at `kernel/core/tcp.cpp:636`. |
| Hosted Navigator smoke | PASS; 288/288 checks, `NAVIGATOR_SMOKE_RESULT: PASS`. Local HTTP/HTTPS fixtures only. |

The first concurrent DNS fixture run hit a Windows file-in-use cleanup error. Its following rerun found the fixture port still owned by that test's Python child process; the exact child was stopped through its fixture stop file, the port was confirmed free, and a serialized rerun passed all 19 DNS checks. The hosted Navigator smoke modified only `desktop.json`'s recent-items entry; that test side effect was inspected and restored. The historic intermittent `CSS inline styles load` and `CSS inline runtime counters` failures remain documented in NAVHTTPS9–11; this run passed all smoke checks and does not resolve their cause. No CSS/compositor work was done.

## 10. Kernel and guest qualification

NAVHTTPS18 changes no production kernel, TLS, or trust-policy code. The host compatibility harness compiles the pinned Mbed TLS implementation using the guideXOS configuration, but it is not a freestanding kernel build. No new kernel image, binary-size, stack, import, or guest runtime claim is made. A fresh AMD64 kernel build and QAC run were not needed for these test/documentation-only changes. No QEMU session was started. NAVHTTPS17 guest acceptance remains the qualified baseline; no public-origin DNS/TCP/TLS request was made.

## 11. Public HTTPS pilot readiness

| Gate | Status | Evidence / remaining requirement |
|---|---|---|
| Public CA source provenance | **PARTIAL** | Dated curl/Mozilla input and matching byte hash are pinned; authenticated, matching source trust metadata is absent. |
| Trust purpose and restriction enforcement | **BLOCKED** | TLS server-auth EKU, date, host, and path checks work; noncritical Name Constraints, selected policy tree, and source-level Mozilla restrictions/distrust are not enforced or unavailable. |
| Authenticated policy approval | **BLOCKED** | Profile checks are strict, but runtime signature verification and a release trust key are absent. |
| Real-world time-source provisioning | **BLOCKED** | No production operator key/identity provisioning or source authentication is staged. |
| Source-independence policy | **PARTIAL** | Three simulated fixture operators were exercised; real operator independence is unverified. |
| Time freshness and uncertainty | **PARTIAL** | Bounded freshness/uncertainty and interval containment are exercised for fixture scope; no production-approved thresholds/evidence exist. |
| Cross-boot limitations | **BLOCKED** | No protected cross-boot rollback floor. |
| Revocation-policy decision | **OWNER DECISION REQUIRED** | No status mechanism is enforced; select hard-fail or explicitly bounded soft-fail and accept its risk. |
| Public-origin DNS/TCP/TLS readiness | **BLOCKED** | No public origin was contacted; the existing results use local/controlled fixtures. |
| Certificate and hostname verification | **PARTIAL** | The pinned offline chain and hostname/date negatives exercise actual Mbed TLS; public root-policy compatibility is not qualified. |
| Response size and resource bounds | **PARTIAL** | Existing caps remain: 32 KiB headers, 256 KiB encoded body, 5 redirects, and 5-second connect/read timeouts. No selected public origin has exercised them. |
| Release-owner pilot authorization | **OWNER DECISION REQUIRED** | Exact source/profile/build/hostname/method/window/expiry and rollback must be signed by an authorized release owner. |

## 12. Recommended NAVHTTPS19 scope

Start with one pinned, machine-readable Mozilla trust-metadata snapshot matching a root-bundle revision. Derive a fingerprint-based allowlist only for roots with explicit server-auth trust and understood purpose/domain/distrust restrictions. In parallel, add a narrowly scoped fail-closed verifier check for any noncritical Name Constraints in the verified path (or implement and test full enforcement); do not use a root whose path relies on ignored constraints. Re-run this offline matrix against the proposed subset. Keep the candidate disabled and do not enter QAC or public-origin testing until the source-policy and verifier gates pass and a release owner provisions authenticated approval and production time policy.

This is the shortest useful next experiment because the current Mbed TLS test gives a reproducible counterexample: a name outside a CA's noncritical permitted subtree is accepted. Resolving that behavior and acquiring matching trust metadata are prerequisites to selecting even a small, reviewable root set.
