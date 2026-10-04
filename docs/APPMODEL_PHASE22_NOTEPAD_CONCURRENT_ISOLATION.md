# App Model Phase 22 — Notepad Concurrent Document-State Isolation

Status: **Outcome B (A2 repair).** Notepad now keeps concurrent document and modal-dialog state isolated through independent Notepad objects, dialog-worker-local UI state, and values returned to each owning Notepad loop. Hosted runtime qualification passed at eight simultaneous documents and 100/100 overlap cycles. Exact editor-buffer bytes are not exposed by the current hosted diagnostics and native-window visual inspection was unavailable, so those two forms of evidence remain bounded limitations.

## Phase gate and repository state

The pasted request expected starting HEAD `6fbc178d6a5a387ff2b182956056cda78fbf479d`, but the authoritative checkout was `main` at `2e75f2678ea19bedc8cde1e635c38cee7bc116e0`, with no `.phase` marker and no upstream divergence. The current-state map named Phase 21 as complete and Phase 22 as next; the Phase 19, 20, and 21 reports were present. The request was current, not stale or duplicated. The starting worktree was clean. No reset, clean, stash, history rewrite, remote change, or credential change was used.

## Pre-change reproduction

The pre-change hosted probe opened two distinct virtual documents at once. `A.txt` loaded as 17 bytes in PID 11/window 1000, and `B.txt` loaded as 41 bytes in PID 12/window 1001. Both processes and windows were real and separately owned. The compositor snapshot then showed both captions as `Untitled - Notepad`, even though the load logs retained their distinct paths. `redrawContent()` sent a clear marker that could coalesce away the queued title update.

Two simultaneous Save As launches produced separate dialog PIDs 15/16 and windows 1004/1005. Closing the first left one window/process; closing the second left zero dialog windows but one live dialog process. The probe recorded `prechangeStaticDialogLeakObserved=true`. The dialog classes also held mutable static fields and shared callback slots, so concurrent workers could overwrite one another's path, window ID, selection, or callback state. Dialog callbacks directly changed the owning Notepad object's path and editor state from the dialog worker thread. OpenDialog did not finish on its own `MT_Close` event. Notepad's modal launch counter waited for a creation acknowledgement that was routed to the dialog PID, so modal input gating could remain stuck.

The save-changes window also declared an outer height of 160 pixels while its buttons extended through client Y=148. Title-bar/padding space put the controls outside the compositor's outer hit-test bounds, dropping button clicks. The baseline and repair probes are retained under `tmp/phase22-prechange-*` and the completed Phase 22 report/logs under `tmp/phase22-notepad-isolation-*`.

## State and ownership audit

| State | Pre-change finding | Qualified owner after repair |
|---|---|---|
| PID / activation path | ProcessTable spawned one PID per launch and supplied an owned `AppActivationContext`; no Notepad instance lookup existed. | Same application-owned launch contract; each local `Notepad` object obtains its thread's activation context. |
| Window ID / title | `s_windowId` was a Notepad object member; titles could remain stale after a redraw clear. | Notepad object; `redrawContent()` reapplies its own title after clearing content. |
| Path / text buffer | `s_filePath` and `s_lines` were already Notepad object members, not shared static fields. | Remain members of each Notepad object. `loadFile` now reports failure so the owning process can close an unowned blank window. |
| Caret, selection, scroll, wrap, key and menu state | Object members. No shared mutable document buffer was found in the Notepad class. | Remain per-object members; no tab/shared-document or global editor state was introduced. |
| Dirty / close state | Dirty flag was per object, but modal close/result bookkeeping did not safely bridge dialog and Notepad workers. | Dirty flag remains per object. Close result and pending-close flags are per-object atomics; Notepad consumes them on its owning loop. |
| Undo / redo | Per-object snapshot vectors. | Remain per object. |
| Open/Save/Save Changes dialog state | Mutable class-wide static window IDs, paths, entries, selection, and callback slots. | Mutable dialog fields are `thread_local` for each ProcessTable worker; `Show` captures callbacks/launch context by value. Dialog completion decrements the requesting Notepad's own shared modal count. |
| Dialog result application | Dialog callback directly mutated the Notepad object from a different worker. | Callback queues an owned path/result value. The Notepad event loop applies it to its own path/buffer and performs saves on that loop. |
| Clipboard | System clipboard may be shared by design; it is not document state. | Unchanged. |

Static/global review found only immutable launch/main entry points and compile-time layout constants in Notepad. The dialog classes' mutable static storage was the cross-instance alias. No AppRegistry, DesktopService, generic process/window selector, IPC framework, or lifecycle policy changed.

## Repair and runtime behavior

The narrow repair is in [notepad.cpp](../notepad.cpp), [notepad.h](../notepad.h), [open_dialog.cpp](../open_dialog.cpp), [open_dialog.h](../open_dialog.h), [save_dialog.cpp](../save_dialog.cpp), [save_dialog.h](../save_dialog.h), [save_changes_dialog.cpp](../save_changes_dialog.cpp), and [save_changes_dialog.h](../save_changes_dialog.h).

Open/Save callbacks now return their selected paths to the correct Notepad loop through a mutex-protected per-instance result queue. Save/Don't Save/Cancel callbacks only set the correct instance's atomic decision; Save and Save As work then execute in that Notepad loop. Modal completion is signaled when each dialog worker exits, rather than inferred from an acknowledgement sent to the child dialog PID. OpenDialog exits on its own close event. SaveChangesDialog closes its own window after a decision and uses 200 outer pixels so all existing buttons fall inside compositor hit bounds. Dirty Cancel recreates the editor window from that same Notepad's still-dirty state.

### Hosted qualification

The production smoke is [smoke-appmodel-phase22-notepad-isolation.ps1](../scripts/smoke-appmodel-phase22-notepad-isolation.ps1). Its completed run reported **43/43 checks** and **100/100 overlap cycles**. It preserved its desktop configuration snapshots and left the run log/report and unique fixtures in `tmp`.

| Scenario | Result |
|---|---|
| Fresh and single-instance baseline; close removes process/window | Pass |
| Two different documents, exact virtual paths, distinct PIDs/windows and loaded byte counts | Pass |
| Edit A, edit B, alternating edits; independent dirty-title markers | Pass |
| Save A/B and reopen while checking each VFS byte length | Pass |
| Same file opened twice; separate process/window and dirty-title ownership; both can save | Pass; first writer's 17-byte value was reopened before the second save, then the later writer's distinct 24-byte value was reopened; existing last-writer-wins behavior remains |
| Empty, longer multi-line, and spaced-name documents | Pass |
| Failed/missing/unsupported activation with healthy editors open | Pass; no stale owner or contamination |
| Three instances and shuffled close | Pass |
| Eight simultaneous unique documents and shuffled close | Pass; maximum exercised in this run |
| Concurrent Save As and Open dialogs; close both | Pass; separate dialog PID/window owners and zero dialog leftovers |
| Two dirty-close prompts; Save, Don't Save, Cancel | Pass; each decision reached its own Notepad, with close/reopen state checked |
| Recent Programs | Pass; one deduplicated Notepad program entry despite multiple document activations |
| Overlap stress | 100/100 cycles returned to baseline; zero observed path mismatches, buffer-contamination markers, orphan Notepads, or orphan windows |
| Final cleanup | Zero live Notepad processes/windows and zero live Open, Save As, or Save Changes dialog processes/windows |

The smoke verifies exact activation paths, process/window owner pairings, titles/dirty markers, callback destinations, save events, and VFS reload byte counts. It does **not** extract a live editor buffer or read back exact VFS byte strings through the current server diagnostics; exact string/hash comparison is therefore not claimed. Same-file saves remain last-writer-wins; no conflict policy was added. Native-window visual capture was unavailable, so no visual acceptance is claimed.

## Historical regressions

All listed commands were run against the standard hosted executable unless marked otherwise.

| Phase / surface | Result |
|---|---|
| Phase 21 Navigator acknowledgement | **66/66**; 100 active-instance actions, 151 consumed/acknowledged targets, 25 close/relaunch cycles, 0 timeouts or stale acknowledgements. The 10-second bound, 32 ticket slots, PID/generation/token validation, active delivery, and launch-on-miss remain intact. |
| Phase 20 File Explorer lifecycle | **246 checks**, 0 failures; 100/100 overlap cycles with zero orphan owner and preserved path ownership. |
| Phase 19 lifecycle conclusion | Still valid: lifecycle behavior remains app-owned; Phase 22's defect was local to Notepad/dialog handoff. No universal lifecycle policy is justified by this evidence. |
| Phase 18 Start/Recent action presentation | App action/menu model **55/55**; Phase 17 action runtime below also passed. |
| Phase 17 generic actions | **13/13**; launch-on-miss and ten real active Navigator actions passed. |
| Phase 16 folder activation | **53/53**. |
| Phase 15 HTTP/HTTPS URI activation | **12/12**, plus **20/20** unknown-protocol activations. |
| Phase 14 legacy BMP/GIF route retirement | **27/27**, plus **106/106** unsupported opens. |
| Phase 13 JPEG | **20/20**, including normal Open, Open With, decoder failures, and `.jpg`/`.jpeg` defaults. |
| Phase 12 Navigator local HTML | **11/11**, run with a 60-second timeout. |
| Phase 11 PNG | **21/21**, including Open, Open With, decoder failures, and Default Apps. |
| Phase 10 Developer Studio | **12/12**; four real `.TXT` Open With launches/closes plus one normal `.CPP` default launch/close. |
| Phase 9 Default Apps | Default-handler model **54/54**; Settings Default Apps model **39/39** and interaction **9/9**. |
| Phase 8 handler persistence | Default-handler persistence/resolution model **54/54**. |
| Phase 7 Open With | **4/4** runtime checks and **20/20** explicit activations; ordinary Notepad default stayed unchanged. |
| Phase 6 document activation | File Explorer **7/7**, Notepad **29/29**, negative routing **3/3**; 20 explicit document activations. |
| Phase 5B readiness / Phase 4D Recent | Both smoke reports returned `result=PASS`; Phase 5B summary explicitly reported `appModelV1StatusReady=true`, `unresolved=0`, `highRisk=0`; Recent Programs persistence semantics remained intact. |
| App Model supporting models | Built-in dispatcher **21/21**; folder model **26/26**; URI model **41/41**; Open With model **17/17**; Navigator action delivery model **20/20**. |
| Settings | Center **89/89**; Default Apps **39/39** plus **9/9** interactions; inventory device **21/21** and storage **24/24**; S6 **42/42**; S7 Accessibility **21/21**, Developer **23/23**, navigation/search **14/14**; Users **11/11**, navigation **8/8**, interaction/layout **13/13**; Network contract **45/45**; Clock/Time smoke passed. |
| Startup | Startup App Model smoke passed; ordinary startup opened no application windows, explicit canonical launches worked, and persisted restarts restored no unexpected app windows. |

## Builds and platform boundary

The standard hosted `build.bat` passed with **177 warnings and 0 errors**, matching the existing baseline. The experimental hosted `build-native-experimental.bat` passed with **174 warnings and 0 errors**. The pasted checklist expected 176 experimental warnings, but the existing Phase 17, 20, and 21 experimental build logs in this checkout also contain 174; this Phase 22 build matches that repository-local baseline. No Phase 22 source warning was observed.

The changed sources are hosted `notepad.cpp` and the hosted dialog implementations. `build-kernel.bat` does not compile those hosted dialog files; bare-metal Notepad is a distinct `NotepadApp` in `kernel/core/kernel_apps.cpp`. Therefore QEMU was not required. No kernel/bare-metal code was changed. Visual evidence remains unavailable as noted above.

## Outcome and maturity decision

**Outcome B — A2 isolation repair.** The reproduced mutable dialog-state alias and cross-thread editor mutation were repaired in the owning app/dialog boundary, and the stress run did not serialize or reject concurrent Notepad instances. The remaining qualification boundaries are exact editor/VFS string visibility, native visual acceptance, un-injected dialog-process spawn failure, and intentionally undefined same-file concurrent-save conflict resolution.

**App Model maturity: MATURE WITH DOCUMENTED LIMITATIONS.** There is no concrete production defect here that warrants a scheduled Phase 23. Future work should be driven by a real consumer or a newly reproduced production defect. The Phase 19 conclusion against universal lifecycle policy remains unchanged.

## Closeout answers

1. **Does each simultaneous Notepad process own independent document, path, editor, dirty, window, and close state?** Yes for the hosted path qualified here: each PID runs a separate Notepad object; its document/editor/window/dirty/close fields are object-owned, dialog results are returned to that owner, and window/PID pairs were unique through eight instances. Exact in-memory text bytes were not directly dumped by the current diagnostics.
2. **Can editing or closing one Notepad instance alter another?** No contamination or cross-close was observed in two-, three-, eight-instance tests and 100 overlap cycles. Close prompts and modal results remained with their originating editor.
3. **What happens when the same document is opened twice, especially if both save?** Two independent buffers/processes/windows can open the same path. Both may save; the later write wins. No file lock or conflict resolution is implemented or added.
4. **Did qualification require changing the Phase 19 conclusion that no universal lifecycle policy is justified?** No. The defect was confined to Notepad's mutable dialog state, callback threading, and modal completion.
5. **Is there a concrete reason for Phase 23?** No concrete new defect or existing-consumer requirement emerged. Treat the App Model as **MATURE WITH DOCUMENTED LIMITATIONS** and let future work follow real consumers or reproduced defects.
