# guideXOS Server Navigator — Intel I219-LM Bring-up Phase 8

Status: implementation complete; AMD64 artifact and QEMU validation are recorded below; AIDA_LPT physical validation is pending.

## Scope and Phase 7 evidence

This phase continues the Phase 6 reset isolation and Phase 7 staged I219 work. Phase 7 was booted on the physical laptop `AIDA_LPT` using `guideXOS-Server-v0.1.0-phase7-aida-i219-register-amd64.iso`. The desktop remained usable, the Intel Ethernet adapter was detected, and `ipconfig /all` reported that the adapter driver was not ready. `nicinfo` produced useful state. The Phase 7 report did not preserve a complete post-boot `nicinfo` capture, so this report does not claim a physical sub-stage from that run.

The device-specific evidence carried forward is PCI `8086:156F`, subsystem `103C:8079`, revision `21`. Phase 6 and Phase 7 established that PCI discovery, BAR handoff, MMIO access, and the bounded PCH reset candidate were the current safe frontier. The permanent reset path still deliberately avoids undocumented ownership/SWFLAG/FWSM operations and speculative PHY writes.

## Initialization architecture found

The physical path is:

`PCI discovery → exact I219 candidate match → bootloader BAR/MMIO handoff → PCI command activation → bounded PCH reset → RAL0/RAH0 MAC acquisition → bounded MDIC PHY reads → static RX/TX ring setup → masked NIC registration → driver-ready publication → ipconfig`

Phase 7 intentionally exposed the logical adapter before a successful final hardware transition, so “detected” and “driver ready” could differ. The Phase 8 implementation preserves that distinction. A failed initialization retains the bound PCI identity and recorded state while leaving the adapter inactive and unregistered.

`driver ready` now means all of the following are recorded as successful:

- exact supported driver binding and mapped MMIO;
- at least one successful MMIO probe with non-`0xFFFFFFFF` CTRL/STATUS reads;
- bounded reset attempted and completed for I219;
- valid station MAC acquired from I219 RAL0/RAH0;
- bounded I219 PHY probe completed with `NIC_PHY_OK`;
- RX and TX rings initialized;
- logical NIC registration completed;
- `initStage == NIC_INIT_READY`, `active == true`, and the explicit `driverReady` publication bit is true.

The final publication order is registration, `NIC_INIT_READY`, then `active`/`driverReady`/polling. `is_active()` and all user-facing readiness projections use the complete gate. A stale ready/active value cannot make a failed initialization ready.

## Old failure boundary and Phase 8 change

The source audit found a concrete ordering gap in the Phase 7 I219 branch: after the proven reset and MAC steps, it entered MDIC PHY discovery without performing the bounded MAC link-control preparation already present in the earlier I219 path. Phase 8 factors that step into `prepare_i219_phy_access()` and invokes it immediately before Phase 7 MDIC reads. It reads CTRL, writes only `CTRL.SLU|CTRL.ASDE`, then records CTRL and STATUS readback. It does not write PHY registers, manipulate SWFLAG ownership, or add a broad PCH sequence.

This is the deepest repository-proven boundary. The Phase 7 physical result alone does not prove that AIDA_LPT stopped specifically at MDIC, because its complete runtime capture was not retained. The Phase 8 image is intended to test this justified boundary and persist the result.

All reset, MDIC, and initialization loops remain bounded. Interrupt causes remain masked throughout incomplete initialization and the I219 registration boundary. No ready flag is set as a diagnostic shortcut.

## Persistent diagnostics

`nicinfo` and its `netdiag` alias now project recorded state without rerunning hardware operations. The compact Phase 8 fields include:

- PCI vendor/device, BDF, subsystem/revision, selected driver, BAR/MMIO, and PCI command;
- MMIO probe attempted/pass;
- reset attempted/completed/timeout/poll count and CTRL before/request/after;
- MAC acquisition attempted/valid/source and raw RAL0/RAH0 values;
- PHY probe attempted/access state, PHY IDs/address/status, link and negotiated mode;
- RX/TX initialization, NIC registration, initialization stage, hardware-init gate, and driver-ready;
- last failure stage and exact bounded failure string.

These fields are recorded at the point of failure or completion. They do not depend on inaccessible early Console scrollback.

## Validation

Focused hosted test:

`g++ -std=c++14 -Wall -Wextra -I kernel/core/include -I kernel/arch/amd64/include tests/network_diagnostics_test.cpp kernel/core/ethernet.cpp -o tmp/phase8-network-diagnostics-test.exe`

Result: PASS. The test covers I219 identification, unsupported-device filtering, valid/invalid MAC logic, complete readiness gating, failed PHY projection, and registration-before-ready semantics.

Freestanding AMD64 kernel build with `GXOS_AIDA_I219_PHASE5_STAGE=8`, `GXOS_AIDA_I219_PHASE6_STAGE=0`, and `GXOS_AIDA_I219_PHASE7_STAGE=4`: PASS. Existing compiler warnings remain outside this narrow change; no new fatal compile or link error was observed.

QEMU canonical release-image test: PASS on two fresh boots using `scripts/test-release-iso.ps1 -IsoPath .\dist\guideXOS-Server-v0.1.0-phase8-aida-i219-init-amd64.iso -TimeoutSeconds 90`. Both runs observed firmware, bootloader, kernel, 64 MiB ramdisk, desktop, and kernel-main-loop markers; the emulated `e1000` initialized and the desktop remained usable. Serial logs were captured under `out/release-iso/qemu-test-4c33fd5237554ba1ad8975da518f219e/serial.log` and `out/release-iso/qemu-test-bd9651cd84124439b92104edf63651b3/serial.log`. This is virtual-machine evidence only and is not physical I219 evidence.

## Phase 8 physical-test artifact

The artifact is produced by `scripts/create-phase8-i219-iso.ps1`, using the established canonical AMD64 build and release-ISO path while retaining the Phase 7 ISO.

- Path: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase8-aida-i219-init-amd64.iso`
- Filename: `guideXOS-Server-v0.1.0-phase8-aida-i219-init-amd64.iso`
- Size: `90,245,120` bytes
- SHA-256: `cef2bad8e286e7b8bf9d24b6738d0a23d7fa5b5d17013637a007d6633443ffc8`
- Checksum sidecar: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase8-aida-i219-init-amd64.iso.sha256`
- Manifest: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase8-aida-i219-init-amd64.manifest.json`
- Manifest verification: bootable UEFI ISO, PyCdlib backend, `phase5=8`, `phase6=0`, `phase7=4 (register)`.

## AIDA_LPT acceptance procedure

1. Put the Phase 8 ISO on a separate USB drive; do not overwrite the Phase 7 ISO.
2. Boot AIDA_LPT and confirm the desktop appears normally.
3. Confirm keyboard and mouse remain usable.
4. Open Terminal/Console and run `nicinfo`.
5. Capture the complete output, including `Init Stage`, `Hardware init gate`, `MMIO probe`, `Reset`, `Reset CTRL`, `MAC acquisition`, `PHY probe`, `NIC registration`, `Driver Ready`, `Last failure stage`, and `Last init failure`.
6. Run `ipconfig /all` and record whether the adapter is still reported as driver-not-ready.
7. If useful, run `netdiag` and capture its complete output.
8. If `Driver Ready: YES`, continue with link, IP, DHCP, gateway, and bounded TX/RX testing.
9. If readiness is still `NO`, stop hardware experimentation and use the exact recorded failure stage and raw boundary values to select the next phase. Do not rely on early-console scrollback.

## Decision for the next phase

- If the driver becomes ready: validate link/IP/DHCP and then bounded TX/RX; physical success still requires those checks.
- If a precise reset, MMIO, MAC, PHY/MDIC, or DMA stage fails: preserve the complete `nicinfo`/`netdiag` capture and address only that proven boundary.
- If boot, desktop, shell, or QEMU networking regresses: treat that as a regression, retain the failure logs, and repair the regression before adding further I219-specific operations.
