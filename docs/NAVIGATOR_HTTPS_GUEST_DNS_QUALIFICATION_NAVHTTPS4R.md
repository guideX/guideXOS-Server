# Navigator HTTPS Guest DNS Qualification — NAVHTTPS4R

**Outcome: PASS — hosted DNS preparation and the QEMU guest DNS-to-HTTPS qualification completed.** Test A and Test B ran independently and passed. The existing four-case validated HTTPS suite passed 4/4. Public-production HTTPS remains disabled.

## Repository and preserved state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`.
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `db8383e6accfd12f3bfa7b606c0d6c5c65b3dafd`.
- NAVHTTPS2 remains the accepted baseline: 4/4 QEMU HTTPS cases and 285/285 hosted Navigator checks passed.
- The I219-LM phase tracker remains `last_completed=PHASE26`, `next_expected=PHASE27`. No I219-LM code or physical NIC settings were changed.
- No branch switch, reset, rebase, stash, clean, push, production rollout, or unrelated project change was made. Competing QEMU workloads observed before admission were left untouched; this run proceeded only after QAC became available.

## Qualification DNS fixture

The host's normal UDP/53 endpoint was already owned by Windows DNS Client (`svchost.exe`); the fixture did not bind, replace, or change system-wide DNS. The run used the existing QEMU `pc` machine, E1000 NIC, and user-mode network. The guest used `10.0.2.15`, gateway `10.0.2.2`, and QEMU's normal resolver `10.0.2.3:53` for Test A. QEMU user networking provides those guest-side gateway and DNS conventions; its built-in resolver does not supply this custom hostname. See the [QEMU networking documentation](https://www.qemu.org/docs/master/system/devices/net.html) and [QEMU command-line documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html).

For the controlled case, a qualification-only fixture bound host loopback `127.0.0.1:55353`, reachable by the guest at `10.0.2.2:55353`. It answers only `prod.guidexos.test A 10.0.2.2`; unknown names receive NXDOMAIN. The fixture checks port ownership, fails on collision, uses a bounded receive timeout, logs JSONL request/response events, and supports clean stop and restart. UDP/53 remains the production resolver port; only the opt-in qualification build/runtime path selects the fixture port. The controlled hostname's static mapping was disabled for this test.

Hosted preparation passed before QAC admission:

- DNS protocol smoke: 13 protocol scenarios passed.
- Controlled DNS fixture: 6 checks passed, covering A response and transaction ID, unexpected source, malformed/truncated input, timeout, shutdown/restart, and port cleanup.
- UDP wire bounds: 6/6 passed (4 UDP and 2 datagram-socket checks).
- URL resolution: 11/11 passed.
- HTTPS qualification outcome contract: 7/7 passed, including blocked/not-run/fail/pass classification and the zero-execution rule.
- Secure-random contract: passed.
- Full hosted Navigator smoke with build: passed (`NAVIGATOR_SMOKE_RESULT: PASS`). The retained run log is listed below.

## QAC guest evidence

QAC admission succeeded after hosted preparation. The guest ran with `GXOS_NAVIGATOR_GUEST_DNS_QUALIFICATION_ACTIVE`; the runner's packet capture and the kernel's resolver diagnostics provide independent evidence. The final run completed with zero blocked or skipped required HTTPS cases.

### Test A — guest UDP DNS

**PASS.** The guest issued an actual UDP A query for `example.com` to `10.0.2.3:53`. The packet verifier matched the guest query and resolver response by transaction ID and question; the response source validated as `10.0.2.3:53`. The guest accepted the response, returned `172.66.147.243`, and attempted TCP to that same address on port 443. TCP connected successfully, so external network status was separately recorded as available. This is a guest resolver result, not a static mapping or cached-address result.

### Test B — controlled DNS to HTTPS

**PASS.** With static mapping disabled for `prod.guidexos.test`, the guest queried `10.0.2.2:55353`. PCAP and fixture JSONL both record transaction ID 2, question `prod.guidexos.test A`, and answer `10.0.2.2`; the guest validated the reply source and handed that address to TCP at `10.0.2.2:8443`.

The guest established TCP, completed an Mbed TLS 1.2 handshake, and reported certificate and hostname validation success with verification flags zero. It received HTTP 200, bounded the 107-byte body, matched expected content, and delivered it to Navigator's parser (2 parsed blocks). The request used the existing controlled fixture trust path. No verification bypass, plaintext fallback, public CA candidate, or public-origin request was enabled.

The existing validated HTTPS qualification also finished **4/4 required cases passed, 0 failed, 0 blocked, 0 skipped**. Public-pilot and real-public probes remained disabled by policy; the serial log reports the trust manifest is not production-ready.

## Cleanup and classification

- Test A: `PASS` (required DNS, validation, address return, and TCP handoff executed and passed).
- Test B: `PASS` (required DNS-to-TCP-to-TLS-to-HTTP assertions executed and passed).
- Required validated HTTPS suite: 4 executed, 4 passed, 0 failed, 0 blocked, 0 skipped.
- QAC: released cleanly by the runner. The qualification x86_64 guest exited, the DNS fixture logged its `stopped` event, and UDP/55353 is released. A separate `qemu-system-aarch64.exe` (PID 25164) appeared during the first post-run check; it was left untouched and had exited by the follow-up check.
- No other QEMU workload was stopped or modified.

## Remaining prerequisites and next milestone

NAVHTTPS4R closes the controlled guest DNS and HTTPS acceptance gap. Public-production HTTPS is still not authorized: the staged one-root bundle is the controlled fixture, its manifest reports `production_ready=no`, and the separate public CA candidate remains unapproved. The public pilot and public probe remain disabled. A next milestone should handle public trust provenance, clock/revocation readiness, and explicit public-origin policy as a separate review; keep the physical I219-LM Phase 27 qualification independent.

## Commands and retained evidence

Hosted commands completed successfully:

```powershell
pwsh -NoProfile -File scripts/smoke-dns-resolution.ps1
pwsh -NoProfile -File scripts/smoke-udp-wire-bounds.ps1
pwsh -NoProfile -File scripts/smoke-navigator-url-resolution.ps1
pwsh -NoProfile -File scripts/test-navigator-https-qualification-contract.ps1
pwsh -NoProfile -File scripts/smoke-secure-random-contract.ps1
pwsh -NoProfile -File scripts/smoke-navigator-hosted.ps1 -Build
```

The guest run was:

```powershell
pwsh -NoProfile -File scripts/smoke-navigator-kernel.ps1 -Build -GuestDnsQualification -ScenarioFilter production_validated -TimeoutSeconds 300
```

Run ID: `20261107-131158-f47a8e6e`.

| Artifact | Path | SHA-256 |
|---|---|---|
| Kernel | `out/navigator-https-qualification/20261107-131158-f47a8e6e/ESP/kernel.elf` | `5410a7978f62bf74e071ce94db0677ade25e1ba211f82dc6b5b5d84cb692d49f` |
| Bootloader | `out/navigator-https-qualification/20261107-131158-f47a8e6e/ESP/EFI/BOOT/BOOTX64.EFI` | `ebd8345a607eb84d01fb42e75059e8fb8b8c65e3ac0dd71feb8477382fd00e25` |
| Ramdisk | `out/navigator-https-qualification/20261107-131158-f47a8e6e/ESP/ramdisk.img` | `e56d254723c05a70c7cabe94103b5bab3e1eb492e3949b9da88f9a9ea146e599` |
| QEMU DNS packet capture | `out/navigator-https-qualification/20261107-131158-f47a8e6e/guest-dns-wire.pcap` | `773c423af938abafded3c330a5d2189ec11392ab19b0cd1b3d7114756444ec2e` |
| Qualification attestation | `out/navigator-https-qualification/20261107-131158-f47a8e6e/navigator-https-qualification.json` | `d662c725cf7822efb0b1f6dad0262787bf789ea550abd027f9904c70c8e26cd3` |
| Guest serial | `logs/navigator-kernel-smoke-20261107-131158-production_validated.serial.log` | `21638977df83747fcc0a2cba81c7fe0cc6a18da14f399447844d491fefa0778b` |
| DNS fixture JSONL | `logs/navigator-kernel-dns-20261107-131158-production_validated.jsonl` | `7967096af91232e902046337a3c347bac504de457868ac7f2d9bd1b0ba06e7fd` |
| HTTPS fixture log | `logs/navigator-kernel-https-20261107-131158-production_validated.log` | `adcfd8c31bb498f8a66c22a3a5c6c8f0b06999485a6b833c80a88f6bdcb044ac` |
| Hosted Navigator smoke | `logs/navigator-hosted-smoke-20261107-125611.log` | `ec6ff72150b33fe69cc14a91479c45a84ed6f8a9642d6e2c173f6a3eea1025a6` |

The source HEAD in the build attestation is the expected starting HEAD. Changes are committed locally; nothing was pushed.
