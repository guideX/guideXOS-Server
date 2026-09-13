# Developer Studio Phase 28M — Integrated Debugger UI

## Outcome

**Outcome B — UI implemented, packaging/runtime integration boundary remains.**

The editable standalone Developer Studio now contains a bounded integrated
debugger surface consuming the current server debugger ABI. The resulting
amd64 and arm64 ELFs were produced by the normal standalone build/staging flow
and copied into this server repository. The remaining boundary is proof inside
the real guideXOS guest: the available computer-use surface exposed no QEMU
application (`apps=[]`), and the existing Phase 28L guest fixture currently
fails during its fixture build before debugger assertions. No visible QEMU
Developer Studio launch, pause, or mouse-driven interaction is claimed.

This is not an ABI incompatibility. The standalone client compiled against the
current server headers, and the focused ABI/debugger host contracts passed.

## Repository state and provenance

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

## Build and ABI synchronization

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

## UI evidence and acceptance matrix

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

## Passing host validation

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

The Phase 28M milestone is therefore implemented and packaged, with the
remaining work precisely bounded at real guest App Model/QEMU UI exercise.
