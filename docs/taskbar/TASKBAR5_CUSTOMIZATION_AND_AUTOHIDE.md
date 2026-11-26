# TASKBAR5 — Customization, Auto-Hide, Sizing & Layout

**Outcome:** `TASKBAR5_HOSTED_VERIFIED_GUEST_PENDING` (Outcome B)
**Starting commit:** `b108990a180a062a17d899e57d498d757cd59fd9`
**Branch:** `TASKBAR_IMPROVEMENTS`

## Implementation

Added the freestanding `taskbar_customization_core.h` model for dock position, sizing, icon presentation, preferences, geometry, usable work areas, reveal strips, and auto-hide transitions. Hosted and native compositors use the shared values and state machine while retaining their existing renderer-specific button layout.

The taskbar supports top, bottom, left, and right docking; compact (32 px), standard (40 px), and expanded (56 px) thickness; and icon-only or icon-with-label buttons. Thickness is clamped for small viewports. Existing application grouping, pin order, running/focus/minimized/attention indicators, previews, overflow, clock, tray, Start, and Show Desktop remain in place. Preview and activity visibility can be switched independently. There is no grouping toggle because grouping remains the existing taskbar behavior.

The hosted taskbar settings popup exposes position, size, auto-hide, labels, previews, and activity controls. Changes apply live. Auto-hide is modeled as `Visible`, `WaitingToHide`, `Hidden`, and `WaitingToReveal`: it waits 500 ms before hiding, reveals after an 80 ms edge dwell, and keeps the bar visible while the pointer is over the taskbar or its popup. A 2 px edge strip remains available when hidden. The compositor timer advances the state; the shared logic introduces no sleeps or animation allocations. Painting, pointer hit-testing, and reveal geometry now use the same viewport bounds. A regression assertion covers the standard bottom dock for the 1024×730 hosted viewport.

The shared geometry computes all four work-area reservations. A permanently visible taskbar reserves its thickness; auto-hide returns the full display work area. Changing position or size updates taskbar geometry and recalculates hosted maximized bounds while preserving restore bounds where supported.

## Settings and persistence

Hosted preferences are stored in the existing `desktop.json` configuration alongside taskbar pins, which remain separate from Start Menu pins. Native preferences are stored as version 2 key/value data in `/desktop.cfg`. Missing and unsupported position, size, and presentation values normalize to safe defaults; unknown keys are ignored. Hosted writes use a temporary file, read-back validation, and replacement so a failed save does not replace the previous valid configuration. Native persistence uses the existing VFS configuration path. Guest restart persistence was not graphically qualified in this run.

## Hosted and native behavior

Both paths use the shared preference enums, thickness and dock geometry, reveal geometry, work-area helper, and timed visibility controller. The native compositor retains kernel-safe input and rendering code; the hosted path retains its GDI renderer. Model coverage includes negative virtual origins and four-edge geometry, but actual independent taskbars on multiple monitors are not implemented. The current compositor exposes one taskbar configuration associated with the primary/main viewport; per-monitor taskbar ownership and popup placement remain future work.

## Screenshot evidence

Genuine hosted screenshots are stored as untracked local evidence under `out/taskbar/TASKBAR5/`:

| Artifact | Captured state |
| --- | --- |
| `hosted-standard-bottom.png` | Standard bottom taskbar |
| `hosted-compact-icon-only.png` | Compact, icon-only taskbar |
| `hosted-expanded-labels.png` | Expanded taskbar with labels |
| `hosted-left-docked.png` | Left-docked vertical taskbar |
| `hosted-right-docked.png` | Right-docked vertical taskbar |
| `hosted-autohide-revealed.png` | Auto-hide taskbar in its revealed state |
| `hosted-settings.png`, `hosted-autohide-settings.png` | Taskbar settings popup |
| `hosted-preview-customized.png` | Application preview with customized taskbar |

The auto-hide revealed screenshot predates the final hosted viewport hit-test adjustment. The corrected 1024×730 geometry is covered by the taskbar model regression; a final UI retake could not be completed because the desktop CUA target resolved to the Codex host window instead of the compositor. No simulated screenshot was created.

## Tests and builds

| Check | Result |
| --- | --- |
| `scripts/run-taskbar-application-model-test.ps1` | PASS — 91 assertions, including sizing, presentation, four-edge geometry, negative origins, work areas, hide/reveal timing, persistence, and safe normalization. Log: `out/taskbar/TASKBAR5/taskbar-model-test-final.log`. |
| `scripts/run-compositor-pointer-buttons-test.ps1` | PASS. Log: `out/taskbar/TASKBAR5/pointer-buttons-test-final.log`. |
| `scripts/run-appmodel-application-actions-test.ps1` | PASS — 55/55. Log: `out/taskbar/TASKBAR5/appmodel-actions-test-final.log`. |
| `tests/window_ui_regression_test.cpp` | PASS — compiled with `g++ -std=c++17 -Wall -Wextra -O2 -iquote .`. |
| `tests/compositor_resize_notification_test.cpp` | PASS — compiled with the same flags. |
| `build.bat` | PASS — hosted executable linked. Log: `out/taskbar/TASKBAR5/hosted-build-final.log`. |
| `mingw32-make -C kernel ARCH=amd64 all` | PASS. Log: `out/taskbar/TASKBAR5/native-build-final.log`. |
| `mingw32-make -C kernel ARCH=x86 all` | PASS. |
| `git diff --check` | PASS; Git reported line-ending normalization notices only. |

The shared controller uses fixed-size state and timestamp comparisons; no blocking waits or new polling loop were added. Existing preview/cache limits remain unchanged.

## Guest qualification and limitations

QAC granted an exclusive reservation for the direct x86 guest run. The guest reported text mode with no framebuffer (serial reported zero framebuffers and 0×0 dimensions), so taskbar pixels and graphical interactions could not be exercised. The guest was closed cleanly and the reservation released. `out/taskbar/TASKBAR5/qac-guest-text-mode.png` preserves the actual guest capture; it contains no usable taskbar view. No repeated guest launch was attempted.

TASKBAR4 documentation or local work was not present at preflight; TASKBAR1–TASKBAR3 reports and behavior were preserved. Outcome B is appropriate: implementation, hosted checks, screenshots, and native builds are complete, while native graphical guest acceptance remains pending.

## Recommended TASKBAR6 work

1. Qualify taskbar input, auto-hide, settings persistence across restart, and prior taskbar behavior in a graphical QAC guest once a framebuffer-capable boot path is available.
2. Retake the auto-hide interaction screenshot after the final viewport hit-test adjustment.
3. Add per-monitor taskbar ownership only if the compositor and window manager can provide stable display-local input, work areas, and popup anchors.
