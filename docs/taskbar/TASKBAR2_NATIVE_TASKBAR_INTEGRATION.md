# TASKBAR2 — Native Taskbar Integration and Feature Parity

**Outcome:** `TASKBAR2_NATIVE_HOSTED_VERIFIED_GUEST_PENDING`

**Branch:** `TASKBAR_IMPROVEMENTS`

**Starting commit:** `5cd74fc3056c4c04be66bd8bd6ae054eceb6bed7`
**Completion commit:** recorded after local commit

## Existing hosted and native behavior

TASKBAR1 added canonical App Model grouping, independent persistent taskbar pins, window selection, and context actions to the hosted Win32 compositor. Its entries were derived from the hosted compositor's live window list and desktop configuration. The native kernel taskbar still made one fixed-array button per window; it did not group by App Model identity, persist taskbar pins, or provide taskbar application menus.

The native `KernelCompositor::s_windows` registry and each window owner's trusted runtime App Model ID provide the data needed for native grouping. Native launches resolve through `AppManager`, and the mounted VFS settings volume provides the durable store when it is writable. Native rendering and input remain separate from the hosted Win32 transport.

## Shared model and native behavior

- Added `taskbar_application_model_core.h`, a standard-library-free policy header shared by hosted and native code. It centralizes canonical identity checks/comparison and click decisions for launch, activation, minimize, and multi-window selection.
- Kept the hosted `TaskbarApplicationEntry` model as the source for hosted behavior. It now retains stale pins visibly, offers safe unpinning, and uses stable application ordering based on the oldest live window ID.
- Reworked native `TaskbarManager` to derive each update from the live compositor window registry. Windows group only when their owners have the same non-empty canonical App Model ID; anonymous windows remain individual entries regardless of title. Pinned entries precede running entries and merge with a running group of the same ID.
- Native entries show labels, application icon colors, focused/running/minimized state, and a count marker for groups. Clicking an idle pinned app launches through `AppManager::launchAppById`; an existing instance is activated instead of duplicated. A multi-window entry opens a chooser with one row per window, and actions use stable window IDs.
- Native taskbar menus support activate/restore, minimize, maximize/restore down, close one window, close all windows in the same canonical group, and pin/unpin. Menu clicks are consumed by the overlay; Escape, arrow keys, Enter, hover, wheel scrolling, and outside-click dismissal are routed through the desktop input path.

## Pin persistence

Native taskbar pins use the versioned VFS file `/desktop.taskbar.pins`, separate from Start Menu pins and existing desktop configuration. The list is bounded to eight canonical IDs. Missing or uninstalled IDs remain visible and can be unpinned; they are not launched. Writes are verified by reading the bytes back.

The taskbar distinguishes an unavailable settings mount/read-only mount (`Unsupported`) from invalid data or failed read/write verification (`StorageFailure`). A corrupt or malformed existing pin file is preserved: taskbar changes remain available for the session, but the invalid file is not overwritten and the menu reports the storage error. Durable persistence is only claimed when the guest has a writable mounted settings volume and a restart confirms the saved pins.

## Input, layout, and rendering

The native desktop now routes taskbar pointer clicks, right-click menus, popup keyboard input, and popup wheel scrolling before underlying window/desktop input. Taskbar app entries consume their clicks even if an app cannot be launched. Focus activation restores minimized windows; minimizing a focused window clears focus and selects another visible, non-minimized window when available.

Taskbar layout reserves space for Start, search, Terminal, Show Desktop, clock, and tray widgets before assigning the application button area. The geometry is recalculated from the current dock rectangle and is bounded for top, bottom, left, and right docks. Popup geometry is clipped to framebuffer boundaries. The hosted layout tests cover non-origin virtual desktop coordinates; native multi-monitor interaction has not been guest-qualified in this run.

## Verification

- Hosted executable build, `build.bat`: **PASS**. The build output contains existing warnings in unrelated code; no taskbar build errors.
- Focused taskbar application model/configuration suite: **PASS**, 37 assertions. Covers canonical grouping, anonymous-window separation, identical-title separation, stable ordering, activation/minimize/restore/launch decisions, per-window group targets, pin/unpin availability, stale pins, persistence round-trip, docked geometry, hit testing, and compact display bounds.
- Compositor pointer-button regression: **PASS**.
- Window UI interaction regression: **PASS**.
- Compositor resize-notification regression: **PASS**.
- App Model identity suite: **PASS**, 25/25 checks.
- App Model application-actions suite: **PASS**, 55/55 assertions.
- Native AMD64 kernel build, `mingw32-make -C kernel ARCH=amd64 all`: **PASS**. This compiles the production native taskbar/compositor integration.
- `git diff --check`: **PASS** before commit.
- QAC guest qualification: **PENDING**. `qac status` reported that the coordinator was unavailable. No reservation or guest was started, so no graphical guest interaction or restart-based pin persistence is claimed.

## Remaining limitations and TASKBAR3

The native taskbar's real pointer/menu behavior, group rendering, dock placement, writable-storage pin persistence, and restart behavior still need bounded guest verification with actual graphical input. The current native integration is build-qualified and shares identity/click policy with the hosted model; the full native window registry and menu renderer remain freestanding platform-specific code. Native multi-monitor behavior also needs qualification on a configuration that exposes multiple displays.

Recommended TASKBAR3 work is to obtain the exclusive QAC lane, exercise multiple native application windows and menu targeting through genuine guest input, pin/unpin and relaunch apps, then restart the guest with writable settings storage and verify pin recovery. Record visible guest evidence and any display-coordinate limitations.

## Local integration state

- Feature changes and this report are committed locally on `TASKBAR_IMPROVEMENTS`.
- No push or merge was performed.
