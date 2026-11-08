# Navigator HTTPS Regression Stabilization — NAVHTTPS5R

**Outcome: B — hosted Navigator is restored and the controlled guest HTTPS/date and production clock-gate assertions pass, but the full guest baseline remains incomplete.** The generic local TLS negative probe still reports a TCP send failure before its first SYN.

## Repository and preserved state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`.
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `a4a682970b53a12db9ff133db06aea8175b83164`.
- The worktree was clean at start. No branch switch, reset, stash, clean, rebase, merge, phase-file update, or push was performed.
- The tracker remains `last_completed=PHASE26`, `next_expected=PHASE27`.
- NAVHTTPS2–5 reports and the retained QEMU artifacts were reviewed before rebuilding.
- Public HTTPS, real-public probes, candidate promotion, and release trust-default changes remained disabled.

## Hosted Navigator window failure

The original hosted smoke reported 55 checks passing, 230 failing, and no Navigator window. `DesktopService::LaunchApp` had returned true, but the compositor process had not yet entered its GUI message loop. The smoke immediately queued the create request and began looking for a window, so the test observed the launch request before the compositor could receive it. The same test seam could also inspect or mutate Navigator state while its first document paint was still running.

The narrow repair adds two bounded readiness barriers:

1. The compositor publishes readiness only after entering its input loop. Hosted smoke waits up to 15 seconds before launching Navigator.
2. Navigator publishes initial-frame readiness after its first `updateDisplay()` returns. Hosted smoke waits for that completion before reading or changing Navigator test state.

The retest used the rebuilt `guideXOSServer.exe`; the compositor logged `Compositor main loop ready`, the Navigator window was created and discovered, and all seven toolbar widget IDs were present. The full suite passed **287/287 checks**, with zero failures. The new readiness checks account for two checks beyond the earlier 285 total. Log: `logs/navigator-hosted-smoke-20261107-202725.log`.

## Local TLS connection failure

The generic `tls_smoke` failure is a pre-TLS transport failure. In both the valid-policy and expired-certificate guest runs, serial output shows a socket being created and immediately closed before any `[TCP] Connecting` line. The result is `tls_status=TcpConnectFailed`, `error=network down`, and one TLS TCP attempt. `tcp_connect()` returns before emitting its SYN when `send_segment()` fails; `send_segment()` currently maps every non-success `ipv4::send_packet()` result to `TCP_ERR_NETDOWN`. The serial diagnostics therefore lose the underlying IPv4 error.

The pcap/fixture evidence is not a TLS handshake from that failed generic request. The first guest TLS connection follows the later successful plain HTTP redirect in the serial sequence. That redirect reaches `guidexos.test`, sends a ClientHello, receives the fixture certificate, and is correctly rejected for the untrusted chain (`verify_flags=8`). Subsequent production-policy cases also reach TLS. The HTTPS listener was ready, DNS resolved locally, and four required controlled HTTPS cases passed in the valid-policy run. These observations rule out fixture startup, TLS setup, and certificate verification as the cause of the generic probe’s `TcpConnectFailed` result.

The established defect boundary is therefore **TCP/IP packet submission before SYN**. The exact `ipv4::send_packet()` status was not preserved, so this run cannot distinguish an ARP-resolution miss from another IPv4/NIC transmit error. A transient first-packet readiness/neighbor-resolution issue is plausible, but not proven. No retry or loosened security behavior was added. Test E remains unresolved pending a diagnostic run that records the underlying IPv4 status and repeats the initial connection with the stack ready.

## Hosted regression results

| Check | Result |
|---|---:|
| `scripts/smoke-navigator-hosted.ps1 -Build` | 287/287 passed |
| `scripts/smoke-dns-resolution.ps1` | 13 protocol cases + 6 controlled-fixture checks passed |
| `scripts/smoke-udp-wire-bounds.ps1` | 6/6 passed |
| `scripts/smoke-navigator-url-resolution.ps1` | 11/11 passed |
| `scripts/test-navigator-https-qualification-contract.ps1` | 7/7 passed; includes NOT_RUN/BLOCKED/FAIL/PASS and zero-execution handling |
| `scripts/smoke-secure-random-contract.ps1` | Passed |
| `scripts/verify-mbedtls-profile.ps1` | Passed; Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, three local patches |
| CA bundle validation | Candidate: 118 certificates, `production_ready=no`; controlled fixture: one root, `production_ready=no`, `test_only=yes` |

The hosted outcome-contract test validates classification and zero-execution handling. Certificate dates, chain verification, hostname rejection, and downgrade behavior were qualified against the actual guest fixtures below; the hosted Schannel smoke does not independently inject a test clock into Mbed TLS.

## QEMU results

All guest runs used local controlled fixtures, AMD64, and the build flags `GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE`, `GXOS_NAVIGATOR_TLS_CAPABILITY_CONTRACT_NEGATIVE_TEST_ACTIVE`, and `GXOS_KERNEL_TEXT_WRITE_GUARD`. No `-GuestDnsQualification`, public probe, public resolver, or public-origin connection was enabled.

| Test | Executed | Passed | Failed | Blocked | Skipped | Evidence |
|---|---:|---:|---:|---:|---:|---|
| A — valid controlled HTTPS | 4 required cases | 4 | 0 | 0 | 0 | Valid chain and hostname, flags 0; redirect, wrong-host, and downgrade checks also passed. Broader wrapper failed on the independent generic pre-SYN probe. |
| B — expired certificate | 4 required cases | 4 | 0 | 0 | 0 | `policy_validated` returned `CertificateVerifyFailed`, flag 1 (`BADCERT_EXPIRED`). Broader wrapper failed on the same generic pre-SYN probe. |
| C — not-yet-valid certificate | 1 date assertion | 1 | 0 | 0 | 0 | Guest time `2026-06-30T12:00:19Z`; `NotBefore` is 2026-07-01; Mbed TLS returned `CertificateVerifyFailed`, flag 512 (`BADCERT_FUTURE`). Scenario wrapper passed. The separate four production-policy cases were correctly blocked by this smoke-only scenario. |
| D — invalid guest clock | 1 policy-gate assertion | 1 | 0 | 0 | 0 | `ClockUnavailable` was observed on the controlled allowlisted request. The direct production-policy request was disabled with TLS TCP attempts 0 and plaintext TCP attempts 0. |
| E — generic local TLS transport regression | 4 probes | 0 | 4 | 0 | 0 | All four guest runs reproduced `TcpConnectFailed` / `network down` before any SYN from the generic probe. |

The expired-certificate case itself passed its negative assertion and all four production-policy required cases passed; its overall scenario status was FAIL because that scenario also requires the generic local smoke to reach certificate verification. The future-certificate scenario wrapper passed its targeted date assertion. Test D's direct `ProductionValidated` path was disabled before TCP: it reported `PolicyBlocked` with the clock-unavailable blocker, zero TLS TCP attempts, and zero plaintext TCP attempts. The separate controlled allowlisted redirect case classified the HTTPS hop as `ClockUnavailable`; that test made one initial HTTP TCP connection and one TCP connection for TLS setup, but no TLS handshake or plaintext fallback. The kernel serial reported its four production HTTPS cases as 0 executed, 0 passed, 0 failed, 4 blocked, 0 skipped because the policy gate was disabled. The runner's qualification contract separately reported `NOT_RUN` with 0 executed and 4 skipped; it did not treat zero execution as PASS. Thus the revised production clock-gate assertions were guest-qualified; the local generic transport failure remains separate.

The first clock retry was blocked by active AArch64 owner PID 17792 at QAC admission before a guest started. After that owner exited, a guarded run executed Test C and then hit a transient PID 0 listener while staging Test D. The listener later cleared; a final QAC-guarded Test D run completed. No QEMU process was terminated. The runner released QAC after Test D; a separate AArch64 QEMU process (PID 16248) appeared after completion and was left untouched. No AMD64 guest from this qualification remains active.

## Artifacts and identities

Hosted run:

- Log: `logs/navigator-hosted-smoke-20261107-202725.log`, SHA-256 `cd2ed83d2bd1869df56ab8ea8704abac6f2338c474a0bab43ef6d46a90152984`.
- Hosted executable `guideXOSServer.exe`, SHA-256 `146477f25c0aa98e1e47d4b095e8dce772cbcfec91ca0392ccb4eb612a7a7ea7`.

Test A build `20261107-203636-701f2dd5` (source HEAD `a4a682970b53a12db9ff133db06aea8175b83164`, source fingerprint `72ba4eefb163806893ae0fc4f89c52261a4e7e88658be740914fea24bc14a514`):

| Artifact | SHA-256 |
|---|---|
| `ESP/kernel.elf` | `923384cbb711c96e2e853aa92278b6dbd9a04a968abdfae57be1e1b28e750d77` |
| `ESP/EFI/BOOT/BOOTX64.EFI` | `ebd8345a607eb84d01fb42e75059e8fb8b8c65e3ac0dd71feb8477382fd00e25` |
| 64 MiB `ESP/ramdisk.img` | `71a8e5487a37bf857cfd693ae620faeabe3dc8cf08dd3787ba45a2baaf6675d7` |
| Qualification attestation | `af412150ed930993183053a85ba0398d19e1672c9b206b145ef469a6601edaf2` |
| Guest pcap `E:\CodexArtifacts\NAVHTTPS5R\test-a.pcap` (364,534 bytes) | `6fa072bee7049682c6b98708e076a9433870730b25e2cc631d88d71592b7fb23` |
| Guest serial `logs/navigator-kernel-smoke-20261107-204607-production_validated.serial.log` | `c972c8337dd8331f8fc87ffe7226e5ec97ee86c954944983ed7c91f5e4dd1c9d` |
| HTTPS fixture log | `8d57591d2d84c9fdd7f19fe2523fb20b6a19e77584677d316e08269fddeaf103` |
| HTTP fixture log | `1b47e7bd6aa7a53f19eef7abe12a7e80a314c0afa8c4b566c604c8f9b19b25f9` |
| DNS fixture JSONL | `33e98b8ae9c87f76df542c746e9242d81b8f3539b346557804c75d202bb37570` |

Test B used build `20261107-205503-8898303e`; Test C used build `20261107-210715-8518fc4d`. Both rebuilt the same kernel and bootloader hashes above. The Test C 64 MiB ramdisk SHA-256 was `f1960f378ee369ec6b5401837584782bb76974ed4f5db3d4145e60faafeb9a91`.

Test D build `20261107-211517-4a56e146` used the same kernel and bootloader hashes. Its 64 MiB ramdisk SHA-256 was `75de002bb2d90fc918b9149161cdd4018c520e947929c31fde3526024472ebe5`.

Serial and fixture logs:

- Test B serial: `E:\CodexArtifacts\NAVHTTPS5R\remaining\20261107-205503-8898303e\logs\navigator-kernel-smoke-20261107-205503-production_expired_cert.serial.log`, SHA-256 `59ca8c5d02a2ad350b5cbf14865a8adebfdc585e75410c6ea6016f0d6098068e`.
- Test B HTTPS fixture: same directory, `navigator-kernel-https-20261107-205503-production_expired_cert.log`, SHA-256 `a245426fc2c8c967684750f979050df487d6cae5f97a5b532fd0bdd3b200812d`.
- Test B HTTP fixture: same directory, `navigator-kernel-http-20261107-205503-production_expired_cert.log`, SHA-256 `1b47e7bd6aa7a53f19eef7abe12a7e80a314c0afa8c4b566c604c8f9b19b25f9`.
- Test B DNS fixture: same directory, `navigator-kernel-dns-20261107-205503-production_expired_cert.jsonl`, SHA-256 `82ad391ecc5dec3858ef89e9126d7dbdf5f2bb78de4be566e79c93b3f503d06e`.
- Test C serial: `E:\CodexArtifacts\NAVHTTPS5R\clock-cases-retry\20261107-210715-8518fc4d\logs\navigator-kernel-smoke-20261107-210715-smoke_future_cert.serial.log`, SHA-256 `e52d3f78dfe64cc1bb2bf8bfab47eb3eb548828b9097a49331f7a62883187257`.
- Test C HTTPS fixture: same directory, `navigator-kernel-https-20261107-210715-smoke_future_cert.log`, SHA-256 `6bbcd75938d0eff11d60965fedf828088c3bb5fa792260ee71676b4725aab8d7`.
- Test C HTTP fixture: same directory, `navigator-kernel-http-20261107-210715-smoke_future_cert.log`, SHA-256 `1b47e7bd6aa7a53f19eef7abe12a7e80a314c0afa8c4b566c604c8f9b19b25f9`.
- Test C DNS fixture: same directory, `navigator-kernel-dns-20261107-210715-smoke_future_cert.jsonl`, SHA-256 `c615f98c4342dd084fbf6e114c45e3f4565e640983ec0dbb987593e0f09c507f`.
- Test D admission/port result: `E:\CodexArtifacts\NAVHTTPS5R\clock-cases-retry-run.log`, SHA-256 `fb23e9098eb45b9b42d6408d4ce3d43eb0901eb65dcef7e24956202d517800c0`.
- Test D serial: `E:\CodexArtifacts\NAVHTTPS5R\clock-invalid-retry\20261107-211517-4a56e146\logs\navigator-kernel-smoke-20261107-211517-production_clock_unavailable.serial.log`, SHA-256 `08ff1484136eca2feead8173b0efd8ad5c3cba3502d5603563bfa4a2790a287e`.
- Test D HTTPS fixture: same directory, `navigator-kernel-https-20261107-211517-production_clock_unavailable.log`, SHA-256 `9a6d2f8429c5670bc058ade2bdf18ce75a21ba85f2d546d20a951e20743d5170`.
- Test D HTTP fixture: same directory, `navigator-kernel-http-20261107-211517-production_clock_unavailable.log`, SHA-256 `1b47e7bd6aa7a53f19eef7abe12a7e80a314c0afa8c4b566c604c8f9b19b25f9`.
- Test D DNS fixture: same directory, `navigator-kernel-dns-20261107-211517-production_clock_unavailable.jsonl`, SHA-256 `1eb1cad310d24c596e4c37d26cbbbcbc190ad81e945973b1810c6826ff50fcbf`.
- Final Test D runner log `E:\CodexArtifacts\NAVHTTPS5R\clock-invalid-retry-run.log`, SHA-256 `98c3f93de73a9838f4b4ba59749abf2324a6bf0a2e78785d858b2ace44c89218`.

The staged one-root controlled trust material remains separate from public trust. The public CA candidate remains `production_ready=no`; the runtime still lacks CRL/OCSP revocation checks. Certificate chain verification must not be described as revocation checking or full browser-equivalent trust.

## Remaining work and NAVHTTPS6

NAVHTTPS5R resolves the hosted window-discovery race and independently confirms valid, expired, future, wrong-host, untrusted-chain, downgrade, and production invalid-clock gate behavior in the actual guest. The goal of a green end-to-end guest baseline is not complete because the generic local TLS probe fails before TLS.

Recommended NAVHTTPS6 scope:

1. Preserve the exact IPv4 status from the initial generic probe through `tcp_connect()` instead of mapping every IPv4 send failure to `network down`.
2. Re-run Test E after a bounded guest network/neighbor readiness observation, without adding a plaintext fallback or treating the probe as skipped.
3. Re-run Test D under QAC with the updated assertions and capture `ClockUnavailable`, `ClockCallbackUnavailable`, zero TLS attempts, and zero plaintext TCP attempts.
4. Keep guest time `Plausible` distinct from authenticated/trusted time; keep public HTTPS and the candidate CA blocked until separate trust-policy, time-provenance, and revocation milestones are accepted.
5. Preserve `last_completed=PHASE26` and `next_expected=PHASE27`; keep I219-LM work independent.

## Commit

Accepted source changes and this report are committed locally on `NAVIGATOR_GENERAL_IMPROVEMENTS`. No push was performed.
