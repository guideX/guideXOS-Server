# Settings Network Phase S2R: Runtime Service Bridge

## Result

**Outcome A: the read-only authoritative network snapshot crossed the runtime
boundary in QEMU.** The inherited S2 snapshot/provider/UI work remains in the
repository and is extended here with a bounded, transport-neutral system
service client and dispatcher. A fresh QEMU boot used the real kernel E1000,
DHCP, and IPv4 owners. Two requests through the production `SystemServiceClient`
received generation 1, adapter `eth0`, driver `Intel 82540EM E1000`, and address
`10.0.2.15`; kernel serial markers matched both responses. The runtime caller
was the minimal production-path client, not a live hosted Settings window.
Settings uses that same client abstraction and is guarded by its hosted
App Model identity.

## Why the original S2 path could not work

Hosted guideXOS Server and the bare-metal guideXOS kernel are separate
execution environments. The Settings process cannot call the kernel provider
or read its memory. The original S2 added the fixed-capacity contract and
kernel provider but no runtime channel between those environments, so its
hosted provider correctly returned Unavailable. S2R preserves those changes
and supplies the missing channel.

The repository audit found no existing hosted-to-kernel service mechanism:

- Native Host API calls cover hosted UI, files, and build/runtime integration;
  they do not dispatch requests into the guest kernel.
- Kernel IPC, process tables, and syscall handlers operate within their own
  runtime and do not bridge the hosted server to the guest.
- Hosted socket telemetry tracks hosted App Model socket activity; it does not
  represent guest NIC state.
- COM1 was a transmit-only kernel diagnostic stream. QEMU already provides
  serial chardev support, which can carry a small bounded request and response.
- The QEMU test harness and Developer Studio debugger paths do not provide a
  production service call from a hosted application into the kernel.

## Architecture

```text
Hosted Settings UI
    │  SettingsNetworkService (checks trusted App Model process metadata)
    ▼
SystemServiceClient ── QemuCom2TcpTransport (bounded localhost socket I/O)
    │
    ▼
QEMU COM2 TCP chardev ── UART COM2 receive/transmit
    │
    ▼
SystemServiceBridge ── versioned protocol dispatcher
    │
    ▼
kernel network_settings_provider
    │
    ▼
NIC / DHCP / IPv4 runtime owners
```

Service semantics live in `system_service_protocol.h`,
`system_service_dispatcher.h`, and `system_service_client.h`. Transport details
live in `system_service_qemu_transport.h` and the kernel COM2 bridge. Settings
depends on `SystemServiceClient`, not on QEMU or UART concepts. The first and
only implemented service is `GET_NETWORK_SNAPSHOT`; the bridge cannot read
arbitrary memory, invoke arbitrary kernel functions, or execute commands.

COM2 was selected because it is a real guest-visible device, QEMU supports it
without changing kernel ownership of network state, and the UART path is
independent of the network stack being observed. Normal QEMU launches bind the
chardev to loopback TCP port 17771. The runtime smoke uses an ephemeral
loopback port. The client opens a connection per request, applies a 200 ms
deadline, and closes it after the bounded response.

## Protocol and bounds

Protocol v1 is canonical little-endian and contains no pointers or
architecture-sized fields. A request is exactly 16 bytes: magic, version,
request type, request ID, and payload byte count. `GET_NETWORK_SNAPSHOT` has no
payload. A response has a 20-byte header and at most 580 bytes of snapshot data
(600 bytes total). The snapshot contains fixed-capacity adapter rows and
fixed-size strings. There are no unbounded strings.

The client, dispatcher, and decoder validate magic, protocol version, request
type, request and response sizes, request IDs, status, adapter capacity,
enumerated values, string termination, boolean values, IPv4 masks, and unused
adapter rows. Invalid requests receive a bounded error response where the
frame permits it. Unknown request types return Unsupported. Truncated,
oversized, malformed, or mismatched responses fail closed and clear the
client's output to Unavailable. Unsupported versions are rejected. A partial
UART request is discarded after the kernel timeout; a full receive ring is
reset instead of growing without bound.

## Authorization boundary and limits

The wire request has no caller enum or caller-supplied identity. Before the
Settings wrapper invokes the client, it reads the current PID from the hosted
runtime and resolves its name and app ID through `ProcessTable`. It requires
the registered identity `settings` / `gxos.builtin.settings`, and verifies
that this built-in app is available in the hosted runtime. That process-table
record is assigned by the trusted Settings launch path, not by a Settings UI
argument or request field. The service is not added to the public Native Host
call table or SDK; ordinary App Model applications do not receive it as a
host call.

At the guest ingress, COM2 is treated as the dedicated trusted system-service
peer and the dispatcher receives that trust out of band. This is the strongest
boundary available in the current architecture, but it is not cryptographic
peer authentication: a host process that can connect directly to QEMU's
loopback COM2 port, or a physical peer wired to COM2, can send the read-only
snapshot request. The Settings client path itself enforces App Model identity;
the raw local transport endpoint does not prove a Windows process identity to
the guest. The service exposes only bounded network state, and no mutation or
general kernel access. A future externally exposed management service would
need an authenticated broker/channel before adding privileged operations.

## Kernel ownership and snapshot meaning

The kernel provider remains the authority. It reads the existing NIC, DHCP,
and IPv4 owners and returns a copied snapshot with generation. The provider
currently reports at most the one adapter supported by the NIC owner. It
preserves explicit unavailable, no-adapter, link-down, DHCP-no-lease,
initializing, acquired/failed DHCP, static/unknown configuration, IPv4, mask,
gateway, and DNS-source states where the owning runtime knows them. Unknown
values remain unavailable. An available `0.0.0.0` is rendered as that address;
it is not silently converted into “unknown.” Link-up and a configured address
do not claim Internet access.

The bridge reads a fresh provider snapshot on each request and transports its
generation. The client keeps no cross-request live cache. Each failed request
clears the output to Unavailable, so a disconnected or restarted service
cannot leave a cached snapshot presented as current. Host tests exercise N
followed by N+1 and reject malformed generations/snapshots.

## Settings and hosted telemetry

Settings Network & Internet calls `SettingsNetworkService`, which uses the
same `SystemServiceClient` tested by the runtime probe. It does not consult
host operating-system adapter state. Missing service, failed connection,
timeout, unauthorized identity, or invalid response produce a bounded result
and a truthful `guideXOS network service unavailable` view. The request path
uses a 200 ms client deadline. Refresh remains limited to the visible,
focused Network page; it stops on category change, focus loss, or close, and
unchanged snapshots do not trigger a full redraw.

Settings network configuration is read-only in this phase. DHCP, static IPv4,
DNS, route changes, and driver reset are not sent. The existing candidate IPv4
validation remains available to the model/tests, but Settings does not expose
an Apply transaction without safe kernel commit and rollback support.

Hosted socket activity remains a separate diagnostic labeled as hosted socket
telemetry. It is not used to infer the guest's adapter, link, address, or
Internet reachability.

## Failure and bridge tests

The bridge tests cover protocol round trips, bad versions and sizes, unknown
requests, capacity overflow, malformed snapshots, unauthorized dispatch,
provider success/unavailability, generation propagation, repeated requests,
timeout/disconnect, truncated responses, service restart, Settings identity
mapping/fallback, and separation from hosted telemetry. The UART ring, request
frame, request/response structures, and socket buffers are fixed capacity.

The focused regression results are:

- Settings Center model: **53/53 passed**.
- Network Settings contract: **41/41 passed** (the inherited baseline was
  38/38; three additional assertions cover truthful status and `0.0.0.0`).
- System-service bridge: **40/40 passed**.
- AMD64 UEFI/kernel Make build: **passed**.
- Changed `settings_center.cpp` syntax compile with MinGW GCC: **passed**;
  only existing warnings in `gui_protocol.h` and `desktop_config.h` were
  reported.

The full Visual Studio hosted build remains blocked by pre-existing
project-wide issues. A direct MSVC compile of both the current Settings source
and the parent-S2 source fails identically because the project include path
selects `kernel/core/include/stdlib.h` in place of the CRT header, leaving
`div_t`, `abort`, and related declarations unavailable to `<cstdlib>`.
The full build also reports pre-existing duplicate-basename output collisions
and GCC-specific kernel code errors under MSVC. These failures were reproduced
with parent-S2 Settings source, so they are not caused by S2R; no broad build
system cleanup was included.

## QEMU runtime proof

`scripts/smoke-system-service-bridge.ps1` performs a direct kernel-only AMD64
build, boots a fresh OVMF/QEMU guest with the E1000 device and COM2 chardev,
then launches `tests/system_service_runtime_client.cpp`. That probe is built
from the production `SystemServiceClient` and `QemuCom2TcpTransport` code. It
sends two requests over TCP → QEMU COM2 → guest UART →
`SystemServiceBridge` → dispatcher → real kernel provider, and validates the
responses field for field against the kernel's per-request serial snapshot,
authorization, request, and response markers. The smoke allows up to five
minutes for full kernel initialization; each hosted Settings request itself
has a 200 ms deadline. The observed result was:

```text
request 1: generation 1, backend Kernel, state AdaptersAvailable, one adapter
           eth0, Intel 82540EM E1000, link Up, mode Static,
           DHCP FailedNoLease, DNS source Unknown,
           10.0.2.15 / 255.255.255.0, gateway 10.0.2.2, DNS 10.0.2.3
request 2: same generation and provider state, returned again successfully
```

The smoke passed twice in one fresh boot with no kernel panic, firmware
re-entry, or hang. Evidence is stored under the ignored
`out/validation/system-service-smoke/` directory. This proves that
authoritative guideXOS kernel network state crossed the runtime boundary into
a production-path client. It does not claim that a live Settings window was
opened in the guest/host simultaneously, or that a physical NIC was tested.
The QEMU E1000 and DHCP path are virtual hardware/runtime evidence only.

## Remaining gap

The service bridge is read-only. Network configuration still needs an
authoritative kernel transaction that validates a candidate, commits it
atomically or rolls it back, reports the result, and updates the generation.
That work should define DHCP/static mode, DNS source/override behavior, and
failure preservation before Settings adds Apply controls.
