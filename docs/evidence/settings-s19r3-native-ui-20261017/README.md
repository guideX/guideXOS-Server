# S19R3 Native Settings Resize and Lifecycle Evidence

This directory records S19R3 production-guest acceptance. The primary guest is one uninterrupted lifecycle run. The resize-back run is a separate boot of a private clone and is identified separately in [runtime-identity.json](runtime-identity.json). [SHA256SUMS.txt](SHA256SUMS.txt) lists every other file in this directory; it excludes itself.

## Result

The first S19R/S19R2 resize failures came from incomplete QMP gestures: the earlier transcripts sent button-down and then button-up without moving while held. Later input probes used large relative-only movements that do not map to absolute screen coordinates and can be clamped or rejected by the guest PS/2 parser. QEMU reported the guest PS/2 mouse as relative (`absolute: false`). S19R3 used explicit button-down, bounded multi-step movement while held, and button-up at the compositor's actual lower-right resize grip.

The primary guest accepted 760×560 → 720×560 → 660×440. A proposal below the minimum remained clamped at 660×440. A separate supplemental guest accepted 660×440 → 700×440 → 760×560. No guest resize, focus, pointer, capture, or teardown defect was reproduced; no production source was changed.

## Screenshots

| Evidence | Files |
| --- | --- |
| Preferred size, actual grip, held motion, minimum, and below-minimum clamp | `preferred-system.png`, `resize-corner-hit-target.png`, `resize-button-down.png`, `resize-held-motion-to-720x560.png`, `minimum-held-motion-660x440.png`, `minimum-system-660x440.png`, `below-minimum-clamped-660x440.png` |
| Minimum-size page review | `minimum-system-660x440.png`, `minimum-display.png`, `minimum-about.png`, `minimum-personalization.png` |
| Personalization reachability at minimum | `minimum-gradient-pending.png`, `minimum-gradient-hover.png`, `minimum-gradient-cancel-focus.png`, `minimum-gradient-cancel-result.png`, `minimum-wallpapers-page1.png` through `minimum-wallpapers-page4.png`, `minimum-wallpaper-cancel-focus.png`, `minimum-wallpaper-cancel-result.png`, `minimum-icons.png`, `minimum-icons-apply-focus.png`, `minimum-icons-cancel-focus.png`, `minimum-icons-cancel-result.png` |
| Supplemental resize-back, separate guest | `resizeback-initial-preferred.png`, `resizeback-live-720x560.png`, `resizeback-minimum-660x440.png`, `resizeback-live-700x440.png`, `resizeback-live-760x560-held.png`, `resizeback-preferred-760x560.png`, `resizeback-personalization-ocean.png`, `resizeback-final-desktop.png` |
| Primary uninterrupted legacy close/reopen lifecycle | `lifecycle-settings-closed.png`, `lifecycle-start-menu.png`, `lifecycle-control-panel.png`, `lifecycle-legacy-display-options-displays-tab.png`, `lifecycle-display-options-closed.png`, `lifecycle-control-panel-closed.png`, `lifecycle-pointer-after-legacy.png`, `lifecycle-keyboard-start-menu.png`, `lifecycle-settings-reopened-system.png`, `lifecycle-settings-reopened-personalization.png`, `lifecycle-stable-desktop.png` |

Apply focus was demonstrated at minimum, but Apply itself was not activated there. Cancel was activated and the saved `/desktop.cfg` stayed byte-identical. Successful Apply persistence remains covered by the prior S19/S19R production acceptance.

## Logs and artifact records

- `production-guest.qmp.log`, `production-guest.serial.log`, and `production-guest-launch.json` belong to the primary guest (PID 29328, QMP `127.0.0.1:60119`). They cover the minimum-size review and single-session legacy close/reopen lifecycle.
- `resizeback-guest.qmp.log`, `resizeback-guest.serial.log`, and `resizeback-guest-launch.json` belong to the supplemental resize-back guest (PID 32216, QMP `127.0.0.1:60120`). It is a separate boot from a private clone, not a continuation of the primary lifecycle process.
- `s19r2-prior-ordered-lifecycle-qmp.log` and `s19r2-prior-ordered-lifecycle.serial.log` are preserved historical inputs used to diagnose the previous incomplete gesture.
- `kernel.elf` is the extracted production kernel used by both S19R3 guests. Its SHA-256 matches the accepted S19R2 production artifact; no S19R3 build was performed because no source changed.
- `desktop.cfg.before`, `desktop.cfg.after`, and `desktop.cfg.resizeback-after` are read-only extracted guest configuration snapshots. Their byte identity confirms no S19R3 setting was persisted.
- `tests/` contains the six focused regression logs listed in the acceptance document.
- `runtime-identity.json` records boot separation, process identity, firmware/kernel/media/configuration hashes, and validation results. `artifact-hashes.json` provides the principal binary/config hashes in machine-readable form.

Full raw media, converted PPM intermediates, QEMU executables, and test binaries remain in the task-owned `out/validation/settings-s19r3-native-ui-20261017/` scratch tree; they are not part of this documentation commit. Only task-owned QEMU processes were stopped using their QMP endpoints. Foreign guests were left untouched.
