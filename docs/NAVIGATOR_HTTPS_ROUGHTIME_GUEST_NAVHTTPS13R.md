# NAVHTTPS13R — final Roughtime requalification and NAVHTTPS7 trust-policy status

**Outcome C — hosted qualification passed apart from the Navigator smoke; current guest acceptance is pending QAC.**

## 1. Preflight and preservation

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `cefde1b57c65b01c0ff6dd90ce5d444ac4f4e38f`, the expected commit.
- The branch began two commits ahead of origin. No tracked file was modified at preflight.
- The pre-existing untracked artifacts were preserved: `tests/__pycache__/`, `tmp/navhttps12-kernel-build/`, `tmp/navhttps13-guest-kernel-build.log`, `tmp/navhttps13-guest-kernel-build/`, `tmp/navhttps13-guest-run/`, `tmp/navhttps13-kernel-build-final.log`, `tmp/navhttps13-kernel-build/`, and `tmp/navhttps8-kernel-build/`.
- The only tracked mutation caused by the hosted Navigator run was a recent-app entry in `desktop.json`. It was restored to its clean preflight version. The final commit changes this report only.
- I219-LM remains `last_completed=PHASE26` and `next_expected=PHASE27`. No I219 source or DMA handling was touched.

## 2. Current source and kernel identity

I rebuilt the AMD64 qualification target from the starting HEAD into a new directory, leaving all prior build artifacts intact:

`mingw32-make -C kernel ARCH=amd64 BUILD_DIR=../tmp/navhttps13r-kernel-build-20261114 EXTRA_CFLAGS=-DGXOS_NAVHTTPS13_GUEST_QUALIFICATION_ACTIVE`

The build completed successfully with the test-only boot hook enabled. The output ELF is SHA-256 `4c9bf62713e4a2323f568b0f6a5ec319ab000496b4b3bdaee4e46e7303d6c614`. This matches the already-built corrected qualification ELF recorded in the NAVHTTPS13 report. The new PE is `d545b4744faa3f5bbd10b2e711cdde91953ca937b9f4f256c4c8cbadfeee87b1`. No change was made to the committed kernel source during this requalification.

The current `main.cpp`, `shell.cpp`, verifier, acquisition client, guest test, fixture, and hosted test sources match the hashes in the artifact table below. In particular, the Roughtime verifier and UDP acquisition sources are byte-identical to the source identity in `[NAVHTTPS13](NAVIGATOR_HTTPS_ROUGHTIME_GUEST_NAVHTTPS13.md)`.

## 3. Formatter correction analysis

The post-guest correction is confined to the unsigned decimal formatter `uint64_to_str` in `kernel/core/shell.cpp`. Its only call is through the shell diagnostic output helper. It does not alter RFC 10049 parsing, signature or delegation verification, nonce validation, Merkle verification, interval arithmetic, assurance state, or UDP request/response handling. The corresponding acquisition and verifier source hashes match the earlier NAVHTTPS13 report.

The formatter uses unsigned division and remainder by ten, stores at most 20 decimal digits, and writes a terminating NUL. The 21-byte output buffer therefore accommodates the 20 digits of `UINT64_MAX` plus the terminator. I compiled the exact function body extracted from the kernel source and checked:

| Input | Expected decimal | Result |
|---|---:|---|
| 0 | `0` | PASS |
| 1 | `1` | PASS |
| 2^63 | `9223372036854775808` | PASS |
| UINT64_MAX − 1 | `18446744073709551614` | PASS |
| UINT64_MAX | `18446744073709551615` | PASS |

The first harness attempt incorrectly required byte 20 to be NUL even when the string terminator was earlier; the formatter does not promise to clear bytes after its terminator. That harness failure and the corrected passing run are both preserved. The corrected test checks the expected string and its actual terminator. No formatter repair was needed.

## 4. Hosted regression results

| Suite | Result |
|---|---|
| Roughtime hosted and portable baseline, including strict Ed25519 acceptance/rejection paths | PASS, 46/46 |
| Clock assurance | PASS |
| HTTPS trust profiles | PASS |
| Secure randomness | PASS |
| DNS and controlled DNS fixtures | PASS, 13 protocol scenarios + 6 fixture checks |
| UDP and socket wire bounds | PASS, 6 cases |
| TCP connect/cleanup and IPv4 attribution | PASS; the existing type-limits warning remains |
| HTTPS qualification outcome contract | PASS, 7/7 |
| Mbed TLS / TF-PSA-Crypto profile | PASS, versions 4.1.0 / 1.1.0, 3 patches |
| Hosted Navigator smoke | FAIL, 53 checks passed and 235 failed after the compositor main loop did not become ready within 15 seconds |

The Navigator smoke failure cascaded into navigation and CSS checks, including the historically intermittent inline-style and runtime-counter checks. The complete output remains visible in the recorded log; no CSS work or assertion changes were made. The smoke used local HTTP/HTTPS fixtures. No public-origin HTTPS probe, public trust activation, or CA promotion was performed.

## 5. NAVHTTPS13 current guest results

The new ELF was **not executed**. Before staging or launching QEMU, admission preflight found an active QEMU process owned by a separate FONT_SUPPORT task:

- PID 22476, started 2026-11-14 07:42:25 local.
- Command line identifies `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R4\run1\...`.
- QEMU was left untouched. The preflight stopped before launching a guest or fixture; UDP port 5319 was free.
- The `Global\guideXOS_QAC_QEMU_EXCLUSIVE` mutex was absent at the check. The active QEMU owner independently blocks admission under the established runner rules, so no mutex bypass was attempted.

Therefore both Test A and Test B for the current ELF remain **PENDING**, not failed and not passed. The preflight evidence is `logs/navhttps13r-guest-preflight-blocked-20261114.log`.

The prior accepted NAVHTTPS13 run remains useful historical evidence for its older ELF (`93560cecd1f47ac6acb464f291815b099540c340d7a56433cc2ba521f20c3403`):

- Test A passed all five independent positive/negative verifier cases.
- Test B passed with a fresh nonce, one actual 1,036-byte guest UDP request, and a 416-byte signed fixture response. The guest verified signature, delegation, nonce, and Merkle proof and printed `trusted_time=not-accepted` and `clock_unchanged=yes`.
- Its serial, packet capture, and fixture log remain under `logs/navhttps13-guest-20261113-172338/` with hashes below.

That historical pass does not qualify the corrected ELF. A repeat run must execute the newly identified ELF and retain serial and packet evidence.

## 6. NAVHTTPS7 guest trust-policy result

NAVHTTPS7 remains **PENDING guest acceptance**, independently of NAVHTTPS13. The existing filtered scenario `production_critical_name_constraints` is present in `scripts/smoke-navigator-kernel.ps1` and its fixture expects complete CA rejection, `TrustStoreMalformed`, and an effective disabled HTTPS policy. The current blocked QEMU owner prevented its guest execution.

The hosted profile and HTTPS contract checks passed, but they do not close the guest gate. The existing guest runner has filters for the requested boundaries, including:

- `no_policy` and `invalid_policy` for default-disabled policy and rejected policy override.
- `public_pilot_candidate_blocked` for review/legacy readiness metadata that cannot authorize public HTTPS.
- `production_validated` for the explicitly controlled fixture HTTPS path and the existing no-plaintext-fallback checks.
- `production_trust_profile_missing`, `production_trust_profile_unsupported_version`, and `production_critical_name_constraints` for missing, unsupported, and malformed trust inputs.
- `production_clock_unavailable` and `smoke_unknown_clock` for fail-closed clock conditions.

The precise remaining requirement is an exclusive QAC turn after the FONT_SUPPORT QEMU owner exits, followed by these existing filtered guest cases with the controlled fixtures. There is no need to add a trust subsystem or public-origin scenario.

## 7. QAC cleanup and security status

No QAC reservation, fixture process, QEMU process, or port was acquired by this run. There was nothing owned by NAVHTTPS13R to release. The separate QEMU process was left running and untouched. At final preflight, no Roughtime fixture listener existed on UDP/5319.

The current guest clock was not changed. A single Roughtime response remains evidence only; time assurance is not promoted to production `Authenticated`. Public HTTPS remains disabled, the public CA candidate remains unapproved, no trust roots or certificate-verification policy changed, and no multi-server consensus or I219-LM DMA work was attempted.

## 8. Readiness for NAVHTTPS14

NAVHTTPS13R establishes a source-identified build and passing hosted Roughtime baseline, but it does **not** establish a stable guest baseline for NAVHTTPS14. First execute Test A and the real UDP Test B with the current ELF under QAC, then close NAVHTTPS7's filtered guest trust-policy cases. Until then, retain the existing pending classifications and keep authenticated-time policy work separate.

## 9. Artifact paths and SHA-256

### Current source and build

| Artifact | SHA-256 |
|---|---|
| `kernel/core/main.cpp` | `bc071e5155138330e8ef0018cf3b39797e9ac216dd47e24fff1a919056998ab8` |
| `kernel/core/shell.cpp` | `f1de0daa774578b7763d001cb0fad591e6129eec52ac550d1a85371f51819754` |
| `kernel/core/gxos_roughtime_acquisition_core.cpp` | `febb12e8bad6f5c06c8707be79641dded7e633665661af1e54bb479eb75c6f5f` |
| `kernel/core/gxos_roughtime_udp_client.cpp` | `eb076197cb9b3280e3823837accc08c825e80504e95e05a86adde1e8b42be382` |
| `kernel/core/gxos_roughtime_guest_test.cpp` | `ff989bf2bd59a4a39ba259407248f2d3d8ec4942969d2764feb804f6212d0cc2` |
| `kernel/core/roughtime_guest_fixture_key.h` | `271b917232a346aa30a67464a16e689e3cd1bd050a9678fc71829be97aa00267` |
| `kernel/core/roughtime_guest_vectors.h` | `db6ee511e7aa33f98e4beb8dfcb242f051b4bcfe88dd7fdaf0cedac412490606` |
| `kernel/core/include/gxos_roughtime_acquisition.h` | `d191e19f2eb8ce7da14a9ca8bf5d1ace059881a3bd72be24e4104074730f449f` |
| `kernel/core/include/gxos_roughtime_guest_test.h` | `b4ff58fe973a58ebee0d1815746d53268a2c133613b33dd483ee2a450bee94d4` |
| `tools/roughtime_v1.py` | `05428a171b048537b43a4cf8843b796a061c8dbc66769aa502dd0a1a72815fce` |
| `tests/test_roughtime_v1.py` | `ac0db89d4508932b6ae4d1f82af8f1f998db6c72984afd68755099211b6f6498` |
| `tests/test_roughtime_portable.py` | `ac3585db19e6ef1c5f7627498f886760edfb6614c9df7c4b6e25db2392e408e3` |
| `tests/roughtime_v1_guest_fixture.py` | `7089d0aaa291086f3510f0adc6c56302979ca1154ae128b04081783ec7292101` |
| `tmp/navhttps13r-kernel-build-20261114/bin/kernel.elf` | `4c9bf62713e4a2323f568b0f6a5ec319ab000496b4b3bdaee4e46e7303d6c614` |
| `tmp/navhttps13r-kernel-build-20261114/bin/kernel.pe` | `d545b4744faa3f5bbd10b2e711cdde91953ca937b9f4f256c4c8cbadfeee87b1` |
| `logs/navhttps13r-kernel-build-20261114.log` | `9ce4631e8f726f383f7c57ac7a51942c05c421de1ac81da5b54532eb4e69f85e` |

### Formatter boundary evidence

| Artifact | SHA-256 |
|---|---|
| Corrected harness source, `logs/navhttps13r-formatter-20261114-073702-e4ec4c8f/formatter-boundaries.cpp` | `3968fa859baffaa7946b9635f037c56564b4fdef78d0c112e89691ac25fb9f3d` |
| Corrected harness executable | `2d0eb42d86267cb095374718e6c4954feb48f60be77da6324914a23073cd1a28` |
| Corrected harness log | `779995d811a2710818a74b8383a311f479a332dbf7de90164be8286f6430a816` |
| Initial harness source with invalid trailing-byte assertion | `4754ba338060ca2b5b8f17d0af3f8f3be19d560c39d1238fe0af117b47ccbbdd` |
| Initial harness log, preserved failed attempt | `7da6bacf350269c21a96420de873cd20199ecd96a91ac8ed52e896a9d10b80e6` |

### Hosted and admission evidence

| Artifact path | SHA-256 |
|---|---|
| `logs/navhttps13r-roughtime-hosted-20261114.log` | `5e28396b60967387e933f27b98bc15339400d74dc0f79bf796c1e7f779a773da` |
| `logs/navhttps13r-clock-assurance-20261114.log` | `465141a6f814fd607fcc1af1b402667f182b1e8e4177befa773351e1b45b564f` |
| `logs/navhttps13r-trust-profiles-20261114.log` | `e313a42b184b4068fa561bb51840153095bc688538bd8f32f1f2fba2607468da` |
| `logs/navhttps13r-secure-random-20261114.log` | `0ad437acee0a0d00f9b5d7ad6e1e3d0df029529662bdd162fa232e0ba864d602` |
| `logs/navhttps13r-dns-udp-20261114.log` | `b8b7812136e05a3722fb44b660fa1e3526e5c33a5bb2d7d37c9e9e1addd29ad3` |
| `logs/navhttps13r-udp-wire-bounds-20261114.log` | `8d496ddf0bab120242038ce17ad004d430cf570d46c9f0779075b85cd10afb5e` |
| `logs/navhttps13r-tcp-cleanup-20261114.log` | `1b7242319a40cea924da316a0175ae790e8bd53eeefb72402aa7e73cc9d7b98e` |
| `logs/navhttps13r-https-qualification-contract-20261114.log` | `767331c616a13a3c0ba35a8448a12652ac49dffcc6379878ad394528c8e1b1a8` |
| `logs/navhttps13r-mbedtls-profile-20261114.log` | `357feb71f2e2a8f5e5d191c7a8dc90ed5daddaebf5f506c779e9101758fc936e` |
| `logs/navhttps13r-navigator-hosted-20261114.log` | `9539da3ac2f6a2d49bf9a194742637d5d5e01946ef114bf258fefd8aced58858` |
| `logs/navigator-hosted-smoke-20261114-073902.log` | `4afc8baea061b6440c9dd1f01a13360a8c499b43d5a49df8a510bad872a2789b` |
| `logs/navhttps13r-guest-preflight-blocked-20261114.log` | `f34ae778b9846c4c12835ce66b6ee38a983aac5fa597778ad11398b06958dc41` |

### Prior accepted guest run (older ELF)

| Artifact path | SHA-256 |
|---|---|
| `logs/navhttps13-guest-20261113-172338/guest-serial.log` | `de5d4f23e20bcea720462ddae730181c9359eff88281c9f217c02e265e9d6ed8` |
| `logs/navhttps13-guest-20261113-172338/guest-network.pcap` | `8df2f64e13066194eb2d3f5bf07e7ac9d07c1367f3b0853599f73f77791fe109` |
| `logs/navhttps13-guest-20261113-172338/fixture-server.log` | `2462604e308f834c8146ed5861cf024b1076f07a14f5c182a87ce9dd73cd816b` |
