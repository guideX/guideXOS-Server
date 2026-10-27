# Developer Studio Debugging DBG3 — Source-Level Stepping & Paused-State Reliability

**Outcome:** A — `DBG3_HOSTED_STEPPING_VERIFIED`

**Qualification scope:** Hosted only. No QEMU was launched and no QAC admission was consumed. Formal Phase30B remains **0/5**.

## Repositories and revisions

The branch and expected starting HEAD preflight matched the DBG3 request in both repositories. DBG1 and DBG2 reports were reviewed before changes. Compilation and Tools sibling workspaces were left untouched.

| Repository | Branch | Validated source/test HEAD |
| --- | --- | --- |
| Server — `D:\dev\guideXOS_DeveloperStudio_Debugging` | `DEVELOPER_STUDIO_DEBUGGING` | `ef49f074b554ac7b11f040306b94473022af9243` |
| IDE — `D:\dev\guideXOS_Developer_Studio_Debugging` | `DEVELOPER_STUDIO_DEBUGGING` | `5723d1b31f2df14ee3ac785f153d0c823555696b` |

This report is committed after the validated source/test revisions in a documentation-only Server commit. The final repository HEADs are recorded in the task result.

## Existing stepping path

- IDE actions and key/menu dispatch live in `src/main.cpp`; Step Into, Step Over, and Step Out enter the debugger controller in `src/developer_studio_debugger.cpp`.
- Commands require a current paused snapshot and valid operation capabilities. Stop, session, thread, stop, and command generations are checked while polling and accepting traps so an old response cannot replace a newer stop.
- Step Into requests one instruction and uses the source mapper to advance to a bounded next source location. Step Over decodes a call, binds a temporary return breakpoint, and validates internal return stops; non-call instructions use the bounded source-step path. Step Out requires a current call stack and caller return address, then updates caller-frame context after the return.
- Temporary breakpoint ownership is separate from persistent user breakpoint ownership. A user breakpoint during Step Over becomes the visible stop and cancels/removes the temporary return owner.
- The IDE refreshes call stack, variables, and watches for each new paused session/stop generation. The defect found was that controller snapshots from an earlier stop could remain marked valid while a new step or owned-breakpoint stop was being published.
- The Server native runtime validates request identity and generations, controls a closed/released execution gate, and delivers software-breakpoint and single-step traps. Native runtime tests use executable host machine code; these exercise real target instructions and trap delivery, but do not stand in for the production guest runtime.

## Changes

### IDE

- Invalidate call-stack and variable snapshots when publishing Step Into, Step Over, or Step Out stops and when applying a newly owned breakpoint stop. Clear the old source location on an owned breakpoint stop so a view cannot pair a new PC with old source metadata.
- Add controller contracts for Step Into mapping into a callee source file/line, stale view invalidation across stepping, callee user-breakpoint interruption of Step Over while preserving persistent user breakpoints, and Stop during pending Step Over with terminal resource cleanup.

### Server

- Add a host-executed Step Over scenario where a persistent callee breakpoint interrupts the call. It verifies the callee body has not run at the breakpoint, the temporary return owner is removed, both user breakpoints remain, and cancellation restores all patched bytes.
- Remove a race-prone post-release assertion that read an instruction byte after the target was allowed to run. The subsequent trap and instruction counter provide the execution evidence instead.
- Correct the breakpoint-manager contract test's request-size assertion: policy bytes stop before the appended command generation, while a full step request includes it.

## Hosted scenarios and results

- **IDE suite:** CMake Debug build and CTest — **34/34 passed**, including debugger stepping, symbols, stack, variables, watches, and breakpoint tests.
- **Server native debugger runtime:** **PASS**. Real host instructions reached a software breakpoint, executed a single-step instruction and delivered its single-step trap; Step Over executed a real `CALL`, ran the callee, and trapped at the return address; Step Out read a real frame return address and reached its trap. In the added interruption scenario, the callee user breakpoint trapped before the body (counter `0`), temporary return ownership was removed while user owners remained, and cancellation restored the call-site, return-site, and callee bytes. The released target then completed the original callee instruction (counter `1`); cancellation restores debugger state and releases execution, it does not forcibly terminate that native thread.
- **IDE Step Into callee mapping contract:** PASS; the fake backend's next PC maps to `src/math.cpp:4`. This is a mapping/controller contract, not a claim of real source-level guest execution.
- **GXSM compiler fixture:** `scripts/build-gxsm-debugger-fixture.ps1` — **PASS** using the positive debugger fixture. The production compiler emitted 4 source rows across `src/main.cpp:3–10`, 2 variable records, and a 5,021-byte ELF; fixture SHA-256: `1E6A90DE539738A7D1C31F59AC5CA748BB073804CBBAEDC4F3C23E1F0ECA4F70`. Entry validation passed. This verifies metadata generation, not NativeAppDebugger acceptance or guest stepping.
- **Stop lifecycle contract:** IDE `debugger_step_test` verifies Stop during a pending Step Over reaches terminal cleanup, clears the temporary ID, and prevents another step command. The Server host runtime interruption case verifies breakpoint cleanup and target release. A production guest session restart after stepping remains unqualified.
- **Server focused contracts:** `run-native-source-step-controller-test.ps1`, `run-native-debug-policy-contract-test.ps1`, `run-native-debug-output-contract-test.ps1`, `run-native-debug-watches-test.ps1`, `run-native-call-stack-controller-test.ps1`, and `run-native-breakpoint-manager-contract-test.ps1` — **PASS**.
- **DWARF capacity fixture:** `run-debug-symbols-capacity.ps1` — **PASS** with 411 DIEs, 2 compilation units, 4 functions, 23 variables, and 191 line rows; no truncation. Fixture SHA-256: `A386C1B3CF60708157DB9AD3C0AC057204ADB3D4DD5F2293717C6892EEC4150D`.
- **IDE package check:** documented package build completed successfully against an isolated temporary ServerRoot. ELF SHA-256: `23B18EB127B1012F259CEFEBBF4C18150132451117B92D47257CB321CB9E4844`. No Server checkout output was used as a build destination.

The runtime build emitted existing `strncpy` truncation and allocator indentation warnings; the test completed successfully.

## Paused-state invariants

1. Step commands start only from an eligible current paused stop with valid thread/stop context.
2. A pending step cannot be replaced by another step command; stop transitions to Stopping and the controller accepts terminal cleanup before becoming Exited.
3. Stop and trap results are associated with the session, thread, stop, and command generations. Stale results do not become the current paused state.
4. New stops invalidate cached call-stack and variable snapshots. Consumers must fetch views associated with the latest session/stop generation.
5. Temporary stepping breakpoint owners are removed when a step completes, is interrupted, becomes stale, or is stopped. Removing a temporary owner preserves persistent user owners at shared or separate addresses.
6. Terminal cleanup clears active step state and temporary IDs; breakpoint teardown restores original bytes before the runtime is unregistered.

## Qualification limits and DBG4 recommendation

Hosted evidence now covers the IDE's source mapping/controller contract and the Server native debugger's real host instruction stepping, breakpoint interruption, and cleanup. It does **not** qualify production guest Step Into/Over/Out, guest GXSM source-line metadata, or guest stack/local refresh after each step. The native runtime scenarios report GXSM metadata as absent, so no guest-equivalent result is claimed.

**DBG4 recommendation:** after the shared-QEMU ownership prerequisite is satisfied, run an admitted production guest scenario with debug metadata covering nested calls, multiple executable lines, normal return, user breakpoints during each step mode, and end-of-function/program behavior. Verify guest source locations, selected frame, locals/watches after every stop, and teardown/restart generations. Only that formal admission may advance Phase30B beyond 0/5.
