# Settings S16 — Native Gradient Selection and Transactional Apply

Status: **Outcome B — implementation and host validation complete; production guest acceptance remains incomplete.**

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

## Production guest acceptance blocker

The mandatory S16 sequence—initial screenshot, mouse and keyboard selection, pending screenshot, explicit apply, reopen checks, legacy Display Options check, Cancel, same-ESP reboot, post-reboot snapshot, and before/after configuration comparison—was not run and is not claimed as passed.

Before launching a guest, process inventory found QEMU PID `10488` running from `D:\dev\guideXOSServer_DiskManagerImprovements`, with QMP port `61440` and that project's writable ESP. It remained active through the permitted ten-minute passive recheck window. This task did not connect to, terminate, or otherwise interact with it. Guest testing stopped at that evidence boundary. The isolated S16 guest directory was prepared but not booted; its hashes are recorded in [the guest preflight note](evidence/settings-s16-20261016/guest-preflight.md). No S16 screenshots, serial log, QMP transcript, post-apply configuration, or reboot persistence proof exist.

This is Outcome B because the implementation, focused tests, regressions, and production build are complete, while the required real guest mutation and same-ESP reboot proof remain outstanding. S9, Settings 1.0, and v0.3 readiness were not advanced.

## Remaining limits and recommended S17 work

Run the complete production guest sequence when a task-owned isolated QEMU slot is available. Capture the requested screenshots, QMP transcript, serial log, configuration copies, and hashes; confirm native Settings and legacy Display Options show the same committed preset; reboot from the same writable ESP; and verify unrelated `/desktop.cfg` fields remain unchanged. If power-loss atomicity is later required, first design and test a filesystem-level commit mechanism rather than expanding this gradient operation's claim. Keep S16's mutation boundary limited to built-in gradients.
