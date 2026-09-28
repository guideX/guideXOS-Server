# Settings Center Phase S2: Network State Boundary

## Result

This change adds a bounded network snapshot contract, a kernel-side provider,
and a Settings Network page that renders provider data without reading the host
machine's NIC configuration. It does **not** complete the hosted-to-kernel
bridge: the current hosted Settings process and bare-metal kernel run in
separate environments, and this repository has no IPC/service transport
between them. The hosted Settings page therefore correctly reports kernel
network state as unavailable. Under the phase acceptance definitions, this is
Outcome C because Settings cannot yet obtain authoritative state. No IPv4 or
DHCP controls are presented as if they had applied.

## Ownership audit

| Concern | Existing owner | Finding |
| --- | --- | --- |
| NIC discovery and identity | `kernel/core/nic.cpp`, `kernel/core/include/kernel/nic.h` | PCI probing owns the supported E1000-family device and its PCI identity/name. The current driver stores one `NICDevice` and selects one supported adapter. Probe state now distinguishes uninitialized, no supported device, unavailable device, ready, and unsupported architecture. |
| Link state | NIC driver | The provider copies the driver's link state. Link-up alone is not displayed as connected. |
| DHCP | `kernel/core/dhcp.cpp`, `kernel/core/include/kernel/dhcp.h` | The client owns lifecycle and lease details. Boot calls synchronous `discover()`; on failure, boot retains its IPv4 fallback. DHCP's state and lease are copied into the snapshot. |
| IPv4 and routes | `kernel/core/ipv4.cpp`, `kernel/core/include/kernel/ipv4.h` | `NetworkConfig` owns the active address, mask, gateway, and one DNS value. `configure()` writes fields and routes directly and has no staged commit, failure result, or rollback. |
| DNS source | DHCP lease / IPv4 config | A live DHCP lease identifies DHCP-provided DNS. The current IPv4 config does not retain whether a non-DHCP DNS value was manually selected, so that source stays Unknown. Only one DNS address is represented. |
| Persistence | Kernel networking initialization | No persistent network configuration store was found. The boot-time QEMU fallback is runtime initialization, not saved user configuration. |
| Hosted networking | Hosted socket telemetry | This measures sockets belonging to the hosted server process. It is not guideXOS kernel NIC state and is shown separately. The hosted Settings provider never queries Windows/Linux adapters. |
| Network Center | Bare-metal Control Panel/desktop paths | The bare-metal adapter/configuration UI is separate from hosted Settings. Its configuration dialog currently reports a successful apply without calling the IPv4/DHCP configuration path; this phase does not treat that dialog as an authoritative service. The hosted Settings advanced action opens the available Console diagnostic tool because no hosted Network Center launch target exists. |

## Interface and trust boundary

`network_settings_contract.h` defines a versioned, fixed-capacity snapshot
(four interfaces), copied strings and values, explicit availability flags, and
a generation value. It exposes no kernel-owned pointers. The kernel provider in
`kernel/core/network_settings_provider.cpp` reads the existing NIC, DHCP, and
IPv4 owners; it does not implement those systems again. Its `appModelProvider()`
adapter returns the same provider contract used by Settings and deterministic
tests. The kernel currently supplies at most one interface because its NIC
owner exposes one device; the shared representation remains bounded for future
multi-adapter support.

The hosted service in `settings_network_service.h` returns Unavailable and does
not synthesize data from host adapters or socket telemetry. The Settings page
uses this provider interface, so its current display is truthful, but there is
no transport that lets the hosted page call `appModelProvider()` in the
separate kernel runtime. Hosted mock providers exercise the common contract in
tests.

The status/configuration service is not registered in the public Native Host
call table or SDK. The existing manifest permission list does not provide a
privileged built-in identity for networking. The internal helper rejects a
request classified as an ordinary app before reaching a provider. That
classification is not an authentication credential: once a runtime transport
exists, it must derive Settings authority from trusted App Model metadata
rather than a caller-chosen enum. Configuration requests from Settings return
Unsupported because the lower-level API cannot guarantee a staged, atomic
update. The IPv4 validator can validate a complete candidate (including mask
contiguity and gateway subnet) without mutating active state, but no Apply path
is exposed until the kernel owner can accept that candidate safely and report
the result.

## Settings behavior

- All twelve S1 categories, category navigation, Display behavior, Control
  Panel routing, and shell/recent-app integration remain in place.
- Network adapter rows and IPv4 details render from the bounded snapshot,
  including Link up/no lease, DHCP lifecycle, and unknown DNS source.
- Hosted socket telemetry remains separately labeled as hosted.
- The Network page refreshes immediately on entry/focus and every two seconds
  while visible and focused. It stops on category change, focus loss, or close,
  and repaints only when the snapshot changes.
- Search includes network, ethernet, adapter, IPv4/IP address, DHCP, DNS,
  gateway, and subnet terms. `settings://network`, `/ipv4`, and `/dns` remain
  internal routes.
- The shared contract provides generation-checked adapter lookup for operations
  that need a current interface handle. The page bounds selection to the latest
  snapshot and clamps selection after refresh. Snapshots exceeding the fixed
  capacity are clamped and marked truncated.

## Verification and remaining work

The focused Settings model suite passed **53/53** checks and the network
contract suite passed **38/38** checks through
`scripts/run-settings-center-model-test.ps1` and
`scripts/run-network-settings-contract-test.ps1`. The AMD64 UEFI/kernel build
passed and compiles the real kernel provider. Kernel boot emits one
`[SettingsNetwork]` serial snapshot after NIC/DHCP initialization so QEMU can
verify the provider against live kernel-owned state. The existing QEMU display
smoke was attempted but stopped before launching QEMU: its prerequisite
rebuild failed while linking the Native PacMan package on undefined audio
symbols (`pacman_audio_load_resources` and `pacman_audio_submit`). A separate
serial boot attempt did not capture the provider marker. There is therefore no
QEMU runtime proof for this change, and no evidence that Settings launches in
QEMU or connects to the hosted provider.

To reach Outcome B, the App Model needs a real bounded runtime service/transport
that exposes the kernel provider to the hosted Settings process, plus runtime
proof that Settings consumes it. To reach Outcome A, that work must also add a
transactional kernel configuration operation (including DHCP mode and DNS
source/override semantics), reread active values after apply, and test failure
preservation. No physical NIC validation is implied by QEMU or hosted tests.
