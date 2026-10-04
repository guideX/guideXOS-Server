# App Model Phase 19 — Application Instance & Activation Lifecycle Authority

Status: **Outcome B — lifecycle behavior is app-owned and currently fragmented; a bounded File Explorer close leak remains.** This phase audited the existing implementation and added documentation only. It did not add a generic instance policy or change application lifecycle behavior.

## Phase gate and scope

The Phase 19 prompt was current. The starting checkout was `main` at `da77fb24a0ca56133b81ff4d55b4cbbd33919cdb`, the expected Phase 18 predecessor. No authoritative `.phase` marker or Phase 19 report existed; the Phase 18 report and [current-state map](APP_MODEL_CURRENT_STATE.md) named Phase 18 as complete. The expected predecessor and current-state checks therefore passed. The unrelated modified `desktop.json`, generated Navigator images, and existing `tmp/` artifacts were preserved.

The audited code spans `process.cpp`, `compositor.cpp`, `desktop_service.cpp`, AppRegistry/activation types, and the built-in app launchers. No source behavior changed. The only tracked edits for Phase 19 are this report and the current-state map update. QEMU is not applicable because no shared process, compositor, or bare-metal code changed. Native-window visual/focus checks were unavailable: the computer-use surface returned no native app windows.

## Authority map

| Concern | Production authority | Phase 19 finding |
| --- | --- | --- |
| Process creation and PID | `ProcessTable::spawn` in `process.cpp` | Allocates a new PID and `Process` row for every spawn. PID identifies a process; `Process::appId` is the canonical app identity. Typed activation is validated and copied into the process record before its worker starts. |
| Process lookup/status | `ProcessTable::list`, `getIdentity`, `getStatus`, `send` in `process.cpp` | Callers enumerate process rows and must test canonical ID and live status themselves. The table does not choose an instance for ordinary/document/URI/folder dispatch. |
| Normal process completion | Worker wrapper in `ProcessTable::spawn` | Captures a bounded tombstone and publishes `running=false`, `finished=true`; the normal-exit row stays in `g_proc`. A caller must filter by status. Tombstones are separately bounded to 64. |
| Explicit termination | `ProcessTable::terminate` | Records a termination tombstone and erases the table row, but does not synchronously stop the detached worker. It is not a valid hosted process-kill test. |
| Message delivery | `ProcessTable::send` | Looks up a PID and queues a mailbox message. It does not atomically check running/closing status and does not acknowledge that the app consumed the message. Queue acceptance is not activation completion. |
| Window ownership and focus | `Compositor::g_windows`, `g_z`, and `g_focus` in `compositor.cpp` | Each `WinInfo` has an `ownerPid`; window ID, PID, and App ID are distinct. A process may own multiple windows and multiple PIDs may carry the same App ID. Creating a window appends it to z-order and focuses it. |
| Close delivery | Compositor `MT_Close` path | Removes the window from `g_windows`/z-order/focus and publishes `MT_Close` to its owner PID. The application event loop decides whether to exit. Closing the final window is not centrally translated into process termination. |
| App registration and capability routing | AppRegistry and `DesktopService` | Registration generation protects selected handlers/actions from stale registry snapshots. This resolves which handler is eligible; it does not resolve which process instance receives an activation. |

There is no second general-purpose process registry and no AppRegistry API that answers “find/reuse the running instance for this activation.” The process table and compositor remain separate authorities.

## Lifecycle matrix

“New process/window” below is observed for the hosted runtime path. “Reject while active” is an app-owned guard, not reuse. A new process being launchable does not, by itself, prove that process-static UI state is safe under simultaneous instances.

| Application | Ordinary launch | Document | URI | Folder | App Action | Policy owner and evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Notepad (`gxos.builtin.notepad`) | Each launch calls `ProcessTable::spawn`; no existing-instance search. | `a.txt`, then `b.txt` while active produced two PIDs and two windows. Each process receives its owned exact path from `CurrentActivationContext()`. Both windows closed and live rows returned to zero in the audit run. | — | — | — | App launcher plus ProcessTable. This demonstrates distinct launches, but process-static state means the audit does not certify general concurrent-instance safety. |
| File Explorer (`gxos.builtin.fileexplorer`) | Each launch spawns; no existing-instance search. | Files are routed to a document handler; directories use the Folder column. | — | Folder A, then B while active produced two PIDs/windows; each receives an owned folder path. After closing both windows, PID 15 remained live while PID 16 exited. | — | App launcher plus shared static Explorer state. Overlapping activation is admitted without an instance policy and exposes a process/window cleanup defect; see Phase 20 boundary. Internal navigation changes the same Explorer's current path and is not external activation. |
| Navigator (`guidexos.navigator`) | New process/window when stopped; a second concurrent ordinary launch is rejected. | HTML/HTM starts a new process/window with the owned path converted to its local URL; rejected while active. | HTTP/HTTPS starts a new process/window with the owned URI; a second URI while active fails closed. | — | `open-home` sends a generation-tagged `MT_AppAction` to a running Navigator; when stopped it launches one. The active Home runtime test retained the same PID and window. | Single-active guard and action receiver are Navigator-owned. Only App Action performs existing-instance delivery. The guard rejects launch; it does not reuse the window for URI/document payloads. |
| ImageViewer (`gxos.builtin.imageviewer`) | Its app-owned launch guard permits one active process/window; another is rejected. | PNG/JPEG create a process/window while stopped. Repeating a PNG activation while active is rejected; no new window is created. | — | — | — | ImageViewer launch mutex/active state. This is single-active fail-closed behavior, not an App Model reuse rule. |
| Settings (`gxos.builtin.settings`) | Two consecutive ordinary launches produced two independent PIDs/windows; closing both returned live rows to zero. | — | — | — | — | Each `SettingsApplication` owns local state and the launcher spawns each time. This is demonstrated multi-process behavior. |
| Developer Studio (`com.guidexos.developerstudio`) | Experimental NativeElf path starts a NativeElf runtime and a hosted ProcessTable wrapper; no canonical-ID reuse lookup was found. | Experimental document activation starts a new NativeElf runtime with the owned document context. Four requested activations plus the ordinary smoke path passed launch/close checks. | NativeElf URI dispatch is not implemented and remains deferred. | — | No declared production action. | NativeElf runtime and host wrapper remain distinct from built-in ProcessTable lifecycle. Ordinary `build.bat` does not enable this experimental path. |

The evidence establishes these current classes without adding policy vocabulary to the manifest:

- **Single active, reject duplicates:** Navigator and ImageViewer. Neither reuses an existing process for typed activation.
- **Spawn on each activation, with concurrency limits unproven or defective:** Notepad and File Explorer. Notepad's two-document runtime sample closed cleanly; File Explorer's simultaneous folder sample did not.
- **Demonstrated independent multi-process launches:** Settings.
- **Separate experimental launch path with no reuse lookup:** Developer Studio / NativeElf.
- **Existing-instance delivery:** Navigator `open-home` only.

No application was found to implement general same-process multi-window selection as an App Model policy. The compositor can represent multiple windows for an owner PID, but that capability is not an app-instance resolver.

## Activation routes and routing safety

`DesktopService` selects canonical handlers through AppRegistry for document, URI, and folder capabilities. Built-in dispatch adapters then call the corresponding app launcher. The adapter registrations for Notepad, ImageViewer, Navigator, and File Explorer are legitimate backend routing by canonical ID; they do not search for an existing process. The legacy `LaunchApp` branches for ordinary built-ins likewise choose the app-owned launcher and are not a generic lifecycle selector.

Ordinary launch from Start, All Programs, and Recent Programs converges on `DesktopService::LaunchApp`. Recent Programs does not implement instance reuse. Successful launches update the canonical app's recent entry (deduplicated/moved to the front); `recordRecent=false` paths suppress that write. Typed handler launches update Recent only on success. Rejected or unsupported typed activation does not mutate Recent. A Navigator action sent to an already-running instance does not add a launch-history entry; launch-on-miss follows the established app-action Recent behavior.

Normal hosted startup registers applications but does not autostart user apps or restore application windows. The startup regression observes zero app windows after normal startup; its explicit multi-app launch path is diagnostic-only. Startup currently introduces no second instance-selection policy.

AppRegistry generation validation protects handler/action selection. Built-in document/URI/folder dispatch validates while the registry snapshot lock is held through the built-in launch decision. NativeElf document dispatch copies the selected registration/decision and releases that lock before launching the runtime; for temporary registrations, a replacement/unregister can race that interval. This narrow gap is unproven in production manifest use and is recorded as a future hardening question, not expanded into a Phase 19 redesign.

Navigator's action request carries the selected registration generation and action ID. Its UI event loop checks the current activation's canonical ID and asks DesktopService to confirm the action is still current before running Home. The runtime regression observed delivery on the active UI thread. It did not exercise minimized/hidden-window restoration or prove that queue acceptance and consumption are atomic.

## Existing-instance lookup and hard-coded lifecycle audit

The only production App-ID lookup that chooses an existing process is `Navigator::InvokeAppAction`: it enumerates `ProcessTable::list()`, checks exact canonical `appId`, obtains status, and queues the action to that PID. If no live process is found it calls Navigator's launcher. The explicit `running` filter is required because normally completed rows remain in ProcessTable.

This is Navigator-specific action delivery, not duplicated generic lifecycle behavior. Navigator also has app-owned active flags that reject ordinary/document/URI relaunches; those checks do not locate/reuse a process. ImageViewer has a similar active guard. No repeated safe `find-by-canonical-ID → deliver typed activation or launch` implementation was found across document, URI, and folder paths. NativeAppProcessTable lookups concern NativeElf runtime/debug ownership, not built-in App Model instance selection.

No speculative fields such as `singleInstance`, `multiWindow`, or `reuseExisting` were added. A generic lifecycle API would either encode policies that are not shared (Navigator action delivery versus typed activation) or silently promise process-static applications are concurrency-safe when current evidence contradicts that promise.

## Closing, failure, and process cleanup behavior

- **Close:** compositor removes the window and queues `MT_Close`; app code owns its exit behavior. A closed window is not proof the corresponding process has exited.
- **Clean exit:** ProcessTable sets the row non-running/finished, so callers that check status will ignore normal completed processes. The row remains in process history until explicit erase/table lifetime ends.
- **Closing race:** Navigator's action path checks running status, then calls `ProcessTable::send` separately. The process can close between those calls. `send` can report queued without an acknowledgement that the UI loop consumed the request; there is no closing-state lease or request/response completion contract.
- **Focus/visibility:** A newly created compositor window becomes focused. No typed activation reuses/focuses a window; Navigator Home executes inside the already-running process. Focus restoration, minimized/hidden behavior, and raising an existing window were not visually testable.
- **Dispatch failure:** app launchers return PID zero on validation/active-guard/spawn failure; DesktopService reports failure and does not claim typed activation success. Existing-instance action delivery reports queue failure rather than silently launching a duplicate if `send` itself fails. The close race after a successful enqueue can still be indistinguishable from consumption.
- **Launch/window failure:** dispatch does not retry a rejected app-owned launch by creating a second route. The audit did not inject compositor window-creation failure, so its app cleanup behavior remains unverified.
- **Abrupt termination:** no reliable hosted process-kill test exists here. `ProcessTable::terminate` erases a record but does not stop the worker and therefore cannot establish crash cleanup.
- **Observed cleanup defect:** external File Explorer folder A/B created PIDs 15/16 and windows 1004/1005. After closing both windows, owner PID 16 exited but PID 15 remained `running=true` with no window. This repeated on the targeted runtime harness. Treat process-row count as historical and compare live rows plus compositor ownership.

## Regression and build evidence

Pure App Model tests passed:

| Suite | Result | Repeated coverage |
| --- | ---: | --- |
| App Actions | 55/55 | 100 current resolution/dispatch cycles, 100 stale/unknown failures, and 50 Start-menu cycles. |
| Folder activation | 26/26 | 100 valid and 100 invalid/stale cycles. |
| URI protocol | 41/41 | 100 resolution/activation cycles, 100 no-handler cases, and 100 persistence reloads. |
| Default handler store | 54/54 | 100 persistence reload and 100 dispatch cycles. |
| Built-in document dispatcher | 21/21 | 100 each PNG, Navigator HTML, and JPEG resolution/dispatch cycles. |

Hosted runtime regressions passed:

| Coverage | Result |
| --- | --- |
| Phase 17 Navigator action | 13/13; launch-on-miss and 10 active Home actions were consumed on the UI thread, preserving PID/window; active actions left Recent unchanged. |
| Phase 16 folder activation | 53 checks, 0 failures. |
| Phase 15 URI activation | 12/12; includes 20 unknown-protocol cases, owned HTTP URI, close, and successful Recent update. |
| Phase 14 unsupported BMP/GIF | 27/27; 106 unsupported opens created no process/window/Recent change. |
| Phase 13 JPEG | 20/20; ordinary Open 3/3, Open With 1/1, decoder failure 7/7, Settings jpg/jpeg 2/2. |
| Phase 12 Navigator local HTML | 11 checks passed. |
| Phase 11 ImageViewer PNG | 21/21; ordinary Open 3, Open With 1, decoder failure 8, and Settings PNG. |
| Phase 10 Developer Studio | 12/12; five real process/window launch-close cycles including the experimental activation path. |
| Phase 7 Open With / Phase 6 document | Open With 4/4; Notepad explicit activation 20/20; File Explorer 7/7; Notepad 29/29; negative routing 3/3. |
| Phase 5B readiness | PASS; `appModelV1StatusReady=true`, `unresolved=0`, `highRisk=0`. |
| Phase 4D Recent Programs | PASS; temporary state restored. |
| Startup App Model | PASS; normal startup had no application windows, canonical registrations remained available, explicit diagnostic launches had canonical owners, and persistence restart restored no app windows. |
| Settings | Center model 89/89; inventory device 21/21 and storage 24/24; Default Apps 39/39; interactions 9/9 plus 100 lifecycle cycles; S6 42/42; S7 accessibility 21/21, developer 23/23, navigation/search 14/14; Users model 11/11, navigation 8/8, interaction/layout 13/13; Network Settings 45/45. |

Standard hosted `build.bat` passed with **177 warnings, 0 errors**, matching the Phase 18 baseline. Experimental hosted `build-native-experimental.bat` passed with **176 warnings, 0 errors**, matching Phase 18. No Phase 19 source was added, so there are no new Phase 19 compiler warnings. The warning count is existing repository baseline and warnings are not lifecycle test failures.

The clock/time Settings smoke passed. Its script performed another standard hosted build first; that build also passed and emitted 177 warnings, 0 errors, matching the Phase 18 baseline.

The focused concurrent lifecycle harness also tested two ordinary Settings launches, two sequential Notepad document activations, two overlapping File Explorer folder activations, repeated ImageViewer PNG activation, Navigator URI activation while active, and the active Navigator Home action. Its decisive observations are recorded in the matrix and cleanup section above. The harness and exact per-run fixtures/logs remain in temporary Phase 19 output under `tmp/`; existing user artifacts were not cleaned.

## Outcome and Phase 20 boundary

**Outcome B.** A general lifecycle abstraction is not justified by repeated generic selection logic: typed activations create app-owned processes; Navigator and ImageViewer reject duplicates; Settings spawns independent processes; and Navigator's single action receiver is a distinct, app-specific message path. But the system does not yet provide safe, coherent behavior for overlapping activations of process-static File Explorer state, and the runtime reproduces a live process with no window after close. The active action queue also lacks an atomic closing-state check/consumption acknowledgement, and visual/focus acceptance is unavailable.

Recommended Phase 20 scope: make File Explorer's overlapping external folder activation and final-window close lifecycle deterministic. First choose an explicit app-owned behavior—serialize/reject a second activation while one is active, or safely deliver the new folder to an existing instance—then ensure every close path releases process/window ownership and add a repeated runtime regression proving no live owner remains after close. Add a generic selector only if a second real application needs the same safe payload-delivery contract. Keep NativeElf lifecycle and arbitrary IPC/session/crash recovery out of that bounded repair.

## Final answers

1. **Who decides launch versus existing instance?** DesktopService/AppRegistry decide the eligible canonical handler and generation; each application's launcher decides whether it can spawn. There is no generic App Model instance selector. Navigator's `open-home` receiver alone searches for a running process and routes to it.
2. **Generic policy, app-owned policy, or unsafe mixture?** Primarily app-owned policy, with fragmented lifecycle enforcement. AppRegistry routing is generic; actual launch/reject/reuse is not. The File Explorer leak is a concrete unsafe overlapping-launch boundary.
3. **Which are single-instance/reuse and multi-instance?** Navigator and ImageViewer are single-active/reject, not reuse, for launch and typed activation. Navigator reuses its active process/window for `open-home`. Settings demonstrated independent simultaneous processes/windows. Notepad spawned two simultaneous processes/windows in the sample, but process-static state prevents claiming fully supported concurrent safety. File Explorer also spawned two, but one remained running after both windows closed; it cannot be classified as safe multi-instance. Developer Studio launches a separate NativeElf runtime and has no reuse lookup; only repeated sequential launch/close was verified.
4. **Do all activation types share lifecycle infrastructure?** No. Document/URI/folder share capability selection, owned context, and generation checks, then delegate to distinct app launchers. App Action has a special Navigator queue. Forcing those into one manifest policy would make the architecture worse without a second proven existing-instance payload receiver.
5. **What belongs in Phase 20?** Resolve File Explorer overlap and final-window cleanup deterministically, with a regression that proves no live process owner remains. Preserve the separate NativeElf path and revisit a generic delivery contract only if another real app needs it.
