# NAVHTTPS5R2 — Local TLS Probe Attribution and Guest Regression

**Outcome: B — bounded diagnostics added; the historical pre-SYN failure was not reproduced, so its root cause remains inconclusive.** The deterministic guest HTTPS scenarios were subsequently run and their expected assertions passed. No transport correction was justified by the captured evidence.

## Scope and source identity

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Host: `LEON2_RDT`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `09ac98437f4290b158162314f78ffc4d5ea7c4e8`
- Source fingerprint used by the guest qualification manifest: `107f09aa43ec2b42450aa98f5366823517f47440ae91edab955339cecb1cc795`
- Hardware tracker preserved at `last_completed=PHASE26`, `next_expected=PHASE27`.

The worktree was clean at preflight. No branch switch, reset, rebase, merge, stash, clean, `.phase` creation, or unrelated-repository modification was made. The hosted UI smoke's `desktop.json` recent-item side effect was restored to its preflight-clean state.

## Original failure and end-to-end trace

The accepted NAVHTTPS5 Test A evidence recorded the generic request to `https://guidexos.test:8443/navigator-smoke/tls-basic.html`, DNS result `10.0.2.2`, and `TcpConnectFailed / network down`. The serial log showed the first TCP socket being closed before a `[TCP] Connecting` line. The corresponding pcap contains no SYN for the first attempted source port (49152), while later connections to the same QEMU gateway and fixture port succeeded. The pcap also contains ARP requests and replies for `10.0.2.2`. This establishes that the earlier attempt did not produce an observed SYN, but it does not identify why.

Source tracing establishes this order:

1. `kernel_http_open_stream()` resolves the host, creates a TCP socket, increments the TLS TCP-attempt count, and calls `tcp_connect()`.
2. `tcp_connect()` selects its source port and addresses, enters `SYN_SENT`, constructs a SYN, and calls `send_segment()`.
3. `send_segment()` calls `ipv4::send_packet()`. The existing implementation maps all non-`IP_OK` results to `TCP_ERR_NETDOWN`.
4. `tcp_connect()` prints `[TCP] Connecting` only after the SYN send path succeeds. Thus its absence was useful evidence of a failed path, but not enough to distinguish precondition, route, neighbor, packet construction, or NIC submission failure.
5. On failure, `kernel_http_open_stream()` records `TcpConnectFailed`, closes the socket, and returns the generic network-down message.

The earliest failed operation in the historical attempt cannot be recovered from the uninstrumented log and pcap. No specific IPv4 status can be inferred from the collapsed TCP status.

## Failing and successful path comparison

The generic local request resolves `guidexos.test` to `10.0.2.2:8443`. The QEMU guest is `10.0.2.15`, with user-mode gateway `10.0.2.2`; the host HTTPS fixture listens on `0.0.0.0:8443`. The controlled production-policy hostname `prod.guidexos.test` also resolves to `10.0.2.2:8443`, and its HTTPS requests succeed. The guest is not treating `127.0.0.1` as the host endpoint.

In the new instrumented `production_validated` runs, the generic local request again resolved to `10.0.2.2`, reported one TLS TCP connect attempt, and reached TLS certificate verification. It returned `CertificateVerifyFailed` because this local smoke certificate is not trusted by the production root store in that scenario. The `[TCP] Connecting` log follows successful IPv4 submission in `tcp_connect()`. No `[TCP-CONNECT-DIAG]` line was emitted, which means the pre-SYN TCP send failure did not recur; it does not identify the historical error.

The controlled policy HTTPS path differs in trust and hostname policy, not in the QEMU gateway or listener port. Its test certificate is validated against the staged deterministic production-source test root and its hostname. This explains its TLS result, but does not explain the historical TCP failure.

**Root-cause classification: INCONCLUSIVE.** The four earlier pre-SYN observations were not instrumented at the failing stage. The instrumented reruns did not reproduce `TcpConnectFailed`; therefore no A–E cause is selected and no transport behavior change is made.

## Implementation

- Added a fixed-size IPv4 send diagnostic record with an attempt ID, source/destination/next-hop, stage, existing IPv4 status, configuration and NIC/link readiness, NIC result, and bounded ARP counters/outcome.
- Instrumented configuration checks, packet validation/construction, route selection, neighbor resolution, Ethernet construction, NIC submission, and successful completion. Existing status codes and send behavior remain unchanged.
- Added a smoke-build-only TCP connect failure line with the tuple, whether SYN construction and IPv4 submission were reached, IPv4 stage/status, next hop, neighbor/ARP outcome, and network readiness. It logs only on a failed connect; it does not log payloads, allocate dynamically, or retry.
- Added focused hosted regression coverage for IPv4 stage/status attribution and TCP failed-SYN cleanup, socket reuse, pending-SYN close, and bounds.
- No TLS verification, route/readiness gate, timeout, DNS validation, retry behavior, production trust, or physical NIC safety gate was changed.

## Hosted qualification

| Qualification | Result |
|---|---|
| IPv4 attribution and TCP cleanup smoke | PASS: configuration, packet validation, route, neighbor timeout, NIC submission; failed SYN cleanup, pending-SYN close, socket bounds |
| DNS protocol checks and controlled fixture checks | PASS: 13 protocol scenarios and 6 fixture checks |
| UDP wire bounds | PASS: 6/6 |
| Navigator URL resolution | PASS: 11 cases |
| HTTPS qualification contract | PASS: 7 cases |
| Secure-random contract | PASS |
| Mbed TLS profile | PASS: Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, 3 patches |
| Hosted Navigator UI smoke | PASS: 287/287; `NAVIGATOR_SMOKE_RESULT: PASS` |
| Freestanding AMD64 kernel build | PASS for each staged QEMU qualification image |

The hosted Navigator log is `logs/navigator-hosted-smoke-20261108-075455.log`, SHA-256 `F5F8B6B9081B2CCE051EAF1AB740AB9D0FBC1D60023D4746BDF2F0077F5D8802`.

The focused hosted compile also printed an existing warning that the ephemeral-port check `ephemeral > 65535` is always false for its integer type. This comparison was not changed as part of NAVHTTPS5R2.

## QEMU guest qualification

Runs were serialized through the existing QAC runner. Two later attempts were safely stopped before guest start: one on a transient port-owner report for PID 0, and one while another repository's QEMU process owned the machine. Neither check was bypassed. The affected scenario was retried after the owner exited.

| Scenario | Guest result and evidence |
|---|---|
| `production_validated` | PASS. The qualification artifact reports all 4 required cases executed and passed: valid, redirect, wrong-host, and downgrade. Generic local probe reached TCP and certificate verification; it did not reproduce the pre-SYN failure. |
| `production_expired_cert` | PASS. Certificate verification rejected the expired certificate with flag `1`. |
| `smoke_future_cert` | PASS. Certificate verification rejected the future-dated certificate with flag `512`. |
| `production_clock_unavailable` | PASS as a runner scenario. The production HTTPS case was disabled and policy-blocked with `tls_tcp_connect_attempts=0`, `https.case.policy_validated.result=PASS`. Its wrapper reports qualification `NOT_RUN`, `required_executed=0`, and four skipped cases. This is a clock-policy gate check, not four executed HTTPS passes. The separate local-only smoke reports `ClockUnavailable`; the scenario's negative smoke marker is expected. |

The final clock scenario passed on retry. After each completed run, guideXOS QEMU and fixture processes exited and the QAC runner released its reservation. At final inspection no NAVHTTPS5R2 QEMU or fixture process remained.

## Artifacts and hashes

Primary artifacts are under `E:\CodexArtifacts\NAVHTTPS5R2`:

- `guest-baseline-complete\20261108-081312-65e3e0c4\logs\navigator-kernel-smoke-20261108-081312-production_validated.serial.log` — SHA-256 `BE666AB273F3C4C941BBE60C68D1EFF532F7D4BFAD523ECBA21C27809E7C3264`.
- `guest-remaining\20261108-081921-d5339a95\logs\navigator-kernel-smoke-20261108-081921-production_expired_cert.serial.log` — SHA-256 `C63C2D85C49FA4DDE7A1694DC24EE4F67E1AC97DA3D5F3891FFEE19BAEA02C8D`.
- `guest-remaining\20261108-081921-d5339a95\logs\navigator-kernel-smoke-20261108-081921-smoke_future_cert.serial.log` — SHA-256 `43887BD034D30FEFEA946248AE69ECF9B722DF9B31BBBF7A9E5D079CBBEE034D`.
- `guest-clock-retry\20261108-083506-96712c8f\logs\navigator-kernel-smoke-20261108-083506-production_clock_unavailable.serial.log` — SHA-256 `45B14485D6E3633D674CA63D0B7CF0851C07F110C5BE0648E4C6E87E13FD007F`.
- Production-validated qualification manifest: `guest-baseline-complete\20261108-081312-65e3e0c4\navigator-https-qualification.json`, SHA-256 `D4C274BF9D1D4CC50D3776E5A03566CABF69A985E587B6880B1D120EA7841F1F`.
- Kernel ELF SHA-256 `3dd1649658e066bb157543ee3a46432e95550c9c241dc7f97c439e91293354a8`; bootloader SHA-256 `ebd8345a607eb84d01fb42e75059e8fb8b8c65e3ac0dd71feb8477382fd00e25`; production-validated ramdisk SHA-256 `c08c31b0dc01d3472034d7862e04d737d09fb205608ef08e66c9fdc1ff8f54b9`.
- Staged trust bundle SHA-256 `c28c679d134e640c21cb739b617034e996d48a535b97499ffa667e832711e606`; manifest bundle type `production-source`, one root, `production_ready=no`; policy SHA-256 `8d65939cc5d09cd9832dc5aed25ae8524058d61a71c9c0c697c61cd6665f50f4`.

Historical NAVHTTPS5 evidence was preserved: Test A serial log SHA-256 `C972C8337DD8331F8FC87FFE7226E5EC97EE86C954944983ED7C91F5E4DD1C9D`; pcap `E:\CodexArtifacts\NAVHTTPS5R\test-a.pcap` SHA-256 `6FA072BEE7049682C6B98708E076A9433870730B25E2CC631D88D71592B7FB23`.

## Security boundaries and next milestone

Public browsing remains disabled. The staged CA candidate remains `production_ready=no`; trust remains fixture-bounded, and the current clock classification remains `Plausible` outside the explicit unavailable-clock test. No CRL or OCSP support is claimed. The I219-LM DMA safety gate and Phase 27 tracker were not touched.

Recommended next milestone: **NAVHTTPS5R3 — reproduce the intermittent generic local TCP connect failure with the new stage diagnostic and the matching QEMU packet capture, then make a narrowly scoped correction only if that run identifies a concrete defect.**
