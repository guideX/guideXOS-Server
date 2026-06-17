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
- Shell `cd` updates only shell-local current-directory state at the moment.
- Display Options currently controls desktop system icon visibility, not desktop directory mode or icon sizing.

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
- Should shell `cd` ever affect the desktop automatically, or should that remain a later opt-in bridge?
