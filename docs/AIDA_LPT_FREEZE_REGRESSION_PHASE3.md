# AIDA_LPT Freeze Regression — Phase 3

## Scope and repository evidence

This phase treats the AIDA_LPT result as a software regression. The same
laptop was immediately verified with an older guideXOS image: the desktop
remained alive, mouse and keyboard input worked, and the Start menu opened.
The Phase 2 image booted through UEFI, rendered the desktop, taskbar, icons,
and the `Welcome to guideXOS / System started successfully` notification, then
became non-responsive. The result was reproduced with the Ethernet cable
connected and disconnected. No AIDA_LPT PCI or USB diagnostic output was
captured, so no hardware-controller identity is assumed here.

The audited Phase 2 range is:

```text
2b02ff1bee2b2bfe82abc940bde34cc379b04c30..ce1b1479e9c39b086b16d5b2539ebeacfb64dc70
```

It changed the UEFI formatter, PCI formatting, UEFI network capture, kernel
read-only network inventory, NIC diagnostic structures/counters, shell
`netdiag`/`lspci`/`lsusb`, and descriptor status volatility. The shell PCI/USB
inventory calls are interactive commands; they are not called during boot.
The automatic DHCP path was already present before that exact Phase 2 range,
but Phase 2's successful NIC handoff makes that older startup hazard relevant
on a physical machine.

The checkout also contained one later local commit,
`df06d74335b665e1caeefdd0c60dd07cf3a67b24`, which attempted an unverified
I219-LM implementation. Phase 3 does not add or enable that support. The
effective Ethernet match remains exactly `8086:100E`, `8086:10D3`, and
`8086:153A`; `8086:156F` is identity-only until real AIDA_LPT evidence exists.

## Root cause and repairs

The strongest source-and-runtime explanation is a late-startup starvation
window:

1. `desktop::draw()` completes before network startup.
2. NIC initialization and the synchronous `dhcp::discover()` run next.
3. DHCP retries use a busy polling receive path and do not service input.
4. PS/2 and input-manager initialization occur only after DHCP returns.

The Phase 2 serial capture records those exact boundaries: desktop drawn at
line 218, NIC initialization at lines 303–313, DHCP at lines 326–353, and PS/2
plus input initialization at lines 354–381. With no cable there is no DHCP
server response, so the same path is expected to remain in its retry/poll
window; no Ethernet traffic is required to trigger it. This explains the
rendered desktop and startup notification followed by dead input without
classifying the laptop's HID hardware as unsupported. The older image remains
a same-machine control, but its exact repository SHA is not proven.

Phase 3 makes the following permanent safety changes:

- Boot DHCP discovery is deferred until the existing `dhcp /discover` shell
  command is explicitly requested after input and the main loop are ready.
  Static IPv4 defaults and all DHCP status/counters remain available.
- NIC interrupt causes remain masked until after `interrupts::register_irq()`
  installs the NIC handler. This closes the real-hardware window in which a
  device could assert an unhandled IRQ.
- `nic::get_link_state()` returns the cached state. Desktop redraw and shell
  status queries no longer perform MMIO or a possible multi-transaction MDIC
  poll. Link-change handling remains in the NIC event path.
- UEFI BAR discovery is read-only. The former all-ones BAR sizing writes were
  removed. The approved E1000-family path maps the bounded `0x6000` register
  window required by the kernel; PCI command writes occur only in the
  explicit later NIC bind, not in diagnostic enumeration.

The descriptor `volatile` repair from Phase 2 is retained. It fixed a real
QEMU TX-completion visibility problem and is not being reverted as a blind
freeze workaround.

## Candidates reviewed

| Candidate | Result |
| --- | --- |
| Ethernet carrier, cable, or traffic | Eliminated as a requirement by the disconnected-cable reproduction. |
| Generic unsupported HID hardware | Not supported by the same-laptop older-image control; the boot order shows input is initialized after the network wait. |
| Kernel PCI inventory or `lspci` | Not a boot path; the new scan is reached only from an interactive shell command. |
| USB/`lsusb` enumeration | Not a boot path; the Phase 2 shell implementation reads existing USB state and does not reset or claim devices. |
| Repeated diagnostic collection | No startup caller was found for the Phase 2 PCI/USB inventory functions. |
| Phase 2 descriptor volatility | Retained; QEMU evidence shows it removes false TX timeouts. |
| Synchronous DHCP before input | Primary repair target; it executes after desktop rendering and before PS/2/input initialization. |
| NIC interrupt enable ordering | Independent physical-hardware hazard; repaired by deferring NIC causes until handler registration. |
| Live link MMIO/MDIC in redraw | Independent unsafe status-query behavior; repaired by using cached state. |
| UEFI BAR sizing writes | Unsafe pre-EBS side effect; removed even though it cannot alone prove the later timing window. |
| I219-specific driver work | Out of scope and unverified; disabled from effective matching. |

## Checkpoints

The kernel emits these serial checkpoints for the next physical test:

```text
[AIDA-PHASE3] checkpoint=network-ready
[AIDA-PHASE3] checkpoint=input-ready
[AIDA-PHASE3] checkpoint=main-loop-ready
```

The first proves networking finished without boot DHCP; the second proves the
input manager initialized; the third proves the normal event loop was entered.
They are intentionally small diagnostic markers, not a new persistent debug
architecture.

## Scope preserved

No new Ethernet or Wi-Fi chipset support was added. UEFI capture retains PCI
vendor/device/subsystem/revision/class information. `netdiag`, `ipconfig /all`,
`lspci`, and `lsusb` remain compiled and available. Network status continues
to distinguish no adapter, adapter detected, driver unavailable,
disconnected, acquiring configuration, IPv4 configured, local network
configured, and verified online.

QEMU and hosted validation remain virtual/hosted evidence only. Bare-metal
repair confidence requires the AIDA_LPT retest.
