# Developer Studio Debugging DBG4 — Session Restart & End-to-End State Reliability

**Outcome:** A — `DBG4_HOSTED_RESTART_VERIFIED`

**Qualification scope:** Hosted only. No QEMU was launched and no QAC admission was requested. Formal Phase30B remains **0/5 accepted guest boots**.

## Repository provenance and baseline

Both repositories were verified on the requested `DEVELOPER_STUDIO_DEBUGGING` branch and expected HEAD. Both worktrees were clean at preflight. No branch switch, reset, rebase, stash, cleanup, or history rewrite was performed. Compilation and Tools workspaces were left untouched.

| Repository | Branch | Expected and starting HEAD | Preflight status |
| --- | --- | --- | --- |
| Server — `D:\dev\guideXOS_DeveloperStudio_Debugging` | `DEVELOPER_STUDIO_DEBUGGING` | `aa7c90a537ebf834db921384ebdfd2524064048b` | clean |
| IDE — `D:\dev\guideXOS_Developer_Studio_Debugging` | `DEVELOPER_STUDIO_DEBUGGING` | `5723d1b31f2df14ee3ac785f153d0c823555696b` | clean |

DBG1–DBG3 and the debugger lifecycle, source mapping, stepping, stack/locals/watches, and Phase30B/QAC documents were reviewed. DBG3's baseline was 34/34 IDE CTests and production package ELF SHA-256 `23B18EB127B1012F259CEFEBBF4C18150132451117B92D47257CB321CB9E4844`. The Server host-runtime warnings recorded by DBG3 were also rechecked below.

## Existing lifecycle contract

- `src/main.cpp` loads symbols from the target artifact and verifies the artifact snapshot before debugger launch. It resets the IDE runtime view, publishes the project generation, remaps persisted breakpoint intent, then calls `DebugControllerStart`.
- `DebugControllerStart` advances `sessionGeneration`, initializes process/runtime/stop identity, invalidates inspection context, and invokes the backend launch callback. Backend snapshots are applied only for the current session generation. A new stop is identified by session and stop generation; stepping operations also carry command generation and thread identity.
- Continue from a software breakpoint may return accepted while the backend is still completing its breakpoint-rearm single-step. The controller exposes `SingleStepPending`; a repeated Continue is rejected until polling reports the resulting running/stop/terminal state. Pause and Stop similarly have explicit acceptance and later observed state transitions.
- Source/address breakpoint mappings belong to the target project/artifact generation. A project-generation mismatch marks a persisted breakpoint Stale. Valid user breakpoint IDs and source intent can survive a session, while backend binding IDs belong to one runtime and must be recreated.
- Step Over and Step Out own temporary breakpoint bindings separately from user breakpoints. Completion, interruption, and Stop release temporary ownership before terminal cleanup; persistent user owners remain.
- Stack and variable views are tied to the current session/stop/frame identity. Target-memory variable inspection is synchronous in this path; there is no queued variable-response callback to arrive late. Snapshot and view APIs reject mismatched identities, while a new session explicitly clears the cached variable/stack context.

## Defects found and bounded corrections

1. **Persisted user breakpoints could retain a dead backend binding ID.** The controller kept `backendBindingId` from the prior NativeAppDebugger runtime, and binding skipped a breakpoint already marked bound. The new runtime could therefore start without reinstalling that user's breakpoint. Start now clears each backend binding and last-hit record while preserving the user breakpoint ID and valid mapping, so the new session binds fresh owners.
2. **Restart left stale variable-view storage behind.** Start cleared the call stack and stop context but did not invalidate the variable view. Start now clears it together with the selected frame and other stopped-session state; watches remain explicitly marked stale.
3. **A bind or poll failure could abandon a launched target.** Poll previously changed the controller to Failed and inactive immediately after an initial binding/poll failure without asking the backend to stop. It now requests backend Stop, remains Stopping until cleanup is observed, and publishes the original failure after terminal cleanup. A rejected partial breakpoint bind can then be retried in a fresh generation.

The corrections are limited to the IDE debugger controller and its tests. No Server production code, compiler/linker/editor/project-management code, protocol shape, or guest acceptance marker changed.

## Hosted qualification

### IDE

- Full CMake Debug build and CTest: **34/34 passed**. This includes debugger model, step, symbols, stack, structured locals, variables, watches, conditional breakpoints, workspace, and editor tests.
- Three deterministic Start → breakpoint stop → Stop cycles reuse one controller and persistent user breakpoint. Each cycle verifies a new session generation, fresh backend binding ID, current breakpoint stop, cleared stack/variables/temporary step state, and terminal cleanup.
- The second cycle issues Continue at a breakpoint, verifies `SingleStepPending`, rejects duplicate Continue, and stops before completion. A deterministic queued Running state is ignored while Stopping. The following generation rejects the prior session's synthetic step completion and accepts a fresh Continue.
- A controlled backend launch failure is followed by a successful new-generation launch. Partial initial breakpoint binding rejection is observed as Stopping, backend Stop is requested, terminal cleanup leads to Failed, and retry binds both breakpoints in a new runtime.
- Existing step tests verify Stop during pending Step Over clears the temporary return breakpoint ID and reaches cleanup. Existing stale stop/command and current-source/stack/variable tests remain passing.
- The DWARF capacity runner passed: 411 DIEs, 2 CUs, 4 functions, 23 variables, 191 line rows, and bounded query truncation. Fixture SHA-256: `A386C1B3CF60708157DB9AD3C0AC057204ADB3D4DD5F2293717C6892EEC4150D`.

### Server native runtime and compiler fixtures

- `scripts/run-native-debugger-runtime-test.ps1`: **PASS**. Existing real host-instruction Step Into, Step Over (`CALL`/return), Step Out, breakpoint trap, and cleanup scenarios passed. Three additional RegisterRuntime/UnregisterRuntime cycles execute a real `INC` instruction under a software breakpoint, cancel/restore the breakpoint, and complete exactly one instruction. Evidence: cycle 1 `session=20 breakpoint=920 counter=1`; cycle 2 `session=21 breakpoint=921 counter=1`; cycle 3 `session=22 breakpoint=922 counter=1`. A stale prior-generation Continue request was rejected while the current target remained paused.
- Focused Server contracts all passed: native watches, debug policy, debug output, breakpoint manager, source-step controller, and call-stack controller.
- The production GXSM compiler emitted the positive fixture to a temp directory: 4 source rows, 2 variable records, 5,021-byte ELF, expected unmapped entry prologue validation PASS. SHA-256: `1E6A90DE539738A7D1C31F59AC5CA748BB073804CBBAEDC4F3C23E1F0ECA4F70`. This proves hosted metadata generation only; it does not prove production guest GXSM acceptance.

### Production IDE package

The Debug AMD64 production package build passed using an isolated temporary ServerRoot populated with the requested Server SDK headers. No package output was written into either checkout. Packaged ELF: 1,158,692 bytes, SHA-256 `F38F9A714B2127413F000F8E50C3050E5FF88ED73FCBEE184A4639441E270F06`. Manifest SHA-256: `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE`.

The Server native runtime build still emits DBG3's existing warnings: possible `strncpy` truncation in `native_app_debugger.cpp:196` and misleading indentation in `allocator.cpp:63`. The runtime and all focused contracts passed; no new warning or failure was observed.

## Session isolation and command results

The new controller generation is assigned before backend launch. Breakpoints are rebound to that generation and receive fresh runtime binding IDs. Poll and snapshot application reject prior-generation state, including a delayed step completion after Stop. A Stop requested during pending breakpoint Continue owns the remaining lifecycle; an intermediate backend Running snapshot cannot reactivate the old command. Once Exited/cleanup is observed, a new Start accepts commands only after its own valid stop.

Inspection is cleared when execution resumes and refreshed only against the latest accepted stop. Restart clears selected frame and variable data. The three-cycle assertions preserve the current breakpoint location when stale stop/step snapshots are injected. Variable inspection itself runs synchronously against a current frame context, so a late variable callback is not an available event class in the audited implementation.

## Future production guest qualification

The remaining gap is production guest acceptance, not hosted controller/restart behavior. The retained Phase30B authoritative report records Outcome B, **0/5 accepted**, and an external shared-QEMU coordination blocker. Its unchanged formal requirement is five independent accepted guest boots with per-boot evidence, ownership, cleanup, and release. Do not substitute a hosted fixture or an older Phase 28Q/28O runner for that gate.

When the shared ownership prerequisite is satisfied:

1. Use the authoritative Phase30B runner/admission process with an atomic reservation honored by every relevant QEMU launcher. Record QAC session/reservation identity, direct QEMU child identity, per-boot output and serial evidence, guest exit code, verified reservation release, and the release time. Hold ownership through all five independent boots; foreign-QEMU detection, launch failure, timeout, cancellation, or unverified release earns no pass and must follow the existing no-retry/evidence-preservation policy.
2. In each admitted guest run, use production GXSM metadata and a mapped source breakpoint. Exercise Step Into, Step Over, and Step Out, then verify each accepted stop's `session_gen`, `target_gen`, `stop_gen`, `command_gen`, raw/normalized PC, `source_map=current`, selected frame, stack, locals, and watch values. The existing `tests/smoke-developer-studio-debugger.ps1` exposes `SteppingLifecycle` and `PositiveGxsmLifecycle` sequences and markers such as `debug_step_complete=StepInto|StepOver|StepOut`, `debug_stop_context=authoritative`, `debug_ui_selected_frame=PASS`, `debug_variables=PASS`, and generation-tagged `debug_ui_watch_result=refreshed`.
3. Exercise stop/restart after normal exit, breakpoint stop, failed launch, and a pending step; verify persistent user breakpoints are rebound, temporary step owners are removed, and old-generation stop/step/inspection data cannot replace the current session. Require `TARGET_EXIT_NORMAL code=N`, `debugger_teardown=PASS`, `debug_inspection=invalidated`, and `debug_watch_runtime=invalidated` as the lifecycle evidence already required by `tests/smoke-developer-studio-debugger-positive-exit-gate.ps1`.
4. Preserve Phase30B's five-boot acceptance and existing markers unchanged. The QAC integration notes describe direct-child launch, QMP shutdown, exit-code comparison, and verified `Released` state for the existing focused QAC paths; the infrastructure work must extend common ownership across all participating launchers before Phase30B runs. No local branch may bypass that prerequisite or advance the formal 0/5 marker.

## Disposition and DBG5 recommendation

All applicable hosted lifecycle, repeated-start, state-isolation, Server runtime, fixture, and production package checks passed, so DBG4 is **A — `DBG4_HOSTED_RESTART_VERIFIED`**. Guest stepping, production guest GXSM acceptance, and the formal five-boot Phase30B gate remain unqualified; Phase30B stays **0/5**.

**DBG5 recommendation:** after coordinated QEMU ownership is available, run the formal five-boot production qualification with one consistent artifact and fixture identity. Capture restart-after-stop and stop-during-step behavior, every post-step source/stack/locals/watch generation, and QAC child exit plus reservation release on every boot. Keep any acceptance-marker change outside DBG5 unless separately authorized by the formal gate owner.
