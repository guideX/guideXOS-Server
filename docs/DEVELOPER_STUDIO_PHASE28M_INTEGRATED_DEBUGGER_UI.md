# Developer Studio Phase 28M — Integrated Debugger UI

## Outcome

**Current result: Outcome A — the packaged Developer Studio debugger UI was
validated in three fresh guideXOS QEMU boots.**

The earlier Outcome E and Outcome B boundaries are retained below as history.
This continuation does not treat external computer-use `apps=[]` as guest
evidence. It adds a dedicated `-Phase28MOnly` QEMU path that stages the exact
packaged Developer Studio, launches it through the guideXOS App Model/native
runtime, and drives the production UI/controller handlers in the real app.

The current closure, including the exact old Phase 28L fixture boundary,
package identities, in-guest markers, host validation, and preserved evidence,
is in the next section. The later Outcome B matrix is a historical snapshot;
its provisional values are superseded by this continuation.

This is not an ABI incompatibility. The standalone client compiled against the
current server headers, the focused ABI/debugger host contracts passed, and
the real packaged UI completed its debugger flow in-guest.

## Phase 28M continuation closure — supersedes the provisional Outcome B matrix

### Repositories and package identity

The continuation started from the requested live state:

- Server: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`, branch
  `v0.5_DEVELOPER_STUDIO`, HEAD
  `dbcc2a3daa79344502a26b64f5747f1dacbc7260`.
- Standalone: `D:\dev\guideXOS_Developer_Studio`, branch
  `phase28m-integrated-debugger-ui`, HEAD
  `b91159eb190f18da506ddbcd265be1d50a2bacdb`.
- Standalone `main` remained at
  `33c37e56df6dd70e0963b2caca824e100f5e3d7e`.
- No fetch, pull, merge, rebase, reset, amend, clean, or push was performed.

The standalone source legitimately changed for the diagnostic UI mode,
diagnostic markers, and one guest-exposed cleanup path, so both packaged
artifacts were rebuilt through the normal build script. The final staged
identities are:

```text
Apps/DeveloperStudio/bin/amd64/developerstudio.elf
  size: 861,856 bytes
  SHA256: BC9D35CB1D9582EB0ABD4CD434E05EA8162B1B1C22F030E4A6D376B4C4546DF3

Apps/DeveloperStudio/bin/arm64/developerstudio.elf
  size: 1,015,504 bytes
  SHA256: DDED73B4B5D9C683CCC051370E1CEBA6FBB9DE30BF6997B7A7B2331DEBAC5A01
```

The earlier `1,051,184`/`68C73D…` amd64 and `1,202,984`/`5C2399…` arm64
identities in the original report are preserved as pre-continuation history;
they were not edited manually.

### What the guest path proves

The authoritative command was:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 -Phase28MOnly -BootCount 1 -TimeoutSeconds 180
```

`-Phase28MOnly` creates a fresh disposable FAT/ESP and QEMU instance for each
boot, stages `P28M`, copies `Apps/DeveloperStudio`, and writes the explicit
`.phase28m-diagnostic` sentinel. The smoke harness checks package discovery,
launch, final cleanup, and required markers, but does not print the pane or
control markers itself.

The actual application contains the bounded diagnostic state machine. It calls
the same `drawShell`, key handlers, pane refresh paths, watch handlers,
breakpoint manager handlers, session controller, and Stop/Cancel route used by
ordinary Developer Studio UI. It is activated only by the staged diagnostic
sentinel; normal Developer Studio launch remains ordinary. The server smoke
path contributes package identity/discovery and App Model cleanup markers. The
UI-originated markers below come from the packaged Developer Studio binary.

The target fixture is `/P28M`, with nested `helper_tail -> helper -> gx_main`
frames in `src/helper.cpp` and `src/main.cpp`. The real app opens the project,
adds the initial breakpoint through its UI path, starts the Debug session, and
waits for the backend pause before refreshing panes.

### UI acceptance evidence

- Launch and initialization: `APP_DISCOVERY_PASS`, `APP_LAUNCH_PASS`,
  `WINDOW_VISIBLE_PASS`, and `PROJECT_OPEN_PASS` were emitted. The guest
  framebuffer was also reported available at `1280x800x32`; no external
  screenshot surface was available or required.
- Debug start and pause: `DEBUG_START_UI_PASS`, `INITIAL_PAUSE_PASS`, and the
  real paused backend state were observed.
- Call Stack: `PANE_CALL_STACK_PASS` and `CALL_STACK_DEPTH_PASS depth=3`.
  The checked rows were `#0 helper_tail`, `#1 helper`, and `#2 gx_main`.
  A Down key through the integrated Call Stack handler selected frame 1 while
  the frame-0 instruction pointer and controller source location stayed
  unchanged.
- Locals and Arguments: the selected caller frame displayed the actual
  arguments `input=10` and `delta=4`; `PANE_LOCALS_PASS` and
  `PANE_ARGUMENTS_PASS` were emitted after the selection refresh.
- Watches: the real watch handler added `input + delta` and the backend
  evaluator returned `14`. It also added `missing_symbol`; the bounded failed
  expression status/error was retained and `WATCH_INVALID_ERROR_PASS` was
  emitted.
- Breakpoints: the pane contained the initial `src/helper.cpp:3` breakpoint,
  a logpoint at `src/helper.cpp:13`, and a caller breakpoint at
  `src/main.cpp:15`. The UI disabled and re-enabled the initial breakpoint
  through the actual manager route. The policy check represented LOG action and
  an AT_LEAST hit policy with threshold `1`; the required enable/disable and
  policy markers passed.
- Stepping and Continue: the actual UI routes passed Step Out from
  `helper_tail` to `helper`, Step Into, Step Over, final Step Out to the caller,
  and Continue. Each path produced a fresh paused state/source refresh through
  the controller.
- Output/logpoint: the real Output pane drained ordered backend output records
  containing `input=10 doubled=28` followed by `input=1 doubled=6`. The
  logpoint did not user-pause the target; execution reached the caller
  breakpoint and `LOGPOINT_NO_PAUSE_PASS` passed.
- Stop/Cancel: the real Stop UI handler dispatched the backend cancellation,
  accepted it, cleared the active session, and emitted `STOP_UI_PASS`.
- Stale state: after cancellation, stack, variables, and output authority were
  cleared; `STALE_STATE_CLEAR_PASS` passed.
- Second generation: a second Debug session got a different generation,
  selected frame reset to 0, and fresh stack/variables were tied to that
  generation. `SECOND_SESSION_PASS` passed.
- Controls and bounds: running/paused/terminal enablement checks passed through
  `SESSION_CONTROLS_PASS`. The diagnostic bound check recorded
  `watches=8`, `call_stack=16`, `breakpoints=8`, and `output=32`.

### Three fresh final-package boots

All three runs below used the final amd64 hash shown above and completed the
same required UI path through `PHASE28M_PASS`, App Model cleanup, and
`GUEST_PASS`:

1. `C:\Users\guideX\AppData\Local\Temp\guidexos-phase28g-2977c7ecab884507a88f6443fedd6392`
   — `boot1.serial.log`, full pass.
2. `C:\Users\guideX\AppData\Local\Temp\guidexos-phase28g-a5400b9214f24a8eab44396a31914bde`
   — `boot1.serial.log`, full pass.
3. `C:\Users\guideX\AppData\Local\Temp\guidexos-phase28g-0669a8fe585f4e1880abfe0ee43abf57`
   — `boot1.serial.log`, full pass.

Earlier attempts are not hidden: one pre-final-package boot stopped at the
diagnostic DWARF mapper with `SourceNotFound`, and an earlier two-boot attempt
had a second-boot debug-start timeout. Neither is counted. The three listed
boots are isolated successful runs of the final package.

### Phase 28L fixture boundary and regression

The historical failing command was the focused three-boot Phase 28L path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 -Phase28LOnly -BootCount 3 -TimeoutSeconds 180
```

The first preserved observable boundary was the `/P28L` fixture's
`Compiler: build FAIL`, before `DEVELOPER_STUDIO_PHASE28L_BUILD_PASS`, runtime
breakpoint assertions, or Developer Studio launch. The old capture did not
preserve a source-level compiler diagnostic, so no narrower historical root
cause can be claimed. It was not a Developer Studio package-discovery or UI
failure. A current isolated `-Phase28LOnly -BootCount 1` rerun passed the
fixture build, policy setup, hit-count/logpoint checks, runtime values, and
cleanup, so the old boundary was not reproduced as a persistent Phase 28M
regression. The Phase 28L assertions were not weakened.

### Host validation and runner correction

The final standalone amd64 and arm64 builds passed the focused model/debugger
tests and native ELF package build. The server-side validation sweep passed:

```text
scripts/run-compiler-functions-host-test.ps1
scripts/run-native-abi-layout-test.ps1
scripts/run-native-breakpoint-manager-contract-test.ps1
scripts/run-native-call-stack-controller-test.ps1
scripts/run-native-debug-output-contract-test.ps1
scripts/run-native-debug-policy-contract-test.ps1
scripts/run-native-debug-watches-test.ps1
scripts/run-native-debugger-runtime-test.ps1
scripts/run-native-source-step-controller-test.ps1
scripts/run-native-elf-host-test.ps1
scripts/run-native-elf-runtime-host-test.ps1
scripts/run-native-elf-development-app-model-host-test.ps1
scripts/run-native-elf-trampoline-host-test.ps1
scripts/run-native-filesystem-contract-test.ps1
```

The filesystem-contract runner omission identified in the prior investigation
was corrected minimally: its source list now includes
`kernel/core/native_elf/native_elf_debug_watches.cpp`. The corrected runner
builds and passes. The runtime host test also had a stale `334` depth
expectation; it now follows the derived `COMPILER_MAX_RUNTIME_CALL_DEPTH`
policy, and the compiler-functions boundary inputs are derived from that same
policy.

The nested NativeElf cancellation path was fixed so a cancelled child that has
already zeroed its image restores the paused parent image/page-table state
before the host UI pump returns. This is the runtime fix exercised by the
Stop/Cancel and second-session guest proof.

### Evidence limits and final repository policy

No screenshot/framebuffer image or physical mouse trace was used. Internal
window creation, framebuffer availability, pane render markers, model counts,
and controller/action markers are the authoritative UI evidence. External
computer-use was unavailable (`apps=[]`), which is recorded but is not treated
as Outcome E. Physical mouse interaction was not required because the real
keyboard/UI-controller paths were exercised in-guest.

Final local commits and status:

- Standalone continuation: `f50ce42`,
  `developer studio: validate integrated debugger ui`.
- Server continuation: `b52e48a`,
  `developer studio: validate integrated debugger ui in guest`.
- Tracked files are clean after those commits. The server worktree retains
  untracked disposable QEMU staging/evidence outputs under `ESP/` and
  `tmp/native-elf-development-app-model-host-test/`; they are intentionally
  preserved and were not cleaned.
- Nothing is pushed, and standalone `main` remains unchanged.

## Historical Outcome B repository state and provenance

Server repository:

`D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`

- Branch: `v0.5_DEVELOPER_STUDIO`
- Starting HEAD: `b76825b5c0ec4c2d1b18e344fd86b8b07e44629a`
- Starting subject: `developer studio: add breakpoint hit counts and logpoints`
- Upstream: `origin/v0.5_DEVELOPER_STUDIO`
- Starting divergence: `0 ahead / 0 behind`
- Starting worktree: one pre-existing untracked/incomplete Phase 28M report
- Ending HEAD: the local Phase 28M package-integration commit containing this report
- Ending worktree: clean after the local commit
- No fetch, pull, merge, rebase, reset, amend, clean, or push was performed.

Standalone repository:

`D:\dev\guideXOS_Developer_Studio`

- Starting branch: `main`
- Starting HEAD: `33c37e56df6dd70e0963b2caca824e100f5e3d7e`
- Phase 28M branch: `phase28m-integrated-debugger-ui`
- Ending HEAD: the local Phase 28M implementation commit
- Ending worktree: clean after the local commit
- `main` remains at its original starting commit and was not rewritten.

The intended local commit subjects are:

- Standalone: `developer studio: integrate debugger ui`
- Server: `developer studio: integrate debugger ui package`

The old tracked server package was an amd64-only Developer Studio ELF of
1,015,064 bytes. The new package is multi-architecture and was generated from
the dedicated standalone branch:

```text
Apps/DeveloperStudio/bin/amd64/developerstudio.elf
  size: 1,051,184 bytes
  SHA256: 68C73D2D9F04B41A71B8E532C42B853184976AD757DD3C86CEE831467B9C53E7

Apps/DeveloperStudio/bin/arm64/developerstudio.elf
  size: 1,202,984 bytes
  SHA256: 5C2399E07ADEEEE12786A0FAE1955BCE52E8F7D3941944A5BA195482CA6F70BC
```

The staged manifest is `Apps/DeveloperStudio/app.json`; it contains matching
amd64 and arm64 `NativeElf` entries, `gx_main`, `guidexos-c-abi-v1`, and the
existing window/filesystem permissions.

## Historical Outcome B build and ABI snapshot

Build command used for amd64 and arm64:

```powershell
.\build.ps1 -ServerRoot 'D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO' -ToolchainRoot 'C:\Program Files\LLVM\bin' -SkipModelTest -SkipProjectTest
.\build.ps1 -ServerRoot 'D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO' -ToolchainRoot 'C:\Program Files\LLVM\bin' -TargetArchitecture arm64 -SkipModelTest -SkipProjectTest
```

The normal flow compiled the modified standalone source and staged the ELFs
and manifest into `Apps/DeveloperStudio`. The standalone source consumes the
current `sdk/include/guidexos/development_debug.h` and the current
`guidexos/abi.h` callback table. No backend debugger service, evaluator,
breakpoint manager, numbered command, object ABI, or public structure prefix
was added or changed.

The only compatibility cleanup was limited to stale standalone reads of
optional build/package fields that are not present in the required server SDK;
those values were not needed by the Phase 28M debugger UI. Bare-metal debug
transport selection was also corrected so the existing
`bare_metal_development_debug` callback is used in the native bare-metal run
path.

## Integrated debugger architecture

The new UI is in `D:\dev\guideXOS_Developer_Studio\src\main.cpp`.

```text
Developer Studio integrated panel
  -> bounded UI presentation model
  -> current gx_host_calls debug callbacks
  -> existing development_debug / call_stack / inspect_variables /
     evaluate_expression services
  -> existing NativeAppDebugger / NativeElf runtime
```

The UI keeps bounded arrays for eight watches and the ABI-defined limits for
breakpoints, frames, variables, expressions, and output records. A new debug
generation resets presentation state. Paused refreshes query the server for
the selected frame, variables, watches, breakpoint manager snapshot, and
structured output. Resume/step/stop invalidates paused-only rows and routes
through the existing controller and current ABI.

The panel is titled `INTEGRATED DEBUGGER` and exposes seven tabs: Breakpoints,
Session, Call Stack, Locals, Arguments, Watches, and Output.

## Historical Outcome B UI evidence snapshot

The following records the requested 54 reporting items. “Source evidence” is
implementation evidence; it is intentionally not presented as a visible guest
run.

1. Outcome: **B**, exact boundary recorded above.
2. Server repository/branch: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`, `v0.5_DEVELOPER_STUDIO`.
3. Server starting HEAD: `b76825b5c0ec4c2d1b18e344fd86b8b07e44629a`.
4. Server ending HEAD: the local package-integration commit containing this report.
5. Server commit: `developer studio: integrate debugger ui package`.
6. Server upstream/divergence: `origin/v0.5_DEVELOPER_STUDIO`, `0 ahead / 0 behind` at preflight.
7. Server final worktree: clean after commit.
8. Standalone repository: `D:\dev\guideXOS_Developer_Studio`.
9. Standalone starting branch/HEAD: `main` / `33c37e56df6dd70e0963b2caca824e100f5e3d7e`.
10. Standalone Phase 28M branch: `phase28m-integrated-debugger-ui`.
11. Standalone ending HEAD: the local implementation commit.
12. Standalone commit: `developer studio: integrate debugger ui`.
13. Standalone final worktree: clean after commit.
14. Standalone main preservation: `main` was not modified, rewritten, or pushed.
15. Developer Studio build: the two commands in the Build section passed.
16. Produced binaries: the amd64 and arm64 paths listed above.
17. Produced identities: sizes and SHA256 values listed above.
18. Server staging path: `Apps/DeveloperStudio/` through `build.ps1 -ServerRoot ...`.
19. Packaged artifact identity: manifest entries match both staged ELF paths and `gx_main`.
20. ABI synchronization: current server SDK headers were the standalone include source.
21. Backend ABI changes: none.
22. Object ABI before/after: unchanged.
23. Debugger UI architecture: raw current-ABI snapshots are the presentation authority; the existing controller owns execution.
24. Call Stack UI evidence: `INTEGRATED DEBUGGER` Call Stack tab renders bounded server frames from `GX_DEVELOPMENT_DEBUG_CALL_STACK`.
25. Frame-selection evidence: Up/Down changes `selectedFrame`; refresh uses `frameIndex` and emits `debug_ui_selected_frame`; CPU execution context is not changed.
26. Locals UI evidence: Locals tab renders server `INSPECT_VARIABLES` values and availability/type information.
27. Watches UI evidence: Watches tab supports bounded add/edit/remove and evaluates expressions through `EVALUATE_EXPRESSION` on pause.
28. Watch error evidence: non-success expression status renders the bounded ABI `errorMessage` rather than dereferencing or evaluating locally.
29. Breakpoints UI evidence: Breakpoints tab renders manager entries, source locations, enabled state, action, raw hits, policy threshold, and condition text.
30. Breakpoint-management UI action: Space/Delete/L/C and F9 route enable/disable/remove/configure/add through the existing manager commands.
31. Hit-count display evidence: each breakpoint row renders `rawHitCount` and `hit==`, `hit%=`, or `hit>=` threshold information.
32. Logpoint configuration evidence: L toggles BREAK/LOG through `CONFIGURE_SOURCE_BREAKPOINT_POLICY`; the row visibly reports `LOG`.
33. Output pane evidence: Output tab drains and renders bounded structured server output records and dropped count.
34. Real runtime logpoint text: not observed in the current run because the Phase 28L guest fixture failed its build before logpoint execution.
35. Continue UI evidence: Session tab advertises F5 and the handler calls existing controller Continue/raw RESUME routing.
36. Step Into UI evidence: F11 routes to existing Step Into/raw `STEP_SOURCE_INTO` routing.
37. Step Over UI evidence: F10 routes to existing Step Over/raw `STEP_SOURCE_OVER` routing.
38. Step Out UI evidence: Shift+F11 routes to existing Step Out/raw `STEP_SOURCE_OUT` routing.
39. Stop/Cancel UI evidence: Shift+F5 routes to the existing Stop/raw cancel path.
40. Running/Paused/terminal UI state: Session presentation derives from the existing controller state and disables unavailable controls; visible guest transition was not exercised.
41. Stale-state clearing: paused-only stack, variables, watches, breakpoint/output snapshots, selected frame, and selected rows reset on a new generation/resume path.
42. Second-session/generation result: host/backend tests pass generation ownership; UI source resets `g_debugUiSessionGeneration` and stop state; guest UI second-session proof remains unobserved.
43. Collection bounds: eight UI watches; server ABI caps remain authoritative for breakpoints, output, frames, variables, and expression text.
44. UI automation mechanism: keyboard/mouse handlers in `main.cpp`, plus deterministic `GUIDEXOS_DEVELOPER_STUDIO_MARKER debug_ui_*` hooks.
45. Screenshot/framebuffer evidence: unavailable; computer-use returned `apps=[]` and no QEMU tab/window surface.
46. Phase 28L regression: the attempted three-boot run did not pass; boot 1’s `/P28L` fixture build returned `FAIL` before debugger assertions. Earlier preflight evidence had reported the three-boot backend proof green, but it was not reproduced after this package build.
47. Host tests: all twelve focused ABI/debugger/native-ELF scripts listed below passed.
48. Phase 28M fresh boot count: `0` successful Phase 28M UI boots; no Phase 28M-specific guest mode exists in the runner.
49. Documentation path: `docs/DEVELOPER_STUDIO_PHASE28M_INTEGRATED_DEBUGGER_UI.md`.
50. Filesystem-contract runner: known omission remains; `run-native-filesystem-contract-test.ps1` omits `kernel/core/native_elf/native_elf_debug_watches.cpp`, causing an undefined `native_debug_watch_evaluate` link symbol. It was not changed because it is unrelated and not required for this UI package.
51. Remaining limitations: no visible guideXOS Developer Studio launch/pause/inspect/resume/stop proof; current Phase 28L fixture build failure; no real runtime logpoint text captured.
52. Physical mouse-driven status: none; no native QEMU surface was exposed.
53. Standalone main: not rewritten; the work is isolated on `phase28m-integrated-debugger-ui`.
54. Push status: nothing was pushed.

## Historical host-validation snapshot

```text
scripts/run-native-abi-layout-test.ps1
scripts/run-native-breakpoint-manager-contract-test.ps1
scripts/run-native-call-stack-controller-test.ps1
scripts/run-native-debug-output-contract-test.ps1
scripts/run-native-debug-policy-contract-test.ps1
scripts/run-native-debug-watches-test.ps1
scripts/run-native-debugger-runtime-test.ps1
scripts/run-native-source-step-controller-test.ps1
scripts/run-native-elf-host-test.ps1
scripts/run-native-elf-runtime-host-test.ps1
scripts/run-native-elf-development-app-model-host-test.ps1
scripts/run-native-elf-trampoline-host-test.ps1
```

The standalone normal build also passed its run-controller, project-search,
debugger-model, source-step, frame-pointer stack, DWARF locals/arguments,
watch, and conditional-breakpoint model tests before producing both ELFs.

The Phase 28M milestone was therefore implemented and packaged, with the
remaining work precisely bounded at real guest App Model/QEMU UI exercise.

## Current final report

1. Outcome: **A**.
2. Server: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`, branch
   `v0.5_DEVELOPER_STUDIO`, starting HEAD `dbcc2a3daa79344502a26b64f5747f1dacbc7260`,
   ending commit `b52e48a`.
3. Standalone: `D:\dev\guideXOS_Developer_Studio`, branch
   `phase28m-integrated-debugger-ui`, starting HEAD
   `b91159eb190f18da506ddbcd265be1d50a2bacdb`, ending commit `f50ce42`.
4. Server upstream was `origin/v0.5_DEVELOPER_STUDIO`; final divergence is
   `2 ahead / 0 behind` (the two local continuation commits), and no remote
   operation or push occurred. Tracked files are clean after the commits; preserved
   untracked `ESP/` and host-test staging output are documented above.
5. Standalone `main` remains
   `33c37e56df6dd70e0963b2caca824e100f5e3d7e`; it was not merged, rewritten,
   or pushed.
6. Old Phase 28L failure: `/P28L` emitted `Compiler: build FAIL` before
   `DEVELOPER_STUDIO_PHASE28L_BUILD_PASS`, before Developer Studio launch, and
   before debugger assertions. The historical source diagnostic was not
   preserved, so no narrower root cause is claimed. A current isolated Phase
   28L rerun passed.
7. Package changed legitimately after diagnostic source changes and rebuild:
   amd64 `861,856` bytes / `BC9D35CB1D9582EB0ABD4CD434E05EA8162B1B1C22F030E4A6D376B4C4546DF3`;
   arm64 `1,015,504` bytes /
   `DDED73B4B5D9C683CCC051370E1CEBA6FBB9DE30BF6997B7A7B2331DEBAC5A01`.
8. In-guest architecture: disposable `-Phase28MOnly` QEMU boots stage the
   real `/Apps/DeveloperStudio` App Model package plus `/P28M`; the packaged
   app's sentinel-gated diagnostic mode runs the production UI/controller
   paths. The server smoke harness only verifies package discovery/launch and
   cleanup.
9. Marker provenance: app/window/debug-start/pane/watch/breakpoint/step/output/
   stop/session markers originate in the real Developer Studio binary; package
   discovery and final cleanup markers originate in the authoritative guest
   harness/App Model checks.
10. UI results: launch, window, project, debug start, initial pause, Call Stack
    (`helper_tail`, `helper`, `gx_main`), caller selection, Locals/Arguments
    (`input=10`, `delta=4`), Watch (`input + delta = 14`), safe invalid-watch
    error, breakpoint manager action/policy, Output, Continue, Step Into, Step
    Over, Step Out, Stop/Cancel, stale clearing, enablement, and second
    generation all passed.
11. Breakpoint/output details: entries covered `src/helper.cpp:3`, the
    `src/helper.cpp:13` LOG point, and `src/main.cpp:15`; output ordering was
    `input=10 doubled=28` then `input=1 doubled=6`, with no logpoint pause.
12. Bounds: `watches=8`, `call_stack=16`, `breakpoints=8`, `output=32`.
13. Screenshot/framebuffer: no screenshot or region hash was captured; the
    guest reported a live framebuffer and the internal pane/render markers and
    model assertions are authoritative.
14. Fresh boots: final-package evidence directories are
    `2977c7ecab884507a88f6443fedd6392`,
    `a5400b9214f24a8eab44396a31914bde`, and
    `0669a8fe585f4e1880abfe0ee43abf57`; all three reached `GUEST_PASS`.
15. Host validation: standalone amd64/arm64 builds and focused model tests,
    ABI layout, breakpoint manager/policy, output, watches, debugger runtime,
    source-step, call stack, NativeElf runtime, App Model, validator,
    trampoline, compiler-functions, corrected filesystem runner, PowerShell
    parsing, and `git diff --check` passed.
16. Filesystem omission: the runner now includes
    `kernel/core/native_elf/native_elf_debug_watches.cpp`; its corrected test
    passed.
17. Physical mouse: not used; the in-guest keyboard/controller path is the
    deterministic proof. External computer-use remained unavailable
    (`apps=[]`) and was not treated as an environment blocker.
18. Remaining limitations: no external screenshot artifact and no physical
    mouse trace. No production/default diagnostic behavior is enabled without
    the explicit staged sentinel. Nothing was pushed.
