# Developer Studio Debugging — DBG5

**Track:** Developer Studio — Debugging  
**Scope:** Hosted source-debugging integrity and production guest readiness  
**Disposition:** **Outcome B — `DBG5_PARTIAL`**  
**Execution:** Hosted only. No QEMU launch, QAC reservation, or guest qualification attempt occurred.

## Repository record

| Repository | Branch | Starting HEAD | DBG5 ending HEAD |
|---|---|---|---|
| Server (`D:\dev\guideXOS_DeveloperStudio_Debugging`) | `DEVELOPER_STUDIO_DEBUGGING` | `19d7026a618e3a24379b6f41b0e109f1c9b337e0` | This report's commit (current branch `HEAD`; exact ID is in the closeout) |
| IDE (`D:\dev\guideXOS_Developer_Studio_Debugging`) | `DEVELOPER_STUDIO_DEBUGGING` | `36792d639c6f36d1a24897ae2f863ddbc345f71b` | `c26787e` (`test(debugger): qualify source mapping and watch generations`) |

The IDE commit contains two regression-test additions and corrections to the hosted stepping runner's Phase 15 expectations. No Server production source, QAC integration, sibling worktree, or phase marker was changed. Both branches remain local; nothing was pushed.

## Findings and corrections

- Source lookup distinguishes a mapped executable row from a valid source line with no executable location. The latter remains Pending, returns `LineNotMapped`, has no address or binding, and reports `Pending: no executable mapping for this line`. The IDE regression now checks this through the controller API; it does not add a fallback to a nearby line.
- Phase 15 `SteppingLifecycle` defaults in the hosted runner referred to obsolete source rows and an unmapped line. The runner now defaults to mapped `src/main.cpp:37`, conditions on `counter == 2` so the loop does not immediately re-hit the persistent user breakpoint during Step Out, and expects the current stops at lines 38, 39, and 48 (`debugLoop` / `debugCaller`). The mapping assertion now accepts the DWARF row's reported column rather than requiring the editor request's column.
- The Watch regression now carries the active session and stop generations through its memory fixture. It verifies that available and invalid-expression results identify the current session, stop, frame, and artifact; Running/Stale results clear paused-state identity; and a subsequent session reevaluates against its new memory image rather than keeping the old value.
- No debugger production-code defect was reproduced in the hosted source-stepping flow. The positive GXSM end-to-end run did not complete its target-exit/relaunch assertions, so DBG5 remains partial rather than claiming full GXSM lifecycle qualification.

## Hosted qualification results

| Area | Result and evidence | Qualification boundary |
|---|---|---|
| IDE CTest | **34/34 passed** after the debugger test changes. | Host tests only. |
| Source mapping and breakpoint | The fixture maps `src/main.cpp:37` to five instruction locations, with primary address `0x200016AC`. The hosted IDE smoke reached an authoritative stop at that source row. The unmapped-row controller regression verifies zero addresses and explicit diagnostics. | No production guest was run in DBG5. |
| Integrated stepping lifecycle | **PASS** using the real hosted Developer Studio and Server backend. Step Into stopped at line 38, Step Over at line 39, and Step Out returned to `debugCaller` at line 48. Each stop published current source, stack, and locals for its stop generation. Continue invalidated paused inspection and shutdown completed with Server exit code 0. | The Phase 15 fixture has no GXSM variable records. Watch correctly reported metadata unavailable; these results are not runtime Watch-value proof. |
| Breakpoint ownership and instruction bytes | The hosted trace recorded the user breakpoint installed at `0x200016AC`. Step Out used a temporary return binding, stopped in the caller, removed the temporary owner, and kept the user breakpoint installed. | Real instruction-byte restoration and trap interaction were also covered by the Server native runtime test below. |
| DWARF parser and capacities | **PASS.** Capacity runner reported DWARF 5, Ready, 2 CUs, 411 DIEs, 4 functions, 23 variables, 191 line rows, and bounded query truncation. It accepted DIE counts 511/512, rejected 513 with `limit_exceeded`, and safely rejected malformed/truncated inputs. | This is host parser evidence, not a guest run. |
| Frame-specific locals | Existing IDE coverage and the hosted Phase 15 UI run verified current stack/local snapshots after the breakpoint and all three step stops. The hosted snapshots were tied to their accepted stop generations. | The Phase 15 trace explicitly lacked GXSM metadata for Watch evaluation. |
| Watch generation integrity | **PASS** in the updated IDE regression: values and errors carry current identity; Running/Stale clears stop/frame identity; restart generation 10 reevaluates `doubled == 42` as false from the new memory image. | The integrated GXSM lifecycle did not complete its final exit/relaunch assertions. |
| GXSM producer | **PASS.** The compiler fixture produced a 5,021-byte ELF with the expected source/variable records and validated its unmapped entry prologue. SHA-256: `1E6A90DE539738A7D1C31F59AC5CA748BB073804CBBAEDC4F3C23E1F0ECA4F70`. | Compiler output alone does not prove debugger reads or guest GXSM. |
| Hosted GXSM reads | The positive UI run accepted GXSM v2 and read live `counter` bytes as 2 at the breakpoint and 3 after Step Over. It switched to the caller frame, reported the callee-local Watch as unavailable there, and completed Step Out to `gx_main`. A later relaunch attempt reached a fresh session and accepted the GXSM records again. | The smoke runner timed out waiting for its final normal-exit marker. This attempt is incomplete and is not counted as a full positive lifecycle pass. No production guest GXSM result is claimed. |
| IDE production package | **PASS** for AMD64 and ARM64 using the isolated package ServerRoot and package validator. The package contains only the canonical manifest and two architecture ELFs. | This proves package structure and binaries, not guest execution. |
| Server native runtime/contracts | **PASS.** The real host-instruction backend exercised software traps, instruction-byte restoration, Step Into, Step Over across a real `CALL`/return, Step Out, persistent user breakpoint interaction, and three fresh runtime registration cycles. Six focused contracts passed: breakpoint manager, source-step controller, call stack, Watch, debug policy, and debug output. | GXSM was absent from the standalone Server runtime fixture; GXSM memory reads are evidenced separately above. |
| Full isolated Server build | **PASS.** `build-native-experimental.bat` built the Server executable from the requested Server HEAD with the AMD64 and ARM64 IDE packages staged in a temp tree. | Temporary build only; no output was written into either checkout. |

### Package provenance

The package was built from IDE starting HEAD `36792d639c6f36d1a24897ae2f863ddbc345f71b` and validated from the isolated temp ServerRoot. The manifest hash is `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE`.

| File | Size | SHA-256 |
|---|---:|---|
| `bin/amd64/developerstudio.elf` | 1,158,692 bytes | `F38F9A714B2127413F000F8E50C3050E5FF88ED73FCBEE184A4639441E270F06` |
| `bin/arm64/developerstudio.elf` | 1,323,380 bytes | `7754F3F83D0423D188968F650F381518F80BA427CB7B5C51E8F4BA57A9ACD822` |

The Server executable built in the isolated source snapshot was 5,979,988 bytes, SHA-256 `EBA10A0DC85D0AC4F61EAE376BF04757ECEBC85E5315767570D7D9498A574720`.

## Phase30B readiness matrix

The authoritative Phase30B contract remains **five independent accepted guest boots** with retained per-boot evidence and verified ownership, cleanup, process exit, and reservation release. The separate debugger observations below are useful acceptance evidence, but DBG5 does not redefine them as additional formal Phase30B predicates.

| Evidence | Formal Phase30B status | DBG5 status |
|---|---|---|
| Guest reaches debugger Ready/Running and executes | Part of demonstrating an accepted production boot; not a separate five-boot counter. | Not run in a guest. Hosted backend exercised. |
| Source breakpoint maps and triggers | Desirable additional debugger acceptance evidence. | Hosted pass at line 37; historical generic guest breakpoint evidence exists in DBG2, but no current package guest run. |
| Step Into, Step Over, Step Out and fresh source stops | Desirable additional debugger acceptance evidence; not a separate Phase30B gate. | Hosted Phase 15 pass; no production guest proof. |
| Stack, locals, selected frame, and Watch belong to the accepted stop | Desirable additional debugger acceptance evidence. | IDE regression and hosted UI evidence pass; positive GXSM hosted end-of-lifecycle remains incomplete. |
| GXSM metadata and target-memory reads | Desirable additional debugger acceptance evidence. | Hosted positive fixture read pass; production guest GXSM remains unverified. |
| Stop/Restart preserve generation and cleanup behavior | Lifecycle evidence to retain with an accepted boot; not an additional five-boot count. | IDE/controller and native hosted coverage pass; no production guest run. |
| QAC/QEMU owner identity, child exit, cleanup, and release | Required to establish the formal gate's ownership and release evidence for each admitted run. | No QAC reservation or QEMU child was started in DBG5. |
| Five accepted boots with evidence and verified release | **Mandatory formal Phase30B gate.** | **0/5**; DBG5 made no guest attempt. |

## Exact blocker and next run plan

The authoritative [`DEVELOPER_STUDIO_PHASE30B_LONG_ROOT_DWARF_ARTIFACT_PATH.md`](../../DEVELOPER_STUDIO_PHASE30B_LONG_ROOT_DWARF_ARTIFACT_PATH.md) records `EXTERNAL_QEMU_COORDINATION_BLOCKER`. The inspected C176 and Task Manager launchers can start QEMU without acquiring a common atomic reservation; their project-scoped process checks and the Phase30B census detect overlap but do not own the lane across all five boots. The earlier Phase30B attempt remains one failed launched boot and **0/5 accepted**. DBG5 did not retry it, create a run token, request QAC, or touch another project's guest process.

When a coordinator honored by every relevant QEMU launcher is available, run the authoritative Phase30B procedure without weakening its no-retry/evidence-preservation rules. Retain one package and fixture identity across five independent boots. For each boot, capture the QAC reservation and direct child identity, the existing guest acceptance result and logs, guest exit status, verified child reap, reservation release, and release time. Collect the additional debugger evidence in the table as appropriate, without changing Phase30B markers or counting hosted runs as guest credit.

## Warnings and recommendation

No new production compiler warning was introduced by DBG5. Existing build warnings included the possible `strncpy` truncation in `native_app_debugger.cpp:196`, misleading-indentation warnings in existing Server sources/headers, and the IDE build's existing unused `clearResult` warning in `developer_studio_build.cpp:197`.

The hosted source mapping, frame/locals, Watch-generation, native runtime, DWARF, compiler fixture, and package checks are useful and mostly pass. The positive GXSM full lifecycle and all production guest claims remain unfinished. **Pause further hosted debugger phases and wait for shared QEMU ownership coordination**, then execute the formal five-boot guest qualification and record the missing guest-only evidence.
