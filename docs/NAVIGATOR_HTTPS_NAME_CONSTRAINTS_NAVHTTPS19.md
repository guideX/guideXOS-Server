# Navigator HTTPS Name Constraints — NAVHTTPS19

**Date:** 2026-11-15
**Branch:** `NAVIGATOR_GENERAL_IMPROVEMENTS`
**Starting HEAD:** `0c66f5e47d9e97f80460a7db342ecae446c91a2c`
**Outcome:** A — corrected Name Constraints enforcement passed in the actual AMD64 guest

## Baseline and correction

NAVHTTPS18 reproduced a certificate-path verification gap in the bundled Mbed TLS 4.1.0 verifier. A valid noncritical Name Constraints extension on the intermediate permitted `.allowed.example`, while the otherwise-valid leaf presented `www.outside.example`. The hostname, signatures, trust, time, EKU, Basic Constraints, and key usage were valid. The verifier accepted that prohibited name with verification flags `0x00000000`.

The NAVHTTPS18 fixture generator and chain remain unchanged. Its regression now expects `MBEDTLS_X509_BADCERT_NAME_CONSTRAINTS` (`0x00100000`); it passes with that flag set and no unrelated verification flags.

## Upstream assessment and pinned port

The change adapts the implementation in [Mbed TLS PR #11027](https://github.com/Mbed-TLS/mbedtls/pull/11027), reviewed at PR head `26dc074328d9bdbfc206285e70cc7c5f6aec509e`. The PR and the linked [issue #8759](https://github.com/Mbed-TLS/mbedtls/issues/8759) were open on 2026-11-15. The upstream work parses the extension and checks constraints after construction of the actual verification chain, before the normal verification callback and merged result. It covers DNS, IP, email, directory names, self-issued intermediates, and unsupported forms that must fail closed.

The downstream port targets the repository's pinned Mbed TLS 4.1.0 commit `0fe989b6b514192783c469039edd325fd0989806` and TF-PSA-Crypto 1.1.0 commit `29160dd877d29658279fd683b2ae57b320ddcf09`. The full development branch and test-framework update were not imported. A deterministic offline certificate corpus was added for the pinned stack. The port also rejects non-contiguous IPv4/IPv6 masks during parsing. The patch series records the upstream PR/head, base revision, and Mbed TLS dual Apache-2.0 or GPL-2.0-or-later licensing; vendor license files and notices remain intact.

## Enforcement behavior

`name_constraints_incl` and `name_constraints_excl` are parsed from both critical and noncritical extensions. The verifier checks every applicable CA in the constructed path against certificates below it, including a trusted root certificate with constraints. It exempts self-issued intermediate certificates from checking as required for path processing, while still applying their constraints to other descendants. These checks run through the regular Mbed TLS verification path; ordinary signature, trust, time, hostname, EKU, CA authorization, key usage, algorithm, and callback failures remain in the merged flags.

| Name form | NAVHTTPS19 behavior | Corpus evidence |
|---|---|---|
| DNS | Case-insensitive label-boundary matching; leading-dot subtrees match descendants but not the apex; permitted subtrees and excluded subtrees account for wildcard names, with excluded matches taking precedence. CN fallback is checked when a leaf has no SAN. | Positive and prohibited names, boundary, case, leading-dot, wildcard, excluded-over-permitted, and CN cases |
| IPv4 and IPv6 | Address/mask matching within the same address family. Non-contiguous masks are rejected as malformed. | Permitted and excluded IPv4/IPv6; malformed-mask parse rejection |
| RFC822 email | Mailbox, host, and leading-dot domain forms are evaluated; local parts are case-sensitive and host comparison is case-insensitive. | Permitted and excluded email SANs |
| directoryName | RDN-prefix constraints are evaluated against certificate subjects and directory-name SANs. | Permitted and excluded subject names |
| URI and other unsupported forms | If a CA constrains a form the verifier cannot evaluate and a certificate below it contains that form, verification fails closed with the Name Constraints flag. An unrelated certificate with no name of that form is not rejected for that constraint alone. | URI-constrained SAN violation rejected; same constraint with no URI SAN accepted |

Malformed extensions are rejected during parsing. The parser also rejects empty constraint lists, unsupported minimum/maximum bounds, invalid GeneralName encoding, and invalid IP address/mask lengths. Unsupported forms are never treated as permission when they occur in a constrained certificate.

## Adversarial certificate corpus

`scripts/test-navhttps19-name-constraints.ps1` generates a fresh offline corpus and verifies it with the pinned Mbed TLS path verifier. The final run passed **40 cases: 16 positive, 24 negative, 22 verification rejections with the Name Constraints flag, and 2 malformed-extension parse rejections**. It includes the unchanged NAVHTTPS18 exploit regression, critical and noncritical extensions, trust-anchor and intermediate constraints, conflicting and nested constraints, self-issued and cross-signed paths, DNS wildcards, valid and excluded IP ranges, email and directory names, unsupported URI constraints, and malformed ASN.1/IP masks.

The preceding NAVHTTPS18 offline verifier remains a 15-case regression and passes. Existing hosted checks also continue to cover unrelated certificate failures and policy boundaries: wrong hostname, invalid/untrusted chains, certificate dates, clock assurance, trust metadata, verified TLS requirements, and plaintext downgrade refusal. OpenSSL CLI was unavailable on the host, so an independent OpenSSL comparison was not run; the generated fixtures were inspected with Python `cryptography`, and the authoritative verification results are from the pinned Mbed TLS implementation used by the kernel.

## Hosted regressions

All requested suites passed:

- Roughtime v1: 96 tests.
- Strict Ed25519: 238 unique cases, all 238 freestanding and hosted checks passing.
- Clock assurance, Navigator trust profile, secure randomness, HTTPS qualification contract (7 cases), DNS (13 protocol plus 6 fixture cases), UDP/socket bounds (6 cases), and TCP cleanup.
- Hosted Navigator smoke: `NAVIGATOR_SMOKE_RESULT: PASS`.
- Mbed TLS profile verification and clean pinned-source reconstruction: pass with four downstream patches.

The only observed unrelated build warning was the existing ephemeral-port comparison warning in `kernel/core/tcp.cpp`. No browser CSS/compositor work was performed. The preserved hosted smoke log (`logs/navigator-hosted-smoke-20261114-214517.log`) records `NAVIGATOR_SMOKE_RESULT: PASS` but does not include an explicit 288/288 tally, so that exact denominator is not asserted.

## Freestanding resources

The ordinary and qualification AMD64 kernels both link Mbed TLS's `mbedtls_x509_crt_verify_restartable` and the freestanding `memchr` routine used by the port. Their PE images have no imported DLLs. The earlier NAVHTTPS17 ordinary kernel measured `.text=2,399,753`, `.data=67,552`, `.bss=575,822,464`; NAVHTTPS19 ordinary measured `.text=2,404,009`, `.data=67,552`, `.bss=575,822,464`. The Name Constraints port adds 4,256 bytes of text to the ordinary image and no data/BSS. Qualification adds the existing smoke and qualification code and uses its separate 128 MiB kernel heap reservation.

Name Constraints lists use the existing Mbed TLS allocator backed by guideXOS's fixed 1 MiB TLS arena. The root bundle is capped at 512 KiB, the manifest at 16 KiB, and the Mbed TLS verification chain at 10 entries (leaf, up to eight intermediates, and root). Subtree iteration is over decoded entries contained in those bounded certificates; no larger parsing or chain limits were introduced. A stack-usage compile of the patched `x509_crt.c` measured a 240-byte subtree parser frame. The verification callback frame is 496 bytes; nested Name Constraints frames are 112, 176, and at most 80 bytes for the directory-name parse path, for an approximately 0.9 KiB nested verifier path before caller frames. The implementation is C-only and adds no STL, exceptions, RTTI, or separate static buffers.

## Freestanding guest qualification

The final controlled QEMU runs used local test roots and servers. Test A used the existing `.guidexos.test` controlled policy hostname and a permitted constrained chain. Tests B/C used the original NAVHTTPS18 name `www.outside.example`; only the `GXOS_NAVHTTPS19_GUEST_QUALIFICATION_ACTIVE` image routes that exact host and `/navigator-policy/` path to the local fixture and admits it to the fixture-only clock scope. The ordinary build has no such exception, the fixture trust profile remains `fixture-only`, and `broad_public_https_enabled=no` throughout. The adversarial guest runs used the explicit static mapping to QEMU's host gateway; their controlled DNS fixture logs contain only `ready` and `stopped` events, with no external DNS queries.

| Test | AMD64 guest result | Kernel SHA-256 | Ramdisk SHA-256 | Serial evidence |
|---|---|---|---|---|
| A — permitted constrained DNS name | Verified TLS `Success`, HTTP 200, flags `0`, certificate and hostname validated; no plaintext fallback on the downgrade case. | `69641f92fd18cb2e7f6989adb0573cd403e8df617822c9d972971d3870733c59` | `4d5349e7686f4c9ddf3960654afb6b56da2938f0c910e40a46d4d116cc0656c5` | `tmp/navhttps19-qac-allowed-nav17-20261115/20261115-053558-7d89c84d/logs/navigator-kernel-smoke-20261115-053558-production_name_constraints_allowed.serial.log` |
| B — original noncritical prohibited name | `CertificateVerifyFailed`, flags `1048576` (`0x00100000`); accepted NAVHTTPS17 fixture lease, TLS attempt `1`, plaintext attempts `0`, no HTTP response served. | `8267ba5e13b98593c30fc16277ea3bae2d7d414ccdc1deebdfe41bfcb4c5859c` | `9801b2b8501395c053c5c4bef5293df83103614d3b0174893a6026dcd91f00d2` | `tmp/navhttps19-qac-exploit-nav17-r4-20261115/20261115-055557-84657385/logs/navigator-kernel-smoke-20261115-055557-production_navhttps19_original_violation.serial.log` |
| C — unsupported URI-constrained SAN | Fail-closed `CertificateVerifyFailed`, flags `1048576`; accepted fixture lease, TLS attempt `1`, plaintext attempts `0`, no HTTP response served. | `8267ba5e13b98593c30fc16277ea3bae2d7d414ccdc1deebdfe41bfcb4c5859c` | `ff4cde78dc17791b02e011a2ae017d2e288526759313800ff6fcb3dd82cce1bb` | `tmp/navhttps19-qac-exploit-nav17-r4-20261115/20261115-055557-84657385/logs/navigator-kernel-smoke-20261115-055557-production_navhttps19_unsupported_uri.serial.log` |
| D — NAVHTTPS17 control | All 4 required HTTPS cases passed, including verified HTTP 200 with flags `0`; NAVHTTPS17 lease and post-TLS invalidation passed. | `672a68a0fbbf09df5a0e11165fc8ee2a537b7a437cf3913fb4fe9b118ae09b7a` | `cfb65e6d9ab329b8095206cf1c7bb377847f8e171f9502afe08b4e9a3eeb2d80` | `tmp/navhttps19-qac-navhttps17-final-20261115/20261115-060112-c8bb952a/logs/navigator-kernel-smoke-20261115-060112-production_validated.serial.log` |

Each run reported `[NAVIGATOR-SMOKE] result=PASS`; B/C additionally reported the exact Name Constraints flag and `https.case.policy_validated_downgrade.result=PASS`. The first allowed-name attempt without the NAVHTTPS17 local Roughtime fixture failed closed because the current controlled TLS lease was absent. The first B/C attempts also stopped before certificate verification: the fixture-ready check still expected `prod.guidexos.test`, and the next attempt hit the existing `.guidexos.test` fixture clock-scope guard. These runs are not counted as certificate evidence. The final A/B/C runs included a verified test-only lease, checked explicitly by the script, and the NAVHTTPS19 compile-time route was narrowed to the exact test hostname and policy path; D independently preserved the existing clock-lease behavior. QEMU and the local HTTP, HTTPS, DNS, and Roughtime fixture processes exited after qualification; a post-run process check found zero remaining QEMU or fixture processes. Build and serial artifacts are preserved under the paths shown above.

## Public CA boundaries

The deterministic post-change accounting report is `assets/certs/candidates/mozilla-cacert-2026-11-05.accounting.json`, SHA-256 `93C53393AF7240D62048415BFCACC292C5110655495537689F356B564F44F935`. Two independent generations matched byte-for-byte: 118 candidate roots, 0 included, 118 excluded, and 0 with a Name Constraints extension in this candidate bundle.

Path constraint enforcement is now present, but Mozilla server-auth purpose, domain constraints/distrust, certificate-policy processing, and root-program provenance/approval are still unavailable or unimplemented. Runtime public trust authorization remains disabled. No public CA candidate was approved, and public HTTPS remains disabled. Physical I219-LM phase tracking remains at `last_completed=PHASE26`, `next_expected=PHASE27`.

## Smallest practical NAVHTTPS20 scope

Build a deterministic, offline importer and validator for Mozilla root-program purposes, domain restrictions/distrust, and provenance for one candidate root. Keep the eligibility calculation deny-by-default, prove it against a small reviewed fixture set, and leave public HTTPS disabled until a separate approval phase. Revocation and real-world Roughtime deployment remain separate work.
