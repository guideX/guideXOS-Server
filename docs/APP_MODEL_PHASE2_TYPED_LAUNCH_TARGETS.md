# guideXOS App Model Phase 2: Typed Launch Targets

Status: design pass only. Do not treat this document as an implementation commitment or runtime behavior change.

Phase 1 made app identity healthier without changing launch dispatch. Phase 2 should add a typed launch target model before replacing the current string-based launch paths. The goal is to describe launch intent in one structure that can be resolved, diagnosed, and eventually executed by both the Windows compositor/test harness and bare-metal mode.

## Current launch constraints

Hosted mode currently accepts strings from the compositor, Start Menu, desktop shortcuts, CLI commands, and file-open paths. Those strings flow through a mix of:

- `Compositor::launchAction()`
- `DesktopService::LaunchApp()`
- `DesktopService::OpenFilesystemEntry()`
- `AppRegistry` and `AppLaunchResolver`
- hardcoded built-in app dispatch
- package manager fallback for installed GXApp-like names

Bare-metal mode currently uses string names through:

- `desktop::launch_app()`
- `try_launch_kernel_app()`
- `AppManager::launchApp()`
- `AppManager::launchAppWithParam()`
- special-case shell actions such as `Console`
- special-case App Model Demo handling through `AppModel`

Phase 2 must preserve those paths while adding typed resolution beside them.

## Launch Target Type

Proposed enum:

```cpp
enum class LaunchTargetType {
    Unknown = 0,
    BuiltInApp,
    ManifestApp,
    NativeElfApp,
    GXAppPackage,
    ShellAction,
    LegacyAlias,
    FileOpen,
    CrossArchEmulatedApp,
    Service,
    HypervisorGuest,
    Script
};
```

Notes:

- `BuiltInApp` represents apps owned by `built_in_app_metadata.h`.
- `ManifestApp` is a manifest-backed app before a concrete runtime strategy is chosen.
- `NativeElfApp` and `GXAppPackage` are concrete manifest-backed runtime targets.
- `ShellAction` is for desktop environment actions that are not app identities, such as `ComputerFiles`.
- `LegacyAlias` preserves old labels such as `AppModel` while pointing to a canonical target.
- `FileOpen` represents a path plus an eventual handler app or shell action.
- `CrossArchEmulatedApp` is reserved for future emulation or compatibility layers and should not imply support today.

## Launch Target Shape

Proposed data model:

```cpp
struct LaunchTarget {
    LaunchTargetType type = LaunchTargetType::Unknown;

    std::string requestedLabel;      // Original string from UI/CLI/config.
    std::string appId;               // Stable app identity when one exists.
    std::string displayName;         // User-facing name.
    std::string launchName;          // Current dispatch-compatible name.
    std::string legacyAlias;         // Alias that resolved to this target, if any.

    std::string shellAction;         // Typed shell command, e.g. ComputerFiles.
    std::string path;                // File, folder, package, or entry path.
    std::string fileAssociationHint; // Extension/content type, optional.

    std::string targetArchitecture;  // Requested/selected app architecture.
    std::string hostArchitecture;    // Current architecture.
    std::string runtime;             // Native ELF, GXApp, script runtime, etc.
    std::string entryPath;           // Manifest entry path after resolution.

    bool availableHosted = false;
    bool availableBareMetal = false;
    bool requiresEmulation = false;
    bool diagnosticOnly = false;

    std::string source;              // BuiltIn, SystemApps, UserApps, Package, Shell.
    std::string manifestPath;        // Optional diagnostic path.
    std::string reason;              // Resolution explanation or warning.
};
```

The model deliberately keeps `launchName` because the current code still needs it. Phase 2 should not force all callers to jump directly to `appId`.

## Mapping Examples

| Current input | Proposed target | Notes |
| --- | --- | --- |
| `Notepad` | `BuiltInApp` with `appId=gxos.builtin.notepad` | Hosted and bare-metal can keep current hardcoded dispatch initially. |
| `gxos.builtin.notepad` | `BuiltInApp` | Stable ID path. |
| `Hello World` from `sdk/samples` | `NativeElfApp` | Uses manifest entry and current architecture. |
| `Future GXApp Package` | `GXAppPackage` | Resolves but remains unsupported until GXApp runtime work. |
| `/Desktop/readme.txt` | `FileOpen` with handler `gxos.builtin.notepad` | Association resolver is future work; current extension logic stays for now. |
| `AppModel` | `LegacyAlias` -> `BuiltInApp` `gxos.builtin.appmodeldemo` | Preserve alias for old pins and saved config. |
| `ComputerFiles` | `ShellAction` | Shell/system label, not a built-in app identity. Can later route to File Manager with a path. |
| native app for another CPU | `CrossArchEmulatedApp` or `NativeElfApp` with `requiresEmulation=true` | Diagnostic-only until emulation exists. |

App Model v1.2 treats `DisplayOptions` itself as the next typed-ready built-in target (`gxos.builtin.displayoptions`). The shell labels `Settings`, `System Settings`, and `Control Panel` still remain separate shell/system affordances and continue to bridge to `DisplayOptions` as non-fatal drift.

## Availability

Availability should be target-aware and non-fatal:

- Built-ins derive availability from `built_in_app_metadata.h`.
- Manifest apps derive hosted availability from manifest entries and current runtime support.
- Bare-metal availability derives from kernel app registration and supported runtime surfaces.
- Shell actions are target-specific and should be explicit about hosted-only, bare-metal-only, or both.
- Cross-arch targets can resolve as known but unavailable until an emulator exists.

Proposed flags are intentionally simple booleans in the first pass. If this grows, replace them with a bitmask like `Hosted`, `BareMetal`, `RequiresRuntime`, `RequiresEmulation`, and `DiagnosticOnly`.

## Diagnostics

Typed resolution should produce read-only diagnostics before it changes execution:

- original requested label
- normalized target type
- stable app ID if present
- launch name used by current dispatch
- source path or source kind
- hosted availability
- bare-metal availability
- selected architecture
- whether alias fallback was used
- whether shell/system fallback was used
- why the target is unsupported, if unsupported

Existing diagnostics should remain non-fatal:

- `desktop.appmodel.summary`
- `desktop.appmodel.coverage`
- `desktop.apps.verbose`
- App Model Demo UI summary

Phase 2 should add target-resolution diagnostics before replacing launch dispatch. A good first command would be `desktop.launch.resolve <label>`, but it should be read-only.

Current Phase 2A status: `desktop.launch.resolve <label>` exists as a read-only diagnostic command in the hosted server shell and the bare-metal kernel shell. It returns the typed target fields from `app_launch_target.h` and does not feed `DesktopService::LaunchApp()`, `AppManager::launchApp()`, or any compositor launch path yet.

`desktop.launch.adapt <label>` exists as the next read-only prototype in both shells. It resolves the label, calls `LegacyDispatchStringForLaunchTarget` or the bare-metal mirror, and reports the legacy dispatch string that current launch paths would expect. The adapter output is diagnostic-only and is not used by `DesktopService::LaunchApp()`, `Compositor::launchAction()`, or bare-metal `AppManager::launchApp()`.

## Migration Plan

### Phase 2A: Add read-only model

Add a small shared header such as `app_launch_target.h` containing:

- `LaunchTargetType`
- `LaunchTarget`
- `LaunchResolutionDiagnostic` if needed

No dispatch changes.

### Phase 2B: Add hosted resolver in diagnostic mode

Add a resolver that accepts a string and returns a `LaunchTarget`.

Inputs:

- display name
- app ID
- launch name
- legacy alias
- shell action label
- file path

Outputs:

- typed target
- canonical app identity when available
- compatibility launch name for existing dispatch
- non-fatal warnings

This resolver should be used only by diagnostics first.

### Phase 2C: Add bare-metal resolver mirror

Add an equivalent bare-metal resolver that maps kernel app names and shell actions to the same target concepts. It can be smaller and static at first because bare-metal does not scan hosted manifests.

Current status: the bare-metal resolver mirror lives behind `kernel::appmodel::resolveLaunchTarget()`. It recognizes shared built-in metadata identities, kernel app names, the `Files` legacy alias for `FileExplorer`, `AppModel`, terminal/shell labels, Start Menu right-column shell labels, and VFS path-like file-open targets for folders and obvious text files. It is diagnostic-only and does not alter `AppManager::launchApp()` or `launchAppWithParam()`.

The comparison diagnostic `desktop.launch.compare` exists in both hosted and bare-metal shells. It runs a fixed sample set through the current target-specific resolver plus a small mirror of the other target's expected behavior. Results are classified as `exact`, `accepted-alias`, `intentional-difference`, or `unexpected-drift`; only unexpected drift changes the diagnostic's overall result to `WARN`, and even then it remains non-fatal.

Hosted `desktop.appmodel.summary` includes compact launch-target comparison counts (`exact`, `acceptedAliases`, `intentionalDifferences`, and `unexpectedDrift`) plus an `OK` / `WARN` line. It intentionally omits detailed rows; use `desktop.launch.compare` for row-level detail.

The hosted App Model Demo renders that same `launchTargetComparison` summary line above the full read-only app-model summary. The UI still uses `DesktopService::AppModelSummaryDiagnostic()` as the source of truth and refreshes the existing snapshot with `R` / `F5`.

Hosted Start Menu app launches and app desktop shortcut launches also emit `[LaunchTargetShadow]` log lines. These are read-only diagnostics that resolve the current dispatch string through `DesktopService::ResolveLaunchTarget()`, adapt the result through `DesktopService::LegacyDispatchStringForLaunchTarget()`, report the typed target fields, and note non-fatal alias/fallback or adapter differences without changing the dispatch string.

The same shadow observations are counted in memory. `desktop.appmodel.summary` reports compact totals, including adapter matches, accepted mismatches, and unexpected mismatches. `desktop.appmodel.coverage` reports source-level counts for Start Menu, desktop shortcuts, and other callers. The counters are non-fatal, non-persistent, and currently have no reset command.

Bare-metal has a matching command-local smoke check named `desktop.smoke.launchshadow`. It is intentionally not wired into real desktop launch paths: it resolves a fixed set of representative labels, runs the bare-metal legacy dispatch adapter, compares the adapter output to the expected current dispatch string, and prints non-fatal rows plus command-local summary counts. This keeps `AppManager::launchApp()` and desktop launch behavior unchanged while still making alias and unsupported-target cases deterministic in the kernel shell. `ImgViewer` is classified as `expected-unsupported` because it is a retained static/legacy label for hosted `ImageViewer`, not a currently registered bare-metal app.

QEMU automation for this check lives at `scripts\smoke-appmodel-launchshadow.ps1`. It uses the existing serial-capture smoke pattern, boots a temporary kernel compiled with `GXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE`, attaches the existing FAT32 test disk so `/` resolves through the file-open path, captures `desktop.smoke.launchshadow` output, asserts the `ImgViewer` and fake-probe classifications, and rebuilds the normal kernel afterward.

The standard smoke checklist in `docs\TESTS.TXT` now includes the hosted and QEMU LaunchTarget shadow smokes. The documented commands are:

```text
.\build.bat
gui.smoke.launchshadow
desktop.appmodel.summary
desktop.appmodel.coverage
desktop.appmodel.typed-dispatch-gate
.\scripts\smoke-appmodel-typed-dispatch-flags.ps1
.\build.ps1 -Arch amd64
.\scripts\smoke-appmodel-launchshadow.ps1 -TimeoutSeconds 35
```

`desktop.launch.storage` inventories the current string-based launch storage sites without migrating them. Hosted output reports the live `desktop.json` fields, `DesktopService` in-memory pin/recent lists, compositor-derived Start Menu/all-program surfaces, and the absence of a separate persisted taskbar-pin store. Bare-metal output reports the static Start Menu arrays, `/desktop.shortcuts`, `/.desktop_icons`, `/desktop.system.icons`, runtime desktop icon fields, and the currently disabled/static taskbar entry surface. Use this map before Phase 2E so storage migration starts from observed behavior instead of assumptions.

`desktop.launch.storage.preview` is the next read-only step before Phase 2E. It resolves inspected storage values to `LaunchTarget`, prints the proposed typed record beside the current string, classifies migration status (`ready`, `alias`, `shell-action`, `unresolved`, or `skip-layout-only`), and reports summary counts including `targetSpecificUnsupportedAliases`. It is intentionally non-mutating and must stay separate from config/VFS migration until the preview is stable.

`desktop.appmodel.summary` now folds in only the compact storage preview counts through `launchStoragePreview`. Full per-record migration details stay in `desktop.launch.storage.preview`.

`desktop.launch.storage.preview.compare` compares hosted and bare-metal preview counts without duplicating per-record rows. Intentional differences include hosted `desktop.json`, bare-metal VFS storage files, target-specific Start Menu sources, shell/system labels, runtime-only desktop/taskbar surfaces, and the bare-metal static `ImgViewer` label. `ImgViewer` is treated as a diagnostic-only legacy/static label for hosted `ImageViewer`; it remains unsupported on bare metal until a real bare-metal ImageViewer/AppManager path exists. Such labels increment `targetSpecificUnsupportedAliases` instead of `unresolved` or `highRisk`, and the compare output groups them under `targetSpecificUnsupportedAliasDetails` by target/label/count. Expected target-specific gaps stay visible without becoming unexpected drift. Unexpected high-risk counts report `WARN` but stay non-fatal.

### Phase 2D: Feed existing dispatch without changing behavior

Once diagnostics are clean, let hosted `DesktopService::LaunchApp()` and compositor UI paths resolve strings to a target, adapt them back to the same legacy dispatch string, then call the same current branches using that string.

Bare-metal should do the same by resolving to a target and then calling the existing `AppManager::launchApp()` or `launchAppWithParam()`.

This phase is an adapter, not a runtime rewrite.

### Phase 2D.1: Feature-flagged typed dispatch handoff design

No typed dispatch handoff is implemented yet. The current default remains legacy string dispatch everywhere: hosted compositor UI paths still call `launchAction(...)` with the existing actual dispatch string, hosted `DesktopService::LaunchApp()` still receives legacy strings, and bare-metal desktop paths still call `AppManager::launchApp()` / `launchAppWithParam()` with current names.

Future handoff work should use two explicit, off-by-default flags:

- `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`: keep legacy dispatch as the executor input, but compute the typed adapter candidate on every eligible launch path and record/log whether it matches the legacy dispatch.
- `GXOS_APPMODEL_TYPED_DISPATCH_ENABLED`: allow the typed adapter candidate to drive launch only after the shadow counters and hosted/QEMU smokes are clean for the target surface being migrated.

Flag contract:

- With neither flag enabled, current legacy dispatch is the only runtime launch input.
- With `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`, typed dispatch is computed and compared for diagnostics, but the actual launch executor still receives the same legacy dispatch string it receives today.
- With `GXOS_APPMODEL_TYPED_DISPATCH_ENABLED`, typed dispatch may become the executor input only for surfaces whose shadow evidence has passed. It must still compare the typed candidate to the legacy dispatch for every launch, and must fall back to legacy dispatch on unresolved targets, unsupported targets, empty candidates, unexpected mismatches, stale evidence, malformed evidence, or target-specific uncertainty.
- `GXOS_APPMODEL_TYPED_DISPATCH_ENABLED` must not imply storage migration, package manager changes, GXApp runtime execution, alias removal, or cross-surface rollout. Hosted and bare-metal surfaces must be enabled independently.

The current code only discovers whether these compile-time flag names are defined. `desktop.appmodel.summary` and `desktop.appmodel.typed-dispatch-gate` report a diagnostic line like `typedDispatchFlags: shadowOnly=OFF enabled=OFF behavior=legacy-dispatch status=OK discoveryOnly=true`. No build script defines either flag by default. The discovery report is not an implementation: even if a developer locally defines one of the names for inspection, launch behavior remains legacy dispatch because the flags are not wired into launch execution. If both names are defined at once, diagnostics must report `WARN` / `invalidConfiguration=true`, still without changing runtime launch behavior.

Invalid flag discovery smoke:

```text
.\scripts\smoke-appmodel-typed-dispatch-flags.ps1
```

The smoke creates temporary copies of `build.bat` under `out/appmodel-typed-dispatch-flags/` and builds three separate hosted binaries: `shadow-only`, `enabled-only`, and `both-flags`. The single-flag builds must report `behavior=legacy-dispatch status=OK discoveryOnly=true`, with only the requested flag set to `ON`. The both-flags build must report `shadowOnly=ON enabled=ON behavior=legacy-dispatch status=WARN discoveryOnly=true invalidConfiguration=true`. Every case verifies `enablesTypedDispatch: false` and `feedsTypedDispatchIntoLaunch: false`, then the script checks the normal hosted binary still reports `shadowOnly=OFF enabled=OFF behavior=legacy-dispatch`. It does not permanently modify normal build flags and does not exercise launch execution.

Phase 2 status aggregation:

```text
.\scripts\smoke-appmodel-phase2-status.ps1
```

The aggregate status script is validation/reporting only. The quick default uses the current hosted binary, runs `gui.smoke.launchshadow`, `desktop.appmodel.summary`, `desktop.appmodel.typed-dispatch-gate`, and the typed-dispatch flag smoke. It prints a compact `PASS` / `WARN` / `FAIL` report and lists the exact commands it ran. It does not enable typed dispatch, does not feed typed dispatch into launch, and does not make QEMU mandatory.

Full validation adds the hosted rebuild and QEMU launch-shadow integration smoke explicitly:

```text
.\scripts\smoke-appmodel-phase2-status.ps1 -BuildHosted -IncludeQemu -TimeoutSeconds 35
```

This runs the quick hosted checks plus `.\build.bat`, the typed-dispatch flag smoke, and `.\scripts\smoke-appmodel-launchshadow.ps1 -TimeoutSeconds 35`. QEMU remains opt-in so normal hosted iteration does not require QEMU, OVMF, or the FAT32 test disk.

Shadow-only implementation readiness:

Before adding any `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY` behavior to real launch paths, the full aggregate handoff gate must pass:

```text
.\scripts\smoke-appmodel-phase2-status.ps1 -BuildHosted -IncludeQemu -TimeoutSeconds 35
```

The required aggregate checks are:

- `hostedBuild: PASS`
- `gui.smoke.launchshadow: PASS`
- `desktop.appmodel.summary: PASS`
- `desktop.appmodel.typed-dispatch-gate: PASS`
- `typedDispatchFlagSmoke: PASS`
- `qemuLaunchShadowSmoke: PASS`

The same handoff report must also show `typedDispatchEnabled=false` and `feedsTypedDispatchIntoLaunch=false` before the implementation starts. Current evidence must have no unexpected real launch mismatches, and all shadow smokes must continue to report `runtimeLaunchBehaviorChanged=false`.

The next implementation boundary is shadow-only, not typed execution. A future `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY` code path may compute typed dispatch inside real hosted and bare-metal launch paths, but legacy dispatch must still be the value that launches. Mismatches must remain diagnostic-only, non-fatal, and measured, and fallback to legacy dispatch remains mandatory for unresolved targets, unsupported targets, empty candidates, unexpected mismatches, stale evidence, malformed evidence, and target-specific uncertainty.

`GXOS_APPMODEL_TYPED_DISPATCH_ENABLED` remains out of scope until shadow-only mode has clean evidence across multiple full aggregate runs. Enabling typed dispatch must remain a later, separate change with its own fallback and rollback validation.

Shadow-only implementation plan:

This plan identifies where a future `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY` code path may observe real launch paths. It is not an enablement plan for typed execution. Every insertion point must preserve the current legacy dispatch value as the value that launches.

Hosted Start Menu launch path:

- Current legacy dispatch source: the Start Menu row action string. `openStartMenuApp(appName)` logs with source `StartMenu` and then calls `launchAction(appName)`. Mouse and keyboard Start Menu launches likewise derive `action` from `g_startMenuPinnedRecent` or `g_startMenuAllProgsSorted`, then call `launchAction(action)`. Right-column shell labels such as `ComputerFiles` pass their current string directly to `launchAction("ComputerFiles")`.
- Typed candidate computation point: immediately before each `launchAction(...)` call, using the same shape already exercised by `logLaunchTargetShadowDiagnostic("StartMenu", label, "", actualDispatch)`, which calls `DesktopService::ComputeTypedDispatchCandidateForUiLaunch(...)`.
- Why legacy dispatch still launches: `launchAction(...)` must keep receiving the original `appName`, `action`, or shell label string. The typed candidate is only logged/counted through `RecordLaunchTargetShadowObservation(...)` and must not be substituted into `launchAction(...)`.

Hosted desktop shortcut launch path:

- Current shortcut target source: app desktop shortcuts use `DesktopItem::targetAppId` to find the registered desktop app, then launch the current hosted display/dispatch string with `launchAction(app->displayName)`.
- Typed candidate computation point: inside the app-shortcut branch of `openDesktopItem(...)`, immediately before `launchAction(app->displayName)`, using source `DesktopShortcut`, UI label `item.label`, shortcut target `item.targetAppId`, and actual dispatch `app->displayName`.
- Why legacy dispatch still launches: the actual executor call remains `launchAction(app->displayName)`. The typed candidate from `ComputeTypedDispatchCandidateForUiLaunch(...)` is diagnostic-only and must not replace either `item.targetAppId` lookup behavior or the display-name dispatch string.

Bare-metal Start Menu and static app launch path:

- Current kernel dispatch source: Start Menu entries use their static label from `s_startMenuApps`. `show_start_menu_notification(label)` special-cases `AppModel` and `Console`, then calls `try_launch_kernel_app(label)`. Desktop app shortcuts and static labels also flow through `launch_app(target)`, `try_launch_kernel_app(label)`, or directly into `AppManager::launchApp(label)`.
- Typed candidate computation point: future observation should be added around the label-to-launch boundary, before `try_launch_kernel_app(label)`, `launch_app(target)`, or `AppManager::launchApp(label)` receives the current label. The resolver side already exists as `kernel::appmodel::resolveLaunchTarget(label)` plus `legacyDispatchStringForLaunchTarget(...)`; the future shadow helper should mirror the command-local `desktop.smoke.launchshadow` row format but record real-source observations.
- Why `AppManager::launchApp()` still receives the legacy/current name: `try_launch_kernel_app(label)` and any direct `AppManager::launchApp(label)` call must keep using the original static label or current alias such as `Files`. The typed adapter candidate is compared to that current name only for diagnostics.

Bare-metal file and folder launch path:

- Current file/folder dispatch behavior: desktop filesystem entries and file/folder shortcuts inspect VFS metadata, then launch folders with `AppManager::launchAppWithParam("Files", path)` and obvious text files with `AppManager::launchAppWithParam("Notepad", path)`. System objects such as This System can call `launchAppWithParam("Files", "/")`.
- Typed candidate computation point: future observation belongs just before each file-open handler call, building or resolving a FileOpen `LaunchTarget` from the path and comparing its adapter dispatch to the current handler app name (`Files` or `Notepad`). `kernel::appmodel::resolveLaunchTarget(path)` already classifies folders and text files as `FileOpen` where VFS metadata is available.
- Why existing dispatch remains unchanged: the handler app name and path parameter remain separate. The current `launchAppWithParam("Files", path)` or `launchAppWithParam("Notepad", path)` call must not be replaced by a typed target executor, and path routing must remain exactly as it is today.

Guarded bare-metal FileOpen shadow observation design:

This is the next design boundary after static app-name observation. FileOpen targets are higher risk than app labels because they carry both a handler app and a path parameter. The shadow-only observation must therefore treat the current handler name and the current path as two separate legacy facts, not as a single replaceable dispatch string.

Folder opens:

- Current legacy behavior: file/folder desktop shortcuts first call `vfs::stat(target, &info)` and, when the target exists and `info.type == vfs::FILE_TYPE_DIRECTORY`, call `AppManager::launchAppWithParam("Files", target)`. Desktop filesystem entries use the already-populated `icon.isDirectory` flag and call `AppManager::launchAppWithParam("Files", icon.path)`. The This System object opens the root folder with `AppManager::launchAppWithParam("Files", "/")` before falling back to `try_launch_kernel_app("Files")`.
- Current handler name: `Files`.
- Path parameter source: shortcut target path for file/folder shortcuts, `icon.path` for desktop filesystem entries, and the literal root path `/` for This System.
- Observation point: immediately before each `launchAppWithParam("Files", path)` call. The shadow helper should resolve the path with `kernel::appmodel::resolveLaunchTarget(path)`, verify the typed adapter candidate is `Files`, and log the handler/path pair as a FileOpen observation.
- Why launch remains unchanged: `launchAppWithParam("Files", path)` must keep receiving the same handler name and the same path variable or literal. The typed candidate is compared only to the handler name and must never rewrite the path.

Text file opens:

- Current legacy behavior: file/folder desktop shortcuts call `launchAppWithParam("Notepad", target)` when `vfs::stat(target, &info)` succeeds, the target is not a directory, and the current shortcut label passes `desktop_entry_is_text(label)`. Desktop filesystem entries call `launchAppWithParam("Notepad", icon.path)` when `icon.isDirectory` is false and the displayed label passes `desktop_entry_is_text(label)`.
- Current handler name: `Notepad`.
- Exact call sites: `kernel/core/desktop.cpp` in `show_icon_notification(...)`, in the file/folder shortcut branch immediately after the folder case, and in the `DesktopItemKind::FilesystemEntry` branch immediately after the folder case.
- Path parameter source: shortcut target path for persisted file shortcuts (`target`, derived from `icon.path` when present, otherwise the label) and `icon.path` for desktop filesystem entries. Both paths are already VFS-observed before the text-file launch decision: shortcuts use `vfs::stat(target, &info)`, while filesystem entries use the desktop icon metadata populated from VFS.
- Observation point: immediately before each `launchAppWithParam("Notepad", path)` call. The shadow helper should resolve the path with `kernel::appmodel::resolveLaunchTarget(path)`, verify the typed adapter candidate is `Notepad`, and log the handler/path pair as a FileOpen observation.
- Why launch remains unchanged: `launchAppWithParam("Notepad", path)` must keep receiving the same handler name and path. Under `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`, a typed FileOpen target cannot substitute a handler app and cannot canonicalize, normalize, or otherwise mutate the path.

Guarded bare-metal text FileOpen shadow observation design:

- Observation shape: log a `[LaunchTargetShadow]` row with source/context (`DesktopShortcutTextFile`, `DesktopFilesystemTextFile`, and a smoke-only text fixture source), `handler=Notepad`, `path=<current path>`, `resolvedType=FileOpen`, `adapterLegacyDispatch=Notepad`, `candidateMatchesHandler=true/false`, `comparison=match` or non-fatal mismatch, adapter status/reason, target status/reason, `nonFatal=true`, and `shadowOnly=true`.
- Resolver expectation: `kernel::appmodel::resolveLaunchTarget(path)` should classify existing `.txt`, `.log`, `.cfg`, `.ini`, and `.md` paths as `FileOpen` with dispatch `Notepad`. If the path is missing, unsupported, or no longer classified as text by VFS/extension checks, the observation remains diagnostic-only and must not affect the current notification path.
- Safety boundary: the helper must be void/log-only and must not return a dispatch string, handler name, normalized path, or any replacement object that could be fed into `launchAppWithParam(...)`.

Text FileOpen QEMU coverage plan:

- Deterministic fixture: reuse the existing FAT32 test disk attached by `scripts\smoke-appmodel-launchshadow.ps1`. `scripts\create-test-disks.ps1` already creates `/test.txt`, `/empty.txt`, `/README.txt`, and `/apps/hello.txt`; `/test.txt` is the preferred first probe because it is small, root-level, and already referenced by other filesystem smoke docs.
- Future smoke hook: under `GXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE` plus `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`, add a diagnostic-only text FileOpen probe that observes `/test.txt` with `handler=Notepad` and does not call `launchAppWithParam(...)`.
- Future serial assertions: extend `scripts\smoke-appmodel-launchshadow.ps1` to keep the existing folder FileOpen assertions intact and add checks for `source=SmokeTextFileOpen`, `handler=Notepad`, `path=/test.txt`, `resolvedType=FileOpen`, `adapterLegacyDispatch=Notepad`, `comparison=match`, `nonFatal=true shadowOnly=true`, and `runtimeLaunchBehaviorChanged=false`.
- Scope: the first QEMU text assertion should prove the resolver and adapter shape before touching real desktop text-file launch call sites. After that passes, add the real-path observation beside both `launchAppWithParam("Notepad", target)` and `launchAppWithParam("Notepad", icon.path)`.

Real text FileOpen observation coverage plan:

- Current coverage: `SmokeTextFileOpen` proves the resolver and legacy adapter shape for `/test.txt` with `handler=Notepad`, `resolvedType=FileOpen`, `adapterLegacyDispatch=Notepad`, and `comparison=match`. The real guarded call-site helpers now exist beside both `launchAppWithParam("Notepad", target)` and `launchAppWithParam("Notepad", icon.path)`, but QEMU does not yet exercise those call sites through desktop icon selection or UI input.
- Desired real-path coverage: `DesktopShortcutTextFile` should be covered for the file/folder shortcut branch where the current path comes from a persisted shortcut target and the label passes `desktop_entry_is_text(label)`. `DesktopFilesystemTextFile` should be covered for the desktop filesystem-entry branch where `icon.path` is populated from VFS metadata and the displayed label passes `desktop_entry_is_text(label)`.
- Option A, smoke-only direct call-site shim: under `GXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE` plus `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`, call the same void/log-only FileOpen observation helper with `source=DesktopShortcutTextFile`, `handler=Notepad`, `path=/test.txt`, then with `source=DesktopFilesystemTextFile`, `handler=Notepad`, `path=/test.txt`. This must not call `launchAppWithParam(...)`, must not create desktop shortcut records, and must not mutate desktop icon state. It proves the real observation labels and log format, but does not prove the actual click/UI path.
- Option B, test disk desktop fixture: create deterministic test-only desktop metadata on the FAT32 smoke disk, such as a `/desktop.shortcuts` file with a `File` shortcut to `/test.txt` and, if needed, layout entries in `/.desktop_icons`. QEMU would then boot with the fixture and exercise the desktop metadata loading path before the smoke observes or opens the text-file item. This has higher fidelity, but it is riskier because it touches storage fixture assumptions, shortcut parsing, desktop icon ordering, and potentially UI/input automation. It must remain isolated to smoke/test images and must not change normal desktop shortcut storage.
- Recommended next code pass: prefer Option A first. Add smoke-only calls for `DesktopShortcutTextFile` and `DesktopFilesystemTextFile` using `/test.txt`, extend QEMU serial assertions for those two source labels, and keep the actual `launchAppWithParam("Notepad", target)` and `launchAppWithParam("Notepad", icon.path)` calls untouched. Consider Option B only after the direct shim is stable and there is a clear need to validate desktop storage fixture behavior.

### Option B planning note: minimal non-UI real-branch coverage in QEMU

Current proven coverage:

- Option A is complete. The smoke-only observations for `DesktopShortcutTextFile` and `DesktopFilesystemTextFile` already prove the resolver and legacy adapter shape for `/test.txt` with `handler=Notepad`, `resolvedType=FileOpen`, `adapterLegacyDispatch=Notepad`, `comparison=match`, and `runtimeLaunchBehaviorChanged=false`.
- The real bare-metal call sites in `show_icon_notification(...)` are also already instrumented with the same SHADOW_ONLY helper, but current QEMU smoke does not reach those branches through real desktop metadata or a real desktop-item open path.

DesktopShortcutTextFile real-path strategy:

- The existing `/desktop.shortcuts` loader can already create a file shortcut from the test disk. `load_persisted_app_shortcuts()` reads tab-separated records, and `add_shortcut_slot(...)` supports `File`, `Folder`, and `App` shortcut types. A deterministic record such as `File<TAB>/test.txt<TAB>test.txt` would populate a `DesktopItemKind::Shortcut` slot whose target path is `/test.txt` and whose label still passes `desktop_entry_is_text(label)`.
- That means a pure storage-fixture path is technically possible without changing parser behavior. However, it would still be mutating the smoke disk's desktop shortcut storage surface and would tie the smoke to persisted metadata assumptions instead of the smallest branch-level trigger.
- The minimal non-UI plan should instead use a smoke-only helper to construct a temporary in-memory shortcut slot representing the same loaded result, refresh or map it into a visible icon index, and then call the same real `show_icon_notification(...)` branch that a double-click would use. This reaches the real `DesktopShortcutTextFile` branch without manual UI input and without persisting `/desktop.shortcuts`.
- Because the helper would create only temporary in-memory state under the smoke flag, it avoids changing production/user desktop storage.

DesktopFilesystemTextFile real-path strategy:

- The existing desktop filesystem icon loader does not use `/.desktop_icons` to create files; `/.desktop_icons` stores layout only. Real filesystem-entry icons come from enumerating `/Desktop`, via `enumerate_desktop_folder_items()`, which maps directory entries under `/Desktop` into `DesktopItemKind::FilesystemEntry` slots.
- Therefore, `/test.txt` will not naturally appear as a desktop filesystem entry unless it exists under `/Desktop`, for example as `/Desktop/test.txt`. Current test-disk content creates `/test.txt`, but not `/Desktop/test.txt`.
- A storage-fixture variant could add a test-only `/Desktop/test.txt` or copy/link-like fixture on the QEMU test disk, then let the normal enumeration path populate the desktop filesystem entry. That would prove the full loader path, but it would also change desktop fixture contents and could make the smoke depend on `/Desktop` inventory and icon ordering.
- The smaller non-UI plan is to use a smoke-only helper to construct a temporary `DesktopItemKind::FilesystemEntry` icon with `label=test.txt`, `path=/test.txt`, and `isDirectory=false`, map it to a visible display index, and call `show_icon_notification(...)`. That still reaches the real `DesktopFilesystemTextFile` branch directly, without UI automation and without requiring a persisted `/Desktop` fixture.

Minimal non-UI trigger option:

- Recommended trigger: add a test-only helper compiled only under `GXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE` and used only from the existing QEMU smoke block in `kernel/core/main.cpp`.
- The helper should not call the shadow observation helper directly. Instead, it should temporarily prepare an in-memory `DesktopIcon`/desktop slot that matches the real branch preconditions, call the existing real open handler (`show_icon_notification(...)`) with the corresponding visible display index, and then restore the prior icon/visibility state before returning.
- For the shortcut case, the constructed slot should behave like a loaded `File` shortcut with `path=/test.txt` and a text label. For the filesystem-entry case, the constructed slot should behave like an enumerated `/Desktop` file entry with `path=/test.txt`, `isDirectory=false`, and the same text label.
- The helper must remain non-persistent, must not save `/.desktop_icons`, must not write `/desktop.shortcuts`, must not add `/Desktop` files, must not require mouse or keyboard automation, and must remain non-fatal.
- Because the helper reaches `show_icon_notification(...)`, it still exercises the same real branches that contain the `DesktopShortcutTextFile` and `DesktopFilesystemTextFile` SHADOW_ONLY observations.
- To avoid accidentally launching Notepad during the diagnostic smoke, the helper design should either run only in a diagnostic path that exits before the real `launchAppWithParam(...)` call or add a smoke-only launch suppression guard local to the helper-driven path that preserves `runtimeLaunchBehaviorChanged=false`. Any future implementation must keep the real production call sites unchanged.

Risks:

- Accidentally testing only a duplicate path instead of the real call site if the future helper invokes `log_bare_metal_fileopen_shadow_only_observation(...)` directly instead of routing through `show_icon_notification(...)`.
- Mutating desktop storage by writing `/desktop.shortcuts`, `/.desktop_icons`, `/desktop.system.icons`, or `/Desktop` during smoke setup.
- Relying on labels instead of paths. The real text-file branch currently depends on the displayed label passing `desktop_entry_is_text(...)`, while the actual open target comes from `target` or `icon.path`; both need to be set intentionally.
- Depending on unstable icon ordering if the future smoke tries to find the injected item through normal grid ordering rather than a known temporary slot/display index.
- Accidentally launching Notepad during a diagnostic smoke if the future helper reaches the real branch but does not suppress the executor path in a smoke-only, non-persistent way.

Recommended Option B implementation:

- Choose the smallest path that reaches the real branches: a smoke-only non-UI helper that prepares temporary in-memory desktop icon state, calls `show_icon_notification(...)`, and restores state.
- Do not make the first Option B pass depend on persisted `/desktop.shortcuts` or `/Desktop` fixtures. Those are valid higher-fidelity follow-ups, but they are not the minimal path.
- Keep typed dispatch disabled and report-only. The future helper must not feed typed dispatch into `launchAppWithParam(...)`, and it must not modify `launchAppWithParam("Notepad", target)`, `launchAppWithParam("Notepad", icon.path)`, or `launchAppWithParam("Files", path)`.
- QEMU assertions should continue to look for the real-source labels `DesktopShortcutTextFile` and `DesktopFilesystemTextFile`.
- `runtimeLaunchBehaviorChanged` must remain `false`.
- If a later follow-up wants full loader coverage instead of just real-branch coverage, prefer a separate explicit storage-fixture pass with isolated test-disk content and its own risk review.

Other file types:

- The desktop has `desktop_entry_is_known_image(...)` for icon selection and visual classification, but current bare-metal desktop open behavior does not route images through `ImgViewer` or `ImageViewer`.
- `ImgViewer` remains a retained static/legacy label and is classified by diagnostics as `expected-unsupported` because there is no current bare-metal `AppManager` registration for hosted ImageViewer.
- Existing non-folder, non-text file opens should remain non-fatal unsupported diagnostics with the current user-facing "No file handler" behavior. Shadow-only FileOpen observation may log `unsupported-file-open`, but must not introduce a new handler.

FileOpen safety invariants:

- A typed FileOpen target is diagnostic-only under `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`.
- The current handler name (`Files` or `Notepad`) remains authoritative.
- The current path parameter remains authoritative.
- The typed candidate must never rewrite, normalize differently, or replace the path.
- The typed candidate must never substitute the handler app under shadow-only mode.
- Unresolved paths, missing shortcut targets, and unsupported file types remain non-fatal diagnostics and must keep existing notification behavior.

Recommended FileOpen code passes:

- Completed folder pass: add a void/log-only helper near the folder `launchAppWithParam("Files", ...)` call sites in `kernel/core/desktop.cpp`; the helper accepts source, current handler name, and current path, resolves the path as a FileOpen target, compares the adapter candidate to `Files`, and returns no dispatch string.
- First text pass: add the smoke-only `/test.txt` text FileOpen probe and QEMU serial assertions, still without touching real `launchAppWithParam("Notepad", ...)` call sites.
- Second text pass: add a void/log-only helper beside `launchAppWithParam("Notepad", target)` for file shortcuts and `launchAppWithParam("Notepad", icon.path)` for desktop filesystem entries. The helper should accept source, `Notepad`, and the current path, resolve the path as a FileOpen target, compare the adapter candidate to `Notepad`, and return no dispatch string.
- Keep text-file observation separate from folder observation because text-file routing depends on both VFS type and extension/label classification.

Safety invariants:

- No launch path may use typed dispatch as the source of truth under `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY`.
- The typed candidate is diagnostic-only and may only be logged, counted, and reported.
- Mismatches are non-fatal and must not block launch.
- Legacy dispatch remains authoritative for hosted `launchAction(...)`, hosted `DesktopService::LaunchApp(...)`, and bare-metal `AppManager::launchApp(...)` / `launchAppWithParam(...)`.
- Fake unknown probe mismatches are allowed only in smoke/test diagnostics.
- `ImgViewer` remains expected-unsupported on bare metal until a real bare-metal ImageViewer/AppManager registration exists.

Future implementation boundaries:

- First code pass: hosted Start Menu only. It should place the compile-time shadow-only guard around diagnostic candidate computation, while leaving all `launchAction(...)` arguments unchanged.
- Second code pass: hosted desktop app shortcuts, preserving the existing `targetAppId` lookup and `app->displayName` launch dispatch.
- Later code pass: bare-metal static app launch observation around Start Menu and desktop app label dispatch, still sending the current label/name into `AppManager`.
- Later file-open pass: bare-metal file/folder observation around `launchAppWithParam(...)`, preserving current handler app names and path parameters.
- `GXOS_APPMODEL_TYPED_DISPATCH_ENABLED` remains out of scope for all of these passes.

The enabled path must remain reversible and conservative. For each launch request, compare the typed adapter dispatch candidate with the legacy dispatch string that would have been used before the flag. If the candidate is empty, unresolved, unsupported for the current target, or classified as an unexpected mismatch, fall back to the original legacy dispatch string and log the fallback as non-fatal. Accepted mismatches such as intentional aliases must stay explicit and measured.

Hosted and bare-metal migration should happen separately because their launch executors differ, but the measurements should stay paired: hosted Start Menu and desktop shortcut shadows, hosted `desktop.appmodel.summary` / `desktop.appmodel.coverage`, bare-metal `desktop.smoke.launchshadow`, and QEMU `scripts\smoke-appmodel-launchshadow.ps1` should all remain part of the gate before any surface flips from shadow-only to typed-dispatch-enabled.

Before any implementation of either flag, these statements must remain true:

- `desktop.appmodel.summary` reports `overall: OK`.
- `desktop.appmodel.typed-dispatch-gate` reports `gateStatus: PASS` after fresh hosted and QEMU evidence.
- Hosted `gui.smoke.launchshadow` passes and remains `launchesApps: false`.
- QEMU `scripts\smoke-appmodel-launchshadow.ps1 -TimeoutSeconds 35` passes.
- `scripts\smoke-appmodel-typed-dispatch-flags.ps1` passes, including shadow-only, enabled-only, both-flags, and normal OFF/OFF default checks.
- No unexpected typed-dispatch mismatch exists for real app labels.
- `runtimeLaunchBehaviorChanged=false` while in shadow mode.
- Storage preview unresolved, high-risk, and unexpected drift counts remain zero.
- Legacy fallback remains available and documented for every migrated surface.

Rollback contract:

- Disabling `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY` stops shadow comparison without changing stored launch data.
- Disabling `GXOS_APPMODEL_TYPED_DISPATCH_ENABLED` returns launch execution to legacy dispatch immediately.
- Evidence files and serial logs are diagnostics only; deleting them can make the gate report `NOT-RUN` or `WARN`, but cannot enable or disable runtime behavior.
- No storage migration is implied by either flag. Any future storage migration belongs to Phase 2E and must keep reading legacy string entries.

Typed-dispatch handoff test matrix:

| Case | Current legacy dispatch | Typed dispatch candidate | Expected comparison | Fallback required? | Allowed under `SHADOW_ONLY` | May be allowed under future `ENABLED` |
| --- | --- | --- | --- | --- | --- | --- |
| Hosted Start Menu Notepad | `Notepad` | `Notepad` | `match` | No | Yes, compare/log only | Yes, after gate PASS for hosted Start Menu |
| Hosted Start Menu Calculator | `Calculator` | `Calculator` | `match` | No | Yes, compare/log only | Yes, after gate PASS for hosted Start Menu |
| Hosted Start Menu ComputerFiles | `ComputerFiles` | `FileExplorer` | `accepted-mismatch` | Yes, until shell action routing is explicitly migrated | Yes, compare/log only | Not as typed executor yet; keep legacy fallback unless a shell-action handoff is separately designed |
| Hosted DesktopShortcut Notepad | `Notepad` | `Notepad` | `match` | No | Yes, compare/log only | Yes, after gate PASS for hosted desktop shortcuts |
| Hosted DesktopShortcut FileExplorer | `FileExplorer` | `FileExplorer` | `match` | No | Yes, compare/log only | Yes, after gate PASS for hosted desktop shortcuts |
| Bare-metal Notepad | `Notepad` | `Notepad` | `match` | No | Yes, compare/log only | Yes, after gate PASS for bare-metal Start Menu/app launch |
| Bare-metal Files/FileExplorer alias | `Files` for alias, `Files` expected for current File Manager surface | `Files` or `FileExplorer` depending on resolver input | `match` for `Files`, `accepted-mismatch` for canonical `FileExplorer` | Yes for canonical-to-alias mismatch | Yes, compare/log only | Only after alias behavior is explicitly preserved; fallback to `Files` remains required |
| Bare-metal guideXOS Navigator | `guideXOS Navigator` | `guideXOS Navigator` | `match` | No | Yes, compare/log only | Yes, after gate PASS for bare-metal Navigator launch |
| Bare-metal ImgViewer expected-unsupported | `ImgViewer` | `ImgViewer` | `expected-unsupported` | Yes | Yes, as diagnostic unsupported coverage | No, until a real bare-metal ImageViewer/AppManager registration exists |
| Bare-metal root folder file-open | `Files` with `/` as parameter | `Files` | `match` | No for current folder-open handler; path parameter must remain separate | Yes, compare/log only | Only if the typed path target preserves the existing handler plus parameter behavior |
| Fake unknown probe | none / fake label only | empty | `unexpected-mismatch` | Yes | Yes, as intentional fake probe only | No |

### Typed dispatch shadow-only gate checklist

This checklist is documentation-only. Passing it does not enable typed dispatch, does not feed typed dispatch into `launchAction(...)`, `DesktopService::LaunchApp()`, or `AppManager::launchApp()`, and does not migrate storage. It only defines the minimum validation bar before a future off-by-default `GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY` flag can be considered.

Hosted mode also has a read-only report command:

```text
desktop.appmodel.typed-dispatch-gate
```

The report evaluates the in-process parts of this checklist from existing diagnostics and counters. It does not run smokes, does not inspect external QEMU log files, and does not enable typed dispatch. External checks such as `.\build.bat`, `.\build.ps1 -Arch amd64`, bare-metal `desktop.smoke.launchshadow`, and `.\scripts\smoke-appmodel-launchshadow.ps1 -TimeoutSeconds 35` remain explicit validation steps.

The hosted smoke and QEMU smoke also write small disposable evidence files under `logs/`:

- `logs/appmodel-typed-dispatch-gate-hosted.evidence.txt`
- `logs/appmodel-typed-dispatch-gate-qemu.evidence.txt`

These files are diagnostic artifacts only. They are not user settings, are not stored in `desktop.json`, do not enable typed dispatch, and can be deleted/recreated by rerunning the corresponding smokes. `desktop.appmodel.typed-dispatch-gate` reads them when present so that `runtimeLaunchBehaviorChanged=false` and QEMU launch-shadow smoke status can be reported without rerunning the smokes automatically. Missing evidence remains non-fatal; stale or malformed evidence is reported as `WARN`.

Pass criteria:

- Hosted build passes with `.\build.bat`.
- Hosted `desktop.appmodel.summary` reports `overall: OK`.
- Hosted `desktop.appmodel.summary` reports no unexpected storage preview drift; `launchStoragePreviewCompare` must be `OK` with `unexpectedDrift=0`.
- Hosted `gui.smoke.launchshadow` completes and prints `mode: diagnostic-only`, `launchesApps: false`, typed candidate fields, and `runtimeLaunchBehaviorChanged: false`.
- Hosted LaunchTarget shadow counters have no unexpected real-app mismatches. `typedDispatchCandidateUnexpectedMismatches` must be zero after real UI smoke surfaces, or limited to intentional fake probe rows when running diagnostic smoke probes.
- Hosted diagnostics report no unresolved real app labels; unresolved rows are allowed only for intentional fake/unknown probes.
- `.\scripts\smoke-appmodel-typed-dispatch-flags.ps1` passes and confirms all temporary flag builds still report `behavior=legacy-dispatch`, `enablesTypedDispatch=false`, and `feedsTypedDispatchIntoLaunch=false`.
- amd64 bare-metal build passes with `.\build.ps1 -Arch amd64`.
- Bare-metal `desktop.smoke.launchshadow` completes as diagnostic-only, reports `runtimeLaunchBehaviorChanged: false`, and keeps `ImgViewer` classified as `expected-unsupported`.
- QEMU smoke passes with `.\scripts\smoke-appmodel-launchshadow.ps1 -TimeoutSeconds 35`.
- QEMU smoke reports `FakeLaunchShadowApp` as the only `unexpected-mismatch`.
- Any future shadow-only implementation still requires fallback-to-legacy behavior to remain designed, tested, and documented before any typed-dispatch-enabled path is attempted.

Fail criteria:

- Any build fails, or hosted rebuild is blocked by a live process locking `guideXOSServer.exe`.
- `desktop.appmodel.summary` reports `overall: WARN`.
- `gui.smoke.launchshadow`, `desktop.smoke.launchshadow`, or the QEMU smoke fails to run to completion.
- `smoke-appmodel-typed-dispatch-flags.ps1` fails, changes normal build flags permanently, or reports anything other than legacy dispatch for any temporary flag build.
- `runtimeLaunchBehaviorChanged` is anything other than `false`.
- Any unresolved real app label appears in hosted or bare-metal diagnostics.
- Any unexpected storage preview drift appears.
- Any unexpected typed-dispatch candidate mismatch appears outside intentional fake/unknown smoke probes.
- Any proposed implementation would remove or bypass legacy dispatch fallback.

### Phase 2E: Move storage to typed targets

Migrate Start Menu pins, desktop shortcuts, and recent items to store typed fields:

- `targetType`
- `appId`
- `displayName`
- `legacyAlias`
- `path`
- `shellAction`

Keep reading old string entries forever or through a clearly documented migration window.

### Phase 2F: Runtime implementation phases

Only after typed resolution is stable:

- wire manifest-driven Native ELF through the existing native runtime gate
- wire GXApp package execution
- add file association resolver
- add cross-arch/emulation path
- consider removing or hiding some legacy string branches after diagnostics prove they are unused

## DesktopService Integration

Hosted `DesktopService` should eventually expose functions like:

```cpp
LaunchTarget ResolveLaunchTarget(const std::string& requested);
bool LaunchTargetResolved(const LaunchTarget& target, std::string& error);
```

Early implementation should keep `LaunchApp(const std::string&, std::string&)` as the public compatibility surface and call the resolver internally. Existing callers keep working.

The launch executor should initially translate targets back into current behavior:

- `BuiltInApp` -> existing hardcoded built-in branch
- `NativeElfApp` -> existing experimental Native ELF branch
- `GXAppPackage` -> existing "not implemented" response
- `ShellAction` -> existing shell/system path
- `LegacyAlias` -> resolve to canonical target, then use existing branch
- `FileOpen` -> existing `OpenFilesystemEntry()` hardcoded associations

## Bare-Metal Integration

Bare-metal should not depend on hosted manifest scanning. It can resolve:

- built-ins from `built_in_app_metadata.h`
- kernel names and legacy aliases registered in `AppManager`
- shell/system actions such as terminal and file manager shortcuts
- file-open requests to existing `launchAppWithParam()` behavior

`AppManager` can continue to register and launch by name while the resolver provides diagnostics and a canonical identity layer.

## Risks

- Over-eager replacement could break Start Menu, desktop shortcuts, Navigator, or File Manager.
- Treating shell actions as apps would recreate `ComputerFiles` drift under a new name.
- Removing `launchName` too early would break hardcoded dispatch paths.
- Bare-metal cannot use all hosted manifest assumptions.
- Cross-arch fields may look like support before an emulator exists, so diagnostics must clearly say unsupported.
- File-open targets should not imply a real association resolver until one exists.

## Recommended First Implementation Step

Add `app_launch_target.h` with the enum and struct only, then add a read-only hosted diagnostic resolver for:

- built-in display names
- built-in app IDs
- launch names
- `AppModel`
- `ComputerFiles`
- manifest app IDs/display names

Expose it through a diagnostic command such as `desktop.launch.resolve <label>`. Do not use it for actual launching until the diagnostic output is stable across hosted and bare-metal expectations.

Implemented diagnostic command:

```text
desktop.launch.resolve <label>
```

The hosted command currently recognizes shared built-in metadata identities, hosted registered app IDs/display names/launch names, the `AppModel` legacy alias, the `ComputerFiles` shell/system label, and path-like file-open targets. The bare-metal command recognizes kernel-side equivalents, including `Files`, `Console`/`Terminal`, Start Menu shell labels, and VFS-backed folder/text-file targets. Unknown labels are reported as `Unknown` / `unresolved` and remain non-fatal.

Comparison command:

```text
desktop.launch.compare
```

Current fixed labels: `Notepad`, `Calculator`, `gxos.builtin.notepad`, `FileExplorer`, `Files`, `guideXOS Navigator`, `ComputerFiles`, `AppModel`, and `TotallyUnknownLaunchThing`.
