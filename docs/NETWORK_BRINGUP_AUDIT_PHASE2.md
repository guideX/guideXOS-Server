# guideXOS Network Bring-Up Audit — Phase 2

Date: 2026-08-28
Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`

## Result

This phase produced a fresh AMD64 guideXOS build with the Phase 1 network diagnostics, corrected two evidence-backed runtime/build defects, added post-boot PCI/USB identity commands, and verified the shipping path in QEMU and the hosted Navigator tests.

The physical laptop has not yet been booted. The recommended physical test image and capture commands are recorded below and in the Phase 2 final report.

## Repository audit

The checkout did not match the older Phase 1 report exactly. At the start of this phase:

- repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- starting HEAD: `2b02ff1bee2b2bfe82abc940bde34cc379b04c30`
- upstream: `origin/NAVIGATOR_GENERAL_IMPROVEMENTS`
- upstream divergence: `0 ahead / 0 behind`
- worktree: clean before Phase 2 changes

Commit `2b02ff1b` already contained the Phase 1 diagnostic foundation and was a child of the Phase 1 report's `55b11047` baseline. The Phase 1 implementation was preserved.

## Freestanding `<string.h>` investigation

The reported complete-build failure was reproducible only in the x86 cross profile. The `i686-elf` compiler is GCC 15.2.0 configured `--without-headers`; its include search has GCC internal headers and `include-fixed`, but no target freestanding `string.h`. The x86 build stopped in `bitmap_font.h`/`framebuffer.cpp`, and the selected Mbed TLS/TF-PSA sources also require freestanding C headers.

The repository has no generated freestanding libc compatibility-header step or repository-owned `string.h` provider. The kernel supplies its own runtime implementations such as `memcpy`, but that does not create the declarations needed by the compiler. The AMD64 shipping path uses the documented `build.ps1`/MinGW environment and builds successfully; it does not depend on a host CRT `string.h`.

No host Windows/MSVC CRT dependency was added. The remaining x86 issue is a toolchain/profile provisioning gap: provide a target-compatible freestanding header set or a supported x86 cross-toolchain profile before claiming an x86 release build. This phase did not invent a replacement header mechanism.

During release-path verification, a separate repository defect was found: the documented and scripted release ISO tooling referenced a missing root `requirements-release.txt`. The exact pinned file was restored from repository history so the canonical release-tools venv can be recreated without global installation.

## Canonical build verification

The primary command was:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64
```

The build completed with exit code 0. It generated the PacMan/runtime ramdisk, built the MSBuild UEFI bootloader, built the AMD64 kernel and Mbed TLS/TF-PSA profile, staged `ESP`, and wrote `ESP\build-identity.txt`.

The following supporting checks also passed:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-mbedtls-profile.ps1
mingw32-make.exe -C kernel ARCH=amd64
```

The Mbed TLS verification reported Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, and three repository patches. The build emitted existing warnings but no errors.

## Bootloader PCI inventory

The bootloader compiles in the real MSBuild path and emits an identity-only network inventory before selecting the supported Ethernet driver. A fresh QEMU boot produced:

```text
=== Network hardware capture ===
Found 1 PCI network controller(s); supported Ethernet drivers: 1
  [00:03.0] Vendor=8086 Device=100e Subsystem=1af4:1100 Class=02/00 ProgIF=00 Rev=03 IRQ=11
    BAR0: Phys=0000000081040000 Size=20000 (32-bit)
    Driver: intel-e1000 family (supported)
=== End network hardware capture ===
```

The scan reads PCI identification, class, subsystem, header, and BAR information. It does not write BAR `0xffffffff` values, bind unsupported devices, or use vendor-only matching. The supported Ethernet list remains exact: `8086:100E`, `8086:10D3`, and `8086:153A`.

## Post-boot capture UX

The transient UEFI output is no longer the only identity source. The kernel now exposes a bounded read-only PCI network inventory through `lspci` and before the selected-NIC details in `nicinfo`/`netdiag`. It reports BDF, vendor/device, subsystem, class/subclass/prog-if/revision, and supported-versus-unsupported driver identity without probing or binding unsupported hardware.

`lsusb` now reports actual enumerated USB device descriptors and interfaces, including VID/PID and device/interface class, subclass, and protocol. This is the capture method for a USB Wi-Fi adapter; Wi-Fi association and a USB Wi-Fi driver remain out of scope.

## Counter and DHCP review

NIC counters are bounded `uint32_t` fields. The NIC layer is the sole owner of TX/RX event increments, avoiding Ethernet/NIC double counting:

- TX attempted increments after basic send validation; ring-full is a TX drop; descriptor completion is a TX-completed frame/byte; timeout is a TX error.
- RX observed increments once for each completed descriptor; accepted frames increment RX frame/byte counters; malformed/error/too-small or multi-descriptor cases increment the corresponding bounded drop/error/malformed counters.
- ARP request and reply counters are updated at the ARP packet boundary; malformed ARP is counted once for rejected malformed input.
- DHCP discover, offer, request, ACK, NAK, and timeout counters are updated at their protocol-event boundaries. Failed sends are visible through the bounded error counter.

The Phase 1 DHCP initialization fix remains ordered after NIC, IPv4, UDP, TCP, sockets, and DNS initialization and before the first `dhcp::discover()`. `dhcp::init()` clears the lease/stat/state once at startup and is not repeated after binding. The QEMU user-mode run sent DISCOVER packets and reached bounded retry/timeout reporting; this setup did not return a DHCP offer during the capture. Static QEMU IPv4 remained configured and the Navigator HTTP/TLS smoke passed.

One additional runtime defect was repaired: NIC RX/TX descriptor status/error bytes written by hardware are now volatile. Before this fix optimized QEMU boots could spin on a cached descriptor status and report false TX timeouts; after the fix no TX timeout appeared in the fresh QEMU run or the kernel Navigator smoke.

## Network status semantics

The live status classifier and Network Adapters UI use these meanings:

| State | Meaning |
|---|---|
| No adapter | No supported or detectable adapter is available to the status provider. |
| Adapter detected | An adapter is present but the driver is not yet ready. |
| Driver unavailable | A relevant device is present but no supported driver is bound. |
| Disconnected | A bound driver is ready but physical link is down. |
| Acquiring network configuration | Link is up but IPv4 configuration is not complete. |
| IPv4 configured | IPv4 address is present without a verified gateway/local-network result. |
| Local network configured | IPv4 and gateway are present; local reachability is not represented as Internet access. |
| Online | Reserved for an explicit connectivity-verified result; no external Internet probe was added in Phase 2. |

Physical carrier alone never claims Internet access. USB network detection is reported as detected/not detected and does not claim connectivity.

## Verification commands and results

Focused hosted checks passed:

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -Ikernel/core/include tests\network_diagnostics_test.cpp kernel\core\ethernet.cpp -o $env:TEMP\guidex-network-diagnostics-test.exe
& $env:TEMP\guidex-network-diagnostics-test.exe
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-url-resolution.ps1
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-resource-diagnostics.ps1
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-resource-scheduler.ps1
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-secure-random-contract.ps1
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-build-wrapper.ps1
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-hosted.ps1 -Build
```

`network_diagnostics_test`, URL resolution, resource diagnostics, resource scheduler, secure-random contract, build-wrapper, and hosted Navigator HTTP/HTTPS/TLS smoke all passed. The hosted smoke log is:

`logs\navigator-hosted-smoke-20260828-055927.log`

The official kernel Navigator smoke also passed its `no_policy` scenario, including the HTTP cases, forms, redirects, and the final `NAVIGATOR-SMOKE_RESULT: PASS` marker. Its serial log is:

`logs\navigator-kernel-smoke-20260828-054608-no_policy.serial.log`

The release-tools synthetic packaging/read-only helper test passed after the local pinned venv was repaired from `requirements-release.txt`. Its dummy files are intentionally not bootable; the actual ISO is tested separately.

## QEMU

QEMU was found at `C:\Program Files\qemu\qemu-system-x86_64.exe` rather than on `PATH`. A fresh normal QEMU boot using the staged ESP reached the desktop/main loop, showed the corrected PCI inventory, initialized the e1000 NIC, enabled secure entropy through the virtio-rng path, and produced the built-in Navigator smoke PASS marker. No TX timeout was present after the volatile descriptor fix.

The normal run showed the current limitation honestly: QEMU user-mode networking supplied the static `10.0.2.15/24` configuration and the Navigator HTTP/TLS smoke passed, but DHCP DISCOVER retried and timed out without an offer. This is retained as a physical-test diagnostic item rather than being presented as a DHCP success.

## Bare-metal capture procedure

Boot the exact release ISO and, after the desktop is visible, open the existing Shell/Console and capture these command outputs in order:

```text
netdiag
ipconfig /all
lspci
lsusb
```

`netdiag` (also available as `nicinfo`) is the primary Ethernet capture. It includes the selected NIC's PCI BDF, vendor/device, subsystem, class/subclass/prog-if/revision, driver, MAC, link, driver-ready state, IRQ/polling mode, MMIO/resources, TX/RX counters, ARP counters, DHCP state/counters, IPv4, mask, gateway, and DNS. `lspci` adds all PCI class `0x02` identities, including unsupported Ethernet/wireless controllers. `lsusb` adds VID/PID and device/interface class identity for USB network hardware.

Also capture the UEFI block between:

```text
=== Network hardware capture ===
=== End network hardware capture ===
```

If camera capture is used, prioritize the post-boot `netdiag`, `lspci`, and `lsusb` output because it remains available after the bootloader text scrolls away. Do not write to a guessed removable drive; use the ISO as read-only media and select the physical target explicitly in the normal deployment workflow.

## Remaining blockers and Phase 3

The laptop has not yet supplied its real Ethernet/Wi-Fi identities. The x86 cross build still requires a target-compatible freestanding header/toolchain solution. QEMU DHCP offer behavior remains unresolved, although static IPv4 transport and HTTP/TLS smoke work. No Wi-Fi driver or association code was added.

Phase 3 should boot the deterministic image on the laptop, capture UEFI plus `netdiag`, `ipconfig /all`, `lspci`, and `lsusb`, correlate the Ethernet ID against the exact supported list, and use link/IRQ/MMIO/TX/RX/ARP/DHCP evidence to isolate the failure boundary. Only then should an evidence-backed NIC or USB-network support change be selected.
