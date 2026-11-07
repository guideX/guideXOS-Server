# Navigator HTTPS Guest DNS Qualification — NAVHTTPS4

**Outcome: C — hosted qualification and a real guest probe build are ready; guest DNS execution was blocked by QAC.** No guest DNS packet, DNS-to-TCP handoff, or DNS-to-HTTPS result is claimed.

## Repository and preserved state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `1f79d4381e93e1ba7e857880b2e5d540978dfc12`
- Starting worktree: clean.
- Hardware audit remains `last_completed=PHASE26`, `next_expected=PHASE27`. No `.phase` file, I219-LM code, or physical NIC settings were changed.
- No branch reset, rebase, switch, merge, stash, clean, or push was performed.

## Accepted HTTPS baseline

NAVHTTPS2 remains the accepted deterministic HTTPS baseline: **4/4 QEMU HTTPS cases passed** and **285/285 hosted Navigator checks passed**. Its controlled fixture exercised Mbed TLS chain and hostname validation, wrong-host rejection, downgrade blocking, HTTP response handling, and bounded Navigator delivery.

The QEMU HTTPS fixture hostname `prod.guidexos.test` is mapped by the smoke path to gateway `10.0.2.2` before DNS is called. That historical HTTPS result remains valid fixture evidence, but its mapping is not wire DNS. NAVHTTPS4 did not rerun those four guest cases because QAC admission was unavailable.

## DNS transport audit

The ordinary IPv4 hostname path is Navigator URL host → `kernel_http_resolve_host` → `dns::resolve` → configured resolver → ephemeral UDP socket → DNS A query → UDP/IP/NIC transmit → validated UDP response → parsed A address → TCP connect. `dns::resolve` validates the response source address and port, transaction ID, question, response flags, and packet structure. NAVHTTPS4 did not alter the resolver implementation or weaken those checks.

The smoke path has a static mapping for `prod.guidexos.test` and other controlled `.guidexos.test` names to `10.0.2.2`; that path bypasses DNS. The new guest diagnostic uses `example.com`, flushes the DNS cache, and calls `kernel::dns::resolve` directly so the test name does not pass through that mapping. If it runs, it records resolver, query ID, ephemeral source port, query/reply sizes, reply source validation, accepted reply/answer counts, and parsed address, then attempts TCP to that same address on port 443.

The hosted resolver fixture now includes a truncated response with the DNS TC flag. It verifies the resolver rejects/retries the response within its existing retry bound and returns no address. Existing wrong-source, invalid transaction ID, malformed question, and timeout checks remain in place.

## QEMU topology and controlled fixture limits

The prepared runner configuration is QEMU machine `pc`, E1000 NIC, QEMU user networking (`-netdev user,id=net0`), and the default virtual DNS forwarder. With QEMU user networking, the usual guest addresses are `10.0.2.15` (guest), `10.0.2.2` (gateway), and `10.0.2.3` (DNS forwarder). QEMU's `dns=` option changes the guest-visible virtual nameserver address; it does not install a custom answer for `prod.guidexos.test`. See the [QEMU networking documentation](https://www.qemu.org/docs/master/system/devices/net.html) and [QEMU command-line documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html).

The host's UDP/53 socket was already held by `svchost` PID 4340 during the environment audit, so a host service could not bind that port for a deterministic local DNS fixture. A controlled DNS-to-HTTPS test for `prod.guidexos.test` needs an isolated guest-reachable DNS backend or a separate network arrangement that returns the fixture's gateway address. Until that arrangement exists, the direct `example.com` DNS probe is suitable for qualifying DNS independently, not the controlled HTTPS hostname.

## Guest admission and results

The required QAC admission probe found an existing QEMU owner and did not take the reservation or launch another guest. At the runner admission point, the active process was `qemu-system-x86_64.exe` PID 11064 for `guideXOS-AMCONV2G-R4R2-candidate-b`, using an ESP under `D:\dev\guideXOSUEFI_APP_MODEL\out\amconv2g-r4r2-20261107\candidate-b\...` and `-nic none`. The process was left untouched.

The qualification runner reported `outcome=BLOCKED required_executed=0 passed=0 failed=0 blocked=4 skipped=0`. Those four required HTTPS scenarios never ran. The independent DNS probe also did not run. No serial log or `guest-dns-wire.pcap` was produced. Therefore the guest DNS query, resolver response, parsed A record, TCP handoff, and Mbed TLS/HTTP path remain **BLOCKED / NOT RUN**, not PASS.

The build is prepared to collect a QEMU filter-dump PCAP and run `scripts/verify-navigator-dns-pcap.py`, which independently checks guest-to-resolver UDP/53 traffic, matching query ID and question, matching resolver response to the guest ephemeral port, and the returned A address. Since there is no capture, the verifier has no guest evidence to report.

DNS-to-HTTPS integration remains unqualified. QAC prevented any network execution, and the existing HTTPS fixture hostname currently has a static mapping rather than a controlled guest DNS answer. The attempted design preserves the existing TLS certificate-chain and hostname checks; it does not introduce an alternate trust bundle or plaintext fallback.

## Hosted tests and build

Hosted qualification completed before QEMU admission:

- DNS resolver fixture: **11/11 scenarios passed**, including TC/truncation rejection.
- UDP/socket bounds: **6/6 passed**.
- Navigator URL resolution: **11/11 passed**.
- HTTPS qualification contract: **7/7 passed**.
- Secure-random contract, Navigator resource scheduler, and resource diagnostics: passed.
- Hosted Phase 11–27 network regression chain: passed.
- `scripts/smoke-navigator-hosted.ps1 -Build`: **285/285 passed**. The retained log is `logs/navigator-hosted-smoke-20261107-070337.log`.
- Actual AMD64 freestanding kernel build with Navigator HTTP smoke, TLS capability negative-test, text-write-guard, and guest DNS qualification compile definitions: passed.
- PowerShell syntax checks for the modified smoke scripts and Python verifier `--help`: passed.

The hosted transport tests exercise deterministic mocks. They do not establish guest packet transmission or DNS reception.

## Artifact provenance

Artifact root: `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_IMPROVEMENTS\NAVHTTPS4-20261107\20261107-071704-26a500a2`.

Build ID: `20261107-071704-26a500a2`. Attestation: `navigator-https-qualification.json`. It records the starting source HEAD and source fingerprint `7a3a960fa959911fd953369606e805ff8147a9266e31ea6c051ef85236d46f57`, plus the required compile definitions and QEMU machine/NIC/network/TLS settings.

| Artifact | SHA-256 |
| --- | --- |
| `ESP\kernel.elf` | `3cdf0fe80d47e765e0cc9146a9aab084713ee52a6100e1ea8cb189b40d7911b5` |
| `ESP\EFI\BOOT\BOOTX64.EFI` | `ebd8345a607eb84d01fb42e75059e8fb8b8c65e3ac0dd71feb8477382fd00e25` |
| `ESP\ramdisk.img` | `225429186317055681476bc4370e7885d41257b61890338f1f70e05c9cec58ce` |
| Staged CA bundle | `c28c679d134e640c21cb739b617034e996d48a535b97499ffa667e832711e606` |
| Staged HTTPS policy (`production-validated`) | `8d65939cc5d09cd9832dc5aed25ae8524058d61a71c9c0c697c61cd6665f50f4` |
| Hosted Navigator smoke log | `dfc513721e49ff519231c75084a6276e7411db8fbac63d52834b0a6a8786f875` |

The staged bundle is the existing controlled one-root fixture; its manifest records `bundle_type=production-source`, `production_ready=no`, and `test_only=no`. These values do not authorize it as a production public-root set. The NAVHTTPS3 public candidate remains unapproved and was not substituted. Its source provenance, external time assurance, and revocation checking remain separate public-trust work.

## Remaining work and NAVHTTPS5

NAVHTTPS4 closes the hosted portion and leaves guest acceptance pending QAC availability. The next bounded milestone is to reacquire exclusive QAC admission when the other owner has released it, run the already-built guest DNS probe, and retain serial plus PCAP evidence. The acceptance must show a guest UDP A query to `10.0.2.3:53`, matching query/response transaction and question, an accepted A record, and TCP to that exact address. Then, if a controlled DNS backend can answer the HTTPS fixture name, repeat the existing Mbed TLS chain/hostname, HTTP parsing, and bounded Navigator delivery checks without static host substitution. Keep public trust promotion and the PHASE27 I219-LM gate independent.

## Git outcome

- Starting HEAD: `1f79d4381e93e1ba7e857880b2e5d540978dfc12`.
- Final HEAD and local commit: recorded in the completion summary after commit.
- Push: none.
