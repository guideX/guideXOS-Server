# NativeAOT C162 — Managed Task Manager Close Application

## Outcome

C162 adds one bounded mutation to Managed Task Manager: close one selected
application lifetime after confirmation. The request carries the C160 identity
(`source` plus a 64-bit lifetime ID) through the native boundary. Native code
resolves that exact identity against live state at mutation time, follows the
application's ordinary close path, and the managed UI rebuilds from a fresh
C160 snapshot.

The user-selected architecture change permits these three managed logical
surfaces to coexist: Notes, Managed Calculator, and Managed Task Manager. The
active snapshot flag continues to mean focused surface; it does not mean the
other applications stopped running. The bounded snapshot now accommodates 16
AppManager entries, one shell surface, and three managed surfaces (20 total).

## Native close audit and lifecycle

`KernelApp::requestClose()` is the canonical window/application close path. It
first calls `onWindowCloseRequested()` and preserves the application if that
hook vetoes. On acceptance it calls `onWindowClose()`, `shutdown()`, unregisters
and deletes the window, marks the instance terminated, and calls
`onWindowClosed()`. The new AppManager operation reuses this path; it does not
add a second destructor path or force-stop a process or thread.

`AppManager::closeApplicationInstance(source, instanceId)` accepts only the
AppManager source and a nonzero lifetime ID. It looks for an exact live
`KernelApp::getInstanceId()` match, requests normal close, requires the object
to reach `Terminated`, removes and deletes that entry, then requests redraw.
The allocator issues monotonically increasing 64-bit lifetime IDs and refuses
to wrap to zero. A missing or already-gone ID returns `NotFound`; it cannot be
reinterpreted as a reused slot. A close veto or incomplete teardown returns
`CloseFailed` and leaves the application in AppManager.

The source is part of the identity as well as the lifetime ID. Native
AppManager surfaces use source 1, shell uses source 2, and managed logical
surfaces use source 3. Shell close is always `Protected`. The managed host
allows the Managed Task Manager caller to close Managed Calculator only;
Managed Task Manager self, Notes, and other managed targets return
`Protected`. This is a trusted host-infrastructure boundary, not an account or
per-application permission system.

Managed Calculator teardown cannot re-enter its own NativeAOT dispatch stack.
For a managed target, the host records one bounded deferred close request with
the target source and lifetime ID, plus the requesting Task Manager's selector
and lifetime ID. After the dispatch returns, native code validates the
requester and resolves the exact target again, then invokes the target's normal
managed close lifecycle. It reports completion back to the same Task Manager;
that app consumes the completion and obtains a fresh snapshot. A pending
request is not an escalation path, and the bounded completion slot is released
when consumed or superseded.

The compositor's window registry can contain holes after closes. Its
bring-to-front operation now finds the last occupied live slot instead of
assuming the count is the final occupied index. This keeps z-order, focus, and
hit testing correct after surface slots are reused.

## ABI and managed wrapper

The starting host ABI was v2, 112 bytes, with the C160 snapshot callback at
offset 104. C162 appends one callback:

| ABI | Table bytes | Snapshot callback | Close callback |
| --- | ---: | ---: | ---: |
| v1 prefix | 104 | — | — |
| v2 prefix | 112 | 104 | — |
| v3 | 120 | 104 | 112 |

The callback accepts only value data: host context, snapshot source, and a
64-bit lifetime ID. No AppManager, window, or surface pointer crosses the ABI.
The capability bit is bit 12. `GuideXosApplicationControl.TryCloseApplication`
validates context, source, nonzero identity, version, table size, and
capability before reading the appended callback. A v2 host returns
`NotSupported` before callback access; C160 snapshot behavior remains
available.

Result values are bounded: `Success` (0), `InvalidArgument` (-2),
`CapabilityUnavailable` (-3), `NotSupported` (-5), `NotFound` (-10),
`Protected` (-11), `CloseFailed` (-12), `StaleIdentity` (-13), `Pending` (-14),
and `NativeFailure` for an unrecognized native result. Current missing-target
lookups report `NotFound`; the stale status is reserved for a host path able to
distinguish a changed lifetime from an absent one.

C160 remains read-only. The only new mutation is closing one exact application
lifetime. The ABI does not add kill, suspend, resume, activation, focus,
priority, or thread enumeration operations.

## Task Manager flow

Managed Task Manager has four controls: Application list, Refresh, Close
Application, and its own Close. Close Application is enabled only for a
selected, running native AppManager application or Managed Calculator. It is
disabled for no selection, stale/terminated rows, shell, self, Notes, and other
managed apps. Native validation remains authoritative.

The user explicitly activates Close Application and receives the C145
confirmation dialog with the captured display name. The dialog owns input
while open. Before opening it, the UI captures the selected source and full
64-bit lifetime ID. Confirm sends that stored identity; it does not consult a
new row selection. Cancel clears the pending target, performs no host call,
keeps the rows and selection, and returns focus to the parent controls.

After a result, the UI requests a fresh C160 snapshot and replaces its rows and
details from that snapshot. Success is reported as closed only when a complete
fresh snapshot no longer contains the captured identity. `NotFound` or
`StaleIdentity` refreshes the UI and reports that the target is no longer
available. A failed or vetoed close leaves the target in the authoritative
snapshot and reports a bounded status. A snapshot failure does not synthesize
row removal.

The modal ABA sequence was covered by native exact-identity replacement tests
and managed selection/snapshot tests, including clearing selection and details
when the old identity disappears and a replacement appears. QEMU boot 3 then
exercised 25 real Calculator close/relaunch cycles and verified unique new
lifetime IDs and a current final Task Manager snapshot. The exact timing race
of replacing a target while the on-screen dialog is open was not injected by
QMP; this limitation is recorded rather than claiming a UI-level race run.

## Verification

The authoritative runner is:

```powershell
.\scripts\dotnet\run-c162-managed-task-manager-close.ps1 `
  -TimeoutSeconds 900 -ReuseBuiltProofKernel
```

The completed run passed all three proof boots and all three ordinary boots.
The proof composite and clean production composite built with zero errors; the
build emitted existing NativeAOT/PDB and unused-code warnings. The Task Manager
focused suite passed 30 cases plus 1,000 wrapper calls. C162 AppManager close
coverage passed 31 cases and 100 mixed close requests; the close wrapper passed
18 cases; the native managed boundary passed 3.

Production boot 1 exercised Managed Calculator Cancel, confirmed close, fresh
snapshot, distinct relaunch, and `7 * 8 = 56`. Boot 2 closed and relaunched
native Calculator and confirmed shell and Task Manager self protection. Boot
3 completed 25 Managed Calculator close/relaunch cycles with distinct
lifetimes, then completed 25 Task Manager close/relaunch cycles and 175 real
Ctrl+R refreshes. Its final Task Manager snapshot was active with 13 records;
the selected identity was native Calculator `source=1`, lifetime `126`
(`0x000000000000007E`), Running and not active, and its detail pane matched.

The C160 regression results were AppManager identity 29/29, native snapshot
34/34 with 1,000-call stress, and managed snapshot 22/22 with 1,000-call
stress. C145 dialogs passed 49/49. C150 lifecycle and return-target suites
passed 8/8 and 10/10. C156 modifier decoding passed 10/10 and shortcut routing
15/15; Control and Shift ended released. C157 Notes passed 47 focused cases
plus production Ctrl+N, edit, and Copy. The production trace also passed
Managed Calculator keyboard/control routing, native Calculator relaunch, and
the Notes/Calculator/Task Manager coexistence path. C128 remains
**unverified**.

The C145 modal suite passed 49/49; it verifies the existing single-modal
architecture, and C162 did not add modal stacking. Notes is retained in the
three-surface coexistence proof and was not used as a destructive close target.

The final boot leaves the newly relaunched Managed Calculator displaying
`56`, while Managed Task Manager remains active with a fresh authoritative
snapshot. The runner validates this C162 state separately from earlier phases,
which expect Calculator to finish at zero.

## Evidence and artifacts

The runner writes proof and ordinary manifests and serial logs under
`out/dotnet/c162-managed-task-manager-close/`.

| Artifact | SHA-256 |
| --- | --- |
| Proof NativeAOT composite ELF | `52E075E8FC06ED28A278541F14DA82F60C99BCBEA33D6CB03B1A0078E41334BD` |
| Clean production NativeAOT composite ELF | `40526E8117EA762E1DBFDBDD0161C0FC85AD3554EF8453053AB52E10931C094A` |
| Proof kernel | `5D27A1FC2D7B80F8E79B92D84E88699090D3235B503123EDCEBC87A3EC7B73F3` |
| Proof ramdisk | `AE3920FF9C73637F7B3A31EE65C769530A1E40F64360C6F60D25814951353E21` |
| Installed production kernel and ESP kernel | `8184F69D53FEDEF175F544CA9AAC58C9BB7C2A02B38A3E78502FD63157220D13` |
| Installed production ramdisk | `92F76C58DC83104A0C81882CB4B44F0274750B0A00E965313408EF94BDB6750F` |

| Proof boot | Serial SHA-256 |
| --- | --- |
| Boot 1 — pointer/arithmetic | `23507B0A113B0604A1444787E4426E0378F57CEEFFD231D0A7ADE5419604C044` |
| Boot 2 — keyboard/focus | `0F7938E27BF425EFFBC9B58662B0E44349790D374E12D1455781C921D4B1FEAE` |
| Boot 3 — error recovery/lifecycle | `7D9ACE02CCA925B6727256024C275063A50D8151DC9D7913C3C31F79C1308B6A` |

| Ordinary boot | Serial SHA-256 |
| --- | --- |
| Boot 1 | `2E11E266AA000C84DEB8CC6F6CF141D3689B4E811AA9250E5063AA4E57F8CB6D` |
| Boot 2 | `B05C4ACDD09C7DBD40E85E22598ED5E04C7A81E7D4DA19281B56BD306D1EEFDD` |
| Boot 3 | `D5B022020CF179F615132F6670FE1C2460B9BA70390EA802AE608F494AAD0267` |

All three ordinary boots kept Managed Task Manager active. Kernel and ESP
kernel hashes matched, and the C162 production ramdisk was installed and
matched its recorded hash. The proof manifest also records the final Task
Manager lifetime (`80`), snapshot count (`13`), selected identity (`1:126`),
active application, modal/popup/drag state, balanced input modifiers, and ABI
v3 layout.

Settings stay at v2. No force-kill authority or process/thread semantics were
introduced. C128 evidence remains unverified.
