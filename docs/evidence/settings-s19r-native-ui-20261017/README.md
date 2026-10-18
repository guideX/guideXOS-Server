# S19R guest evidence

This directory preserves S19R evidence separately from the original S19 build and source evidence. The production candidate under `out/validation` was read and booted from private writable copies. The source candidate remained unchanged.

## Identity and execution

- `artifact-identity.txt` records the original candidate, S19R rebuilt kernel, firmware, configuration, and hash identities.
- `rebuilt-media-identity.json` records the pre-boot and post-acceptance hashes for the rebuilt private image and its `/desktop.cfg`.
- `qemu-cold-relaunch.json`, `qemu-final-acceptance.json`, and `qemu-resume2.json` record the first guest run. `qemu-rebuilt-verification.json` records the guest booted with the repaired kernel. All instances used TCG, no NIC, private writable media, read-only OVMF, and the task-owned QMP endpoint.
- `qemu-post-legacy-settings-check.json`, `qmp-post-legacy-settings-check.log`, and `s19r-post-legacy-settings-check.serial.log` record the final isolated guest check. It returned to the desktop after closing legacy surfaces, but a subsequent native Settings relaunch was not confirmed. `/desktop.cfg` stayed unchanged.
- `qmp-session.log` contains the QMP interaction history. `qmp-shutdown-rebuilt.json` records the rebuilt guest's clean QMP shutdown.
- `s19r-cold-relaunch.serial.log`, `s19r-final-acceptance.serial.log`, `s19r-final-resume2.serial.log`, and `s19r-rebuilt-verification.serial.log` contain guest serial output.
- `desktop.cfg.before`, `desktop.cfg.after-cancel`, `desktop.cfg.after-apply`, and `desktop.cfg.after-apply-rebuilt` are the configuration byte snapshots.
- `validation-results.md` records the newly run runtime regression and production build.

## Screenshots

Initial production guest review:

- `desktop-initial.png`, `settings-initial.png`, `settings-system-return.png`, `settings-about.png`, and `settings-display.png` show the desktop and informational pages.
- `settings-personalization-current.png`, `gradient-pending-ocean.png`, `gradient-cancel-result.png`, `wallpapers-page1.png`, `wallpapers-pending-page2.png`, `wallpapers-pending-page3.png`, and `wallpapers-pending-page4.png` show Personalization and wallpaper paging.
- `desktop-icons-current.png`, `icon-pending-toggle.png`, `icon-cancel-focus.png`, `icon-cancel-result.png`, `apply-gradient-pending.png`, `apply-gradient-focus.png`, and `apply-gradient-result.png` show icon state, pending selection, focus, Cancel, and Apply.
- `desktop-after-apply.png`, `desktop-cold-relaunch.png`, `settings-reopened-personalization.png`, and `desktop-after-settings-escape.png` show persistence, relaunch, and return to the desktop.
- `legacy-control-panel.png`, `legacy-display-options.png`, `legacy-display-options-backgrounds.png`, and `legacy-display-options-desktop-icons.png` show the pre-repair legacy UI state.
- `settings-resize-start-final3.png`, `settings-resize-held-final.png`, and `settings-resize-after-final.png` record the attempted lower-right resize drag; the window geometry remained at the normal size.

Rebuilt guest review after the legacy marker repair:

- `rebuilt-settings-initial.png`, `rebuilt-gradient-pending-ocean.png`, `rebuilt-gradient-cancel-focus.png`, and `rebuilt-gradient-cancel-result.png` show the rebuilt Settings launch and Cancel path.
- `rebuilt-apply-result.png` shows the applied Ocean state.
- `rebuilt-control-panel.png`, `rebuilt-legacy-display-options.png`, and `rebuilt-legacy-backgrounds-after-fix.png` show Control Panel, Ocean's applied marker on Gradients, and the corrected Backgrounds marker.
- `rebuilt-after-legacy-escape.png` is the final stable desktop after closing the legacy surfaces.

The rebuilt final desktop capture with the Start menu open was excluded from the evidence set; `rebuilt-after-legacy-escape.png` is the stable desktop capture.
