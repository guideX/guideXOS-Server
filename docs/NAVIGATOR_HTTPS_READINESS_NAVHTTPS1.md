# Navigator HTTPS Readiness — NAVHTTPS1

## Repository and safety state

- Audit started on `NAVIGATOR_GENERAL_IMPROVEMENTS` at the requested HEAD
  `9466a72543aeeea3de88e687b1316d998025658c`; the worktree was clean and the
  branch was one commit ahead of its remote.
- The Phase 27 record remains open at `last_completed=PHASE26`,
  `next_expected=PHASE27`. No I219-LM DMA gate or networking code was changed,
  and no physical TX experiment was run.
- This report is the only intended repository change.

## Implemented path

`NavigatorApp::navigateTo` parses a URL and asks the kernel HTTP client for a
response. The client resolves numeric IPv4 directly or uses the kernel DNS
client for an A record, opens a kernel TCP socket, and adapts TCP reads/writes
to the shared HTTP byte-stream interface. HTTPS selects the Mbed TLS stream;
its configuration requires certificate verification and sets the original
hostname for SNI and hostname validation. HTTP/1.1 requests and bounded
responses are parsed, redirects and gzip/deflate are handled, and supported
document content is handed to Navigator's parser and resource scheduler.

The principal implementations are in `kernel/core/kernel_apps.cpp`
(`navigateTo`, URL/DNS resolution, stream open, HTTP framing, and document
delivery), `kernel/core/dns.cpp`, `kernel/core/tcp.cpp`, `kernel/core/ipv4.cpp`,
`kernel/core/nic.cpp`, and `gxos_tls_foundation.cpp`.

DNS and URL handling are IPv4-only; IPv6 URLs are explicitly rejected. HTTP
has fixed safety limits and Brotli is unsupported. Bare-metal public HTTPS is
deliberately fail-closed until a production-ready CA bundle and manifest,
secure entropy, plausible clock, and validated policy are present. The
deterministic test CA is not production-ready and cannot enable arbitrary
public origins.

## Evidence and actual blocker

The hosted smoke command
`scripts/smoke-navigator-hosted.ps1` completed with 284 checks passing and one
failure. Its local HTTPS fixture completed TLS 1.2 handshakes, and HTTPS POST,
redirect, gzip/deflate, downgrade blocking, and HTTPS image cases passed. The
localhost fixture uses the explicit smoke-only self-signed bypass, so those
local checks do not prove certificate trust.

The same hosted run fetched `https://example.com/`; its Page Info check passed
for the native Schannel stream, Windows certificate trust, and hostname
validation, with an HTML response body. One companion check failed because it
still requires the literal text `Example Domain`, which was absent from the
response. This is a stale external-content assertion, not evidence of a TLS
failure. This result is from the hosted Windows/Schannel path only; it does not
qualify kernel Mbed TLS. Log:
`logs/navigator-hosted-smoke-20261024-154327.log`.

The QEMU command
`scripts/smoke-navigator-kernel.ps1 -ReuseActiveBuild -ScenarioFilter production_validated -TimeoutSeconds 240`
returned failure. QEMU 11 booted its `8086:100e` emulated E1000; the kernel
initialized 32 RX and 64 TX descriptors and reported link up. The runner
staged its controlled CA, certificate, and `production-validated` policy, but
the reused kernel reported the root bundle missing and
`http.active_cases=skipped`. No Navigator DNS/TCP/HTTPS request reached the
fixture server. The QEMU run therefore neither proves nor disproves emulated
NIC packet transmission or kernel HTTPS. Its `PASS` kernel summary covered
only the non-network smoke; the outer runner correctly failed because the
expected network evidence was absent. Log:
`logs/navigator-kernel-smoke-20261024-154519-production_validated.serial.log`.

The Phase 27 one-shot qualification is for I219-LM identity `8086:156F`; the
QEMU device here is `8086:100e`. QEMU uses the generic E1000-family driver and
is a separate, meaningful qualification target. The test run stopped before
ordinary network traffic, so no conclusion about QEMU TX/RX can be drawn from
the physical I219 result.

The current functional boundary for arbitrary-origin bare-metal HTTPS is the
production trust prerequisite. The immediate integration-qualification
blocker is that the available `-ReuseActiveBuild` artifact was not built with
active Navigator HTTP smoke cases. The runner needs a verified active build
before its existing deterministic `production_validated` scenario can test
the Mbed TLS, TCP, and emulated NIC path. Do not treat the failed reused-build
run as a transport defect.

## Next milestone

**NAVHTTPS2 — deterministic kernel HTTPS qualification.** Make reuse fail
fast unless the kernel identifies itself as an active Navigator HTTP smoke
build; avoid deleting or overwriting the existing ignored object tree to
produce that build. Run only the existing `production_validated` QEMU scenario
against its local controlled certificate and CA. Capture evidence for guest
network setup, DNS, TCP connection, TLS certificate and hostname validation,
HTTP status/body, and delivery to Navigator. Fix only the first demonstrated
integration defect. Separately replace the hosted `example.com` body-text
predicate with a stable assertion; keep certificate validation enabled.

Estimated scope: about half to one day for the harness correction and focused
qualification if the existing stack works; longer only if QEMU identifies a
real network-layer defect. The key risk is that the generic E1000 path shares
DMA address translation checks with other NIC traffic; any failure must be
diagnosed against the emulated `8086:100e` path without relaxing the I219
safety boundary. Do not enable public HTTPS from the test CA or run a physical
I219 TX probe.

Small, test-focused implementation can begin safely. A TCP/IP, TLS, CA-store,
or browser rewrite is not indicated by this evidence.

**Exact next prompt phase:**

> PROMPT PHASE: NAVHTTPS2 — Preserve Phase 27 at `last_completed=PHASE26`,
> `next_expected=PHASE27`, and leave the I219-LM DMA safety gate unchanged.
> Make the QEMU smoke runner reject a reused kernel that skips active HTTP
> cases, using a build artifact path that preserves the existing object tree.
> Then run only `production_validated` with the existing local CA/certificate
> fixture under QEMU `e1000` user networking. Record evidence at each stage
> from network setup through Navigator document delivery, and correct only
> the first reproducible integration defect. Replace the hosted
> `example.com` literal body assertion with a stable HTTPS result assertion.
> Do not run the public pilot, disable certificate verification, change the
> Phase 27 gate, or transmit on physical I219-LM hardware. Stop when the
> deterministic QEMU fixture passes or a specific transport blocker is
> isolated.
