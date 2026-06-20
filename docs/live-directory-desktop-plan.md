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
- Should `Go to Desktop` mean the real root desktop folder, the user’s `Desktop` folder, or both depending on runtime?
- Should non-desktop directory icon size be a view-local setting, a persisted display option, or a transient mode?
- Should `Show on Desktop` mean “copy/pin the item to `/Desktop`” or “add a desktop reference/shortcut record”?
- Should the bare-metal kernel shell eventually mirror the hosted `cd` bridge, or remain independent for parity reasons?
