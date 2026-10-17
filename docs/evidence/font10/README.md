# FONT10 guest evidence

These screenshots come from isolated QEMU desktop runs documented in [`../../fonts/FONT10_SHELL_TYPOGRAPHY.md`](../../fonts/FONT10_SHELL_TYPOGRAPHY.md). The first set shows the live TrueType UI, the bitmap switch, a fresh-process persistence boot, and missing-font recovery. The smallest live Navigator titlebar was 204 px wide; its title fit at the role cap, while the narrow synthetic title/taskbar cases are covered by the 71-check hosted renderer suite.

## Screenshots

| Screenshot | Guest state |
|---|---|
| [01 — TrueType Start Menu](01-ttf-start-menu.png) | Start Menu application and navigation labels with Roboto Italic active. |
| [02 — TrueType Terminal taskbar](02-ttf-terminal-taskbar.png) | Terminal caption and taskbar label in scalable text. |
| [03 — Multiple windows](03-ttf-multiple-windows.png) | Settings and Terminal windows open together. |
| [04 — Focus and resize](04-ttf-focus-resize.png) | Focus state and resized window with shell labels retained. |
| [05 — Narrow Navigator title](05-ttf-narrow-navigator-title.png) | Navigator at 204 px minimum width; the real title fits and is not ellipsized. |
| [06 — 17 px preference](06-ttf-size-17px.png) | Settings Fonts after applying the 17 px preference. |
| [07 — Persistence reboot](07-ttf-persistence-reboot.png) | Fresh process restored TrueType and the Start Menu labels. |
| [08 — Bitmap Start Menu](08-bitmap-start-menu.png) | Bitmap mode remains readable and usable. |
| [09 — Missing-font bitmap recovery](09-missing-font-bitmap-menu.png) | Start Menu remains available after the configured font is missing from the recovery clone. |
| [10 — Missing-font Settings](10-missing-font-settings.png) | Settings Fonts reports bitmap active while its UI remains usable. |

## Raw run records

The following validation artifacts are intentionally kept under the untracked `out/` directory and were not committed:

- Boot 1 UI serial output: [`guest-boot1-ui.serial.log`](../../../out/font10-validation/guest-boot1-ui.serial.log)
- Boot 1 visible QEMU launch manifest: [`qemu-boot1-ui-launch.json`](../../../out/font10-validation/qemu-boot1-ui-launch.json)
- Boot 1 census: [`qemu-boot1-ui-prelaunch-census.json`](../../../out/font10-validation/qemu-boot1-ui-prelaunch-census.json)
- Boot 2 persistence serial output: [`guest-boot2.serial.log`](../../../out/font10-validation/guest-boot2.serial.log)
- Boot 2 fresh-process manifest and census: [`qemu-boot2-launch.json`](../../../out/font10-validation/qemu-boot2-launch.json), [`qemu-boot2-prelaunch-census.json`](../../../out/font10-validation/qemu-boot2-prelaunch-census.json)
- Boot 3 missing-font serial output: [`guest-boot3-missing-font.serial.log`](../../../out/font10-validation/guest-boot3-missing-font.serial.log)
- Boot 3 fresh-process manifest and census: [`qemu-boot3-launch.json`](../../../out/font10-validation/qemu-boot3-launch.json), [`qemu-boot3-prelaunch-census.json`](../../../out/font10-validation/qemu-boot3-prelaunch-census.json)
- Transcribed live GDB/state measurements: [`font10-live-state-observations.md`](../../../out/font10-validation/font10-live-state-observations.md)
- Offline clone mutation record: [`remove-configured-font-from-clone.py`](../../../out/font10-validation/remove-configured-font-from-clone.py)
- Hosted FONT10 output: [`font10_shell_typography_test.final.log`](../../../out/font10-validation/font10_shell_typography_test.final.log)
- Forced AMD64 build: [`amd64-build-accepted.log`](../../../out/font10-validation/amd64-build-accepted.log)

The private firmware, accepted and recovery FAT images, detailed UI snapshots, and QEMU captures are preserved alongside those records in `out/font10-validation/` for local inspection.
