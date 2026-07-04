# guideXOS App Model Current-State Map

Status: inspection pass only

This document maps the app model as it exists in the repository today. It is intended to ground future GXApp runtime work without rewriting or destabilizing current launch behavior.

## 1. Actual languages and components in this repo

The repository is not primarily .NET.

Current app-model-relevant implementation is mostly native C++:

- Hosted / Windows compositor / test harness:
  - `desktop_service.cpp`
  - `compositor.cpp`
  - `server.cpp`
  - `app_registry.cpp`
  - `app_manifest*.cpp`
  - `app_launch_resolver.cpp`
  - `native_app_runtime.cpp`
  - `native_elf_launch_pipeline.cpp`
  - `package_manager.cpp`
  - `gxapp_container.cpp`
  - `gxm_loader.cpp`
- Bare-metal / kernel mode:
  - `kernel/core/desktop.cpp`
  - `kernel/core/kernel_app.cpp`
  - `kernel/core/kernel_apps.cpp`
- There is also a smaller C# FileExplorer area, but it is not the active cross-target app-model path for the OS runtime.

## 2. Current app model in the repo

### 2.1 Hosted manifest model exists

Hosted mode has a real manifest data model:

- `app_manifest.h` defines:
  - `AppKind` (`BuiltIn`, `NativeElf`, `GXAppPackage`, `Service`, `HypervisorGuest`, `Script`)
  - per-architecture `entries`
  - `permissions`
  - `fileAssociations`
  - `defaultWindow`
  - `desktopRegistryHints`
- `app_manifest_loader.cpp` loads `app.json`
- `app_manifest_validator.cpp` validates manifest fields and relative paths
- `app_registry.cpp` scans for `app.json` and registers apps from:
  - `/system/apps`
  - `sdk/samples`
  - `examples/apps`
  - `/Apps`
  - `/users/default/apps`

### 2.2 Hosted built-ins are also mirrored as synthetic manifests

`AppRegistry::RegisterBuiltInAppsAsManifests()` creates synthetic manifest records for built-in apps such as:

- Notepad
- Calculator
- Clock
- Console
- FileExplorer
- Trash
- TaskManager
- Paint
- ImageViewer
- OnScreenKeyboard
- ShutdownDialog
- DiskManager
- ControlPanel
- DisplayOptions
- guideXOS Navigator
- App Model Demo
- Native App Debug Viewer
- HDInstaller

This lets hosted UI surfaces use a manifest-shaped registry even though actual built-in launch still uses hardcoded dispatch.

### 2.3 App ID ownership convention

Phase 1 diagnostics treat duplicate app IDs as non-fatal, but app IDs now have explicit ownership:

- `gxos.builtin.*` is reserved for built-ins described by `built_in_app_metadata.h`.
- `com.guidexos.samples.*` is reserved for SDK source sample manifests under `sdk/samples`.
- `com.guidexos.examples.*` is reserved for repository example manifests under `examples/apps`.
- Installed `/Apps/*` manifests use normal installed app IDs, even when they are staged from SDK sample source manifests.

This convention is display/identity-only in Phase 1. It does not replace the current hosted or bare-metal launch dispatch paths.

The hosted app-model diagnostics include a non-fatal namespace validator. It reports warnings when:

- filesystem manifests use the reserved `gxos.builtin.*` built-in namespace
- SDK source manifests under `sdk/samples` do not use `com.guidexos.samples.*`
- repo example manifests under `examples/apps` do not use `com.guidexos.examples.*`
- installed `/Apps/*` manifests use built-in, SDK sample, or repo example namespaces

These warnings are informational only. They do not block registration, change duplicate handling, or alter launch behavior.

Use `desktop.appmodel.summary` for a compact one-screen app-model health check. It reports registered app totals, manifest and synthetic built-in counts, duplicate ID status, namespace warning status, hosted and bare-metal metadata coverage, target drift, compact launch-target comparison counts, and an overall `OK` / `WARN` result. Detailed output remains available through `desktop.appmodel.coverage`, `desktop.apps.verbose`, and `desktop.launch.compare`.

`desktop.appmodel.coverage` also includes a non-fatal compositor/UI launch alias check. It compares known hosted UI launch labels, registered built-in display names, and legacy aliases against `built_in_app_metadata.h`. This is diagnostic-only: it reports clean metadata matches, labels that still need alias fallback, labels with no metadata match, and hosted metadata launch names that do not appear reachable from current UI label paths.

The remaining alias warnings are expected Phase 1 residue, not launch failures:

- `AppModel` is a legacy hosted UI alias for `App Model Demo` / `gxos.builtin.appmodeldemo`. Keep the alias until Phase 2 introduces a proper launch-resolution surface for old pins, shortcuts, and saved config.
- `ComputerFiles` is a shell/system label for the File Manager entry. It is intentionally not a built-in app metadata identity yet because it represents a desktop shell affordance, not a separate app registration.

`Console` remains a shell/system affordance and does not participate in normal typed-ready built-in app promotion.
`ControlPanel` is a hosted-only built-in metadata identity, while the visible `Control Panel`, `Settings`, and `System Settings` affordances stay on the `DisplayOptions` bridge path as non-fatal drift. Their user-visible launch behavior remains unchanged.

App Model v1.2 adds `DisplayOptions` as the next typed-ready built-in target (`gxos.builtin.displayoptions`). App Model v1.3 adds `TaskManager` as an additional typed-ready built-in target (`gxos.builtin.taskmanager`). App Model v1.4 adds `Trash` as another typed-ready built-in target (`gxos.builtin.trash`). App Model v1.5 adds `DiskManager` as another typed-ready built-in target (`gxos.builtin.diskmanager`). App Model v1.7 promotes `Clock` as another typed-ready built-in target (`gxos.builtin.clock`).

App Model v1.6 target selection was deferred: no v1.6 typed target had been selected yet. `Paint` remains deferred because it is hosted-only in built-in metadata and does not yet have hosted and bare-metal parity without changing runtime launch behavior. `DiskManager` remains the accepted v1.5 baseline. `Clock` is now accepted as the v1.7 typed-ready promotion target.

App Model v1.x closure:
- No further safe typed-ready target remains without new bare-metal parity work.
- v1.5 `DiskManager` is the current accepted baseline.
- v1.6 was deferred.
- v1.7 promotes `Clock`.
- `Paint` still requires bare-metal implementation before typed-ready promotion.
- `ImageViewer` remains deferred until active Image Viewer stabilization is complete.

Both should be revisited during Phase 2 launch-resolution cleanup. That pass should decide whether aliases become explicit app-model launch aliases, shell commands, or another typed launch target while preserving existing user-visible shortcuts.

Phase 2 typed launch target planning now lives in `docs/APP_MODEL_PHASE2_TYPED_LAUNCH_TARGETS.md`. That document is design-only and does not change the current hosted or bare-metal dispatch paths.

Phase 2A has started with a diagnostic-only typed target surface. `app_launch_target.h` defines the lightweight enum/struct, and `desktop.launch.resolve <label>` reports how a launch label resolves without launching anything. Hosted diagnostics call `DesktopService::ResolveLaunchTarget()` without changing `DesktopService::LaunchApp()`. Bare-metal diagnostics call `kernel::appmodel::resolveLaunchTarget()` without changing `AppManager::launchApp()`. Use it to inspect labels such as `Notepad`, `Calculator`, `gxos.builtin.notepad`, `AppModel`, `ComputerFiles`, `Files`, and `guideXOS Navigator` before any runtime dispatch migration.

`desktop.launch.adapt <label>` is also available in hosted and bare-metal shells. It resolves the label, runs the diagnostic-only legacy dispatch adapter, and reports the string that current dispatch surfaces would expect. This does not feed back into launch yet.

`desktop.launch.storage` reports the current launch-string storage sites without migrating them. Hosted output includes live `desktop.json` counts for `pinned`, `recent`, `desktopShortcuts`, and `iconPositions`, plus in-memory `DesktopService`, compositor-derived Start Menu sites, and the fact that hosted taskbar buttons are not a separate persisted pin store. Bare-metal output maps static Start Menu lists, `/desktop.shortcuts`, `/.desktop_icons`, `/desktop.system.icons`, runtime desktop icon fields, and the currently disabled/static taskbar entry surface.

`desktop.launch.storage.preview` is a read-only Phase 2 migration preview. It resolves each inspected stored string to a `LaunchTarget`, shows the existing value/kind, adapter legacy dispatch string, proposed typed record shape, risk, and status, then summarizes `ready`, `alias`, `shellAction`, `unresolved`, `skip-layout-only`, and `targetSpecificUnsupportedAliases` counts. It does not write `desktop.json`, VFS files, or any in-memory launch state.

`desktop.appmodel.summary` includes those storage preview counts as a compact `launchStoragePreview` line. The summary stays `OK` while `unresolved` and `highRisk` are zero; future non-zero counts may produce `WARN`, but remain non-fatal and do not trigger migration.

`desktop.launch.storage.preview.compare` compares compact hosted and bare-metal storage preview counts without printing row-level details or migrating storage. It calls out intentional target-specific differences such as hosted `desktop.json`, bare-metal VFS storage files, target-specific Start Menu sources, shell/system labels, and the bare-metal static `ImgViewer` label. `ImgViewer` is recognized as a diagnostic-only legacy/static label for the hosted `ImageViewer` identity, but `ImageViewer` is still hosted-only and has no current bare-metal `AppManager` registration. These labels increment `targetSpecificUnsupportedAliases` and appear in grouped `targetSpecificUnsupportedAliasDetails`, so they remain visible without becoming `unresolved`, `highRisk`, or unexpected drift. Unexpected high-risk preview rows report `WARN` but remain diagnostic-only.

`desktop.launch.types` is a read-only Phase 2 coverage diagnostic. It resolves a practical shared label set that includes registered apps, shared built-in metadata identities, known legacy aliases, hosted/bare-metal shell labels, and a small unknown probe, then groups the results by `LaunchTargetType`. Each type line reports compact `hostedAvailable`, `bareMetalAvailable`, `hostedOnly`, `bareMetalOnly`, `expectedUnsupportedOnTarget`, `unexpectedUnsupportedOnTarget`, and `unknownLabels` counts. Documented target-specific unsupported labels such as bare-metal `ImgViewer` are tracked as expected unsupported coverage gaps, so they stay visible without making the compact app-model summary look unhealthy. Unknown and unexpected unsupported rows remain non-fatal and informational; the command does not write config, migrate storage, or feed typed targets back into runtime launch.

`desktop.launch.compare` is also available as a non-fatal side-by-side diagnostic in both hosted and bare-metal shells. It compares a fixed shared label set and classifies each row as `exact`, `accepted-alias`, `intentional-difference`, or `unexpected-drift`.

The compact summary includes the same launch-target comparison as counts only. Unexpected drift changes the summary line and overall result to `WARN`, but it remains diagnostic-only and does not block registration or launch.

`desktop.appmodel.summary` also now includes a compact `launchTargetTypes` line. That line summarizes total inspected labels, how many `LaunchTargetType` buckets are currently covered, and the aggregate hosted/bare-metal availability plus `expectedUnsupportedOnTarget`, `unexpectedUnsupportedOnTarget`, and `unknownLabels` counts from `desktop.launch.types`. The compact summary stays `OK` when only documented expected target-specific unsupported labels exist; it changes to `WARN` only when unexpected unsupported labels or unknown labels remain. Like the detailed command, it is diagnostic-only and non-fatal.

The hosted App Model Demo window also shows the same compact summary in a read-only section, with the launch-target comparison status repeated as a one-line readability check above the full summary. Launch `App Model Demo` from the Start Menu or desktop shortcut and press `R` or `F5` to refresh the snapshot. For hosted smoke testing, `gui.open.appmodeldemo` queues the same compositor UI launch message used by desktop UI surfaces. The older `desktop.launch App Model Demo` command still exercises `DesktopService::LaunchApp()` directly and remains intentionally unchanged until the launch rewrite phase.

Hosted Start Menu app launches and app desktop shortcut launches now emit read-only `[LaunchTargetShadow]` log lines before dispatch. Phase 2's first behavior bridge pass adds a shared `ComputeTypedDispatchCandidateForUiLaunch(...)` helper that resolves the UI launch identity from the UI label or desktop shortcut target, computes a typed-dispatch adapter candidate with `DesktopService::LegacyDispatchStringForLaunchTarget()`, and compares that candidate to the actual legacy dispatch string currently used by the compositor. This remains shadow-only: the compositor still calls `launchAction(...)` with the original dispatch string, and the typed candidate is not fed into `DesktopService::LaunchApp()` yet.

`desktop.appmodel.summary` includes compact in-memory `[LaunchTargetShadow]` counts for total observations, unresolved observations, alias/fallback observations, adapter matches, adapter accepted mismatches, adapter unexpected mismatches, and the new `typedDispatchCandidateMatches`, `typedDispatchCandidateAcceptedMismatches`, and `typedDispatchCandidateUnexpectedMismatches` counters, plus Start Menu / desktop shortcut source totals. `desktop.appmodel.coverage` includes the same counters split by source, including typed-dispatch candidate counts for Start Menu, desktop shortcuts, and other sources. These counters are diagnostic-only, are not persisted, and currently have no reset command.

Bare-metal also has a command-local mirror smoke diagnostic: `desktop.smoke.launchshadow`. It resolves representative labels such as `Notepad`, `gxos.builtin.notepad`, `Files`, `FileExplorer`, `guideXOS Navigator`, `ImgViewer`, `/`, and an unknown probe, adapts each target back to the current legacy dispatch string, and classifies the result as `match`, `accepted-mismatch`, `expected-unsupported`, or `unexpected-mismatch`. The command does not launch apps and does not instrument real desktop click paths, so its counters are summary-only for that command invocation. `ImgViewer` remains an expected unsupported legacy/static label for hosted `ImageViewer` because there is no current bare-metal ImageViewer `AppManager` registration.

Run `scripts\smoke-appmodel-launchshadow.ps1` to automate that bare-metal diagnostic under QEMU. The script builds a temporary smoke kernel with `GXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE`, boots with the existing FAT32 test disk attached so `/` resolves through the file-open path, captures serial output in `logs\appmodel-launchshadow-kernel-smoke-*.serial.log`, asserts that `ImgViewer` is `expected-unsupported`, and fails if any `unexpected-mismatch` row is not the intentional `FakeLaunchShadowApp` probe. It restores a normal kernel build after the smoke run.

The standard smoke checklist in `docs\TESTS.TXT` now includes the hosted launch-shadow smoke, the typed-dispatch flag smoke, and the QEMU bare-metal launch-shadow smoke. Keep them diagnostic-only: they are evidence for a future feature-flagged typed dispatch handoff, not launch executors.

`desktop.appmodel.typed-dispatch-gate` is a read-only hosted report for the Phase 2 typed-dispatch shadow-only gate. It evaluates current in-process diagnostics such as `desktop.appmodel.summary`, launch-target comparison, storage preview comparison, launch-target type coverage, and LaunchTarget shadow counters, then lists external checks that still require manual or script validation. The command does not run smokes, does not enable typed dispatch, and does not write storage.

The gate report can ingest disposable diagnostic evidence from `logs/appmodel-typed-dispatch-gate-hosted.evidence.txt` and `logs/appmodel-typed-dispatch-gate-qemu.evidence.txt`. These files are created by `gui.smoke.launchshadow` and `scripts/smoke-appmodel-launchshadow.ps1` respectively, can be deleted and recreated by rerunning the smokes, and never enable typed dispatch behavior.

The Phase 2 handoff contract remains documentation-only. Future `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY` would compute and compare typed dispatch while legacy dispatch still launches. Future `GXOS_APPMODEL_TYPED_DISPATCH_ENABLED` would be off by default, allowed only after gate evidence passes, and must fall back to legacy dispatch on unresolved, unsupported, empty, stale, malformed, or unexpected-mismatch cases. The current implementation does not define or enable either runtime behavior flag.

Diagnostics now include no-op compile-time flag discovery for those two future names. By default the report is `shadowOnly=OFF enabled=OFF behavior=legacy-dispatch`; both-defined local builds are reported as invalid/WARN. This is discovery/reporting only and is not wired into `launchAction(...)`, `DesktopService::LaunchApp()`, or bare-metal `AppManager::launchApp()`.

`scripts/smoke-appmodel-typed-dispatch-flags.ps1` covers single-flag and invalid-configuration paths without changing normal build flags. It creates temporary hosted builds for `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`, `GXOS_APPMODEL_TYPED_DISPATCH_ENABLED`, and both names together, verifies summary/gate diagnostics report the expected discovery-only state, and confirms the normal hosted binary still reports the default OFF/OFF legacy-dispatch state.

`scripts/smoke-appmodel-phase2-status.ps1` is a lightweight aggregate validation reporter. By default it uses the current hosted binary, runs `gui.smoke.launchshadow`, `desktop.appmodel.summary`, `desktop.appmodel.typed-dispatch-gate`, and the typed-dispatch flag smoke, then prints compact `PASS` / `WARN` / `FAIL` status with the exact commands it ran. QEMU is optional; pass `-IncludeQemu` to run `scripts/smoke-appmodel-launchshadow.ps1 -TimeoutSeconds 35`, and pass `-BuildHosted` when the aggregate should rebuild hosted first.

Before any future `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY` implementation is added to real launch paths, the required handoff gate is `scripts\smoke-appmodel-phase2-status.ps1 -BuildHosted -IncludeQemu -TimeoutSeconds 35`. It must pass hosted build, hosted launch-shadow smoke, app-model summary, typed-dispatch gate, typed-dispatch flag smoke, and QEMU launch-shadow smoke while still reporting `typedDispatchEnabled=false`, `feedsTypedDispatchIntoLaunch=false`, and `runtimeLaunchBehaviorChanged=false`.

The first shadow-only implementation plan is documented in `APP_MODEL_PHASE2_TYPED_LAUNCH_TARGETS.md`. Its intended order is hosted Start Menu first, hosted desktop shortcuts second, and bare-metal observation later. In every step the typed candidate stays diagnostic-only and the existing legacy dispatch string remains the value passed to the launch executor.

### 2.4 Hosted launch resolution exists, but execution is hybrid

`app_launch_resolver.cpp` resolves a manifest to a launch strategy:

- `BuiltIn`
- `NativeElf`
- `GXAppPackage`
- `Service`
- `HypervisorGuest`
- `Script`

But `desktop_service.cpp` still performs the actual launch like this:

- `BuiltIn` -> hardcoded `if/else` dispatch to concrete app launchers
- `NativeElf` -> experimental hosted ELF path only
- `GXAppPackage` -> explicitly not implemented in `DesktopService::LaunchApp`
- other strategies -> explicit unsupported error

So the current hosted app model is:

1. manifest discovery and registration
2. strategy resolution
3. hardcoded dispatch for built-ins
4. partial experimental NativeElf execution
5. no real GXApp runtime yet

### 2.4 Bare-metal has a separate kernel app model

Bare-metal does not use `AppRegistry` as the authoritative launch source.

Instead:

- `kernel/core/kernel_app.cpp` provides `AppManager`
- `kernel/core/kernel_apps.cpp` registers kernel GUI apps via `registerKernelApps()`
- `kernel/core/desktop.cpp` launches apps by name using `AppManager::launchApp()` / `launchAppWithParam()`

The bare-metal app model is therefore still a separate kernel registry + factory model, not the hosted manifest registry.

## 3. Known app discovery and registration paths

### 3.1 Hosted manifest/app discovery

- `app_registry.cpp`
  - recursive scan for `app.json`
  - source roots include `/system/apps`, `/Apps`, `sdk/samples`, `examples/apps`, `/users/default/apps`
- built-in apps are also registered as synthetic manifests in the same hosted registry

### 3.2 Hosted desktop registration surface

- `desktop_service.cpp`
  - `ensureDefaultAppsRegistered()`
  - scans manifests
  - registers built-ins as manifests
  - copies registry apps into `DesktopService::s_apps`

### 3.3 Bare-metal registration surface

- `kernel/core/desktop.cpp`
  - calls `apps::registerKernelApps()` during desktop init
- `kernel/core/kernel_apps.cpp`
  - registers kernel-mode apps directly into `AppManager`

There is no shared manifest-driven registration flow used by both hosted and bare-metal today.

## 4. Known app launch paths

### 4.1 Hosted / Windows compositor / test harness

#### A. Start Menu -> hosted app launch

- `compositor.cpp`
  - `refreshAllProgramsList()` populates Start Menu items from `DesktopService::GetRegisteredApps()`
  - `openStartMenuApp()` calls `launchAction()`
  - `launchAction()` calls `DesktopService::LaunchApp()`

Status: hosted only

#### B. Desktop app shortcut -> hosted app launch

- `compositor.cpp`
  - desktop shortcuts store `targetAppId`
  - `openDesktopItem()` resolves shortcut target
  - app shortcut launch calls `launchAction(app->displayName)`
  - `launchAction()` calls `DesktopService::LaunchApp()`

Status: hosted only

#### C. Desktop file/folder entry -> hosted file open path

- `compositor.cpp`
  - `openDesktopItem()` calls `DesktopService::OpenFilesystemEntry()`
- `desktop_service.cpp`
  - directories -> `FileExplorer::Launch(path)`
  - text-like files -> `Notepad::LaunchWithFile(path)`
  - image-like files -> `ImageViewer::Launch(path)`

Status: hosted only

#### D. Direct command / service launch

- `server.cpp` and diagnostics call `DesktopService::LaunchApp()` directly for some scenarios

Status: hosted only

#### E. Native ELF hosted path

- `DesktopService::LaunchApp()`
- `AppLaunchResolver`
- `NativeElfLaunchPipeline`
- `NativeElfImageLoader`
- `NativeAppRuntime`
- `NativeElfExecutor`

Status: hosted only, experimental and intentionally narrow

#### F. GXApp package launch

- `PackageManager::LaunchGXApp()` -> `UniversalAppLoader::Execute()` -> `GXAppLoader::Execute()`
- `DesktopService::LaunchApp()` only reaches this path if no desktop-registered app matches the requested name
- manifest-driven `GXAppPackage` launch is still blocked with `GXApp launch pipeline not implemented`

Status: hosted only, split from manifest launch path

#### G. GXM script/image launch

- `gxm_loader.cpp` can execute GXM/GUI text payloads by sending GUI bus messages

Status: hosted-side utility path, not integrated as the main app model

### 4.2 Bare-metal / kernel desktop

#### A. Desktop icon / shell label -> kernel app launch

- `kernel/core/desktop.cpp`
  - `try_launch_kernel_app()`
  - direct `AppManager::launchApp()` by app name

Status: bare-metal only

#### B. Desktop shortcuts to folders/files

- `kernel/core/desktop.cpp`
  - folder shortcuts -> `AppManager::launchAppWithParam("Files", path)`
  - text files -> `AppManager::launchAppWithParam("Notepad", path)`

Status: bare-metal only

#### C. Start Menu / desktop object actions

- `kernel/core/desktop.cpp`
  - system objects and menu selections dispatch directly into `AppManager`

Status: bare-metal only

#### D. Bare-metal registration

- `kernel/core/kernel_apps.cpp`
  - `registerKernelApps()` registers built-ins directly

Status: bare-metal only

## 5. Shared vs duplicated launch paths

### 5.1 Shared concepts

Shared in concept only:

- app names like `Notepad`, `Calculator`, `FileExplorer`, `guideXOS Navigator`
- desktop/start menu presence
- file-open behavior for some text/folder cases
- some common visual/app naming

### 5.2 Actually shared implementation

Shared implementation is limited:

- manifest types / app registry are hosted-side only
- kernel `AppManager` is bare-metal-side only
- built-in launch dispatch is duplicated across hosted and bare-metal
- desktop shortcut handling exists in both environments, but with different storage and different launch backends

## 6. Duplicated logic / drift already present

### 6.1 Built-in registration drift

Hosted built-ins are registered as synthetic manifests in `app_registry.cpp`.

Bare-metal built-ins are separately registered in `kernel/core/kernel_apps.cpp`.

These lists overlap but are not sourced from one shared definition.

### 6.2 Built-in launch drift

Hosted built-ins launch through the `DesktopService::LaunchApp()` hardcoded branch table.

Bare-metal built-ins launch through `AppManager::launchApp()` using separately registered factories.

The two launch systems can drift in:

- availability
- naming aliases
- per-app behavior
- error handling

### 6.3 File association drift

Manifests support `fileAssociations`, but actual hosted file opening still uses extension checks in `DesktopService::OpenFilesystemEntry()`.

Bare-metal file opening also uses direct special cases in `kernel/core/desktop.cpp`.

There is no shared manifest-driven association resolution yet.

### 6.4 GXApp drift

The repo contains:

- `.gxapp` container format
- package installation
- architecture selection support
- loader entry points

But this is not integrated into the main manifest launch path. Hosted manifest `GXAppPackage` entries resolve as a launch strategy, then stop with `GXApp launch pipeline not implemented`.

At the same time, package-manager launch is a separate path that bypasses the manifest registry when no desktop-registered app matches.

### 6.5 Navigator naming/path drift risk

Hosted Navigator is registered in the synthetic built-in manifest list and launched by the hosted dispatcher.

Bare-metal Navigator is registered independently as a kernel app.

Names currently line up (`guideXOS Navigator`), but implementation remains split.

## 7. Fat executable / multi-arch concepts found

The repository already contains multiple multi-arch concepts:

1. Manifest `supportedArchitectures` + per-entry `entries`
2. `.gxapp` packages with one binary per architecture
3. package-manager architecture validation using shared CPU architecture naming
4. repository documentation describing a universal binary direction

What is missing is a single runtime/launcher contract that both hosted and bare-metal honor consistently.

## 8. File association / MIME mapping status

Present:

- manifest-level `fileAssociations`
- extension + content-type fields in the manifest model

Missing:

- registry/index of associations
- MIME resolution service
- default app selection logic
- hosted `OpenFilesystemEntry()` using manifest associations
- bare-metal file opening using manifest associations

Current file opening is still hardcoded by extension.

## 9. What is missing before guideXOS Server has a real GXApp runtime

At minimum:

1. A shared app identity model across hosted and bare-metal
   - one source of truth for built-in app IDs, display names, icons, and launch names
2. A shared launch contract
   - manifest/registry resolution should feed the actual launcher for both targets
3. A real `GXAppPackage` execution path
   - manifest-driven `GXAppPackage` launch must stop being a placeholder
4. A target-aware runtime selection layer
   - hosted compositor/test harness and bare-metal must both report honest availability
5. A shared built-in app registry table
   - hosted synthetic manifests and bare-metal kernel registration should derive from the same metadata where possible
6. Association resolution
   - file associations should become a queryable service instead of hardcoded extension branches
7. Clear unsupported-path diagnostics
   - especially where hosted or bare-metal cannot yet execute a given runtime

## 10. Safest Phase 1 step

The safest Phase 1 step is:

**Add and keep a current-state app-model map in-tree before changing launch behavior.**

Rationale:

- current hosted and bare-metal launch systems are intentionally different
- built-in registration is duplicated
- GXApp exists in pieces but is not fully wired
- changing launch behavior now risks breaking Start Menu, desktop shortcuts, File Manager, Navigator, or kernel app registration

This document is that Phase 1 step.

It improves the app model foundation by making the existing launch graph explicit and by identifying the exact seams for future shared abstractions without removing legacy paths.

## 11. Recommended low-risk follow-up after this pass

Next safe implementation target:

**Create one shared built-in app metadata table used by both hosted synthetic manifest registration and bare-metal kernel registration, without changing actual launch dispatch yet.**

That would reduce naming drift first, while preserving:

- current hosted `DesktopService::LaunchApp()` behavior
- current Start Menu behavior
- current desktop shortcuts
- current File Manager behavior
- current Navigator behavior
- current kernel `AppManager` launch behavior

## 12. Phase 1 progress update

This repository now includes a shared built-in app metadata table in `built_in_app_metadata.h`.

- Hosted synthetic manifest registration derives its built-in list from this shared table.
- Bare-metal kernel registration references the same metadata where a kernel app factory already exists.
- This remains metadata-only for now and does not replace the current hosted or bare-metal launch dispatch paths.

## 13. Phase 3 typed-dispatch pilot scaffolding (default-off)

Status: scaffolding/evidence only. No runtime hook implemented.

### 13.1 First pilot candidate: StartMenuNotepad

`StartMenuNotepad` has been selected as the first future typed-dispatch pilot candidate based on Phase 2 launch-shadow evidence:

- Kind: `BuiltInApp`
- `typedDispatchCandidateMatchesActual=true`
- `comparison=match`
- No known drift
- No embedded or special behavior
- Already validated in the Phase 2 launch-shadow smoke

App Model v1.1 adds `Calculator` as an additional safe typed-dispatch built-in target in the diagnostic matrix. That expands the ready-only evidence set without changing the pilot order, legacy aliases, or any runtime launch behavior.

### 13.2 Phase 3 pilot compile flags

Two new compile flags are defined for the future pilot. Both are **default-off** in all normal builds and in all CI/smoke builds:

| Flag | Default | Purpose |
|------|---------|---------|
| `GXOS_APPMODEL_TYPED_DISPATCH_PILOT_START_MENU_NOTEPAD` | OFF | Future bare-metal-only pilot hook, scoped to `label == "Notepad"` inside `show_start_menu_notification()` |
| `GXOS_APPMODEL_TYPED_DISPATCH_PILOT_FALLBACK_TO_LEGACY` | OFF | Required companion; guarantees fallback to legacy dispatch when typed candidate is empty (`target.dispatchLaunchName[0] != '\0'` check fails), unresolved, unsupported, stale, malformed, or an unexpected mismatch |

Neither flag is defined in `build.bat`, `build.ps1`, or any normal build configuration.

### 13.3 Product-default-on status markers

The following markers are emitted by `desktop.appmodel.summary` and `desktop.appmodel.typed-dispatch-gate` in every normal build:

```
appModelPhase3PilotCandidate=StartMenuNotepad
appModelPhase3PilotStartMenuNotepadFlag=OFF
appModelPhase3PilotFallbackToLegacyFlag=OFF
appModelPhase3PilotEnabled=false
appModelPhase3PilotFeedsTypedDispatchIntoLaunch=false
appModelPhase3PilotRuntimeLaunchBehaviorChanged=false
appModelPhase3PilotScopedToStartMenuNotepad=true
appModelPhase3PilotDefaultBuildSafe=true
appModelActiveDispatchFeatureGate=appmodel.active-typed-dispatch
appModelActiveDispatchDefaultOnCandidateGate=appmodel.active-typed-dispatch-default-on-candidate
appModelActiveDispatchCandidateEnabled=false
appModelActiveDispatchEnabled=true
appModelActiveDispatchEffectiveStateSource=product-default
appModelActiveDispatchRuntimePath=active
appModelActiveDispatchRuntimeLaunchBehaviorChanged=true
appModelActiveDispatchVisibleLaunchBehaviorChanged=false
appModelActiveDispatchPersistentDesktopStorageWrites=false
```

The candidate gate is now a compatibility/testing marker only. `desktop.appmodel.active-typed-dispatch-gate reset` returns to the product default, which is enabled, and emergency `force-off` / `force-on` overrides are still preserved. These markers are asserted by `scripts/smoke-appmodel-typed-dispatch-flags.ps1` and `scripts/smoke-appmodel-phase2-status.ps1`.

### 13.4 What is not implemented yet

- The actual bare-metal runtime pilot hook inside `show_start_menu_notification()`.
- Any change to `launchAction(...)`, `DesktopService::LaunchApp(...)`, `AppManager::launchApp(...)`, `try_launch_kernel_app(...)`, `launchAppWithParam(...)`, or `shell::open()`.
- Any new launch probes.

The fallback flag requirement (`GXOS_APPMODEL_TYPED_DISPATCH_PILOT_FALLBACK_TO_LEGACY`) must be enforced before any runtime pilot hook is added. The correct non-empty dispatch name check is `target.dispatchLaunchName[0] != '\0'`.

### 13.5 Compile-flag discovery location

`kernel/core/desktop.cpp` contains a compile-time comment block documenting the two pilot flags and the future hook location. It does not add any runtime code. `desktop_service.cpp` contains discovery/reporting for both flags in the `TypedDispatchCompileFlags` struct and the `phase3PilotSummaryLine()` helper.

## 14. Phase 4A built-in identity registry

Status: cleanup in progress, no visible launch behavior change.

Active typed dispatch is still product-default-on, emergency force-off remains available, and legacy fallback remains intact.

The shared built-in identity registry now lives in `built_in_app_metadata.h`. It owns:

- stable app IDs
- display names
- canonical launch names
- known aliases
- category/group
- Start Menu and desktop coverage flags
- recent-program eligibility
- file/folder target acceptance
- system/shell-object classification
- risky/destructive dispatch exclusion flags

Phase 4A stays out of scope for:

- GXApp runtime and package execution changes
- ELF/native app runtime broadening
- package install, uninstall, or app store behavior
- sandboxing, permissions, IDE, or Open With behavior
- broader file-association cleanup
- any visible launch behavior change

The current evidence markers include:

- `appModelPhase4ABuiltInRegistryExists=true`
- `appModelPhase4AStableAppIdsUnique=true`
- `appModelPhase4AStartMenuAppsRegistered=true`
- `appModelPhase4AActiveDispatchAppsRegistered=true`
- `appModelPhase4ARecentProgramNamesAligned=true`
- `appModelPhase4ARiskyEntriesNotActiveDispatchOwned=true`
- `appModelPhase4AVisibleLaunchBehaviorChanged=false`
- `appModelPhase4APersistentDesktopStorageWrites=false`


