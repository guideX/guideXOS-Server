# Live Directory Desktop Recon

Date: 2026-06-16

This note captures the current implementation anchors for the Phase 0 / Phase 1 live-directory desktop arc. It is intentionally scoped to observed code paths and avoids design overreach.

## Current Source Anchors

### Hosted desktop / compositor

- [compositor.cpp](../compositor.cpp)
  - `refreshDesktopItems()` builds the hosted desktop surface from system objects, VFS-backed desktop entries, and persisted shortcuts around lines 938-972.
  - `openDesktopItem()` launches system objects, filesystem entries, and shortcuts around lines 1569-1619.
  - `openDesktopShortcutTargetLocation()` already opens the parent location for file/folder shortcuts around lines 1547-1567.
  - Desktop right-click handling and icon hit-testing are in the WM_* handlers around lines 2480-2506 and 3020-3050.
  - Desktop icon persistence uses `desktop.json` `iconPositions` plus `saveDesktopConfig()` around lines 1050-1053 and 2529-2532.

- [desktop_service.cpp](../desktop_service.cpp)
  - `OpenFilesystemEntry()` routes folders to File Explorer and obvious file types to Notepad/Image Viewer around lines 3559-3590.
  - `LaunchStorageDiagnostic()` and preview helpers document hosted vs bare-metal launch storage around lines 2316-2499 and 2529-2625.
  - `ResolveLaunchTarget()` and `LegacyDispatchStringForLaunchTarget()` remain the current launch-resolution boundary around lines 1872-2032.

- [desktop_config.h](../desktop_config.h)
  - `DesktopConfigData` currently persists `pinned`, `recent`, `desktopShortcuts`, `windows`, `iconPositions`, and desktop icon visibility flags.
  - There is no stored desktop-directory field yet.

### Hosted File Explorer

- [file_explorer.cpp](../file_explorer.cpp)
  - Navigation state lives in `s_currentPath`, `s_backHistory`, and `s_forwardHistory` around lines 358-376.
  - `navigate()`, `goBack()`, `goForward()`, `goUp()`, and `goHome()` are implemented around lines 510-558.
  - The toolbar already exposes `< Back`, `> Fwd`, `Up`, `Refresh`, `Address`, and create/rename/delete actions around lines 1111-1165.
  - The context menu now exposes `Pin to Desktop` and folder-only `Show on Desktop` actions around lines 1006-1033 and 1266-1276.
  - `renderToolbar()` draws the back/up controls and `renderNavigationPane()` provides root navigation around lines 1111-1190.

- [file_explorer.h](../file_explorer.h)
  - `navigate()`, `goBack()`, `goUp()`, `goHome()`, and `pinSelectedToDesktop()` are explicit API points.

### Bare-metal kernel desktop

- [kernel/core/desktop.cpp](../kernel/core/desktop.cpp)
  - Desktop icon persistence uses `/desktop.shortcuts`, `/.desktop_icons`, and `/desktop.system.icons` around lines 1797-1818, 2115-2248, and 2276-2329.
  - The desktop folder enumerator reads `/Desktop` via VFS in `enumerate_desktop_folder_items()` around lines 1890-1935.
  - Icon rendering uses `s_desktopIconSize` and the themed icon cache around lines 658, 3933-4005.
  - `show_icon_notification()` launches folders with `Files`, text files with `Notepad`, and system objects with built-in shell/app handlers around lines 8244-8508.
  - The desktop right-click menu and click handling remain desktop-surface specific around lines 4900-4990, 6515-6565, 9773-9794, and 8351-8508.

- [kernel/core/shell.cpp](../kernel/core/shell.cpp)
  - `s_cwd` and `cmd_cd()` implement shell current-directory state around lines 170 and 593-613.
  - `get_cwd()` exposes the current shell directory around lines 3258-3260.
  - There is no desktop sync hook in the `cd` path yet.

### Display options

- [display_options.cpp](../display_options.cpp)
  - The current UI persists only desktop system icon visibility flags (`Trash`, `ThisSystem`, `FileManager`, `SystemSettings`) around lines 145-160 and 537-567.
  - The UI’s Desktop Icons tab is currently a checkbox list, not a directory-mode or icon-size control surface.

- [right_click_menu.cpp](../right_click_menu.cpp)
  - Phase 1D adds a persisted hosted live-folder compact icon preference without changing the existing checkbox-first layout.
  - Desktop context menus currently expose `Refresh`, `Display Options`, and a placeholder `Icon Size` submenu around lines 92-147.
  - The submenu logs a selected size but does not persist or apply it yet.

### Existing smoke / status scripts

- [scripts/smoke-appmodel-launchshadow.ps1](../scripts/smoke-appmodel-launchshadow.ps1)
- [scripts/smoke-appmodel-phase2-status.ps1](../scripts/smoke-appmodel-phase2-status.ps1)
- [scripts/smoke-appmodel-typed-dispatch-flags.ps1](../scripts/smoke-appmodel-typed-dispatch-flags.ps1)
- [scripts/smoke-navigator-hosted.ps1](../scripts/smoke-navigator-hosted.ps1)
- [scripts/smoke-navigator-kernel.ps1](../scripts/smoke-navigator-kernel.ps1)
- [scripts/smoke-taskmanager-snapshot.ps1](../scripts/smoke-taskmanager-snapshot.ps1)

There does not appear to be a dedicated desktop live-directory smoke yet. The existing smoke pattern is a good template for a future report-only status check.

## Current Behavior Summary

- Hosted desktop icons are assembled from:
  - persisted app pins and recents,
  - hosted desktop shortcuts stored in `desktop.json`,
  - a live enumeration of the host-side desktop folder through `DesktopFolderResolver`.
- Hosted File Explorer already supports directory navigation, history, and `Pin to Desktop`.
- Bare-metal desktop already enumerates `/Desktop` from VFS and has file/folder open handling, but the desktop itself still behaves like a static icon board with persisted slots rather than a true live directory surface.
- Hosted GUI console `cd` now updates the live hosted desktop surface; the bare-metal kernel shell still keeps its own current-directory state.
- Display Options currently controls desktop system icon visibility, not desktop directory mode or icon sizing.
- Phase 1D extends that same Display Options path with a persisted hosted live-folder compact icon preference.

## Phase 1B Note

- File Explorer now exposes a hosted-only `Show on Desktop` action for folder rows in the right-click menu.
- The new action is wired through a small compositor bridge that reuses the hosted live desktop directory state from Phase 1A.
- Source anchors changed:
  - `file_explorer.cpp` now owns the folder-only context-menu entry and dispatch.
  - `compositor.cpp` now exposes a hosted desktop folder navigation helper.
- This slice stays hosted-only and intentionally does not add shell `cd` sync, smaller non-root icon mode / Display Options persistence, or bare-metal parity.
- Remaining parity gaps:
  - shell `cd` sync,
  - smaller non-root icon mode / Display Options setting,
  - bare-metal parity.

## Phase 1C Note

- Hosted shell/CLI `cd` sync is now wired through the GUI console path.
- Source anchors changed:
  - `console_service.cpp` now parses `cd` and `pwd`, resolves hosted virtual paths, and only updates the desktop after a successful directory change.
  - `desktop_service.h` / `desktop_service.cpp` now expose a small hosted desktop folder bridge that calls the compositor live-navigation helper.
  - `scripts/smoke-live-directory-desktop-status.ps1` now reports the hosted shell bridge instead of treating shell sync as missing.
- This applies to hosted only.
- Bare-metal parity remains deferred because the kernel shell path is still separate and has not been mirrored through the hosted desktop bridge.
- Remaining parity gaps:
  - smaller icon mode / Display Options setting,
  - bare-metal parity,
  - shell edge cases such as quoted paths and broader POSIX/Windows command compatibility.

## Phase 1D Note

- Hosted live desktop folder views now use a compact icon mode when the displayed folder is not the root desktop and the new Display Options preference is enabled.
- The new preference is persisted in `desktop.json` as `smallLiveDesktopFolderIcons`.
- Root desktop views keep the normal icon size and spacing.
- Remaining parity gaps:
  - bare-metal parity,
  - any future shell-path edge cases if the hosted `cd` bridge expands.

## Phase 1E Note

- The hosted desktop right-click `Folder View Icon Size` submenu is now wired to the same `smallLiveDesktopFolderIcons` setting that Display Options uses.
- Choosing `Normal folder icons` or `Small folder icons` updates the live hosted desktop runtime and persists to `desktop.json`.
- Root desktop views still remain normal-sized regardless of the preference.
- The existing Display Options Desktop Icons tab remains the same shared source of truth for the setting when it opens or reloads config.
- Remaining parity gaps:
  - bare-metal parity,
  - visual live run / eyeball verification if still needed,
  - richer shell path parsing, if still relevant.

## Phase 2A Note

- Bare-metal anchors inspected:
  - `kernel/core/desktop.cpp` and `kernel/core/desktop.h`
  - `kernel/core/shell.cpp` and `kernel/core/shell.h`
  - `kernel/core/vfs.cpp`
  - `desktop_folder.h`
  - `desktop_service.cpp`
  - `display_options.cpp`
  - `right_click_menu.cpp`
- What we found:
  - Bare-metal desktop already enumerates `/Desktop` through VFS and already persists desktop icon slots and visibility state.
  - The shell already owns local `cd` / `get_cwd()` state, but there is no desktop sync hook yet.
  - Hosted live-directory helpers in `desktop_folder.h` and `compositor.cpp` are host-filesystem based, so they should not be moved into kernel code as-is.
- Scaffold added:
  - `kernel/core/desktop.cpp` now carries a small bare-metal desktop directory state scaffold with home/current path storage and a refresh hook.
  - The scaffold is intentionally narrow and keeps the default path at `/Desktop`, so hosted behavior and existing bare-metal rendering stay unchanged.
- Exact next bare-metal slice:
  - Wire folder activation to update the bare-metal desktop current path.
  - Add Back and Go to Desktop affordances once the path state can actually change.
  - Decide whether shell `cd` should soft-sync the bare-metal desktop path or remain independent.
  - Add non-root smaller-icon handling only after the navigation state is real.
- Hosted parity status:
  - Hosted live-directory desktop remains fully intact and still owns the richer navigation path, shell bridge, and smaller non-root icon mode.
- Risks and blockers:
  - Kernel desktop rendering still depends on fixed slot/grid assumptions, so navigation should be introduced one state transition at a time.
  - Bare-metal shell sync is not safe to force yet because the kernel shell path is separate from the hosted bridge.
  - Shared helper reuse should stay limited to path normalization ideas unless the helper is fully kernel-safe.
- Historical blocker before Phase 2B:
  - There was no actual bare-metal navigation action to exercise the new directory state, so the scaffold remained intentionally dormant until folder activation was wired up.

## Phase 2B Note

- Bare-metal folder activation now updates the desktop current path instead of launching `Files` for directory targets.
- Source anchors changed:
  - `kernel/core/desktop.cpp` now owns `bare_metal_desktop_resolve_directory_target()`, `bare_metal_desktop_set_current_directory()`, and the folder-activation branch in `show_icon_notification()`.
  - `scripts/smoke-live-directory-desktop-status.ps1` now reports the bare-metal directory state and folder-navigation slice as present instead of scaffold-only.
- Validation behavior:
  - Folder targets are resolved against the current bare-metal desktop path when needed, normalized through VFS, and verified with `vfs::stat()`.
  - The current path changes only after the target exists and is a directory.
  - Successful folder activation refreshes the desktop through the existing `bare_metal_desktop_request_folder_refresh()` path, which re-enumerates the current folder and requests redraw.
  - Root startup behavior remains unchanged: bare-metal still starts at `/Desktop`.
  - Slot persistence remains untouched, and this pass does not persist the current live folder across restart.
- What remains deferred:
  - `Back` and `Go to Desktop` affordances.
  - bare-metal shell `cd` sync.
  - bare-metal non-root smaller icons.
  - persistence of the current live folder, which is intentionally not desired in this slice.
- Non-folder behavior preserved:
  - App launches and file-open behavior still use the existing paths for non-folder targets.
  - Existing desktop shortcut and built-in/system icon behavior is unchanged.

## Phase 2C Note

- Bare-metal now has live desktop navigation affordances for `Back` and `Go to Desktop` when the current folder is not `/Desktop`.
- History is kept only in memory in `kernel/core/desktop.cpp`:
  - navigating into a folder pushes the previous current folder when the target actually changes,
  - `Back` pops the most recent folder and returns there,
  - `Go to Desktop` returns to `/Desktop`,
  - the live folder path and history are not persisted across restart.
- Source anchors changed:
  - `kernel/core/desktop.cpp` now carries the bare-metal navigation history helpers, the `DesktopBack` / `DesktopHome` system objects, and the navigation-only activation branches.
  - `scripts/smoke-live-directory-desktop-status.ps1` now reports `bare-metal-back-go-desktop=present`.
  - `scripts/smoke-appmodel-launchshadow.ps1` now treats bare-metal folder activation as live desktop navigation evidence instead of the stale `Files` launch assumption for the desktop folder cases.
- Rendering behavior:
  - `Back` and `Go to Desktop` render only when the current bare-metal desktop folder is off-root.
  - They use the existing desktop icon rendering path, with no new image assets.
  - They are excluded from desktop slot / shortcut persistence by not participating in the normal desktop icon layout key path.
- Still deferred:
  - bare-metal shell `cd` sync.
  - bare-metal non-root smaller icons.
  - persistence of the current live folder, which remains intentionally undesired.

## Phase 2C.5 Note

- There was no bare-metal home-path drift in the kernel code; the earlier `/` wording came from the report/smoke narrative and the root-folder probe names, not from the live desktop home state.
- Confirmed bare-metal desktop home path: `/Desktop`.
- Confirmed bare-metal `Go to Desktop` target: `/Desktop`.
- Visibility rule: `Back` and `Go to Desktop` render only when the current bare-metal desktop folder is not `/Desktop`.
- `scripts/smoke-live-directory-desktop-status.ps1` now reports explicit home-path evidence so this distinction stays visible in future status runs.
- `scripts/smoke-appmodel-launchshadow.ps1` still uses `/` for the intentional real-root folder probe cases; that smoke is not the source of the bare-metal desktop home semantics.
- Remaining parity gaps are unchanged:
  - bare-metal non-root smaller icons,
  - any cleanup needed for appmodel smoke.

## Phase 2D Note

- Bare-metal shell `cd` now syncs the live desktop folder after a successful directory change.
- Source anchors changed:
  - `kernel/core/include/kernel/desktop.h` exports `sync_live_directory_from_shell_cwd()`.
  - `kernel/core/desktop.cpp` forwards that sync into the existing bare-metal live-directory setter.
  - `kernel/core/shell.cpp` resolves and validates explicit `cd` targets, then calls the desktop sync helper after updating `s_cwd`.
  - `scripts/smoke-live-directory-desktop-status.ps1` now reports `bare-metal-shell-cd-sync=present`.
- Shell-driven desktop navigation pushes the same in-memory Back history used by folder activation because the desktop setter is reused with history enabled.
- Path scope supported:
  - Any valid VFS directory can drive the live desktop view if the bare-metal desktop can enumerate it safely.
  - `/Desktop` remains the bare-metal desktop home.
  - `/` is accepted when it is a valid VFS directory and the desktop can enumerate it.
- Failed or no-op `cd` commands do not update the desktop.
- Still deferred:
  - bare-metal non-root smaller icons,
  - current live folder persistence, which remains intentionally undesired,
  - any extra shell parser edge cases beyond the current `cd` path resolution.

## Phase 2E Note

- Bare-metal non-root live desktop folders now use a compact icon layout with smaller icons, tighter spacing, and narrower hitboxes than `/Desktop`.
- Source anchors changed:
  - `kernel/core/desktop.cpp` now carries the bare-metal layout metrics helper, compact-layout detection, and layout-sensitive icon placement / hit-testing updates.
  - `scripts/smoke-live-directory-desktop-status.ps1` now reports the bare-metal compact-layout anchor as present and exposes the compact-mode config story explicitly.
- Compact behavior is default-on for bare-metal non-root folder views.
  - The shared hosted `smallLiveDesktopFolderIcons` setting is not loaded into kernel code here.
  - That keeps the kernel path self-contained and avoids host-only config dependencies.
- Root `/Desktop` layout remains unchanged.
  - The kernel still uses the normal root desktop icon size, spacing, and hitboxes at `/Desktop`.
  - Compact metrics only apply once the current live desktop folder is not `/Desktop`.
- Slot persistence safeguards:
  - root `/Desktop` still saves and reloads icon positions through `/.desktop_icons`,
  - compact non-root views skip saving icon positions and reflow transiently,
  - Back / Go to Desktop navigation icons stay out of persistence.
- Remaining parity gaps:
  - visual runtime proof / eyeball validation,
  - any dedicated shell or navigation smoke beyond the current status script,
  - current live folder persistence remains intentionally disabled.

## Phase 2F Note

- Evidence tightened in `scripts/smoke-live-directory-desktop-status.ps1`:
  - hosted navigation evidence now points at the actual `Back` and `Go to Desktop` desktop items instead of a history-scaffold line,
  - Display Options and the right-click `Folder View Icon Size` submenu now report as a shared setting,
  - bare-metal compact mode now reports the absence of any kernel `desktop.json` load path, which matches the intended kernel-safe default-on behavior,
  - the summary now includes `hosted-parity`, `bare-metal-parity`, and `live-directory-desktop-parity` keys so the current feature state is easier to read at a glance.
- Validation that passed:
  - `.\scripts\smoke-live-directory-desktop-status.ps1`
  - `.\scripts\smoke-appmodel-launchshadow.ps1`
  - `cmd /c build.bat`
  - `cmd /c build-kernel.bat`
- Runtime / QEMU evidence:
  - deferred for this slice; the existing harnesses gave us reliable source-level smoke and a launch-shadow regression check, but not a practical interactive desktop navigation run.
- Current hosted status:
  - live directory desktop, shell `cd` bridge, `Show on Desktop`, and compact non-root icon handling remain present.
  - hosted parity is treated as live-directory-present in the status script.
- Current bare-metal status:
  - `/Desktop` remains the home path,
  - `Back` / `Go to Desktop` remain present,
  - shell `cd` sync remains present,
  - non-root compact layout remains present and kernel-safe,
  - bare-metal parity is treated as feature-present-evidence-partial rather than full runtime proof.
- Remaining risks:
  - visual eyeball proof is still missing,
  - a dedicated shell-navigation runtime smoke is still deferred,
  - the shell no-arg `cd` home convention is still undecided,
  - current live folder persistence remains intentionally in-memory only and should stay that way.

## Phase 2G Note

- Runtime evidence is now in place for the bare-metal live-directory desktop path.
- Source anchors changed:
  - `kernel/core/desktop.cpp` now includes `run_live_directory_runtime_smoke()` behind `GXOS_LIVE_DIRECTORY_DESKTOP_RUNTIME_SMOKE_ACTIVE`.
  - `kernel/core/main.cpp` now calls the runtime smoke at boot when that diagnostic flag is enabled.
  - `scripts/smoke-live-directory-desktop-runtime.ps1` builds the kernel with the smoke flag, boots QEMU, captures the serial log, and writes a stable evidence file.
  - `scripts/smoke-live-directory-desktop-status.ps1` now treats fresh runtime evidence as the stronger bare-metal parity signal.
- Runtime behavior proven:
  - the smoke aliases `/Desktop` to the existing `/system/wall` directory in the boot image so the home path is actually reachable during the run,
  - live navigation into `/system/wall` succeeds,
  - compact non-root layout activates,
  - `Back` returns to `/Desktop`,
  - shell-driven sync to `/system/wall` succeeds,
  - `Go to Desktop` returns to `/Desktop`,
  - the runtime state is restored before exit.
- Evidence markers now present in the serial log:
  - `LIVE_DESKTOP_HOME_ALIAS`
  - `LIVE_DESKTOP_TARGET`
  - `LIVE_DESKTOP_NAV`
  - `LIVE_DESKTOP_LAYOUT`
  - `LIVE_DESKTOP_NAV_BACK`
  - `LIVE_DESKTOP_SHELL_CD_SYNC`
  - `LIVE_DESKTOP_NAV_HOME`
  - `LIVE_DESKTOP_CLEANUP`
  - `result=PASS`
- Validation that passed:
  - `.\scripts\smoke-live-directory-desktop-status.ps1`
  - `.\scripts\smoke-appmodel-launchshadow.ps1`
  - `cmd /c build.bat`
  - `cmd /c build-kernel.bat`
  - `.\scripts\smoke-live-directory-desktop-runtime.ps1`
- Remaining risks:
  - the runtime proof still depends on the smoke-only `/Desktop` alias because the boot image does not expose a native writable `/Desktop` mount in this environment,
  - the smoke proves navigation and shell sync, not filesystem write support,
  - current live folder persistence remains intentionally in-memory only.

## Phase 2H Note

- We inspected the boot path and confirmed the runtime smoke environment does not expose a writable mounted filesystem at the point where `/Desktop` would need to be created. The kernel auto-mount attempts fail, so the native path remains unavailable in this environment.
- `scripts/smoke-live-directory-desktop-runtime.ps1` now logs whether the smoke ran in `native-desktop-live-smoke` mode or `alias-fallback` mode, and the status script reports that distinction explicitly.
- The smoke still prefers the native path first, but it falls back to the alias route when `/Desktop` cannot be provisioned. The alias remains the documented blocker path for this boot layout.
- The runtime evidence now records the blocker directly so future runs can tell whether the environment is still alias-only or has gained a writable root volume.

## Proposed Phased Plan

### Phase 0

- Confirm the current anchor map and keep the feature surface intentionally read-only.
- Add report-only evidence tooling so we can track what already exists before changing behavior.

### Phase 1

- Introduce a small desktop-directory state model without changing launch semantics.
- Teach the desktop surface to distinguish between:
  - real desktop/root presentation,
  - live folder presentation,
  - navigation affordances (`Back`, `Go to Desktop`).
- Keep non-desktop directory icon sizing separate from the existing desktop icon size path.

### Phase 2

- Wire File Explorer context actions toward a desktop-directory target only after the Phase 1 surface is stable.
- Decide whether shell `cd` should remain independent or become a soft source of truth for the desktop directory.

## Risks / Regression Areas

- App launch dispatch is shared between hosted and bare-metal surfaces; any new desktop-directory plumbing must not disturb `DesktopService::LaunchApp()`, `launchAction()`, or `AppManager::launchApp()`.
- File Explorer navigation already has history, prompts, and context-menu actions; adding a desktop-directory bridge could accidentally create feedback loops if it mutates its own source path.
- Bare-metal and hosted persistence differ:
  - hosted uses `desktop.json`,
  - bare-metal uses VFS files such as `/.desktop_icons`, `/desktop.shortcuts`, and `/desktop.system.icons`.
- The current desktop icon system already has stable-position migration logic; directory-view work should not rewrite the existing icon-position model in the first slice.

## Hosted vs Bare-Metal Parity Notes

- Hosted already has a live host-filesystem desktop enumeration path via `DesktopFolderResolver`.
- Bare-metal already has a live VFS desktop enumeration path via `enumerate_desktop_folder_items()`.
- The two runtimes do not yet share a common desktop-directory state abstraction.
- Hosted currently has richer launch/storage diagnostics, while bare-metal has more direct VFS-backed desktop state.

## Smoke / Evidence Plan

- Keep the feature-status script report-only.
- Reuse the current smoke style by printing:
  - file anchors,
  - presence/absence of desktop-directory state,
  - presence/absence of desktop navigation affordances,
  - presence/absence of icon-size hooks,
  - presence/absence of `Show on Desktop` or equivalent actions,
  - presence/absence of shell-to-desktop sync.
- When Phase 1 starts changing behavior, add one narrow smoke at a time rather than bundling desktop directory mode, shell sync, and context actions together.

## Open Questions

- Should the live desktop directory be a new desktop-only state, or should it mirror `File Explorer`’s current path?
- Should non-desktop directory icon size be a view-local setting, a persisted display option, or a transient mode?
- Should `Show on Desktop` mean “copy/pin the item to `/Desktop`” or “add a desktop reference/shortcut record”?
- Should the bare-metal kernel shell eventually mirror the hosted `cd` bridge, or remain independent for parity reasons?
