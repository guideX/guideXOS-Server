# Settings S16 — Native Gradient Selection and Transactional Apply

Status: **Outcome A — S16R completed production guest acceptance and verified same-ESP persistence.**

## Repository checkpoint

- Repository: `D:\dev\guideXOSServer`
- Branch: `main` (the starting branch was retained)
- Starting HEAD: `6b89e42ef9a188da4c55fc0a5e80863e32ad79a4`
- The S15 acceptance report states Outcome A, but no S15 ending commit is present in the starting branch history. The S15 implementation, report, and `docs/evidence/settings-s15-20261016/` were pre-existing working-tree content and are included with this coherent S15/S16 local commit.
- Pre-existing untracked files and directories under `out/` were preserved and excluded from the commit.

## Existing ownership and compatibility

Built-in gradient IDs and palettes remain owned by the desktop registry in `kernel/core/desktop.cpp`. Both Native Settings and legacy Display Options obtain preset snapshots from that registry and call the same desktop-owned `apply_builtin_gradient_preset` operation. Native Settings cannot provide arbitrary colors, paths, framebuffer writes, or raw configuration bytes.

The desktop owns active wallpaper/gradient state and `/desktop.cfg` is the canonical configuration file. Startup now prefers a nonempty wallpaper ID in `/desktop.cfg`; legacy wallpaper sidecars remain fallback inputs. Legacy `set_wallpaper_by_id` routes built-in gradients through the shared apply operation. `DisplayOptionsApp` uses that operation for its gradient Apply action and receives the same bounded appearance-change notification as other running applications.

The existing display configuration generation remains separate. A desktop appearance revision changes when a wallpaper/gradient or supported system icon visibility changes. The mutation request carries the appearance revision captured when the user selected a preset; a stale revision is rejected and Native Settings refreshes before requiring a new selection.

## Authorized operation and transaction

The provider exposes one mutation: `ApplyBuiltInGradientPreset`. Its bounded request contains its structure size and version, a fixed-capacity preset ID, and the expected appearance revision. It contains no caller identity. Authorization uses the runtime ID assigned by `AppManager` and accepts only the registered `gxos.builtin.settings` runtime. Tests cover unauthorized callers, spoofed launch arguments, malformed/unsupported requests, malformed and unknown IDs, and unavailable storage.

For a valid request, the desktop owner reads the current appearance, checks the expected revision, saves the previous active wallpaper configuration, and applies the selected palette in memory. The persistence helper patches only `wallpaperId` in `/desktop.cfg`, preserving other bytes, keys, values, and line endings. It writes through VFS, reads the file back, and verifies the exact serialized bytes. The desktop advances the appearance revision, schedules redraw, and notifies applications only after successful verification.

On write or readback failure, the helper attempts to restore the exact bounded prior file image and verifies that restoration. The desktop restores its prior active configuration and does not advance the revision or publish a commit notification. The result distinguishes save failure with verified rollback, readback failure with verified rollback, and rollback failure. It reports the actual active ID and any persisted ID that could be read after failure; it does not infer rollback success from the attempted write.

Matching the current preset is a no-op and does not write configuration. A changed preset fails before application when the canonical file is absent, read-only, malformed, or outside the helper's 511-byte payload bound. When `/desktop.cfg` has no `wallpaperId`, the helper appends that key and otherwise preserves its bytes.

The VFS FAT writer updates existing file data in place and then updates directory metadata. It does not provide an atomic rename, journal, or power-loss transaction. S16 therefore provides bounded runtime rollback and verified readback, not crash-atomic persistence. A sudden power loss during the filesystem write can still leave on-disk state incomplete.

## Native Personalization controls

The existing current-appearance, scale, palette, and desktop-icon information remains visible. Personalization now displays the eight built-in gradient presets as a four-column swatch grid, marks the current and pending presets separately, and provides explicit Apply and Cancel controls with status feedback. Mouse selection and keyboard navigation/select are handled by the existing compositor input path. Apply is explicit; Cancel, page navigation, and window close do not submit a mutation. Successful apply refreshes the provider snapshot and resets the pending selection from authoritative state. Stale revisions require the user to select again.

The shared S10 presentation core continues to own category navigation and reusable focus transitions. Gradient transaction state stays in the Native Settings runtime adapter. Automated tests cover the shared focus model and production provider contract. Because the isolated production guest could not be started during this phase, the new UI and legacy cross-application behavior have not received S16 visual guest confirmation.

## Validation results

Newly executed results are listed in [the S16 test summary](evidence/settings-s16-20261016/test-results.md). In brief:

- Native Settings runtime/provider and persistence helper: **1,068/1,068 passed**, including 100 lifecycle repetitions, 250 provider refresh cycles, 1,000 notification/state transitions, repeated commits, and injected rollback cycles.
- S10 presentation core: **2,649/2,649 passed**.
- Settings Center model: **89/89 passed**.
- Default Apps model and interactions: **48/48 passed**, with 100 lifecycle cycles.
- Settings inventory: **45/45 passed**; Users: **32/32 passed**; S6: **42/42 passed**; S7: **58/58 passed**.
- App Model identity: **25/25 passed**.
- Compositor pointer-button test passed; display configuration control-plane and persistence-source checks passed.
- Shared presentation freestanding compile, bounded link, and unresolved-symbol checks passed. The production kernel also compiled and linked the new persistence helper under the repository's freestanding AMD64 flags.

The AMD64 production build used:

```text
mingw32-make -C kernel ARCH=amd64 BUILD_DIR=build/s16-clean-final-verified-20261016
```

It completed 156 compile commands with 0 errors and 233 warning lines. The 233 warning diagnostics match the accepted S15 build set; no new S16 warning diagnostic remains. The ELF is 2,724,704 bytes with SHA-256 `AE14A4645BE1E5D5C7A021DB39F67C7860937E80B9BFB9033847752EB83EAEB1`. The PE is 2,838,667 bytes with SHA-256 `7F2B53159FCFF832A5DAC77323DE4B4FA2E80CFC37CE1173952A41FBEFBFBFEA`. The new virtual appearance callback reference has a matching definition in the linked image; the only unmatched `nm` entries, `__dso_handle` and `mainCRTStartup`, were also present in the S15 baseline. Build log and artifact details are in [S16 build evidence](evidence/settings-s16-20261016/build-summary.md).

## Original S16 guest acceptance blocker (historical)

The mandatory S16 sequence—initial screenshot, mouse and keyboard selection, pending screenshot, explicit apply, reopen checks, legacy Display Options check, Cancel, same-ESP reboot, post-reboot snapshot, and before/after configuration comparison—was not run and is not claimed as passed.

Before launching a guest, process inventory found QEMU PID `10488` running from `D:\dev\guideXOSServer_DiskManagerImprovements`, with QMP port `61440` and that project's writable ESP. It remained active through the permitted ten-minute passive recheck window. This task did not connect to, terminate, or otherwise interact with it. Guest testing stopped at that evidence boundary. The isolated S16 guest directory was prepared but not booted; its hashes are recorded in [the guest preflight note](evidence/settings-s16-20261016/guest-preflight.md). No S16 screenshots, serial log, QMP transcript, post-apply configuration, or reboot persistence proof exist.

The original S16 checkpoint closed as Outcome B because the required guest mutation and same-ESP reboot proof were still outstanding. S16R below resumes from the committed S16 checkpoint and clears that acceptance blocker. S9, Settings 1.0, and v0.3 readiness were not advanced.

## S16R resumed guest acceptance (2026-10-17)

S16R resumed on `main` from `655702c8329b8d4afcd2d3c261ae0af5a86697d7`. The implementation was already committed at that checkpoint. Acceptance used the existing isolated writable ESP clone at `out/validation/settings-s16r-20261017/guest-acceptance/ESP-repair`; the original preflight ESP and pre-existing `out/` artifacts were preserved. No foreign QEMU process was connected to or modified. The owned QEMU launches, serial logs, screenshots, and QMP exchanges are recorded in `docs/evidence/settings-s16r-20261017/`; see the [S16R test results](evidence/settings-s16r-20261017/test-results.md) and [evidence manifest](evidence/settings-s16r-20261017/evidence-manifest.sha256).

The first real Apply exposed a guest-only VFS mount loss: `/desktop.cfg` could no longer be reached through `/`, although the writable ATA device remained present. The guest serial log records recovery on device `00`, followed by `gradient.apply status=0 active=gradient_forest persisted=gradient_forest`. The repair caches the writable root device immediately after the boot-time persistent mount succeeds and remounts `/` if the Settings persistence path later finds no mount. The change is in `kernel/core/desktop_gradient_persistence.cpp`, its public declaration is in `kernel/core/include/kernel/desktop_gradient_persistence.h`, and the boot-time cache call is in `kernel/core/main.cpp`. The clean rebuilt kernel used for the post-Apply reboot is SHA-256 `D55E066DC56D72A60DE542DC1FEF75FE1086EBB0CAE62A550A0524D397683F92`.

Acceptance results:

- **Native selection and Apply — PASS.** The guest started with Sunset, showed Forest as pending, accepted Apply, and displayed the Forest gradient on the desktop. The Apply result screen and serial result are preserved.
- **Cancel — PASS.** Ocean was selected as pending and then cancelled. The post-Cancel Settings screen still showed Forest as current and reported “Pending selection cancelled.”
- **Settings close and reopen — PASS.** Settings closed to the visibly green Forest desktop and reopened with Forest still marked current.
- **Legacy Display Options — PASS.** The legacy Control Panel → Display path opened the shared Gradients gallery, where Forest was marked current with `*`.
- **Same-ESP reboot persistence — PASS.** After the Apply session, a fresh QEMU process booted the repaired kernel from the same `ESP-repair` path. Its serial log shows `/desktop.cfg` mounted from writable FAT32 device `00`, then `wallpaper load id=gradient_forest source=primary-config` and `wallpaper apply source=persisted id=gradient_forest`. The post-reboot desktop screenshot shows the green Forest background.
- **Configuration preservation — PASS.** `desktop.cfg.before`, `desktop.cfg.after-apply`, and `desktop.cfg.after-reboot` are preserved with hashes. The after-Apply and after-reboot files have the same SHA-256. The saved unified diff contains exactly one change: `wallpaperId=gradient_sunset` to `wallpaperId=gradient_forest`; every other line remains byte-for-byte the same.

The representative acceptance captures are `desktop-initial.png`, `repair8-pending-forest.png`, `repair8-apply-result.png`, `repair8-after-cancel.png`, `repair8-settings-closed-desktop.png`, `repair8-settings-reopened-personalization.png`, `repair9-legacy-display-options.png`, and `repair9-post-reboot-desktop.png`. `guest-com1-repair8.serial.log` records Apply and Cancel; `guest-com1-repair9.serial.log` records the boot from the same ESP; `qmp-transcript-repair8.log` and `qmp-transcript-repair9.log` record the guest interaction and relaunch. Artifact hashes are in `repair4-media-manifest.txt` and `evidence-manifest.sha256`.

A later host QMP `system_reset` attempt did not produce a second kernel entry in the serial log, so it is not used as reboot evidence. The passing reboot evidence is the recorded cold restart into a fresh QEMU process with the same writable ESP directory. The build completed successfully; the pre-existing S16 host-test results were not rerun.

## Remaining limits and recommended S17 work

The S16 mutation boundary remains limited to built-in gradients. Runtime rollback and verified readback do not provide crash-atomic filesystem writes; a future power-loss atomicity requirement should be handled with a separately designed filesystem commit mechanism rather than by expanding this gradient operation.
