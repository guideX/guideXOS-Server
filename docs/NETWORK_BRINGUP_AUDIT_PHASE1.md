# guideXOS Navigator General Improvements

## Phase 1: bare-metal network bring-up audit

This is the implementation/audit record for the bounded NIC diagnostic pass. It
does not claim that the reported laptop has working Ethernet; physical
confirmation remains required.

## Repository baseline

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `55b11047237067d04374a66dc12ccd94bb34dcc4`
- Upstream: `origin/NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting ahead/behind: `0/0`
- Starting worktree: clean; no staged, unstaged, or untracked files.
- Ending HEAD: `55b11047237067d04374a66dc12ccd94bb34dcc4` (no commit made)
- Ending ahead/behind: `0/0`
- Ending worktree: only the intended Phase 1 modified and untracked files;
  no staged files and no generated test executable.

## Files changed

- Bootloader PCI inventory and NIC handoff: `guideXOSBootLoader/pci.cpp`,
  `guideXOSBootLoader/pci.h`, `guideXOSBootLoader/main.cpp`,
  `guideXOSBootLoader/guidexOSBootInfo.h`.
- Kernel NIC, Ethernet, ARP/IPv4, boot initialization, shell, and desktop
  status surfaces: `kernel/core/nic.cpp`, `kernel/core/include/kernel/nic.h`,
  `kernel/core/ethernet.cpp`, `kernel/core/ipv4.cpp`,
  `kernel/core/include/kernel/ipv4.h`, `kernel/core/main.cpp`,
  `kernel/core/shell.cpp`, `kernel/core/desktop.cpp`.
- New status helper and focused test: `kernel/core/include/kernel/network_status.h`
  and `tests/network_diagnostics_test.cpp`.
- This audit and checklist: `docs/NETWORK_BRINGUP_AUDIT_PHASE1.md`.

## Current production architecture

1. `guideXOSBootLoader/pci.cpp` scans PCI buses 0-7 for base class `0x02`
   network controllers. The Phase 1 inventory includes all network subclasses,
   including wireless-class devices, and prints vendor/device, subsystem,
   class, programming interface, revision, IRQ, and BAR information.
2. The bootloader selects the first supported Ethernet device and maps its
   memory BAR into `guideXOS::BootInfo::Nic`.
3. `kernel/core/nic.cpp` consumes that handoff and initializes the legacy Intel
   E1000 descriptor-ring path. The supported IDs are `8086:100E`,
   `8086:10D3`, and `8086:153A`. The fallback kernel PCI scan is also limited
   to the first eight buses and binds only those exact Ethernet IDs.
4. Ethernet framing is in `kernel/core/ethernet.cpp`. ARP is currently inside
   `kernel/core/ipv4.cpp`, using a 16-entry cache and synchronous resolution.
5. IPv4 routing adds a local route and a default route when a gateway is
   configured. UDP, TCP, ICMP, DHCP, and DNS sit above IPv4.
6. `kernel/core/main.cpp` registers the NIC IRQ, initializes the IP/transport
   layers, installs the QEMU-compatible static defaults, and then attempts
   DHCP. The main loop drains RX through bounded calls to
   `ipv4::poll_network()`; the NIC interrupt acknowledges events and updates
   link state.
7. DHCP is the existing synchronous RFC 2131 state machine in
   `kernel/core/dhcp.cpp`. Its states, lease fields, and counters are now
   included in `netdiag`/`nicinfo` output.
8. DNS uses the configured IPv4 DNS address and already had query/cache
   diagnostics. Navigator’s bare-metal network clients use the kernel socket
   path; hosted Navigator uses its hosted HTTP/TLS path.
9. USB ECM/RNDIS/USB-Wi-Fi backends exist in `kernel/core/usb_net*`, but they
   are not currently connected to the IPv4 polling path used by Navigator.

## Exact meaning of `Connected`

Before this phase, the Network Adapters dialog displayed a hardcoded Intel
row with status `Connected`; it did not query hardware, carrier, DHCP, IPv4,
gateway, DNS, or Internet reachability. That false positive has been removed.
The dialog now reports live local states such as `Driver unavailable`,
`Disconnected`, `Acquiring address`, `IPv4 configured`, and `Local network
configured`. Its Status button directs the tester to `netdiag`.

The taskbar widget still has intentionally limited semantics: its provider is
present when an active PCI NIC or USB network device exists, and its link flag
is carrier/link state. Green means link plus the IPv4 `configured` flag; it is
not an Internet connectivity test. The IPv4 `configured` flag itself is also
not proof of DHCP: boot currently installs QEMU defaults before DHCP and keeps
those static values if DHCP fails. `netdiag` exposes the DHCP state alongside
the resulting IPv4/gateway/DNS values so this distinction is visible.

## Supported Ethernet devices

The production driver intentionally matches exact PCI vendor/device pairs:

| PCI ID | Current binding |
| --- | --- |
| `8086:100E` | Intel E1000 / 82540EM path; QEMU default |
| `8086:10D3` | Intel E1000E / 82574L path |
| `8086:153A` | Intel I217-LM path |

There is no generic vendor-only fallback. Unsupported network controllers are
enumerated by the bootloader for identity purposes and explicitly printed as
`unsupported (identity only; no binding)`. BAR sizing is not performed on an
unsupported controller during this inventory pass.

## Diagnostics added

The existing shell diagnostic surface was extended rather than creating a new
logging framework.

- `netdiag` is an alias for the expanded `nicinfo` command.
- `nicinfo`/`netdiag` reports PCI location, vendor/device, subsystem IDs,
  revision, class/subclass/ProgIF, driver name, MAC, MMIO virtual/physical
  address and size, IRQ registration, and main-loop polling mode.
- It reports driver-ready state, PHY link state, and explicitly says when
  negotiated speed/duplex is not exposed by the current E1000 status path.
- NIC counters now distinguish TX attempted/completed/errors/drops and RX
  observed/accepted/errors/malformed/drops, plus interrupts.
- IPv4/ARP counters report ARP requests sent, replies received, and malformed
  ARP frames dropped.
- DHCP state and counters report discover/offer/request/ACK/NAK/timeout
  progress, lease application, and lease lifetime where available.
- IPv4 address, mask, gateway, and DNS values are shown without implying that
  those values came from DHCP.
- The fake network rows in the shell `lspci` output were removed; it now shows
  the kernel-bound controller and directs the tester to the pre-kernel UEFI
  network inventory for unsupported/Wi-Fi devices.
- The MAC formatter defect that emitted only `A` and `B` for hexadecimal
  nibbles A-F was corrected.

## Wi-Fi discovery and future architecture

The UEFI PCI inventory now reports every base-class `0x02` controller found in
its bounded bus scan, including a wireless controller with no driver. The
tester can capture its PCI vendor/device, subsystem IDs, class, and explicit
unsupported status before kernel handoff. No Wi-Fi association or chipset
driver was added in this phase.

The recommended future sequence is: identify the actual laptop controller,
select one supported chipset, implement one end-to-end path, then generalize.
The minimal architecture should be:

`PCI/USB device -> chipset driver -> wireless interface manager -> existing
Ethernet-frame/IP handoff -> DHCP/DNS/TCP -> shell/status provider`

The wireless interface manager needs bounded enumeration and interface
up/down state; scanning with SSID/BSSID, signal, channel/frequency, and
security metadata; association/authentication; WPA2-Personal credential
handoff using a protected/short-lived credential interface; disconnect and
reconnect; frame delivery to the existing network stack; DHCP after
association; and shell-visible state. WPA3, enterprise authentication,
roaming, powersave, and broad multi-chipset support remain out of scope.

## Defects found and repairs

- Hardcoded desktop `Connected` status: replaced with live, local-state
  reporting.
- Ethernet MAC display corruption for nibbles C-F: fixed.
- DHCP initialization was not called before the boot-time discovery attempt;
  the boot path now resets DHCP state/counters and seeds its transaction source
  from the active MAC.
- Unsupported PCI network devices previously were filtered out by the
  bootloader’s Ethernet-only class filter; they are now reported without
  binding. Unsupported BARs are not probed by destructive size writes.
- A detected-but-not-initialized NIC could be queried through an invalid MMIO
  address; link refresh now returns cached state until the device is active and
  mapped.

No Ethernet hardware compatibility claim or device ID was added. No Wi-Fi
driver was implemented. DHCP lease renewal remains an existing follow-up
item: the current kernel does not advance the DHCP lease tick/call
`check_renewal()` from the main loop.

## Verification status

- Focused hosted test: `tests/network_diagnostics_test.cpp` passed. It covers
  all connection-state classifications, exact supported-ID matching, and MAC
  formatting.
- Existing hosted Navigator smoke passed:
  `scripts/smoke-navigator-hosted.ps1`. Its HTTP, HTTPS/TLS, navigation,
  form, resource, and Navigator lifecycle assertions completed with
  `NAVIGATOR_SMOKE_RESULT: PASS`.
- Existing focused hosted regressions passed:
  `scripts/smoke-navigator-url-resolution.ps1`,
  `scripts/smoke-navigator-resource-diagnostics.ps1`,
  `scripts/smoke-navigator-resource-scheduler.ps1`, and
  `scripts/smoke-secure-random-contract.ps1`.
- Modified x86 kernel units compiled successfully, including `nic.cpp`,
  `ipv4.cpp`, `ethernet.cpp`, `main.cpp`, `shell.cpp`, and `desktop.cpp`.
- Full x86 kernel make was attempted with the available cross compiler. It did
  not complete because the existing freestanding toolchain profile cannot find
  `<string.h>` in unrelated pre-existing units such as `framebuffer.cpp`,
  `image_adapter.cpp`, `kernel_apps.cpp`, `native_elf_baremetal.cpp`, and the
  pinned Mbed TLS profile. This is a build-environment/profile blocker, not a
  Phase 1 source error.
- QEMU was not available on the development machine, so a fresh QEMU boot was
  not run.
- Actual bare-metal verification was not run and must not be inferred from
  hosted compilation.

## Next laptop boot checklist

Capture the UEFI `=== Network hardware capture ===` block first. Then, once the
shell is available, run `netdiag` and transcribe the bounded output in this
shape:

```text
NIC:
PCI <bus>:<device>.<function>
vendor/device: <vendor>:<device>
subsystem: <subvendor>:<subdevice>
class: <class>/<subclass>  progif: <progif>  rev: <revision>
driver: <name or unsupported>
MMIO: mapped/unmapped, virt=<address>, phys=<address>, size=<size>
IRQ: registered/not registered; RX/TX mode: main-loop polling
MAC: <address>
driver ready: <yes/no>
link: <up/down>
speed/duplex: <value or not exposed>
tx attempted/completed/errors/dropped: <...>
rx observed/accepted/errors/malformed/dropped: <...>
ARP requests/replies/malformed: <...>
DHCP state: <INIT/SELECTING/REQUESTING/BOUND/ERROR/...>
DHCP discover/offer/request/ack/nak/timeout: <...>
IPv4: <address or not configured>
mask: <mask>
gateway: <address or none>
DNS: <address or none>

Wi-Fi / other PCI network controllers:
PCI <bus>:<device>.<function>  <vendor>:<device>
subsystem: <subvendor>:<subdevice>  class: <class>/<subclass>
driver: unsupported / <name>
```

Interpretation: TX attempted with no completions points at the TX/DMA/device
completion path; RX observed with no accepted frames points at RX validation or
stack dispatch; no ARP replies points at link, TX, RX, or local-network reach;
DISCOVER with no OFFER points at broadcast/receive/DHCP; an OFFER with no ACK
points at DHCP request/response handling; a lease with failed higher-level use
points next at ARP, routing, DNS, or transport. These are hypotheses for the
next test, not conclusions from counters alone.

## Outcome

**Outcome C — Partial progress / blocker identified.** The diagnostic
foundation is implemented in source and the focused hosted test plus modified
kernel compilation units pass, but the full freestanding build is blocked by
pre-existing missing `<string.h>` toolchain headers in unrelated units, QEMU is
not installed on this workstation, and the updated UEFI bootloader was not
built here. The smallest next action is to restore the documented freestanding
libc/toolchain inputs, build the bootloader and image, run the QEMU smoke boot,
then perform one physical laptop boot using the checklist above. The laptop
Ethernet issue is not called solved.
