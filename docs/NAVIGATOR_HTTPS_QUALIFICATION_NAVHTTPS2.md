# Navigator HTTPS Qualification — NAVHTTPS2

**Outcome: A — the existing `production_validated` HTTPS path passed under QEMU.**

This phase qualified the selected existing Navigator path with its controlled
fixture. It did not qualify arbitrary public HTTPS, the public pilot, or
physical I219-LM transmission.

## Baseline and preserved state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`.
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `00c5d2cb1cb13348df3e1124a2a7c016b842c412`.
- Starting worktree: clean.
- The NAVHTTPS1 readiness record and the Phase 26/27 audit records were
  reviewed. The hardware audit remains `last_completed=PHASE26`,
  `next_expected=PHASE27`; no `.phase` file was created or changed. No I219-LM
  TX, DMA, or IOMMU work was performed.
- No duplicate NAVHTTPS2 implementation or qualification report existed.

## Hosted stale assertion

NAVHTTPS1's 284-pass/1-fail result came from a literal body assertion for
`Example Domain`. The live `https://example.com/` response no longer contained
that text; its response was an HTML 200 page with the expected valid Schannel
TLS and hostname-validation evidence. This was external page-copy drift, not a
certificate or HTTPS failure. The old check also accepted a timeout/no-TLS
Page Info state as an alternate success, which was not an acceptable HTTPS
contract.

The hosted assertion now checks the stable contract: the requested HTTPS URL,
HTTP 200, connected and validated Schannel TLS, Windows trust, explicit
`example.com` hostname validation, no smoke bypass, non-empty response, body
metrics within 256 KiB, and no response truncation/body-cap hit. A separate
Page Info assertion retains evidence for the Schannel credential and handshake
path. No public body text is assumed.

Result: `scripts/smoke-navigator-hosted.ps1 -Build` passed **285 checks, 0
failures**. Log: `logs/navigator-hosted-smoke-20261105-152402.log`.

## Build reuse and artifact identity

Previously, `-ReuseActiveBuild` could boot an existing ESP without proving
that its kernel had active HTTP smoke cases or matching trust configuration.
NAVHTTPS1 observed `http.active_cases=skipped` and a missing root bundle in the
reused guest, despite trust files being staged by the runner. A successful
boot could therefore be mistaken for qualification unless the network cases
were checked independently.

Fresh qualification builds now use a unique
`out/navigator-https-qualification/<build-id>/` tree and a dedicated AMD64
kernel `BUILD_DIR`. Existing build outputs are left in place. The runner
copies the existing UEFI loader into the isolated ESP, stages its own ramdisk,
and writes a `navigator-https-qualification.json` attestation. It records the
source HEAD and source fingerprint; required compile definitions and HTTPS
cases; TLS/hostname/plaintext policy; kernel, loader, ramdisk, CA bundle,
manifest, and policy hashes; and the trust manifest's root count and fixture
identity.

Reuse fails before QEMU if the attestation is absent, unreadable, stale, or
incompatible. Validation compares source identity, active HTTP build flags,
the serial active-case marker, artifact hashes, controlled CA and compatible
manifest, test policy, and the required TLS/hostname and no-plaintext-fallback
settings. The post-change legacy reuse probe exited before QEMU with the
specific error that the HTTPS qualification attestation was missing at
`ESP/navigator-https-qualification.json` and that a manifest-bearing active
HTTP smoke build with verified trust configuration is required. Reuse of a
properly attested build remains supported; missing prerequisites are reported
instead of silently rebuilding or overwriting artifacts.

## Trust and fixture behavior

The selected `production_validated` scenario uses the existing controlled
`prod.guidexos.test` fixture, the test certificate and its single controlled
CA root, and the `production-validated` HTTPS policy. They are staged only in
the isolated qualification artifact/ramdisk; they are not installed into the
host trust store or copied into a release ESP. Public-pilot and broad-public
HTTPS policy remain disabled.

The attested CA bundle has one root and SHA-256
`c28c679d134e640c21cb739b617034e996d48a535b97499ffa667e832711e606`; the
policy content is `production-validated`, SHA-256
`8d65939cc5d09cd9832dc5aed25ae8524058d61a71c9c0c697c61cd6665f50f4`. The
existing manifest semantics record `bundle_type=production-source`,
`production_ready=no`, and `test_only=no`; the test does not relabel or change
those fixture semantics. The controlled fixture root is staged only into the
isolated qualification image because the local production-validated policy
path needs it to validate the paired fixture certificate. It is not an
approved shipped/public root: the guest reports public trust not ready and
public-pilot/broad-public HTTPS disabled. The leaf SAN is
`prod.guidexos.test`; an offline X509 chain build also completed without chain
status errors. Guest evidence independently shows `verify_flags=0`,
`certificate_validated=yes`, and `hostname_validated=yes`. The wrong-host
control was rejected with `verify_flags=12` and
`hostname_validation_result=failure`. Global TLS verification was not
disabled.

## QEMU HTTPS execution

The command was:

```powershell
scripts/smoke-navigator-kernel.ps1 -ScenarioFilter production_validated -TimeoutSeconds 240
```

The harness acquired its existing exclusive QEMU admission after checking
that no competing QEMU guest owned the run slot. QEMU used OVMF, the
directory-backed FAT UEFI ESP from the unique build, 1024 MiB, `-netdev
user`, `-device e1000,netdev=net0`, and the existing secure-RNG arguments.
The AMD64 kernel built successfully. The UEFI loader was copied from the
existing ESP because loader sources were unchanged. The selected staged image
consisted of the following verified files:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `ESP/kernel.elf` | 2,758,864 | `86ef72ed3f194fdec3f1d5d258443e84e3bba4f12076326b9dd249b8d3c97958` |
| `ESP/EFI/BOOT/BOOTX64.EFI` | 60,928 | `ebd8345a607eb84d01fb42e75059e8fb8b8c65e3ac0dd71feb8477382fd00e25` |
| `ESP/ramdisk.img` | 67,108,864 | `b0e010d4ea2665d52512510e0ed167e0c896bb4e614db1c0faf0f43512a376e1` |

Guest serial evidence shows E1000 initialization, `http.active_cases=enabled`,
the `production_validated` policy, and the Navigator main-loop readiness
checkpoint. The `policy_validated` case connected over TCP, negotiated TLS
1.2 with `TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256`, sent an HTTP/1.1 request,
received status 200 and 107 body bytes, parsed two Navigator blocks, and
reported bounded body delivery and expected fixture content. The fixture log
records the matching `GET /navigator-policy/ok.html HTTP/1.1` with status 200.

The redirect-to-HTTPS case also reached the controlled response. The
wrong-host case verified rejection. The downgrade control received its
redirect but made zero plaintext TCP attempts and reported
`BlockedPolicy` / `HTTPS downgrade redirect blocked`. The fixture log records
`GET /navigator-policy/redirect-downgrade` with status 302.

The fixture-specific name resolution is an explicit smoke mapping from
`prod.guidexos.test` to QEMU gateway `10.0.2.2`. The guest reports
`dns_used=yes` and `dns_resolution_source=controlled-smoke-gateway-map`; this
path does **not** claim a UDP DNS query was sent. TCP establishment, E1000
transport, TLS, HTTP/1.1 parsing, and bounded Navigator delivery were exercised
through the QEMU user-networking fixture.

Required HTTPS result from serial and the qualification classifier:

```text
total=4 executed=4 passed=4 failed=0 blocked=0 skipped=0
outcome=PASS
```

All four required cases executed: `policy_validated`,
`policy_validated_redirect`, `policy_validated_wrong_host`, and
`policy_validated_downgrade`. The runner also required the fixture's HTTP/1.1
200 success request and 302 downgrade request in its server log. QEMU and the
fixture were stopped at the end of the run.

## Qualification outcome contract and hosted coverage

The smoke harness classifies qualification as:

- `NOT_RUN` when qualification was not attempted.
- `BLOCKED` when active cases, build/trust prerequisites, explicit counters,
  or case-result evidence are missing, inconsistent, or prevent execution.
- `FAIL` when required cases ran but any required result/assertion failed or
  the executed set was incomplete.
- `PASS` only when every required case has an executed PASS marker, explicit
  counts agree, and fixture/QEMU assertions pass. Zero executed cases cannot
  pass.

The runner emits `NOT_RUN` if the selected scenario set does not include
`production_validated`. The legacy ESP reuse probe emitted `BLOCKED` with zero
executed and four blocked cases before QEMU, alongside the missing-attestation
prerequisite.

Six hosted regression scenarios cover `NOT_RUN`, inactive HTTP, missing trust
configuration, all required cases passing, a required case failure, and
missing count evidence. A seventh case verifies that a QAC admission block
before guest execution is reported as `BLOCKED`: **7/7 passed** in
`scripts/test-navigator-https-qualification-contract.ps1`. The URL-resolution
suite passed **11/11**. The hosted resource diagnostics, resource scheduler,
secure-random contract, and network Phase 11–27 regression suites passed. The
Phase 27 command here was hosted regression coverage only; no physical probe
was run and the DMA translation safety gate remains unresolved.

## Evidence files and hashes

Run build ID: `20261105-160134-d197c636`. This was the final fresh build after
adding structured `BLOCKED` reporting for preflight, fixture, and QAC
admission failures and explicit `NOT_RUN` reporting when the production HTTPS
scenario is not selected. Its source HEAD is the expected starting HEAD and
its source fingerprint is
`f6167c60d6459d3e55822ce4b2f39223268a72001ffad48aa3ae684aa2a401fc`.

| Evidence | SHA-256 |
| --- | --- |
| Attestation JSON `out/navigator-https-qualification/20261105-160134-d197c636/navigator-https-qualification.json` | `8b27a5d487ff66dab17db51e3b08e491621ca071fd0e03e7063650aa52d17fd7` |
| QEMU serial `logs/navigator-kernel-smoke-20261105-160134-production_validated.serial.log` | `94c371d276434970e0c792604e4128c13e637dae25787390336d647158f68b92` |
| HTTPS fixture `logs/navigator-kernel-https-20261105-160134-production_validated.log` | `bbabdb6a909634776b84f7273a825c036054e9c27d0d059766fd2061a16f6ba0` |
| HTTP fixture `logs/navigator-kernel-http-20261105-160134-production_validated.log` | `1b47e7bd6aa7a53f19eef7abe12a7e80a314c0afa8c4b566c604c8f9b19b25f9` |
| Hosted smoke `logs/navigator-hosted-smoke-20261105-152402.log` | `8bb0b2ea90579452c56b0b62eb8e18866ba32fcc967efffa7c99d8b14fd89def` |

The HTTPS fixture stderr log is empty. The artifact attestation also records
the RAM disk contents, source fingerprint, trust manifest, and policy hashes.

## Remaining boundary and next milestone

This result qualifies only the deterministic, controlled-origin HTTPS path.
The fixture map does not qualify the guest's on-wire DNS protocol behavior;
public origins, production CA readiness, and public-pilot policy were not
tested or enabled. Navigator continues to deliver content through its current
bounded supported-document parser and interface; this work adds no general
JavaScript, CSS, or full browser engine coverage.

Recommended next milestone: **NAVHTTPS3 — production-trust and public-origin
readiness**, separately requiring a production-ready trust bundle and manifest,
verified guest clock/entropy prerequisites, protocol-level DNS evidence, and
explicit public-origin policy review. Keep the physical I219-LM Phase 27 DMA
gate as a separate open hardware qualification.
