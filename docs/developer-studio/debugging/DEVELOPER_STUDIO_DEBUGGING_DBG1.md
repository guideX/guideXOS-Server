# Developer Studio Debugging — DBG1

**Date:** 2026-10-26
**Outcome:** `DBG1_HOSTED_READY_GUEST_PENDING` (Outcome B)
**Track:** Developer Studio — Debugging

DBG1 establishes an isolated IDE checkout, verifies the inherited debugger baseline, fixes a reproduced debug-start rejection for workspace-discovered source files, and passes hosted qualification. The one QAC guest run reached the debugger start path but failed before debugger readiness. Phase 30B remains open; this report does not claim guest acceptance.

## Repository topology

| Repository | Working directory | Branch | Starting HEAD | Remote | Starting status |
|---|---|---|---|---|---|
| Server | `D:\dev\guideXOS_DeveloperStudio_Debugging` | `DEVELOPER_STUDIO_DEBUGGING` | `e24ee589a0f13132d42487e1b04532a6e6d3cde3` | `origin git@github.com:guideX/guideXOS-Server.git` | Clean |
| IDE source checkout | `D:\dev\guideXOS_Developer_Studio` | `main` | `058c83fe1ae270a103eadc7caca961328e8abe2a` | `origin git@github.com:guideX/guideXOS_Developer_Studio.git` | Clean |
| IDE debugging worktree | `D:\dev\guideXOS_Developer_Studio_Debugging` | `DEVELOPER_STUDIO_DEBUGGING` | `058c83fe1ae270a103eadc7caca961328e8abe2a` | Same IDE remote | Created clean from IDE `main` |

The IDE `main` checkout remained on `main` and clean. The Server debugging branch and the dedicated IDE debugging branch are separate worktrees. No source tree was copied into Server. The Server’s remote `DEVELOPER_STUDIO_TOOLS` and `v0.5_DEVELOPER_STUDIO` refs were also present at the verified baseline commit. No sibling track worktree was changed.

Before the Server report commit, the Server branch remained at its starting HEAD. The IDE implementation commit is `d09e8e1b8ed40fd7a1277ebe835acf56344241d1`. The exact Server ending HEAD is the commit containing this report and is recorded in the final task response.

## Code ownership and debugger baseline

Server owns OS-side Native ELF debugging integration: debugger host calls, native runtime/session ownership, debug services, exception/breakpoint handling, and the QAC guest harness. The IDE repository owns debugger launch orchestration, project/build identity, source/DWARF/GXSM mapping, breakpoint state, stepping, stack/variable/watch views, and session UI/lifecycle. This separation remains intact.

The inherited IDE implementation already includes:

- Debug launch admission and lifecycle state, readiness/state polling, and failure diagnostics.
- Software breakpoints with address/source mapping and generation-scoped binding identities.
- DWARF v5 and GXSM metadata parsing, source-line mapping, and symbol-capacity checks.
- Continue, pause, Step Into, Step Over, and Step Out paths.
- Call-stack, argument/local-variable, selected-frame, and watch evaluation support.
- Stop/relaunch/teardown handling and stale-session ownership checks.

These capabilities are implemented and covered by hosted tests. The current guest result below shows that production guest start is still not accepted end to end.

Relevant IDE design docs include `docs/DEBUGGER_FOUNDATION.md`, `docs/DEBUGGER_SOFTWARE_BREAKPOINTS.md`, `docs/DEBUGGER_SOURCE_MAPPING.md`, `docs/DEBUGGER_STEPPING.md`, `docs/DEBUGGER_CALL_STACK.md`, `docs/DEBUGGER_LOCALS_ARGUMENTS.md`, and `docs/DEBUGGER_WATCHES.md`.

### Historical qualification state

- Phase 29I recorded incomplete guest acceptance (one accepted guest out of five); later sentinel/readiness work is documented in Phase 29J.
- Phase 29R recorded hosted lifecycle/stress passes alongside a Server exit access violation; Phase 29S did not reproduce that access violation and did not establish a production debugger defect from it.
- Phase 30A’s final combined hosted run recorded 25/25 passes.
- Phase 30B remains **Outcome B / open**. Its report records 0/5 accepted boots in the latest formal attempt, with QEMU isolation and shared-lane ownership still unmet. Phase 30C was not started.
- No authoritative `.phase` marker was found in either repository. DBG1 did not create or advance a phase marker.

See IDE reports `docs/DEVELOPER_STUDIO_PHASE29I_GUEST_DIAGNOSTIC_SENTINEL_DETECTION_STABILIZATION.md`, `docs/DEVELOPER_STUDIO_PHASE29J_FRESH_QEMU_PRE_GUEST_BOOT_RELIABILITY.md`, `docs/DEVELOPER_STUDIO_PHASE30A_BREAKPOINT_MANAGER_REMAP_RETENTION.md`, and `docs/DEVELOPER_STUDIO_PHASE30B_LONG_ROOT_DWARF_ARTIFACT_PATH.md` for their full historical evidence and limits.

## Reproduced issue and correction

The focused Phase28O guest run exposed a debug-start failure after the project workspace had reopened and the bare-metal build completed. The Phase28O project has no explicit source list or `sourceEntry` in `guidexos.project`. For bare-metal builds, `BuildControllerStart` therefore discovers the project’s source files through the workspace and stores them in `g_buildController.request`.

At debug start, `DebugTargetFromBuild` previously called `RunRequestFromBuild` with only the project and build result. `RunRequestFromBuild` rebuilt a request from project metadata; for this project that request had an empty source list. Its source-membership hash could not match the successful build’s workspace-discovered source hash, so the build was rejected as stale and the debugger returned `NoRunnableTarget`.

The IDE fix threads the active build request into `DebugTargetFromBuild` and `RunRequestFromBuild`. The code still verifies project identity, root, target profile, build system, artifact path, configuration, and any explicitly declared source list. For projects whose source set is workspace-discovered, it validates against the request that produced the successful build. A regression in `tests/debugger_test.cpp` covers the metadata-free bare-metal project case and verifies the old empty-request path rejects while the exact build-request path succeeds.

The same IDE commit also:

- Qualified two build-diagnostic calls with their declared namespace so the documented Native ELF package build compiles.
- Added missing source dependencies to the documented `build.ps1` run/debugger test link commands.
- Replaced hard-coded shared-checkout paths in debugger test fixtures with a helper rooted at the test process working directory, so the isolated worktree can run the same fixtures.

## Hosted qualification and builds

| Qualification | Result | Evidence |
|---|---|---|
| IDE hosted CMake/CTest, after the fix | **34/34 PASS** | `C:\Users\guideX\AppData\Local\Temp\guidexos-dbg1-ide-hosted-20261026` |
| Debug-symbol capacity harness | **PASS** — 411 DIEs, 2 CUs, DWARF 5, no truncation; 512-DIE capacity boundary passed | Harness output and fixture ELF SHA-256 `A386C1B3CF60708157DB9AD3C0AC057204ADB3D4DD5F2293717C6892EEC4150D` |
| IDE documented `build.ps1` qualification | **PASS** after link inputs and namespace qualification were corrected | `C:\Users\guideX\AppData\Local\Temp\guidexos-dbg1-ide-package-final-20261026.log` |
| Final IDE package | **PASS** — ELF64, ET_EXEC, AMD64; sectionless package | SHA-256 `97F6B011507C4A1F85C48C5A270355F3EFE4200E9BCF78AFF9DF9A6218CCD4C2`; manifest id `com.guidexos.developerstudio` |
| Server `build-native-experimental.bat` | **PASS** | SHA-256 `5D656B218D5BB5282B702511FC11B30893887EA83173286F931E6AF2703E02B8`; build ran in a detached staging worktree |
| Server native debugger runtime | **PASS** after restoring the unmodified Server source | `C:\Users\guideX\AppData\Local\Temp\guidexos-dbg1-server-runtime-postrestore-20261026.exe` |
| Server debug policy, output, and watch contracts | **PASS** | `C:\Users\guideX\AppData\Local\Temp\guidexos-dbg1-server-hosted-20261026` |

A first fresh IDE CMake run reported 9/13 because the new worktree did not contain generated Phase 8/9/10 ELF fixtures. Those fixtures were built with their documented procedures; the focused suite then passed 13/13, and the final post-fix CTest run passed 34/34.

The first documented IDE build attempt found that `build.ps1` omitted linked source files from its run/debugger hosted test commands. It also exposed the two unqualified build-diagnostic references in `main.cpp`. The test link inputs and namespace references are now corrected in the IDE commit, and the documented package build passes. The optional CMake Native ELF target was not used for production packaging; the repository directs Native ELF packaging through `build.ps1`.

The Server runtime harness had one initial timing-sensitive assertion failure around asynchronous `Continue`; an immediate rerun and a final rerun after restoring the original Server test source both passed. No Server debugger code change was retained from that investigation.

## QAC guest attempt

One focused `Phase28OOnly` guest attempt used the existing QAC runner and the QAC CLI. Before launch, QAC was online with zero QEMU processes and zero managed sessions. Reservation `a68d8b226e1f40398181538bbb07bab9` was granted for QEMU PID `12228`.

The guest booted and passed the Phase28M application launch/window steps. Phase28O passed workspace restore, project open, configuration, capacity, isolation, save/close/relaunch, no-runtime-id, and dirty-buffer checks. The successful bare-metal build reported `build_state=5`, `artifact_valid=1`, `entry_point=1`, and a nonempty artifact hash. Debug start then emitted:

```text
DEVELOPER_STUDIO_PHASE28V_STARTUP_FAILURE target_construct error=no_runnable_target project_valid=1 project_kind=0 build_state=5 artifact_valid=1 entry_point=1 artifact_hash=1
DEVELOPER_STUDIO_PHASE29F_DEBUG_START_RESULT code=DEBUG_START_TARGET_INVALID error=no_runnable_target
DEVELOPER_STUDIO_PHASE28O_FAIL reason=phase28m_debug_start
```

The later materialization, breakpoint-hit, execution-frame, watch-reevaluation, second-generation, and cleanup PASS markers were therefore absent. This is the guest evidence that led to the correction above. The guest used the pre-fix IDE package SHA-256 `7013F4BB0E0BF1F46886F5C28ED0AA40691391F422580B568A4FACCED6EB5854`. The post-fix package hash is `97F6B011507C4A1F85C48C5A270355F3EFE4200E9BCF78AFF9DF9A6218CCD4C2`; that binary was built and header-validated but was not run in a guest during DBG1.

QAC’s six-minute runtime cap expired after the debug-start failure. The QAC host trace records `QMP QUIT SENT`, QEMU exit code 0, and reservation release. Final coordinator status was online with zero managed sessions and zero foreign QEMU processes. Evidence is preserved at:

- Serial: `C:\Users\guideX\AppData\Local\Temp\guidexos-phase28g-21a736ea3d754064b406793e7bd85e8d\boot1.serial.log`
- QAC host lifecycle/QMP: `C:\Users\guideX\AppData\Local\Temp\guidexos-phase28g-21a736ea3d754064b406793e7bd85e8d\boot1.host-trace.log`
- Bounded Phase28O failure trace: `C:\Users\guideX\AppData\Local\Temp\guidexos-dbg1-qac-phase28o-20261026\developer-studio-phase28o-20261026-164209.log`
- Runner output: `C:\Users\guideX\AppData\Local\Temp\guidexos-dbg1-qac-phase28o-20261026.log`

The Server staging package was restored to its original manifest and ELF hashes before the staging worktree was removed: `Apps/DeveloperStudio/app.json` SHA-256 `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE`; `Apps/DeveloperStudio/bin/amd64/developerstudio.elf` SHA-256 `1387CB09F2026252E4A277E96256C2BB59D06AE70ACFAA56A84EFEFF2F9CF845`. The primary Server working tree remained clean until this report was added.

## Remaining Phase 30B work

The post-fix hosted regression and package build pass, but the production guest path has not yet exercised the fix. Phase 30B’s formal 5/5 fresh QEMU acceptance remains open, and its shared QEMU coordination prerequisite is still unresolved. No Phase 30B credit or Phase 30C readiness is claimed from this single Phase28O attempt.

## Recommended DBG2 scope

1. Build the committed IDE debugging branch and rerun focused Phase28O under an exclusive QAC reservation; verify debug start, readiness, breakpoint materialization/hit, one basic command, stop, QMP/QEMU exit, and reservation release.
2. Once the shared-QEMU ownership prerequisite is met, run the formal Phase30B five-boot acceptance without retries or weakened criteria and record every boot’s debugger state and cleanup evidence.
3. Start Phase30C only after the formal Phase30B criteria and its phase gate are satisfied.

## Commits and publication

- IDE `DEVELOPER_STUDIO_DEBUGGING`: `d09e8e1b8ed40fd7a1277ebe835acf56344241d1`.
- Server `DEVELOPER_STUDIO_DEBUGGING`: this report is committed separately on the Server branch; the commit id is recorded in the final task response.
- Push: not performed.
