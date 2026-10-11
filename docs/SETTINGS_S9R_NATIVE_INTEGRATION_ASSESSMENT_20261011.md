# Settings S9R native integration assessment

Date: 2026-10-11

Result: **Outcome C — blocked at the production runtime boundary.**

The existing twelve-page Settings Center is a hosted server application. The
bare-metal image is a freestanding kernel with different application,
window, input, and provider interfaces. No production Settings entry point or
adapter currently connects those runtimes. Reusing the hosted application
would require a new managed runtime in the guest; using the current native
application ABI would require replacing its hosted UI and provider adapters.
Both exceed the allowed integration boundary, and a second Settings
implementation would violate S9R's reuse requirement. No Settings launch
route was changed and no simplified substitute was added.

This updates the S9R investigation only. It does not accept S9 / Settings 1.0,
advance a phase marker, or change v0.3 release artifacts.

## Repository and evidence reviewed

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `51b28451dabffc6275bb8092ac5c9f18c3dfcbe0`
- Starting tracked worktree: clean
- `.phase`: absent and left absent
- `HEAD` was the authorized starting revision. Its only S9-related continuation
  commit fixes hosted compositor resize notifications and records the earlier
  464-check / QEMU recovery evidence in
  `docs/SETTINGS_S9_ACCEPTANCE_RECOVERY_20261011.md`.
- Preexisting untracked candidate images and logs under `out/` were preserved.
  Rerun hosted-test output is under
  `out/validation/settings-s9r-final-20261011/`; the isolated kernel build is
  under `out/validation/settings-s9r-architecture-20261011/`.

## Runtime architecture discovered

### Hosted Settings Center

The full Settings application is implemented by root-level
`settings_center.cpp` and `settings_center.h`. The hosted server project
compiles `settings_center.cpp` (`guideXOSServer.vcxproj:422`). Its
`SettingsCenter::Launch` creates a hosted process with App Model identity
`gxos.builtin.settings`, then publishes a `Settings|width|height` create
message to the server compositor. The app consumes `gui.output` IPC messages,
handles resize/focus/input, and draws through the hosted compositor protocol.
The page renderers and existing models cover all twelve categories.

The metadata entry in `built_in_app_metadata.h:123` has
`BuiltInAvailabilityHosted`, a null kernel app name, and no bare-metal
availability bit. Consequently the kernel's metadata-driven registration
does not register Settings. Its `registerKernelApps()` factory map has no
Settings Center factory (`kernel/core/kernel_apps.cpp:16475`).

The hosted code directly depends on `ProcessTable`, `ipc::Bus`, `Compositor`,
`DesktopService`, display/background services, and C++ standard-library
containers and strings. A compile probe with the kernel's freestanding flags
stops at `<array>` in `settings_center_model.h`; the kernel uses
`-ffreestanding -nostdlib -nostdinc++` (`kernel/Makefile:97`). The Makefile
builds only `kernel/core/*.cpp` (`kernel/Makefile:119`), so the root-level
Settings implementation is not part of the production kernel image.

### Bare-metal application and window APIs

The built-in kernel app API is `KernelApp` plus `AppManager`: apps provide
`init`, `shutdown`, input callbacks, and `draw`; registration takes a kernel
factory, and `KernelCompositor` owns their windows and focus. Existing apps
such as Display Options implement this API directly in `kernel_apps.cpp`.

The Native ELF runtime is another genuine guest app path. Its C ABI supports
window creation, text/rectangle drawing, frame presentation, event polling,
file access, and exit (`sdk/include/guidexos/abi.h:100-124`). It does not
expose Settings provider calls or an immutable built-in App Model identity to
the app. The ABI therefore cannot call the existing Settings providers while
preserving their authorization boundary. The current C ABI could host a newly
written settings app, but that would replace the existing hosted UI rather
than compile and run it unchanged.

### Provider and authorization boundary

The Settings provider design is already valuable and must remain
authoritative: Network, Devices, and Storage read service snapshots; Apps
uses the live registry; Display and Personalization use their existing
configuration owners; Accessibility uses the shared preference; Users
accurately reports unsupported human-account administration. The hosted
Network and inventory calls authorize through the active process table: the
current PID must identify as process `settings` with app ID
`gxos.builtin.settings` (`settings_network_service.h:49`). The identity is
assigned by `SettingsCenter::Launch`, not accepted from UI arguments. The
Native ELF host-call table has no corresponding identity or provider service
call. Passing a Settings role from app-controlled text or bypassing that check
would be unsafe and was not attempted.

The production kernel has some underlying services and snapshots, but there
is no existing Settings-facing runtime bridge from a native guest app to all
of the hosted provider contracts. Thus “compile the hosted Settings source
into the kernel” is not a source-list-only change.

## Launch route and legacy Control Panel

The Start menu already displays distinct right-column labels for `Control
Panel` and `Settings` (`kernel/core/desktop.cpp:1933-1947`). Their dispatch
currently collides in two places:

1. `show_start_menu_notification()` explicitly sends `Control Panel` to the
   legacy embedded control area and sends `Settings` to that same area
   (`kernel/core/desktop.cpp:11854-11866`).
2. The launch target resolver maps `Settings` to `Control Panel` with a
   documented temporary fallback (`kernel/core/app_launch_target_resolver.cpp:164-168`).

The legacy `Control Panel` route remains independently represented and its
existing Display Options path remains in the desktop. The historic guest
capture `out/validation/settings-s9-guest-20261011-130249-105f29e5/display-options-desktop-icons.jpg`
shows the real Display Options window. No routing change was made because
there is no native Settings app to receive a dedicated dispatch. Start-menu
Settings must not be redirected to a new target until its native launch and
navigation work.

## Validation performed

### Hosted and shared contract suites

All S9 Settings/provider regression suites were rebuilt and rerun after the
freestanding header repair in an isolated output directory.
**464/464 checks passed**:

| Suite | Checks |
|---|---:|
| Settings Center | 89/89 |
| S6 Apps and Date & Time | 42/42 |
| S7 Accessibility, Developer, navigation/search | 58/58 |
| S8 Users, navigation, interaction/layout | 32/32 |
| Devices and Storage inventories | 45/45 |
| Network contract and transaction | 66/66 |
| System-service bridge | 78/78 |
| Block-device generation | 5/5 |
| Default Apps model and interaction | 48/48 |
| Compositor resize notification | 1/1 |
| **Total** | **464/464** |

The Default Apps suite also completed 100/100 lifecycle cycles. The S7 build
reported one existing unused-variable warning; no suite failed. These tests
validate hosted models, providers, bridge contracts, and the existing resize
protocol. They do not demonstrate a Settings page in a guest. There is no
dedicated `app_launch_target_resolver` test source or runner in the repository;
the current resolver path was inspected directly.

### Production kernel build

An isolated AMD64 production kernel build initially exposed a pre-existing
freestanding include mismatch: shared `app_model_limits.h` included
`<cstddef>` even though the kernel uses `-nostdinc++`. Replacing it with the C
header `<stddef.h>` preserves the same global `size_t` constants and allows
the existing freestanding target to build. The full kernel build then passed
with 0 compiler errors and 80 warning diagnostics. The resulting
`kernel.elf` is 2,646,208 bytes with SHA-256
`ED141D01B7C826B8E4C6DA1E834058DB2F3CF0EBF1075F31E49E615F3E783F8D`.
The build log and artifact are retained under
`out/validation/settings-s9r-architecture-20261011/`.

This build verifies the existing kernel target after the header portability
repair. It does not compile `settings_center.cpp` and is not a Settings
integration build.

### QEMU and guest evidence

The existing S9 recovery artifacts contain real QEMU serial logs and
screenshots. The recovery report records three independent ordinary boots
and a separate graphical boot; subsequent isolated folders contain additional
guest boot/reboot evidence. A calibrated QMP pointer procedure is present in
the retained guest artifacts. The guest serial log reports a real E1000
network snapshot, and the Display Options capture shows the existing native
window. The same-ESP reboot evidence shows `gradient_sunset` loaded after
restart.

There is no screenshot, serial marker, or QMP record proving that the twelve
Settings categories opened or were navigated in the guest. The legacy Control
Panel/Display Options paths are evidence for their own surfaces only.

Before validation, QEMU process inspection found PID 3448 running from the
sibling `D:\dev\guideXOSServer_DiskManagerImprovements` checkout. It was left
untouched. No new QEMU process was started during this assessment; therefore
the freshly built `ED141D01...` kernel does not have a new boot result here.
No full native Settings guest lifecycle, keyboard/mouse navigation,
close/reopen, or category evidence is claimed.

## Configuration and FAT findings

The retained first guest export
`out/validation/settings-s9-guest-20261011-130249-105f29e5/ESP/desktop.cfg`
is 257 bytes and ends with the partial key `autoArrangeDesktopIcon`. The
later same-ESP reboot configuration is 261 bytes and contains the complete
`autoArrangeDesktopIcons=0` line. More decisively, the guest serial log records
`/desktop.cfg` overwrite of 261 bytes with `FAT_FILE_WRITE_OK` and
`VFS_OK`, followed by a wallpaper readback with `match=1`; the next boot also
logs a successful 261-byte primary write. The serializer in
`kernel/core/desktop.cpp:1260-1310` emits the complete field, and the missing
boolean value is the default false value.

No kernel write defect was reproduced. The available artifacts do not record
the exact moment the first 257-byte host export was taken, so whether it was a
live snapshot/export race or a stale seed copy cannot be proven. The evidence
points to an incomplete external snapshot rather than the guest's completed
VFS write; the precise export cause remains unverified.

The serial logs also report `FAT_FILE_WRITE_INVALID_NAME` for legacy dotted
sidecar paths such as `/display-options.cfg`, `/desktop.taskbar.position`,
`/desktop.system.icons`, `/wallpaper.id`, and
`/desktop.background.scale`. These names do not fit the current FAT filename
writer's supported name form. The primary `/desktop.cfg` is a valid 8.3 name,
contains the authoritative settings, and is written/read back successfully.
The sidecar failures are real compatibility errors, but they did not prevent
the demonstrated primary wallpaper persistence. No filesystem-wide change
was made within S9R scope.

## Smallest feasible follow-up milestone

S9R implementation should resume only after a separately scoped shared
runtime seam is approved and designed. The smallest useful milestone is to
define a native Settings application contract with:

1. A reusable Settings presentation/state layer that can run under both the
   hosted compositor and the production `KernelApp` or Native ELF window API.
2. Provider adapters for the existing authoritative kernel snapshots and
   existing hosted owners, with unsupported capabilities represented
   explicitly.
3. A trusted, runtime-assigned `gxos.builtin.settings` identity/capability
   path for native Settings service calls; app arguments must not grant it.
4. A dedicated native registration and Start-menu dispatch while keeping the
   legacy Control Panel route unchanged.

That milestone must first prove one read-only provider-backed page and a
native window/input/close-reopen cycle. Only then should the existing twelve
categories be connected and full QEMU acceptance resumed. A freestanding
port of all hosted renderer/provider code or a new managed process runtime
would require a wider scope decision.

## Acceptance status

| S9R gate | Status |
|---|---|
| Existing Settings Center compiled into production | **Not met** |
| Native Settings entry point and dedicated launch | **Not met** |
| Twelve categories visited in QEMU | **Not met** |
| Native Settings mouse/keyboard and lifecycle | **Not met** |
| Representative native provider-backed state | **Not met** |
| Legacy Control Panel source path preserved | **Pass by inspection; prior guest Display Options evidence** |
| Hosted/provider regression suites | **Pass, 464/464** |
| Current production kernel build | **Pass after freestanding header repair** |
| Native Settings guest evidence | **Absent** |
| Sunset persistence through same-ESP guest reboot | **Previously demonstrated** |

S9 / Settings 1.0 remains unaccepted. v0.3 readiness was not advanced, and
no physical hardware test is needed to resolve this software architecture
boundary; the eventual acceptance target remains a real QEMU guest.
