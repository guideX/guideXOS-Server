# guideXOS Server Navigator — Intel I219-LM Bring-up Phase 9

Status: implementation complete; automated validation and the Phase 9 image
are recorded below. Physical AIDA_LPT validation of `nicinfo brief` remains
pending.

## 1. Phase 8 physical evidence carried forward

AIDA_LPT was rebooted with the Ethernet cable connected before power-on and
guideXOS startup. The desktop and Console remained usable. The visible tail
of full `nicinfo` again showed no configured IPv4 address, no gateway or DNS,
and zero ARP requests/replies/malformed frames.

The earlier Phase 8 capture reported `DHCP State: INIT`. The newer report was
described as showing `none`, but the listed line is specifically:

```text
DHCP Lease: none
```

These are different fields in the existing implementation:

- `DHCP State: INIT` comes from `dhcp::get_state()` and means the DHCP client
  is in `STATE_INIT` (“Not started”); no DISCOVER/OFFER/REQUEST/ACK exchange
  has been entered.
- `DHCP Lease: none` comes from `lease->valid == false` and means no valid
  lease is currently applied.

They can occur together. The code does not render the literal `none` as a
DHCP client state through `dhcp::state_to_string()`. This phase does not infer
why the reports differed and does not alter hardware initialization to make
them match.

Full `nicinfo` remains the detailed network report. `nicinfo brief` adds one
single DHCP state/lease line without counters, ARP statistics, IPv4 details,
or packet statistics so the two projections are unambiguous in a physical
capture.

## 2. Why the decisive Phase 8 diagnostics were inaccessible

Phase 8 already recorded the I219 initialization boundary in `NICDevice`,
including MMIO, reset, MAC, PHY, ring, registration, readiness, failure, and
cached-link state. Full `nicinfo` printed those fields before the DHCP/ARP
tail. The physical Console viewport had no usable backward scrollback, so the
early hardware lines disappeared above the visible tail. Shell output
redirection such as `nicinfo > nicinfo.txt` is not implemented either.

Console scrollback/scrollbars and shell redirection/pipes remain follow-up
usability work and are intentionally outside this phase.

## 3. `nicinfo brief` design

`nicinfo brief` is observational only. It reads the already-recorded
`NICDevice` state and cached link state; it does not scan PCI, probe MMIO,
poll MDIC, reset the adapter, initialize descriptors, acquire SWFLAG
ownership, or repeat any hardware bring-up step. The existing full output is
unchanged when `nicinfo` has no subcommand. The existing `nicstat` and
`netdiag` aliases also accept `brief`.

The device-present brief has exactly 18 logical lines, below the declared
20-line limit:

```text
NIC Hardware Summary
Device / selected driver / driver bound
PCI BDF / vendor:device
Subsystem / revision
I219 path selectors (or standard path)
MMIO mapped / probe result
Reset attempted / complete / timeout
Reset CTRL before / request / after, when captured
MAC / validity / source
PHY attempted / access / IDs / address
PHY status / negotiated mode
RX ring / TX ring
Registration / active / IRQ / interrupt mask
Current init stage / hardware-init gate
Exact last failure stage and reason
Driver Ready
Cached Link
DHCP state / lease (no counters)
```

The important distinction between `Registered` and `Driver Ready` is retained:
registration can be present while the complete readiness gate is not. A
failed or intentionally stopped stage is shown through the existing
`lastFailureStage` and bounded `lastInitFailure` fields.

## 4. Tests

`tests/network_diagnostics_test.cpp` now covers:

- `nicinfo` mode parsing for full, `brief`, case mismatch, and unknown input;
- the supported I219 driver projection;
- complete I219 readiness and non-ready PHY projection;
- registration-versus-readiness distinction;
- exact initialization-stage and PHY-access names;
- valid/invalid station MAC logic;
- cached link-state names;
- the 18-line expected brief contract and 20-line maximum.

Focused command:

```text
g++ -std=c++14 -Wall -Wextra -I kernel/core/include -I kernel/arch/amd64/include tests/network_diagnostics_test.cpp kernel/core/ethernet.cpp -o tmp/phase9-network-diagnostics-test.exe
tmp/phase9-network-diagnostics-test.exe
```

Result: PASS.

`git diff --check`: PASS.

## 5. Build and QEMU validation

The canonical AMD64 freestanding build uses the unchanged Phase 8 selectors:
`I219Phase5Stage=8`, `I219Phase6Stage=0`, and `I219Phase7Stage=4`.

The canonical build passed with:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64 -I219Phase5Stage 8 -I219Phase6Stage 0 -I219Phase7Stage 4
```

The release wrapper, focused hosted diagnostics test, `git diff --check`, and
build-wrapper self-test also passed. The final release ISO passed a fresh
bounded QEMU run: firmware, bootloader, kernel, 64 MiB ramdisk, desktop,
main-loop readiness, and emulated E1000 startup markers were all observed.
Serial evidence is in:

```text
out/release-iso/qemu-test-94f5e21884054756b1c56b294a44c022/serial.log
```

QEMU evidence is virtual-machine evidence only and is not an I219 physical
success claim. The interactive QEMU desktop was also inspected, but its
launcher did not provide a usable Console, so no `nicinfo brief` command
output is claimed from QEMU.

## 6. Phase 9 artifact

The image is uniquely named and does not overwrite the Phase 8 image.

- Filename: `guideXOS-Server-v0.1.0-phase9-aida-i219-briefdiag-amd64.iso`
- Path: `dist/guideXOS-Server-v0.1.0-phase9-aida-i219-briefdiag-amd64.iso`
- Size: 90,245,120 bytes
- SHA-256: `2fd97f49f02580e0521c5e021ffbac9f9dde6f1a6d0ac484907bf13262c50e43`
- Checksum sidecar: `dist/guideXOS-Server-v0.1.0-phase9-aida-i219-briefdiag-amd64.iso.sha256`
- Manifest: `dist/guideXOS-Server-v0.1.0-phase9-aida-i219-briefdiag-amd64.manifest.json`
- Structural result: bootable UEFI, platform `0xEF`, no-emulation media.
- Manifest source commit: `268fd103d51aef90af2f7bf0b0a15a870c484e87`.

## 7. Physical AIDA_LPT procedure

1. Put the Phase 9 ISO on a separate USB drive; retain the Phase 8 image.
2. Boot AIDA_LPT and confirm the desktop appears.
3. Confirm keyboard and mouse remain usable.
4. Open Terminal/Console.
5. Run only `nicinfo brief` first.
6. Photograph the complete one-screen output.
7. Optionally run `ipconfig /all` and `netdiag` only after the brief capture.

If `Driver Ready: YES`, Phase 10 should validate link, DHCP discover/
request/ack, IPv4 configuration, gateway, ARP, bounded TX/RX, and a ping or
equivalent network proof. If `Driver Ready: NO`, Phase 10 should modify only
the exact recorded failure boundary. If the brief is incomplete or spills
past one screen, improve observability before changing hardware initialization.

Physical AIDA_LPT validation: pending.
