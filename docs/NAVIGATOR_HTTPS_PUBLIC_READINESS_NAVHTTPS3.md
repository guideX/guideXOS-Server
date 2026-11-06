# Navigator Public HTTPS Readiness — NAVHTTPS3

**Outcome: C — DNS validation and trust-source evidence improved; real guest DNS and public-origin qualification remain pending.**

This phase hardens the existing DNS path and adds hosted protocol checks. It does not claim that the guest sent or received a DNS packet, and it does not enable general public HTTPS. The prior controlled HTTPS result remains accepted as a separate fixture qualification.

## Repository and preserved state

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `bc50211a8d4072d84a7eede8e77f79ad209e0f7d`
- Starting working tree: clean.
- Hardware audit remains `last_completed=PHASE26`, `next_expected=PHASE27`. No `.phase` file or I219-LM DMA/networking source was changed.
- No branch reset, rebase, switch, merge, stash, or clean was performed.

## NAVHTTPS2 accepted baseline

NAVHTTPS2 remains the accepted deterministic fixture result: **285/285 hosted Navigator checks passed** and **4/4 QEMU HTTPS cases executed and passed**. It proved the real Mbed TLS handshake, valid chain and hostname verification, wrong-host rejection, downgrade blocking, HTTP 200, bounded body delivery, and parsing of two Navigator blocks.

The QEMU fixture explicitly mapped `prod.guidexos.test` to gateway `10.0.2.2` under the smoke build. Its `dns_used=yes` field did not mean an on-wire DNS exchange; the reported source was `controlled-smoke-gateway-map`. The TCP/TLS/HTTP result remains valid fixture evidence, not DNS or public-origin evidence. See [NAVHTTPS2](NAVIGATOR_HTTPS_QUALIFICATION_NAVHTTPS2.md).

## Guest DNS audit and code changes

The kernel's normal IPv4 path is: Navigator URL host → `kernel_http_resolve_host` → `dns::resolve` → configured resolver → UDP socket bound to an ephemeral port → DNS A query via `udp_sendto` → UDP/IPv4/NIC transmission. The resolver polls the network, receives a UDP datagram, parses the answer, returns the A address, and the HTTP client passes that address to TCP connect.

Three other paths must be distinguished from a DNS exchange:

- Numeric IP literals return directly without DNS.
- `dns::resolve` may return an existing cache entry without sending a packet.
- In the NAVHTTPS smoke build, `prod.guidexos.test` and the controlled `.guidexos.test` fixture names map to `10.0.2.2` before the resolver is called.

QEMU user networking gives the guest a virtual DNS forwarder (normally `10.0.2.3`) and routes through its user-mode network backend. That provides a possible external-DNS path, but the existing deterministic HTTPS scenario substitutes the hostname before it reaches that resolver. The current runner does not supply a local authoritative DNS fixture on the guest-visible side. See [QEMU user networking](https://www.qemu.org/docs/master/system/devices/net.html) and [QEMU network invocation options](https://www.qemu.org/docs/master/system/invocation.html).

The existing resolver was repaired in place. It now checks that the UDP source address and port match the configured DNS server; validates the transaction ID, response flags, question name/type/class, section bounds and resource-record structure; rejects truncation, malformed trailing data, unrelated A-record owners, and oversized datagrams; and uses the PIT timeout for bounded retries. UDP parsing now checks the checksum over the UDP-declared length and rejects invalid checksums. Socket receive no longer returns a truncated datagram as if it were complete. Serial diagnostics expose source validation and accepted, malformed, unexpected-source, and oversized reply counts for a future guest run.

**NAVHTTPS3 guest evidence:** no QEMU guest was started, no QAC reservation was acquired, and no NAVHTTPS3 guest DNS packet was captured. The hosted DNS tests use deterministic mock socket/NIC boundaries and prove parser and resolver behavior only; they are not guest-network evidence. Therefore genuine guest DNS, hostname-to-address handoff in the guest, and TCP to a DNS-resolved address remain unqualified.

## Production trust assessment

The release bundle currently comes from `assets/certs/mozilla-cacert-2026-08-13.pem`, a curl CA Extract conversion of Mozilla public roots. The checked-in description records **121 roots**, **191,850 bytes**, SHA-256 `303daa9461b9617eb8e6209b272613fcf2923959ff32e9422eaaae195c55c780`, and a manual update procedure. The normal wallpaper/ramdisk generator pins this bundle, the `mozilla-2026-08-13` rotation identifier, and `production-validated` policy. This phase did not change those release defaults.

A current-source review candidate is staged at [candidate README](../assets/certs/candidates/README.md), [PEM bundle](../assets/certs/candidates/mozilla-cacert-2026-11-05.pem), and [manifest](../assets/certs/candidates/mozilla-cacert-2026-11-05.manifest). The downloaded official curl bundle matched its official checksum before staging. Its metadata is **118 roots**, **200,116 bytes**, SHA-256 `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`, `bundle_type=production-source`, `production_ready=no`, `test_only=no`, and rotation id `mozilla-2026-11-05-review`. It is not wired into the pack generator, ramdisk, or runtime trust selection.

The manifest fields have distinct meanings. `production_ready` is a profile/readiness label; `test_only` identifies fixture-only trust. Thus `no/no` means “not marked test-only, and not approved as production-ready”; it does not mean production authorization. The current smoke fixture manifest uses `no/yes`. The candidate validator accepts a bounded PEM, parses the certificates, and generates the root count, byte count, and SHA-256 manifest. Runtime checks the PEM digest against that manifest and fails closed for missing, malformed, empty, oversized, mismatched, or test-only trust material. The manifest itself is not signed, so its hash proves consistency between files, not upstream provenance. Runtime parses `root_count` and `pem_bytes` as metadata but does not compare them against a recount of the loaded bytes.

curl's [CA Extract documentation](https://curl.se/docs/caextract.html) says its PEM distribution does not carry all browser-specific trust constraints. The candidate is therefore a review/source artifact, not a production-trust promotion. The update strategy remains manual: retrieve a versioned source, verify its published checksum, validate PEM and manifest, review trust constraints and source, then update pinned package metadata deliberately. Fixture roots stay separate under `scripts/fixtures`; the normal pack does not append them to the shipped public bundle.

Bare-metal Mbed TLS requires certificate verification and sets the original hostname; no plaintext fallback is permitted. Missing trust fails closed. The guest's RTC provides only a plausible clock classification, not independently synchronized time. OCSP/CRL revocation checking is not implemented. Those are material public-trust limitations even though hostname verification, chain validation, secure entropy checks, and bounded TLS resources already exist.

## Hosted regression results

- `scripts/smoke-dns-resolution.ps1`: **10/10 resolver scenarios passed** using hosted transport mocks: valid A handoff, unexpected source, malformed question count/name, wrong transaction ID, NXDOMAIN, oversized response, bounded timeout, unrelated answer owner, and `resolve_full`.
- `scripts/smoke-udp-wire-bounds.ps1`: **6/6 cases passed** (4 UDP checksum/length cases and 2 complete-datagram socket cases).
- `scripts/smoke-navigator-url-resolution.ps1`: **11/11** passed.
- `scripts/test-navigator-https-qualification-contract.ps1`: **7/7** passed, including `NOT_RUN`, `BLOCKED`, `FAIL`, and QAC-admission classification with zero-execution results never classified as PASS.
- `scripts/smoke-secure-random-contract.ps1`: passed.
- CA bundle validator: passed on the current shipped bundle (`shipped-public`, 121 roots) and the staged review candidate (`production-source`, 118 roots).
- `scripts/smoke-navigator-hosted.ps1 -Build`: the host build completed, but its first UI smoke run failed at Navigator window creation (55 checks passed; 230 downstream checks failed because the window was absent). A clean rerun against that built binary passed **285/285 checks**. The passing run exercised hosted Schannel HTTPS, trusted `https://example.com/`, the localhost-only test certificate path, hostname rejection for `127.0.0.1`, redirects, downgrade blocking, and bounded response handling. This is hosted Schannel evidence, not guest Mbed TLS or guest DNS evidence.
- The reviewed public-target setup matrix recorded **16/16 `SETUP_BLOCKED`** results before DNS/TCP/TLS because no explicit public-root bundle was configured. Its wrapper exited 1 after the matrix summary; treat this as a blocked setup observation, not a passing suite or transport test.
- The freestanding AMD64 kernel build passed with the Navigator HTTPS smoke and text-write-guard compile flags. `kernel.elf`: **2,763,152 bytes**, SHA-256 `04b90d743500f95be97793b28ea5b92d111a3f2425aa874c98e9ec7b80b66008`. The artifact and build log were retained under the system temporary directory. UEFI loader sources were unchanged, so the loader was not rebuilt.

The hosted renderer checks are not visual-rendering qualification. NAVHTTPS2's two-block content result remains the accepted bounded-parser baseline.

## QEMU and public-origin results

NAVHTTPS3 QEMU: **not run**. Counts are `executed=0, passed=0, failed=0, blocked=1, skipped=0`; the guest DNS and public transport cases remain blocked/pending until a deterministic guest-visible DNS fixture and a reviewed production trust configuration are available. No QEMU PASS is claimed for zero execution. The NAVHTTPS2 `4/4 PASS` remains historical accepted evidence only.

Public-origin guest HTTPS: **BLOCKED / not attempted**. The reviewed public-origin harness had no explicit public-root bundle in its configured input path and refused transport setup before DNS, TCP, TLS, or HTTP. The review candidate above is deliberately `production_ready=no` and was not supplied as a production trust input. A hosted Schannel request is not equivalent to bare-metal guest DNS or Mbed TLS trust qualification.

No public policy or release default was changed. No general public browsing was enabled.

## Remaining limits and next milestone

The bounded protocol is IPv4 DNS only. Recursive CNAME resolution may issue more than one query; a future wire qualification should preserve per-query evidence. The resolver has no EDNS option in its request. The checked-in CA source is manually pinned and older than the current curl extract; current-source status alone does not establish suitability for the product's trust policy. Manifest provenance is unauthenticated, bare-metal time is not externally verified, and revocation checking is absent. Navigator remains a bounded document viewer; this result does not establish general JavaScript, full CSS, or visual browser behavior.

Next bounded milestone: add an isolated, deterministic DNS server reachable by the guest in the QEMU fixture lane. Capture the guest query and response and assert resolver destination, source port, transaction id, qname/QTYPE, response source, answer A owner/address, and NIC/UDP receive diagnostics; then prove TCP connects to exactly that A address. Add malformed, wrong-source, oversized, failure, and timeout cases at the guest boundary. Only after reviewed trust provenance, trustworthy certificate time, and DNS pass should one reviewed stable public HTTPS origin be attempted under exclusive QAC admission. Keep the default policy unchanged until that qualification succeeds; handle the separate PHASE27 I219-LM gate independently.

## Git outcome

- Starting HEAD: `bc50211a8d4072d84a7eede8e77f79ad209e0f7d`.
- Final HEAD and commit SHA: recorded in the completion summary after local commit.
- Push: none.
