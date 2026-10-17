# S16R — Resume Guest Acceptance

Status: **S16 — Outcome A.** Guest acceptance resumed from `655702c8329b8d4afcd2d3c261ae0af5a86697d7` on `main` and completed on 2026-10-17. No push was performed.

## Guest identity and repair

Acceptance used the existing isolated writable ESP clone at `out/validation/settings-s16r-20261017/guest-acceptance/ESP-repair`. Firmware, bootloader, kernel, ramdisk, and configuration hashes are recorded in [the repaired media manifest](repair4-media-manifest.txt). The original preflight ESP was preserved.

The first Apply exposed a live VFS mount loss: the writable ATA device remained present, but `/desktop.cfg` had no usable `/` mount. The guest serial recorded restoration of the cached root device (`00`) and a successful Apply with `active=gradient_forest persisted=gradient_forest`. The bounded repair caches the writable root device after boot-time mounting and remounts `/` if the persistence path later finds no mount. The repair is in `kernel/core/desktop_gradient_persistence.cpp`, with its declaration and boot-time cache call in `kernel/core/include/kernel/desktop_gradient_persistence.h` and `kernel/core/main.cpp`.

The final clean kernel built successfully with the incremental AMD64 target under `kernel/build/s16r-diag-20261017/`. Its SHA-256 is `D55E066DC56D72A60DE542DC1FEF75FE1086EBB0CAE62A550A0524D397683F92`. Temporary pointer hit-test logs were removed before that clean build. Previously passing S16 host suites were not rerun.

## Acceptance results

| Requirement | Result | Evidence |
| --- | --- | --- |
| Initial Sunset state and native gradient choice | PASS | `desktop-initial.png`, `repair8-pending-forest.png` |
| Apply Forest and confirm visible desktop change | PASS | `repair8-apply-result.png`, `repair8-settings-closed-desktop.png`, `guest-com1-repair8.serial.log` |
| Cancel a pending Ocean selection | PASS | `repair8-pending-ocean.png`, `repair8-after-cancel.png`, `guest-com1-repair8.serial.log` |
| Close and reopen Settings | PASS | `repair8-settings-closed-desktop.png`, `repair8-settings-reopened-personalization.png` |
| Legacy Display Options consistency | PASS | `repair9-legacy-display-options.png`; Forest is marked current with `*` |
| Same-ESP cold reboot persistence | PASS | `guest-com1-repair9.serial.log`, `repair9-post-reboot-desktop.png`, QMP transcript `qmp-transcript-repair9.log` |
| `/desktop.cfg` before/after preservation | PASS | `desktop.cfg.before`, `desktop.cfg.after-apply`, `desktop.cfg.after-reboot`, `desktop.cfg.diff` |

The Apply and Cancel interaction session used the repaired diagnostic kernel with extra serial pointer logs; those logs were removed before rebuilding the clean kernel used for the reboot. The behavior-affecting repair was unchanged between the two builds.

The before file hash is `ED2CED9BD60AC15E67663BD955CBFC9DAAD36A47D368AB9DD3C2EF1CCA074A1D`. The after-Apply and after-reboot files both hash to `3E4773874A2E14FBC4D539FF79D3ADF41F87A6909A72AEBE83207D4ECEA6688F`. A byte comparison after substituting only the wallpaper ID confirms the complete files otherwise match exactly. The recorded diff changes only `wallpaperId=gradient_sunset` to `wallpaperId=gradient_forest`.

The reboot proof is the clean relaunch of QEMU against the same `ESP-repair` directory. Its serial log shows writable FAT32 device `00`, loads `gradient_forest` from the primary `/desktop.cfg`, and applies that persisted ID before the desktop is drawn. A later host `system_reset` QMP command did not produce another kernel entry and is not counted as reboot evidence.

## Evidence map

- Native interaction QMP and serial: `qmp-transcript-repair8.log`, `guest-com1-repair8.serial.log`.
- Legacy display and same-ESP relaunch QMP and serial: `qmp-transcript-repair9.log`, `guest-com1-repair9.serial.log`.
- Representative screenshots: `desktop-initial.png`, `repair8-pending-forest.png`, `repair8-apply-result.png`, `repair8-pending-ocean.png`, `repair8-after-cancel.png`, `repair8-settings-reopened-personalization.png`, `repair9-legacy-display-options.png`, `repair9-post-reboot-desktop.png`.
- SHA-256 inventory of committed acceptance artifacts: `evidence-manifest.sha256`.
