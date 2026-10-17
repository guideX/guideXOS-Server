# S18 Desktop Icon Configuration Evidence

This folder contains the retained desktop configuration snapshots, guest screenshots, serial/QMP transcripts, and binary identities for the S18 raw-FAT acceptance. The acceptance summary is [Settings S18 Desktop Configuration Integrity and Native Icon Visibility Acceptance](../../SETTINGS_S18_ICON_CONFIGURATION_AND_INTEGRITY_ACCEPTANCE.md).

The repository attributes keep the `desktop.cfg.*` captures and serial/QMP transcripts byte-for-byte across checkout; serial/QMP logs are treated as raw evidence rather than whitespace-formatted source.

## Configuration snapshots

- [desktop.cfg.before](desktop.cfg.before) — 237-byte original fixture.
- [desktop.cfg.after-apply](desktop.cfg.after-apply) — File Explorer hidden; only its two backing booleans change.
- [desktop.cfg.field-diff.txt](desktop.cfg.field-diff.txt) — semantic field comparison and supported-state notes.
- [desktop.cfg.after-restore](desktop.cfg.after-restore) — byte-identical to the original fixture after restoring visibility.
- [desktop.cfg.after-close-pending](desktop.cfg.after-close-pending) — byte-identical to the original fixture after closing Settings with a pending edit.
- [desktop.cfg.historical-s9-257-initial](desktop.cfg.historical-s9-257-initial) and [desktop.cfg.historical-s9-261-after-reboot](desktop.cfg.historical-s9-261-after-reboot) — exact historical malformed and complete forms used for the Gate A comparison.
- [close-with-pending-result.txt](close-with-pending-result.txt) — reopen/discard result, config checksum, media checksum, and historical checksums.
- [artifact-identities.txt](artifact-identities.txt) — raw media, kernel, firmware, and config checkpoint hashes. It notes the unlocalized whole-image hash change across the final guest runs.

## GUI sequence

- [desktop-before.png](desktop-before.png) and [settings-icons-before.png](settings-icons-before.png) — original desktop and native icon choices.
- [icons-pending.png](icons-pending.png), [icons-applied.png](icons-applied.png), and [desktop-after-apply.png](desktop-after-apply.png) — pending state, applied state, and resulting desktop.
- [settings-reopened-after-apply.png](settings-reopened-after-apply.png) — persisted hidden state after reopening Settings.
- [legacy-display-options-icons.png](legacy-display-options-icons.png) — legacy Display Options reflects the same persisted choices.
- [cancel-pending.png](cancel-pending.png) and [cancelled.png](cancelled.png) — pending edit and Cancel result.
- [desktop-after-coldboot-hidden.png](desktop-after-coldboot-hidden.png) and [settings-after-coldboot-hidden.png](settings-after-coldboot-hidden.png) — cold relaunch with File Explorer hidden and Settings reopened.
- [visibility-restore-pending.png](visibility-restore-pending.png), [visibility-restored.png](visibility-restored.png), and [desktop-final-restored.png](desktop-final-restored.png) — restoration to the original icon fixture and final desktop.
- [start-menu-usable.png](start-menu-usable.png) — Start remains available after restoration.
- [close-with-pending-selection.png](close-with-pending-selection.png), [close-with-pending-closed-desktop.png](close-with-pending-closed-desktop.png), [close-with-pending-reopened-settings.png](close-with-pending-reopened-settings.png), and [close-with-pending-final-desktop.png](close-with-pending-final-desktop.png) — closing Settings discarded the pending File Explorer hide; it remained shown after reopening.

## Guest and build logs

- [guest-apply.serial.log](guest-apply.serial.log) and [guest-apply.qmp.log](guest-apply.qmp.log) — apply, Cancel, Settings reopen, and legacy Display Options sequence.
- [guest-final-coldboot.serial.log](guest-final-coldboot.serial.log), [guest-final-coldboot.qmp.log](guest-final-coldboot.qmp.log), and [guest-final-coldboot-launch.json](guest-final-coldboot-launch.json) — final-kernel cold boot, persisted state read, restoration, and stable desktop.
- [close-with-pending.serial.log](close-with-pending.serial.log), [close-with-pending.qmp.log](close-with-pending.qmp.log), and [close-with-pending-launch.json](close-with-pending-launch.json) — close-with-pending acceptance. The serial contains no `/desktop.cfg` write.
- [build-identity.txt](build-identity.txt) — kernel and firmware identity used for the earlier interaction pass.
- [build-validation.txt](build-validation.txt) — clean build, import/symbol review, hosted test results, and the boundary between fake-VFS fault injection and real raw-FAT acceptance.

The pre-fix cold-boot trace and intermediate experimental screenshots remain under ignored `out/validation/settings-s18-icon-20261017/guest/`; the committed evidence here keeps the final accepted sequence plus the diagnostic serial trace needed to explain the startup fix.
