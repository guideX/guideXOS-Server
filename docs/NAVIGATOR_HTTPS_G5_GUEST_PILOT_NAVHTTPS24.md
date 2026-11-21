# Navigator DigiCert G5 Guest Pilot Readiness — NAVHTTPS24

Date: 2026-11-21; repository branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`; starting HEAD: `96901fe93547998203778f4d607ed9cabd1150fa`.

## Outcome

**Outcome B — hosted/build qualified; guest qualification remains pending.** The disabled one-root candidate packages with its exact policy metadata, and the pinned Mbed TLS verifier accepts the captured G5 path while rejecting the tested invalid paths. The qualification AMD64 image links the guest verifier. QAC guest tests were not started because PID 24672 was running another project's QEMU guest (`guideXOS-TASKBAR6R7-A`). The exclusive-admission script rejects an existing QEMU owner, so no guest or fixture process was started for NAVHTTPS24.

No public HTTPS connection was made from a guideXOS guest. The candidate is **not production-approved**. Production public HTTPS, general public browsing, and release-owner authorization remain disabled/pending. No production root was added; the other 117 public roots remain ineligible. No I219-LM or bare-metal ABI change was made; physical I219 status remains `last_completed=PHASE26`, `next_expected=PHASE27`.

## 1. NAVHTTPS23 accepted baseline

NAVHTTPS23 selected exactly one technically eligible public root, completed authoritative source coverage for that selected root, and passed 11/11 candidate-specific Mbed TLS verification cases plus 76/76 importer/restriction/candidate Python tests. It retained ordinary TLS certificate policy compatibility while excluding EV claims and full RFC 5280 policy-tree processing. Zero public roots are production-approved.

The accepted identity is **DigiCert TLS ECC P384 Root G5**, exact certificate DER SHA-256:

`018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05`

Baseline pins retained:

| Artifact | SHA-256 |
| --- | --- |
| NAVHTTPS23 candidate bundle PEM | `05161ad2ac04a0df956ef803e127aa877cc5131e0a727ed8e5de43f02e8868c4` |
| NAVHTTPS23 selected-root trust manifest | `8fb3889404f5c632f67f51ef517ff025d8e49b2e9f0625ccd0fe2fa160be6790` |
| NAVHTTPS23 candidate sidecar | `e54283023e73f1b7fd02d3b3f81be2ecf4d3810322e315b4a66b43c2794660e1` |
| NAVHTTPS23 source lock | `31add38b4446f26b3a533cb45645ee40d725ab97af1d9d856e9c7ef4406f9c87` |
| NAVHTTPS23 restriction overlay | `01046c433ab7b70ca8fd518c6f7d544b631321479c3e1c3b3b338ca6dfebf08d` |

The Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 pins are unchanged. NAVHTTPS18 offline verification, NAVHTTPS19 Name Constraints, NAVHTTPS21 distrust-after policy, Roughtime, and strict Ed25519 baselines remain in force.

## 2. Exact candidate packaging and policy

The qualification-only AMD64 image stages the candidate in the existing certificate layout:

| Guest path | Purpose |
| --- | --- |
| `/certs/ca-bundle.pem` | One-root candidate bundle; exact NAVHTTPS23 bytes |
| `/certs/ca-bundle.manifest` | Existing CA-bundle manifest format, schema `guidexos.navigator.ca-bundle-manifest.v0.1` |
| `/certs/trust-profile.json` | Existing trust-profile format, schema `guidexos.navigator.trust-profile.v1` |
| `/certs/navhttps24-candidate-manifest.json` | NAVHTTPS24 qualification manifest, schema `guidexos.navhttps24.g5-disabled-candidate-qualification.v1` |
| `/certs/navhttps24-*.pem` | Hash-pinned public-chain and explicitly synthetic negative-case fixtures |

The bundle is checked by the existing CA manifest loader and then checked again by the qualification routine: exactly one parsed certificate, exactly one root in the bundle manifest, exact bundle SHA-256, exact root DER SHA-256, expected rotation ID and bundle type, and exact qualification-manifest SHA-256. The manifest generator verifies schema and artifact pins. Its byte hash is also pinned in the packer and in the qualification-only kernel code, so changed or missing metadata fails closed.

The guest trust profile is `profile_class=candidate`, profile ID `navigator-candidate-navhttps24-digicert-g5`, approval state `pending`, and authorization evidence `none`. It carries an exact set of seven supported restrictions and five explicitly unsupported restrictions; the required verifier capability set includes strict PEM accounting, SHA-256 integrity, hostname verification, serverAuth EKU, authenticated-time readiness, and signed-approval verification. The candidate manifest records `candidate_included=true`, while `candidate_enabled=false`, `production_approved=false`, `public_https_enabled=false`, and `general_public_browsing=false`.

The production activation policy remains `PublicPilotCandidate` selected and `Disabled` effective. The candidate scenario refuses `-CandidateReviewed`, public-origin probe flags, or a different bundle hash/rotation. Its verifier is compiled only under `GXOS_NAVIGATOR_NAVHTTPS24_G5_CANDIDATE_QUALIFICATION_ACTIVE`; the ordinary AMD64 image contains no qualification function symbol. No user-facing CA-import path was added.

The first packaging attempt showed that the FAT image writer only supports existing root-level `/certs` entries. The qualification fixtures now use unique `navhttps24-` filenames in that existing directory. Staging then succeeded, with one candidate root and the qualification manifest hash present. No second certificate-store framework or FAT ABI was introduced.

## 3. Public-chain provenance and analysis

Discovery used the [DigiCert G5 chain demo](https://digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com/) and DigiCert's [published root and intermediate certificate page](https://knowledge.digicert.com/general-information/digicert-trusted-root-authority-certificates). DigiCert lists the same G5 root SHA-256 fingerprint and identifies active, expired, and revoked demo endpoints. The public endpoint's leaf and chain can change; the captures below are evidence for this run, not an evergreen server pin.

On 2026-11-21, a development-host Python 3.12/OpenSSL 3.0.16 TLS 1.3 connection to `digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com:443` captured the peer certificates using SNI. Peer address was `216.168.244.44`; negotiated cipher was `TLS_AES_256_GCM_SHA384`. Certificate validation was disabled only for byte capture; that capture is not itself a successful trust verification and is not guest acceptance. No guideXOS guest connected to this public origin.

The peer supplied five certificates in one handshake:

| Index | Identity | SHA-256 | Notes |
| --- | --- | --- | --- |
| 0 | Demo leaf | `812cad93a5ac78f8f546777a13cb531bf692739f6bffe59734d20002d6a10fd7` | SAN exactly the demo hostname; valid 2026-11-14 through 2026-12-13; RSA 2048; ECDSA/SHA-384 signature; serverAuth and clientAuth EKUs |
| 1 | DigiCert G5 TLS ECC SHA384 2021 CA1 | `97a073957c5812725757672f222690655a1407d71011edfc8b2fadc5128911da` | P-384, ECDSA/SHA-384; CA path length 0 |
| 2 | Self-signed DigiCert TLS ECC P384 Root G5 | `018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05` | Exact NAVHTTPS23 trust anchor |
| 3 | Cross-signed G5 certificate | `3142f99d358d9b500e5cdd2dff8f719a8ccecf589652334848522cca807d0807` | Same subject as index 2, different DER identity; issued by Global Root G3 |
| 4 | DigiCert Global Root G3 | `31ad6648f8104138c738f39ea4320133393e3a18cc02296ef97c2ac9ef6731d0` | Alternate path terminus, not in the candidate bundle |

The exact candidate path is leaf → CA1 intermediate → self-signed G5. The alternate path is leaf → CA1 → cross-signed G5 certificate → Global Root G3. Subject and public-key similarity do not collapse these DER identities. The verifier reports its trust anchor, and an exact G5-only candidate bundle cannot silently add Global Root G3.

The leaf contains certificatePolicies OIDs `2.16.840.1.114412.2.1` and `2.23.140.1.1`; the intermediate lists additional policy OIDs. This is not an EV decision: NAVHTTPS23 records no required ordinary policy OID, no EV support/claim, and full policy-tree processing unsupported and disabled. There are no Name Constraints extensions in the captured leaf, intermediate, or root. No G5-specific distrust-after cutoff was identified in the selected-root evidence; the existing NAVHTTPS21 distrust-after enforcement is retained.

The leaf advertises OCSP and CRL distribution URLs. Those extensions are pointers, not evidence that guideXOS fetched or enforced revocation status. The current guideXOS chain verifier passes no CRL list and does not perform OCSP or stapling checks.

The peer-returned chain and locally constructed path files are distinguished in `capture-manifest.json`. Exact file hashes are recorded below and in that manifest. A separate authentic DigiCert expired-demo chain supplies the expired-leaf case. Signature-corrupted leaf and intermediate fixtures are derived by changing a signature octet and are explicitly synthetic invalid-signature controls, not DigiCert-issued certificates.

## 4. Hosted candidate qualification

The actual pinned Mbed TLS 4.1.0 verifier ran against the captured public chain and hash-pinned controls. Result: **10/10**.

| Case | Result |
| --- | --- |
| Exact G5 path, correct hostname | Accepted; anchor fingerprint exactly G5 |
| Wrong hostname | Rejected with `MBEDTLS_X509_BADCERT_CN_MISMATCH` |
| Expired DigiCert demo leaf | Rejected with expired flag |
| Active public leaf evaluated before `notBefore` | Rejected with future flag |
| Missing issuing intermediate | Rejected as untrusted |
| Modified leaf signature (synthetic) | Rejected |
| Invalid intermediate signature (synthetic) | Rejected |
| Exact G5 path with Global Root G3 as incorrect anchor | Rejected as untrusted |
| Alternate cross-signed path with exact G5 anchor | Accepted; reported anchor remains exact G5 |
| Alternate path with separately supplied Global Root G3 anchor | Accepted only in that explicit alternate-anchor test |

This verifies certificate chains offline. It does not claim a guideXOS guest connection to the site. The real capture chain, exact root, and alternate root are separately pinned.

The broader case list in the request is covered across the dedicated lanes as follows:

| Requested area | Evidence and result |
| --- | --- |
| Correct chain, hostname, wrong hostname, expired/future validity, missing/invalid intermediate, altered signature, alternative path, wrong anchor | NAVHTTPS24 Mbed TLS verifier 10/10 |
| Unsupported policy/tree behavior | NAVHTTPS18's Mbed TLS case records policy-tree processing as not applied. NAVHTTPS23 metadata and NAVHTTPS24 validation require ordinary policy `not-required`, EV false, and full RFC 5280 policy tree `unsupported-and-disabled`; this does not claim policy-tree rejection or EV validation. |
| Name Constraints violation | The public G5 chain has no applicable Name Constraints. NAVHTTPS19's separate pinned verifier passed 40/40, including 22 Name Constraints rejections. Candidate-specific guest NC violation remains unrun. |
| Missing/modified candidate metadata, root count, extra root, scope/time drift, approval, unsupported policy enablement | NAVHTTPS24 manifest tests and exact-hash packer checks reject tampering; 82/82 NAVHTTPS20/21/23/24 Python tests passed. |
| Plaintext downgrade | Existing HTTPS qualification contract passed 7/7 cases. The NAVHTTPS24 candidate code makes no socket call and reports no plaintext fallback. |

The candidate manifest validator checks the exact source manifest, candidate sidecar, source lock, restriction overlay, capture manifest, certificate DER, and chain fixture hashes. Tampering with policy scope, EV/policy-tree status, Name Constraints metadata, time scope, approval state, root count, root fingerprint, or included-root count raises a validation error.

## 5. Regression qualification

| Suite | Result |
| --- | --- |
| NAVHTTPS24 pinned Mbed TLS cases | 10/10 |
| NAVHTTPS20–22 metadata/restriction and NAVHTTPS23/24 candidate Python tests | 82/82 |
| NAVHTTPS23 Mbed TLS candidate baseline | 11/11 |
| NAVHTTPS19 Name Constraints | 40/40; 16 positive, 24 negative, 22 Name Constraints rejections |
| NAVHTTPS18 offline verifier | 15/15 |
| NAVHTTPS21 distrust-after validation | 12/12 |
| HTTPS qualification contract | 7/7 |
| Trust-profile enforcement | PASS |
| Mbed TLS profile and dependency pins | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 4 existing patches |
| Roughtime v1 baseline | 96/96 |
| Strict Ed25519 | 238/238 hosted and 238/238 freestanding; 0 failures; pinned libsodium 1.0.22 |
| Clock assurance | PASS, including rollback, freshness, validity-date and insufficient-public-assurance checks |
| Secure-random contract | PASS |
| DNS | 13 protocol scenarios + 6 controlled fixture checks PASS |
| UDP/socket bounds | 6 cases PASS |
| TCP cleanup/IPv4 attribution | failed-SYN, pending-SYN close, bounds and send-attribution checks PASS |
| Hosted Navigator smoke | FAIL on the preexisting untracked `guideXOSServer.exe` (built 2026-11-14): two CSS-inline checks failed. The same log's remaining Navigator checks completed; a preserved 2026-11-19 log records PASS. This run did not rebuild or overwrite that untracked executable. No CSS/JavaScript work was included. |

The NAVHTTPS17 authenticated fixture-time HTTPS behavior (HTTP 200 and invalidation checks) was not run in a guest because QAC was occupied. The optional NAVHTTPS21 distrust-after guest scenario was also left pending and is reported independently.

## 6. Guest verification, time scope, and activation gates

Guest verification code is present in the qualification image and checks the disabled candidate bundle, bundle-manifest state, root count, exact root DER fingerprint, candidate rotation ID, trust-profile class and approval fields, exact restriction arrays, and qualification-manifest SHA-256. It then calls `mbedtls_x509_crt_verify_with_profile` on pinned files and prints the trust-anchor fingerprint, verification flags, and explicit test-time scope.

The guest routine changes only Mbed TLS's platform time callback during each offline certificate test, using the manifest's fixed `1795089600` Unix time or the explicit pre-validity time for the future case. It restores `gxos_mbedtls_time_callback` after verification. It does not change CMOS or the system clock, and the interval is not Roughtime evidence or production authorization. Fixture Roughtime operators remain fixture-only.

The following production time gates remain open: independently operated approved Roughtime servers; authenticated server-key provisioning; freshness and uncertainty bounds; cross-boot rollback protection or an explicitly reviewed equivalent bootstrap policy; and production-scoped TLS time authorization. No network time service was added.

No guest acceptance result exists yet. There is no NAVHTTPS24 QEMU serial log or guest-evidence manifest hash. Existing QAC owner PID 24672 was left untouched. Hosted local HTTP/HTTPS fixture processes exited after their smoke run; no NAVHTTPS24 QAC or public-origin fixture process was started.

## 7. Revocation decision record

| Mechanism | Current guideXOS behavior | Pilot requirement |
| --- | --- | --- |
| Root-store distrust | Root inclusion is controlled by the trust bundle/manifest. NAVHTTPS21 distrust-after validation remains enforced. | Preserve exact root identity and distrust policy. |
| Intermediate CA revocation | Not checked by the current Navigator Mbed TLS verification call. | Must be checked or separately covered by an explicitly authorized diagnostic exception. |
| Leaf revocation | Not checked. | Must be checked or separately covered by an explicitly authorized diagnostic exception. |
| CRL validation | No CRL supplied to the current verifier; no CRL retrieval/validation path in the candidate flow. | Unknown, stale, malformed, or revoked status must fail closed for public-root pilot use. |
| OCSP validation | No OCSP request/response validation in the candidate flow. | Require validated status with freshness and signer checks, or another reviewed fail-closed mechanism. |
| OCSP stapling | No stapled OCSP status is consumed by the candidate path. | Do not treat a TLS handshake as revocation evidence; define and enforce stapling semantics before pilot. |
| Leaf/SPKI pin | No diagnostic pin is installed. The G5 candidate is a CA trust anchor, not a leaf pin. | A single-origin pin-only diagnostic could be considered under a separate release-owner decision, with exact identity, expiry/rotation, and short scope. It would not be Mozilla-root browser trust. |

Soft-fail revocation is not approved. Public-root eligibility does not remove the revocation requirement. A pinned-certificate/SPKI experiment is only a possible diagnostic alternative if general revocation cannot be delivered; it is not implemented or authorized here and cannot be used to imply general trust.

## 8. Proposed single-origin HTTPS pilot (not authorized)

| Control | Proposed setting |
| --- | --- |
| Origin | `https://digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com:443/` |
| DNS/SNI | Resolve only this hostname under the controlled QAC fixture; use the exact hostname as SNI and certificate hostname. No IP-literal substitute. |
| Trust anchor | Exact self-signed DigiCert TLS ECC P384 Root G5 DER, SHA-256 `018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05`; no additional public root. |
| Chain | Leaf SAN must equal the origin; require serverAuth EKU and valid path signatures/constraints. Require path to the exact G5 anchor and report the selected anchor. Do not accept a G3-terminated path from a candidate-only bundle. The peer may present both captured path alternatives; their DER identities remain distinct. |
| TLS/authentication | TLS 1.2 or later, certificate-chain and hostname validation on, server authentication only. No client certificate or application credentials. No EV claim; no unsupported policy-tree claim. |
| Time | Production-scoped authenticated time with freshness, bounded uncertainty, and rollback protection. The offline fixture timestamp is insufficient. |
| Revocation | Pilot remains blocked until a reviewed fail-closed leaf and intermediate revocation policy is implemented. No soft-fail. |
| Method/path | One `GET /` only. No cookies, credentials, POST, CONNECT, arbitrary links, redirects, subresources, or other origins. Reject redirects that change scheme or host. |
| Transfer bounds | Maximum response body 64 KiB; maximum total session 15 seconds; one attempt, zero retries; no decompression expansion beyond the same body cap. |
| Downgrade | No HTTP fallback, no TLS bypass, no retry over plaintext. |
| Evidence | QAC serial log, kernel PE/ELF and ramdisk SHA-256, qualification manifest hash, exact selected trust-anchor fingerprint, verification flags, DNS query/answer pcap, TLS protocol/cipher, HTTP status/body byte count and body hash. Do not log secrets or send more than the single GET. |
| Authorization | Release owner must authorize the exact hostname, root fingerprint, method/path, expiry window, QAC session, time policy, and revocation policy in writing. No authorization is implied by this report. |
| Cleanup/rollback | Stop QAC fixture processes, retain signed logs and hashes, discard the one-session ramdisk, restore the prior disabled policy/bundle, verify ordinary image remains unchanged, and revoke/remove any temporary pin or fixture material. |

The captured leaf is valid through 2026-12-13. Re-capture and re-verify the current public chain before any later pilot; the stored capture is not a permanent endpoint identity. This proposal is not release-owner authorization and must not be used to activate public HTTPS.

## 9. AMD64 build and resource accounting

Both AMD64 builds completed after the NAVHTTPS24 integration. The ordinary image was built without the qualification macro. The qualification image contains the candidate verifier symbol; the ordinary image does not. `objdump -p` reports an empty PE Import Directory for both.

| Image | Bytes | SHA-256 |
| --- | ---: | --- |
| Ordinary `kernel.elf` | 2,928,456 | `ed83ab38948a24f0022fbbf49db809d9ef1a6d73f19eadcdf315d42bec59163e` |
| Ordinary `kernel.pe` | 3,041,038 | `59117bbf18b381d8927338aa1e62bcf526805e3933592b15c66adb460940eaf5` |
| Qualification `kernel.elf` | 2,959,520 | `4d37ef89dcc88e26e5d2349c84cf589f06f3d51b4f4385e63aa87eeebcd5b751` |
| Qualification `kernel.pe` | 3,072,020 | `ab1e1dd7dd97f176ce8f29f3d2baa7ea9805bad9dfb3ac5572cd8bdaff6395e0` |
| Candidate qualification FAT ramdisk | 67,108,864 | `3ff5e2bced297c05fa198b9c3be959b64952303a15d7c96504c14303a2a8f161` |

Qualification ELF growth is 31,064 bytes over ordinary. The root bundle is still 790 bytes and has one certificate. Certificate fixture reads are bounded by the existing 8,193-byte PEM scratch buffer; the manifest uses the existing 16 KiB manifest limit. GCC `-fstack-usage` reports 1,792 bytes for `gxos_tls_navhttps24_candidate_qualification_run` and 1,632 bytes for `navhttps24_verify_case`; these are per-function static frames and exclude transitive Mbed TLS verifier stack. The existing Mbed TLS pin/profile is unchanged. No new bare-metal ABI was introduced. The full Mbed TLS call-chain stack maximum was not separately measured in this phase.

## 10. Source, certificate, manifest, and evidence hashes

| Artifact | SHA-256 |
| --- | --- |
| `gxos_tls_foundation.cpp` | `a27d413f0d7a057b010caa3cfd6258350f1d1057e13f62a2bd4a03c87b59c840` |
| `gxos_tls_foundation.h` | `1759839f2f535b78b23569781d9a26f1b29d5d6d554307148179f61afcf5186a` |
| `kernel/core/kernel_apps.cpp` | `a65ed1227fec62c645f691985a2cb47a1fca8d641b3102ed092406c4305b2135` |
| `scripts/generate-wallpaper-pack.ps1` | `0045b186bacfbe478fad88bd3218994a5081a14a82b01afa0e0a3458784b9cf7` |
| `scripts/smoke-navigator-kernel.ps1` | `62c222a42752369de81cd02e5aa604ad800fdbba6a8db46d950b7c9ac775d783` |
| `scripts/build_navhttps24_candidate_manifest.py` | `8301e0d435c4c458949e03408c25f6102f099811ef156525710a5434418c4509` |
| `tests/navhttps24/navhttps24_mbedtls_verifier.c` | `da77446d853dfbb4920c6882b424f0679e40d51b20bf8687948436a62f6d01c6` |
| `tests/navhttps24/CMakeLists.txt` | `f6b9876ccfe824d4e37eaf3f2f23300bab8f1331cd47df4106430b0458e2084b` |
| `tests/test_navhttps24_candidate_qualification.py` | `47401ea358c660e3d35427bfc21de398725780ecb7f432bbd166e93ee37b8817` |
| NAVHTTPS24 manifest | `4dbfe2cad8f72494b846bbb8543579933a7963b534c52f0efdb42d180e4a97c9` |
| NAVHTTPS24 capture manifest | `203b64d1671bb7d17ba6c188b397ac89c280012e3afb62419482fdac984bd345` |
| Candidate G5 bundle PEM | `05161ad2ac04a0df956ef803e127aa877cc5131e0a727ed8e5de43f02e8868c4` |
| Presented leaf DER | `812cad93a5ac78f8f546777a13cb531bf692739f6bffe59734d20002d6a10fd7` |
| Presented CA1 intermediate DER | `97a073957c5812725757672f222690655a1407d71011edfc8b2fadc5128911da` |
| Presented exact self-signed G5 root DER | `018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05` |
| Presented G5 cross-certificate DER | `3142f99d358d9b500e5cdd2dff8f719a8ccecf589652334848522cca807d0807` |
| Presented Global Root G3 DER | `31ad6648f8104138c738f39ea4320133393e3a18cc02296ef97c2ac9ef6731d0` |
| NAVHTTPS24 Mbed TLS host result log | `a0184af97006fbc1fa091258ce621967856e334853aa83fadb72c2f4f87671ce` |
| NAVHTTPS20–24 Python regression log | `794d71d2913f09cf871a5d73f9a625a2dd68453cede78dc1a4195dbad4802720` |
| Strict Ed25519 hosted/freestanding result log | `8b63a5a9b7b7244089c049fd52f7ef26e5b500e41a295ec1d6037e4c854c23f3` |
| Roughtime 96-test result log | `19a317c113c0e1f024f63d1c4dd574f2e688de943d8d104f5b5d0415d2a37830` |
| Hosted Navigator smoke result log | `23ac570ba5218b29ecdbd2d3bd837de077b976a1e748756ebbec2c53d5157170` |

The guest-evidence artifact hash is **not available** because QAC could not be admitted; no guest serial log was produced. The qualification kernel and ramdisk hashes above are build evidence, not guest-execution evidence.

## 11. Pending distrust-after guest test

NAVHTTPS21 hosted distrust-after validation passed 12/12. Its guest test was not started because the QAC-exclusive slot was occupied by another project's QEMU process. This remains an independent pending guest result; no NAVHTTPS21 policy was weakened.

## 12. Remaining readiness gates

1. Acquire QAC-exclusive admission after the other QEMU owner exits; do not terminate or reuse that guest.
2. Run NAVHTTPS24 Tests A–D against the qualification image, retain serial log and image hashes, and confirm candidate, manifest, chain, flags, time scope, and disabled production authorization.
3. Run Test E for NAVHTTPS17 controlled fixture-time HTTPS, including HTTP 200 and invalidation checks.
4. Run Test F (NAVHTTPS21 distrust-after guest) if QAC time allows; record separately.
5. Decide and implement a reviewed fail-closed revocation mechanism before a public-root session. Keep soft-fail disabled.
6. Complete production authenticated-time server/key provisioning, freshness/uncertainty, rollback, and production TLS authorization gates.
7. Obtain release-owner authorization for the exact single-origin session before any public HTTPS guest connection.
8. Re-capture the endpoint certificate chain at execution time and repeat the exact-anchor verification.
9. Measure full transitive Mbed TLS verifier stack use in a future resource pass.

## 13. Recommended NAVHTTPS25 scope

Use QAC-exclusive admission to complete the NAVHTTPS24 offline guest acceptance and NAVHTTPS17 fixture-time regression, then the NAVHTTPS21 distrust-after guest case. Add guest-visible fail-closed probes for candidate-manifest/metadata tampering and policy/time scope drift if the current test context can expose those branches without creating a user-facing import mechanism. Keep general public HTTPS disabled while the production time and revocation gates remain open. Separately review whether a narrowly scoped, release-owner-authorized SPKI diagnostic is useful; it must not be treated as browser trust or silently substituted for the public-root pilot.
