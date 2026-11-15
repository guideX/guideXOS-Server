# TASKBAR1 — Core Application Management

**Result:** `TASKBAR1_CORE_MANAGEMENT_HOSTED_VERIFIED`

**Branch:** `TASKBAR_IMPROVEMENTS`

## Existing behavior

The hosted compositor displayed individual taskbar buttons for windows and already handled basic minimize and restore. It did not retain canonical application identity on each window, group an application's windows, or persist taskbar-specific pins. The native kernel desktop has a separate taskbar implementation with basic per-window activation/minimize/restore and dock positioning.

## Implemented

- Added an application-entry model that groups windows only by canonical App Model ID. Windows without an ID stay separate, even when their titles match.
- Derived hosted taskbar entries from current compositor windows and persistent taskbar pins. Taskbar pins use their own `taskbarPinnedAppIds` setting and do not change desktop shortcuts or Start Menu pins.
- Added click-to-activate/minimize/restore, group window selection, supported window controls, close-all for actual multi-window groups, and launch/unpin/pin actions for installed apps with a valid hosted launch target.
- Added dock-aware taskbar layout and matching hit testing for top, bottom, left, and right positions, with virtual desktop coordinates and reserved tray/clock space. The application menu is clamped to the active virtual display.
- Added focused model/configuration coverage for identity grouping, same-title separation, click decisions, group/context actions, stale-window removal, pin persistence, and dock geometry/hit testing.

## Architecture and files

The compositor records the owning process's canonical app ID when a window is created. Each paint/input pass derives entries from the live window list plus `DesktopConfig`; it does not maintain a second running-process list. The App Model remains responsible for launch resolution, and desktop configuration owns the separate persistent pin list.

Changed files:

- `taskbar_application_model.h`
- `desktop_config.h`
- `compositor.h`, `compositor.cpp`
- `right_click_menu.h`, `right_click_menu.cpp`
- `guideXOSServer.vcxproj`, `guideXOSServer.vcxproj.filters`
- `tests/taskbar_application_model_test.cpp`
- `scripts/run-taskbar-application-model-test.ps1`

## Verification

- Hosted `build.bat`: **PASS** (`guideXOSServer.exe` built). The repository's pinned Mbed TLS dependency was bootstrapped first because it was absent.
- Focused taskbar model/config test: **PASS**, 20 assertions.
- Native kernel `mingw32-make -C kernel ARCH=amd64 all`: **PASS**. This verifies the native target still builds; the hosted taskbar changes are not compiled into the separate kernel desktop implementation.
- `git diff --check`: **PASS**.
- Guest QEMU qualification: **PENDING**. No exclusive QAC reservation was confirmed, so no guest session was started.

## Limitations and TASKBAR2 scope

The native kernel desktop remains on its legacy per-window taskbar and does not yet have persistent taskbar pins, canonical app grouping, or taskbar application context menus. TASKBAR2 should port the application-entry behavior into `kernel::compositor::TaskbarManager`, add native pin persistence and menu actions, and qualify it in a guest under an exclusive QAC reservation. Hosted window chooser rows are rendered in the shared context menu; very large groups may need paging in a later usability pass.
