# S14R ordinary QEMU guest sequence

The guest used an isolated writable copy of the S14 production ESP and an isolated OVMF copy. No shared boot media was overwritten. The guest booted to the desktop and emitted canonical UEFI GOP framebuffer metadata in COM1 serial output.

1. Opened Start → Settings. Settings reported `snapshot=ready revision=1`.
2. Selected Display with mouse input. The screenshot and serial log agree: 1280×800, 32 BPP, B8G8R8A8, 5120 bytes per line.
3. Used keyboard navigation through System and About, then returned to Display. System reported guideXOS, kernel 0.1.0, AMD64 (x86-64). About reported the same identity and explicitly showed that an OS release version is not defined.
4. Closed Settings and reopened it through Start. The new instance reported a fresh ready snapshot at revision 1 and opened on System. The captured reopened System page matches that fresh launch.
5. Opened Start → Control Panel and verified the legacy embedded panel. The launch diagnostic records `target=ControlPanel` as `blocked-unknown-fallback`; the existing desktop fallback still opened the embedded panel, and no routing change was made. Invoked its Display tile, which launched Display Options through the existing typed dispatch.
6. Viewed Backgrounds and Displays. No background or display mode was applied. The Displays query accurately reported the backend and monitor information unavailable; the page displayed `Display query failed`.
7. Closed Display Options. Settings and the legacy Control Panel were independently usable in sequence; the legacy Control Panel is a desktop modal/overlay. In the overlapping attempt, closing the modal left the Settings window active, and Settings was then closed separately. The Control Panel was also reopened and captured by itself before final cleanup.
8. Closed all surfaces and captured the stable desktop. The task-owned QEMU guest was shut down through its own QMP endpoint. A separate foreign QEMU process observed during the run was left untouched.

QMP pointer calibration used small individual relative events and visible screenshots to locate controls. Initial host pointer capture was handled without treating a lost first click as a guest defect. No production source repair was needed.

## Artifact identities

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `465F7C86957B4E41E1FC8A3392177875167F9850C048348F9BB03BEADB842102` |
| UEFI bootloader | `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA` |
| OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |

`qemu-launch.json`, `ESP-file-manifest.sha256`, and `ESP-build-identity.txt` bind the guest run to those artifacts. The isolated ESP build identity retained `phase=S14` and points to the S13 production artifact source directory; the kernel and bootloader content hashes match the final S14 artifacts recorded in the acceptance report. The S14R run did not use a stale candidate with an unverified hash.
