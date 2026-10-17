# Settings S18: Desktop Configuration Integrity and Native Icon Visibility

Date: 2026-10-17

Result: Gate A passed with a bounded historical finding; Gate B passed on an isolated raw-FAT guest image. The accepted guest image was restored to the original icon visibility fixture.

## Outcome summary

- **A — Configuration integrity: PASS, bounded.** The historical 257-byte config is malformed and its original cause is not established. The icon writer now refuses malformed input without changing bytes. On valid config, it edits only the requested icon fields and verifies the stored result. No new production guest writer defect was reproduced.
- **B — Native icon settings: PASS.** The supported visibility choices applied, survived Settings close/reopen and cold relaunch on the same raw FAT image, matched the legacy Display Options view, and were restored to the original fixture. Cancel and closing Settings with pending changes discarded those changes.
- **C — Follow-up, not an acceptance blocker.** Auto-arrange remains session-only and is not a persistent Settings control. The original source/timing of the historical malformed export remains unknown. A whole-media raw image checksum changed between the final visibility restore and close-with-pending runs even though the latter serial log contains no `/desktop.cfg` write and the config remained byte-identical; the pre-close raw block snapshot was not retained, so that unrelated image delta is unlocalized.

## Gate A — desktop configuration integrity

### Historical evidence and finding

The exact first S9R snapshot retained from `out/validation/settings-s9-guest-20261011-130249-105f29e5/ESP/desktop.cfg` is 257 bytes, SHA-256 `ED2CED9BD60AC15E67663BD955CBFC9DAAD36A47D368AB9DD3C2EF1CCA074A1D`. Its final bytes are the bare token `autoArrangeDesktopIcon`: there is no equals sign, value, or line ending. The later same-ESP S9R reboot snapshot is 261 bytes, SHA-256 `9BE8F3D5AD9349D23FFA9622C0D490FDDD9A004CD47007CEDD46F392270FBAE1`, and ends with the complete line `autoArrangeDesktopIcons=0\n`. Exact copies are retained as [the 257-byte historical config](evidence/settings-s18-icon-20261017/desktop.cfg.historical-s9-257-initial) and [the 261-byte reboot config](evidence/settings-s18-icon-20261017/desktop.cfg.historical-s9-261-after-reboot).

S17R snapshots show 257 → 263 → 259 bytes while `wallpaperId` changed from `gradient_midnight` to the longer `legacy_guidexos_space` and back. The length changes equal the wallpaper identifier length changes; the malformed final token remained. Cancel was byte-identical to the pre-change config. See the retained [S17R content comparison](evidence/settings-s17r-20261017/esp-file-content-comparison.txt) and its [before](evidence/settings-s17r-20261017/desktop.cfg.before), [after wallpaper](evidence/settings-s17r-20261017/desktop.cfg.after-wallpaper), [after gradient](evidence/settings-s17r-20261017/desktop.cfg.after-gradient), and [after Cancel](evidence/settings-s17r-20261017/desktop.cfg.after-cancel) snapshots.

The S9R serial record reports a successful 261-byte overwrite and VFS/FAT success. It does not establish when or why the first 257-byte host snapshot was produced. The evidence therefore supports a malformed historical snapshot, but not a claim that the guest writer truncated the file. S18 does not manufacture a missing line or silently repair that input.

### Persistence behavior added

The desktop icon persistence path now reads a bounded config snapshot and validates the icon fields before writing. It rejects malformed or duplicate recognized fields and invalid boolean values. A missing optional System Settings field uses the documented default. When the input is valid, the targeted update retains original line endings, ordering, unknown fields, and all unrelated raw bytes; the writer reads the result back and verifies the requested state. Short/partial writes and readback mismatch attempt restoration of the original bytes and report whether rollback was verified. Failure to restore is represented separately. Conflicting expected state and no-op requests do not rewrite the file.

Hosted tests use a controlled fake VFS for injected short writes, partial writes, mismatched readback, and rollback failures. The exact historical 257-byte malformed config is rejected byte-preservingly; the complete 261-byte form accepts a targeted update without changing its size. This is not a claim of power-loss/crash atomicity, and no real FAT short-write injection was performed.

The S18 native fixture is [desktop.cfg.before](evidence/settings-s18-icon-20261017/desktop.cfg.before), 237 bytes, SHA-256 `B3098D72195A16ED553B12EFF7DB9092A056222783A448ABDBBC092448D1273F`. Applying the File Explorer choice changed exactly two backing booleans from `1` to `0` without changing file length: `showDesktopThisSystem` and `showDesktopFileManager`. This pairing is the existing composite File Explorer desktop target. Trash, System Settings, wallpaper, scale, theme, taskbar, and small-folder-icon fields were preserved. The semantic comparison is in [desktop.cfg.field-diff.txt](evidence/settings-s18-icon-20261017/desktop.cfg.field-diff.txt).

## Gate B — native desktop icon visibility

The Personalization page now offers visibility controls for Trash, File Explorer, and System Settings. The operation goes through a versioned, size-checked native provider API. It authorizes the caller by the active Settings runtime app identity, validates the target/visible masks, and uses an expected appearance revision to reject stale edits. There is no general-purpose Settings file-write interface. Apply reports active and persisted results; failed persistence can restore the active state when storage rollback is verified.

Visibility edits remain pending until Apply. Cancel and closing the app discard pending changes. The UI explains that auto-arrange is session-only and is not saved by Settings. No persistent auto-arrange setting or icon positioning/layout engine was added or claimed.

The final raw-FAT test exposed an initialization ordering issue: the desktop first loaded icon defaults before VFS became available, then kept those cached defaults after mount. The post-VFS-ready desktop refresh now reloads persisted icon visibility before refreshing the icons. The final cold boot verified the stored hidden state after this correction.

### Raw-FAT guest acceptance sequence

The sequence used an isolated 134,249,984-byte raw FAT image (`guideXOS-S18-production-v6.raw`, partition start LBA 63) under QEMU with the final clean AMD64 kernel and retained OVMF firmware. The final kernel was read back from the raw image and its SHA-256 matched the built ELF. The image was not the user’s shared ESP. The recorded sequence was:

1. Open native Settings and confirm the initial icon state.
2. Turn off File Explorer and verify that the edit is pending while the desktop icon is still visible.
3. Apply; verify the 237-byte config write, FAT/VFS success, serial apply result, and desktop update.
4. Close and reopen Settings; confirm File Explorer is persisted as hidden.
5. Open legacy Control Panel → Display Options → Desktop Icons and confirm its choices match the same persisted state.
6. Create another pending icon edit, select Cancel, and verify no config write and no state change.
7. Cold relaunch QEMU against the same raw FAT image; verify File Explorer remains hidden on the desktop and in reopened Settings.
8. Restore File Explorer through native Settings, apply, and verify the config returns byte-for-byte to the original fixture. Confirm the desktop and Start menu remain usable.
9. In a separate run, make a pending File Explorer hide and close Settings. Reopen Settings and confirm File Explorer remains shown. The serial transcript has zero `/desktop.cfg` writes, and its config readback is byte-identical to the original fixture.

The accepted images and logs are indexed in the [S18 evidence README](evidence/settings-s18-icon-20261017/README.md). Representative screenshots include [applied desktop](evidence/settings-s18-icon-20261017/desktop-after-apply.png), [legacy Display Options](evidence/settings-s18-icon-20261017/legacy-display-options-icons.png), [hidden state after cold boot](evidence/settings-s18-icon-20261017/desktop-after-coldboot-hidden.png), [restored desktop](evidence/settings-s18-icon-20261017/desktop-final-restored.png), and [Start menu after restoration](evidence/settings-s18-icon-20261017/start-menu-usable.png).

The whole-media SHA-256 after visibility restoration was `AD91AFBEF4EB9B11C8E8D55C5968C85868E97F7C1503785840781624E7F91D19`; after the separate close-with-pending guest run it was `4D1CA60915F2C9DEEEF32862C52762ABC99DCE0FC27E34A3F7B28C9D8A0AF5FC`. The latter serial transcript contains no `/desktop.cfg` VFS write, and the 237-byte config checksum remained the original `B309…1273F`. Because no raw block copy was made immediately before the close-with-pending run, S18 does not localize that whole-image hash delta to any specific sector or file. This observation is kept separate from the proven byte-level config result.

## Build and hosted verification

The final source passed the clean AMD64 production build, PE import and linked-symbol review, and hosted checks. Results are summarized in [build-validation.txt](evidence/settings-s18-icon-20261017/build-validation.txt), with the full build transcript in ignored validation output. The icon fault-injection cases run against the controlled hosted fake VFS; production raw-FAT coverage verifies the successful write/readback path and persistence across cold relaunch.

## Scope and next steps

S18 accepts desktop visibility configuration only. It does not accept or establish behavior for a persistent auto-arrange feature, historical export provenance, power-loss atomicity, a physical device/ESP, or a QEMU directory-backed `fat:rw` comparison. A follow-up can capture the export-time source and destination bytes, preserve a pre/post raw block copy while investigating the unlocalized whole-media hash change, and define actual icon layout semantics before adding auto-arrange persistence. Prior S9R/S17R evidence is diagnostic context and is not re-certified by this S18 run.
