# Developer Studio Debugging — DBG5R1 GXSM Hosted Exit/Relaunch Qualification

**Disposition:** Outcome A — `DBG5R1_HOSTED_GXSM_LIFECYCLE_ACCEPTED`

**Execution:** Hosted only. No QEMU launch, QAC admission, or production guest qualification occurred.

**Recommendation:** `HOSTED_QUALIFIED — PRODUCTION_GUEST_PENDING`. Do not start DBG6 automatically.

## Repository record

| Repository | Branch | Starting HEAD | Implementation ending HEAD | Closeout ending HEAD |
|---|---|---|---|---|
| Server (`D:\dev\guideXOS_DeveloperStudio_Debugging`) | `DEVELOPER_STUDIO_DEBUGGING` | `cdaf99b2f2b9264a00e9743cf692d548d83a72c1` | `5fa7f21bf11e9b8e212271932c8517058a361433` | This report's documentation commit |
| IDE (`D:\dev\guideXOS_Developer_Studio_Debugging`) | `DEVELOPER_STUDIO_DEBUGGING` | `c26787eae76218dfc78b5d3a6458951ad627a028` | `90aa7646e8b81dbe81c24e904c6c22481adccbbc` | `90aa7646e8b81dbe81c24e904c6c22481adccbbc` |

The exact Server closeout HEAD is the commit that adds this report; its full ID is recorded in the closeout message and Git history. Both repositories remain on their required branches. Changes were committed locally and not pushed. No sibling checkout, shared QAC implementation, unrelated launcher, or production debugger source was modified.

## Original DBG5 gap and root cause

DBG5 had already proven hosted source stepping, stack/frame inspection, GXSM-backed reads, and a later fresh launch that accepted GXSM v2. The final exit/relaunch gate did not finish, so DBG5 correctly left the lifecycle partial.

The captured behavior points to stale hosted-runner expectations rather than a target termination, debugger teardown, process ownership, or session-reinitialization failure:

- The Phase 29Q fixture's `gx_main` returns exit code `0`; the positive-exit gate defaulted to `7`. That value prevented a real normal exit from satisfying the gate.
- The old second-cycle script waited a fixed two seconds, sent Continue, and required a second normal-exit marker. DBG5R1's required sequence is to inspect the fresh Cycle 2 stop and Stop that target. The old sequence therefore did not prove the requested second-cycle operation.
- The source breakpoint is line-keyed and resolves to any executable column on line 4. The backend reported column `0`, while one assertion incorrectly demanded column `1`.
- Some terminal records were captured in the live runner output but not duplicated in the child trace. The revised gate checks the explicit fresh-session wait results for Cycle 2's Stop request, target termination, and debugger exit, while retaining separate trace assertions for runtime cleanup and debugger teardown.

No production debugger defect was reproduced. The changes correct the hosted driver and make it observe the required state transitions before proceeding. They do not waive either cycle's terminal or cleanup evidence.

## Changes

**IDE commit `90aa7646e8b81dbe81c24e904c6c22481adccbbc`** updates the positive GXSM smoke and gate:

- Uses the fixture's actual normal exit code, `0`.
- Removes the timed delay before Cycle 2 action. It waits for the fresh session, source breakpoint, stack, Watch, and GXSM variable-read evidence first.
- Checks that Cycle 2 reports `session_gen=2`, a fresh breakpoint at `src/main.cpp:4`, current stack/locals, `counter == 2`, and a new target read of `counter=2`.
- Stops Cycle 2 through the visible Debug menu and independently waits for `debug_stop=requested`, `TARGET_EXIT_TERMINATED reason=user_requested`, `debugger_teardown=PASS`, and `debug_state=EXITED`.
- Requires Cycle 1 normal exit and Cycle 2 user Stop, two distinct sessions/processes/runtimes, matching runtime cleanup records, watch invalidation in both sessions, and hosted Server reap with exit code `0`.
- Matches the line-4 source breakpoint at reported column `0`, consistent with its line-keyed executable-location semantics.

**Server commit `5fa7f21bf11e9b8e212271932c8517058a361433`** extends the existing native debugger runtime test. An eight-byte image footer declares a 48-byte GXSM payload, exercising the out-of-bounds rejection path. The test asserts registration and target execution remain available, while `InspectVariables` returns `UNSUPPORTED`, zero variables, and the metadata-unavailable diagnostic. The test continues through breakpoint, stepping, and restart checks.

No product debugger code or GXSM format changed.

## Hosted lifecycle evidence

The final positive gate ran one bounded iteration with no retry and exited `0`:

`hosted_target_exit_iteration_001=PASS target_sessions=2 cycle1_exit_code=0 cycle2=stopped ... server_reaped=true`

Cycle 1 used target process `12`, native runtime `2`, and debugger session generation `1`. The compiler-produced ELF was `5,021` bytes, SHA-256 `1E6A90DE539738A7D1C31F59AC5CA748BB073804CBBAEDC4F3C23E1F0ECA4F70`. GXSM v2 was accepted (trailer offset `4273`, size `748`, four source records, two variable records). The debugger reached `debugProbe` at `src/main.cpp:4`, produced a three-frame stack with valid locals, and read `counter=2` from target memory. Step Over stopped at line 5 and read `counter=3`; Step Out reached `gx_main` at line 10, where the callee-local Watch correctly became unavailable. The target then exited normally with code `0`, its runtime cleanup completed with no remaining windows, debugger teardown passed, and inspection/Watch state was invalidated.

Cycle 2 used a fresh target process `13`, runtime `3`, and session generation `2`. It accepted GXSM v2 against a fresh build/symbol generation, rebound the line-4 source breakpoint, reached a current breakpoint stop in `debugProbe`, rebuilt a three-frame stack and locals, and freshly reevaluated the existing Watch as true. A new GXSM read returned `counter=2` from the new target address. The runner then used the Debug menu Stop path. Logs independently recorded the user stop request, `TARGET_EXIT_TERMINATED reason=user_requested`, `debug_state=EXITED`, debugger teardown, Watch invalidation, and runtime cleanup (`state=Exited`, application exit code `0`, zero remaining windows). The two sessions had distinct process/runtime identities, and both runtime cleanup records matched their exits.

The hosted Server process exited with code `0` and was reaped. The final gate completed in about 76 seconds. The retained trace is under `%TEMP%\guidexos-dbg5r1-repro10-20261027\trace`; the run's summary log is `%TEMP%\guidexos-dbg5r1-repro10-20261027\positive-gate.stdout.log`.

### Transient build observation

One earlier exploratory iteration failed while building the second fixture, with the bootstrap generator reporting failure. The same preserved fixture build script completed successfully when run directly, and the final no-retry positive gate completed both cycles. No code change was made to hide or retry that failure. It was not reproduced in the final qualification; retain it as an environmental flake to watch in future hosted runs.

## Regression qualification

| Check | Result |
|---|---|
| IDE CTest | **34/34 passed.** Configured and built in `%TEMP%\guidexos-dbg5r1-ide-build`; no build output was written into the IDE checkout. |
| Server native debugger runtime | **PASS.** Real breakpoint/trap, instruction restore, source-step, three restart cycles, stale-context rejection, and the new malformed-GXSM inspection assertion passed. |
| Six focused Server contracts | **6/6 passed:** breakpoint manager, source-step controller, call-stack controller, Watch, debug policy, and debug output. Binaries were emitted under `%TEMP%\guidexos-dbg5r1-server-tests`. |
| Hosted Phase 15 stepping lifecycle | **PASS.** F11 Step Into reached line 38, F10 Step Over line 39, and Debug-menu Step Out returned to line 48. Every stop published current source, stack, and locals; the temporary return breakpoint was cleaned; Continue and shutdown completed with Server exit code `0`. The metadata-free fixture's Watch reported unavailable metadata rather than reusing a cached value. |
| Hosted positive GXSM exit/relaunch gate | **1/1 passed**, exit code `0`; complete Cycle 1 normal exit and Cycle 2 user Stop evidence above. |
| GXSM malformed/unavailable/stale boundaries | **PASS.** The Server runtime regression rejects an out-of-bounds GXSM payload and returns no variable records. Phase 15 verifies unavailable metadata is reported safely. The GXSM relaunch scenario verifies the new session's current source, stack, Watch, and raw target value; Server runtime and IDE CTest coverage reject stale generation/frame responses. |
| DWARF capacity fixtures | **PASS.** Parsed the 411-DIE/2-CU fixture; bounded storage/query truncation passed; 511/512 DIE capacity cases passed and overflow/malformed cases returned `limit_exceeded`/`malformed_dwarf`. Source-path capacity accepted 159 bytes and rejected 160/161-byte inputs at capacity 160. |
| AMD64/ARM64 package validation | **PASS** for both ELF64 little-endian ET_EXEC packages and exact package contents. Hashes are recorded below. |

The Server runtime compiler emitted the same existing warnings noted in DBG5: possible `strncpy` truncation in `native_app_debugger.cpp:196` and misleading indentation in `allocator.cpp:63`. The new test code emitted no new warning. No production artifacts required rebuilding because production sources were unchanged.

## Artifact identities

The isolated ServerRoot used for hosted qualification was built from the requested starting revisions. Its Server executable was `5,979,988` bytes, SHA-256 `EBA10A0DC85D0AC4F61EAE376BF04757ECEBC85E5315767570D7D9498A574720`. The packaged manifest SHA-256 was `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE`.

| Developer Studio package file | Size | SHA-256 |
|---|---:|---|
| `Apps/DeveloperStudio/bin/amd64/developerstudio.elf` | 1,158,692 bytes | `F38F9A714B2127413F000F8E50C3050E5FF88ED73FCBEE184A4639441E270F06` |
| `Apps/DeveloperStudio/bin/arm64/developerstudio.elf` | 1,323,380 bytes | `7754F3F83D0423D188968F650F381518F80BA427CB7B5C51E8F4BA57A9ACD822` |

The positive fixture project ID was `com.example.debuggerphase29qpositive`. Its compiled ELF hash matched the hosted build record in both target sessions. These package and hosted results are host evidence; they do not establish production guest execution.

## Production guest boundary and Phase30B

Production guest verification remains outstanding: guest execution of this package, guest GXSM variable reads, guest debugger termination/relaunch, and the formal Phase30B five-boot acceptance process. DBG5R1 did not invoke QEMU or request QAC, and hosted runs do not count toward Phase30B. The formal count remains **0/5**.

DBG5 records the external prerequisite as `EXTERNAL_QEMU_COORDINATION_BLOCKER`: a common atomic reservation must be honored by every relevant QEMU launcher, including C176 and Task Manager, for the whole five-boot acceptance process. Resolve that coordination prerequisite independently before production guest qualification. Do not bypass it or modify unrelated launchers as part of this closeout.

## Recommendation

Outcome A is supported by both hosted GXSM cycles, their terminal observations and cleanup, fresh-session inspection and reads, the malformed/unavailable/stale handling checks, and the requested hosted regression matrix. Place Developer Studio Debugging in **`HOSTED_QUALIFIED — PRODUCTION_GUEST_PENDING`**. Pause further hosted feature work until the shared QEMU coordination prerequisite is resolved, then perform the formal production guest qualification. Do not initiate DBG6 automatically.
