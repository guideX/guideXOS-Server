# App Model Phase 21 — Navigator Action Delivery Acknowledgement

Status: **Outcome B.** Existing-instance and launch-on-miss `open-home` calls now succeed only after the selected Navigator consumes the exact request on its UI thread. The hosted stress and model tests pass. A deterministic real-process close-between-lookup-and-consumption injector and native-window visual capture are unavailable; those boundaries are covered by token/PID model tests and ownership logs.

## Phase gate and starting state

The prompt expected Phase 21 after Phase 20 at `0e5db706fe97663e61009b95f9bcd38e30767425`. No authoritative `.phase` marker existed. `APP_MODEL_CURRENT_STATE.md`, the Phase 20 report, and Git history identified Phase 21 as next. The starting branch was `main`, HEAD matched the expected commit, and the checkout was one commit ahead of `origin/main` with no commits behind. The worktree already had a modified `desktop.json`, generated Navigator images, and numerous temporary Phase 11–20 logs and fixtures. Those preexisting files were preserved and excluded from the Phase 21 commit.

## Pre-change behavior

The pre-change hosted reproduction passed the Phase 17 runtime smoke (13/13 checks, including 10 active-instance Home calls). Source tracing established the success boundary:

```text
T0 Navigator PID is running
T1 Navigator is on about:bookmarks
T2 DesktopService validates app/action and registration generation
T3 Navigator::InvokeAppAction finds the running PID and canonical App ID
T4 ProcessTable::send enqueues generation|open-home in that process mailbox
T5 send returns; InvokeAppAction and DesktopService report success
T6 Navigator's UI loop receives MT_AppAction from its process mailbox
T7 Navigator revalidates the generation and calls handleToolbarAction(kWidgetIdHome)
T8 Home state can then be queried
```

Therefore pre-change success proved **enqueue acceptance**, not target observation, handler execution, or an observable resulting URL. The old request had no token or target PID in its payload. If the selected process finished between lookup and delivery but its ProcessTable row remained, `ProcessTable::send` could still enqueue and return success. A replacement process had a separate mailbox, but the caller had no way to know the old target had consumed anything.

## Transport and identity audit

| Property | Phase 21 behavior |
|---|---|
| Request transport | Existing directed `ProcessTable` mailbox. Navigator consumes it through `ipc::Bus::pop("gui.output")`, which checks the current process mailbox before the shared channel. |
| Request queue | FIFO, no overwrite or coalescing. Each Home request has an independent token and result. |
| Request queue bound | `Process` constructs `Mailbox` with the existing 1,024-message normal-lane capacity. `ProcessTable::try_send` uses nonblocking `Mailbox::try_push`; full returns failure immediately. |
| Acknowledgement bound | Navigator-local fixed table of 32 outstanding tickets. A full table rejects before enqueue. |
| Request identity | Canonical app ID and AppRegistry registration generation, plus target ProcessTable PID and a monotonically increasing 64-bit request token. |
| Process lifetime | ProcessTable PIDs advance monotonically during the server runtime; there is no separate process epoch. A token never wraps: after `UINT64_MAX` is issued, new reservations fail closed. The PID/token pair prevents a replacement process or old acknowledgement from completing another request. |
| Synchronization | The ticket table uses one mutex and condition variable. It is in-process and shared by the action caller and Navigator process threads; it is not a reply bus. |
| Timeout | 10,000 ms after successful enqueue. Timeout expires and frees that exact ticket; a later consumer cannot claim it or acknowledge it. |
| UI affinity | Only Navigator's message loop calls the Home handler and publishes the acknowledgement. |

The request payload is `targetPid|registrationGeneration|requestToken|actionId`. The target checks its current process PID, canonical activation App ID, action identity, and current registration generation before claiming the ticket. It then marks the ticket as consuming, invokes `handleToolbarAction(kWidgetIdHome)`, and acknowledges that exact ticket after the handler returns. Acknowledgement does not wait for pixels to be presented or asynchronous/network work to finish.

`DesktopService` releases its AppRegistry lock before invoking the application callback, because the callback waits for the UI thread and Navigator revalidates the registration on that thread. The callback validates again before enqueue, and the consumer performs the final generation check before calling the handler.

## Failure and lifecycle boundaries

- If a target is already gone or its mailbox is full at enqueue time, `try_send` fails immediately and DesktopService returns `dispatch-failure`.
- If the target closes after enqueue but before consumption, no ticket is acknowledged; after 10 seconds the caller gets `dispatch-failure`. Any late message is rejected because its ticket has expired.
- If the selected target closes after lookup but before enqueue, a missing/finished process fails the nonblocking enqueue. If closure races after the final finished check, the message may enter the old mailbox, but it still cannot produce success without consumption before timeout.
- If Home is consumed and the process closes afterward, the completed ticket remains a legitimate success.
- Once an active target was selected, failure never launches a second Navigator. Launch-on-miss is separate: `Launch()` returns the new PID, then the same ticketed request is queued to that PID and must be consumed before success. Failure on this path does not create a Recent Programs entry.
- Recent Programs is written once for a successful launch-on-miss action. Active-instance Home calls do not add entries.

The ticket table is deliberately local to Navigator. The only mailbox API addition is a generic nonblocking bounded enqueue primitive; no message bus, RPC protocol, action arguments, new production actions, or universal lifecycle policy was introduced.

## Verification

- Pre-change Phase 17 hosted runtime smoke: **13/13** passed; source/runtime evidence showed success returned after queue insertion.
- Phase 21 delivery model: **20/20** passed. Covers immediate and delayed consumption, no consumer, stale/expired token, wrong PID/generation/token, cross-caller acknowledgement rejection, rejected generation, target disappearance before and after enqueue, close after consumption, sequential requests, 16 parallel callers, 32-ticket capacity, 1,024-message mailbox capacity, and 100 modeled stale/not-consumed attempts.
- Final production hosted smoke: **66/66** passed. It delivered 100 active-instance Home calls, one initial launch-on-miss, and 25 launch/active/close cycles. All **151** requests had unique tokens; the queued target PID equalled the consuming and acknowledging PID for each request. There were zero timeouts, queue rejections, or late acknowledgements. The evidence trace is `tmp/phase21-runtime-action-trace.log`.
- Existing Phase 17 runtime and AppRegistry model regressions: passed; generic action enumeration/invocation, stale-generation rejection, unknown app/action handling, and Recent Programs behavior remain intact.
- Phase 20 File Explorer lifecycle smoke: passed, retaining eight-instance and overlap-cycle coverage.
- Phase 18 zero-action check and Phases 16–6 activation/runtime and model regressions: passed.
- Phase 5B closeout: passed with ready status, zero unresolved records, and zero high-risk records.
- Phase 4D Recent Programs, Startup App Model, and Settings Center, inventory, network, S6, S7, users, and Default Apps regressions: passed.
- Standard hosted build: passed, **177 warnings, 0 errors**, matching the Phase 20 baseline. No new Phase 21 action-path warnings.
- Experimental hosted build: passed, **176 warnings, 0 errors**, matching the Phase 20 baseline. No new Phase 21 action-path warnings.
- QEMU was not run: the changed code is hosted `Navigator`, `DesktopService`, `ProcessTable`, and the hosted mailbox; kernel sources do not include the hosted process/mailbox headers. The unrelated Phase 20 UEFI `EFI_OUT_OF_RESOURCES` startup-sync failure was not rerun or attributed to this change.
- Native-window capture remains unavailable. Runtime evidence is process/window ownership, current URL, request token, queue target PID, consuming PID, and acknowledging PID.

Regression logs and build logs are retained under `tmp/phase21-*` and were not committed.

## Closeout answers

1. **Before Phase 21, what did success prove?** The request was enqueued to the selected process mailbox; target consumption was not part of the return contract.
2. **What establishes success now?** The intended Navigator UI thread validated the exact target PID and registration generation, claimed the matching token, returned from `handleToolbarAction(kWidgetIdHome)`, and published the acknowledgement for that ticket.
3. **Can a closing or replacement process falsely acknowledge?** No. A target that closes before consumption times out or fails enqueue. A replacement has another PID and cannot claim the old ticket. A close after handler completion does not revoke legitimate success.
4. **Was general IPC or a universal lifecycle policy needed?** No. The action uses the existing directed mailbox plus a 32-slot Navigator-local acknowledgement table and the existing AppRegistry authority.
5. **What concrete App Model work remains?** Notepad admits separate document processes, but Phase 19 found its process-static state insufficiently audited for safe concurrent document activation. A focused overlap test should verify that opening two documents concurrently cannot cross-share editor/window state. This is a concrete app-owned activation boundary; it does not justify a universal instance policy.
