# AArch64 RUNSAFE2 R2: QAC Supervision and Bounded EL0 Qualification

- **Repository:** `guideXOSServer_AARCH64`
- **Branch:** `AARCH64_SUPPORT`
- **Starting HEAD:** `b8983fb34395d246f628a51bdd6f9057cab04e11` (two commits ahead of `origin/AARCH64_SUPPORT`)
- **Outcome:** `QAC_SUPERVISION_DEFECT_LOCALIZED`
- **Studio Run:** remains disabled
- **Scope:** QAC-owned launch supervision for the preserved trusted r6 EL0 diagnostic; not process scheduling

## Closeout

The R2 supervisor and deterministic host regressions are implemented. The host
state-machine suite passes 32 assertions, and the existing production-backed
EL0 hosted suite passes all 169 assertions plus its source-contract checks. The
preserved candidate's manifest, kernel, EFI, and ramdisk hashes match their
recorded r6 identities, and each source file listed by its manifest still
matches the captured hash.

At 2026-11-11 16:43:14 UTC, QAC was occupied by reservation
`72ff13635458464baec7a8b523629494` (`guideXOSUEFI-FONTS7R1`), QEMU PID `33000`.
No R2 request was made at that time. Once the exclusive lane became free, the
single authorized rerun received reservation `78b4fbd69662483586e6898f50ab31f2`
and launched the exact QAC child PID `21912` at `2026-11-11T16:48:08.3014738Z`
on QMP port `55496`.

The guest produced the positive SVC result (`observed-exit=2a`), kernel-data FAR
`0x40066170` and GICD FAR `0x08000000` permission faults (both ESR
`0x9200000f`), kernel-root restoration and page cleanup for each child, scheduler
continuation, two `AARCH64_PHASE12_PASS` markers, 1,000 timer ticks and
preemptions, and zero unexpected IRQs or exceptions. The monitor started cleanup about 29.5 seconds
after launch, but `Get-NetTCPConnection` returned a no-match CIM error despite
the exact child command line and a live QMP endpoint. The runner treated that
socket-table query as a hard gate, sent no QMP commands, and reached its 45-second
cleanup deadline with the child still under QAC ownership. The child lifetime
was 154.018 seconds, so bounded lifecycle acceptance was not met.

After that deadline, exact QAC inspect data, child PID/parent/executable/profile,
the configured QMP port, and the QMP v11 greeting were checked. One graceful
QMP `qmp_capabilities` / `quit` sequence was acknowledged at
`2026-11-11T16:50:42.2614467Z`. QAC logged child exit code 0 at
`16:50:42.3192901Z` and reservation release. A subsequent QAC status/list
reported zero managed QEMU processes and zero reservations. The original
session record remains unmodified and retains the failed cleanup observation;
`qmp-fallback.json` records the later graceful stop.

Outcome C applies because the only authorized guest run exposed a concrete
cleanup defect and exceeded its cleanup bound. The QMP path now uses the exact
child command line and a valid QMP greeting when the OS socket table is
unavailable, then checks a per-session QEMU name through QMP `query-name` before
sending `quit`. It still rejects a reported foreign owner. This fix passes
deterministic host regressions, but the one authorized guest run used the
pre-fix version. No second guest was launched, so bounded supervision is not
qualified and Outcome A is not claimed.

## R1 deviation reconstruction

The preserved R1 session record identifies the failure: after QAC granted the
exclusive reservation and launched direct child PID `16576`, its reservation
list briefly reported `Starting`. The monitor treated that state as terminal
and stopped active supervision before it had a verified child handle. The
guest log already contained valid EL0 evidence 23.286 seconds after launch, but
QMP shutdown was not sent until 365.984 seconds after launch. That exceeded the
intended 300-second budget by 65.984 seconds. QAC's own log then recorded child
exit code 0 and reservation release; its final status/list reported no active
session or QEMU process.

The immutable original monitor source was not included in the preserved
session folder, so an original source line cannot be quoted from an archived
revision. The deviation description comes from the R1 session record. The
saved follow-up runner at
`out/aarch64-runsafe2-el0-native-20261110-201806/run-el0-negative-qac.ps1`
already added `Starting` to its transient-state allowlist. That follow-up was
not a bounded-cleanup correction: its lifecycle deadline began only after the
child had been identified, guest PASS ended the observation loop, and cleanup
had no separate enforced release deadline.

## QAC lifecycle contract

The QAC 2.0 source and CLI contract were checked read-only in
`D:\dev\guideXOS_QAC`. The reservation model defines `Waiting` (queued),
`Blocked`, `Granted`, `Starting`, `Running`, `Released`, `Cancelled`, `Expired`,
`Failed`, and `Recovery Required`.

For `qac run`, the relevant sequence is `Waiting` or `Blocked` → `Granted` →
`Starting` when QAC records launch intent → `Running` when QAC registers its
direct child → `Released` after the exact child exits and QAC completes the
reservation. A child exit is proven by the QAC child-exit record and process
identity check; it is not a reservation state. QAC has no `Stopping` reservation
state. The runner records `ChildStopping` as its own supervision phase while
the reservation remains `Running`.

QAC's `cancel` operation applies to pending `Waiting` or `Blocked` requests.
`release` applies to admitted reservations and requires a registered child to
be verified exited. A `Starting` reservation without a registered child cannot
be released as if its launch had failed. `Recovery Required` keeps admission
blocked. QAC's `run` command launches QEMU directly, waits for that child, and
reports its exit and release; the runner does not kill the QAC client or use a
process-tree termination fallback.

## R2 supervisor correction

[`run-aarch64-runsafe2-qac.ps1`](../../scripts/run-aarch64-runsafe2-qac.ps1)
uses the shared state machine in
[`aarch64-runsafe2-qac-supervisor.psm1`](../../scripts/aarch64-runsafe2-qac-supervisor.psm1).

- It recognizes QAC's actual states and polls through `Starting`, delayed child
  registration, and temporary or stale observations without treating them as
  process exit or ownership release.
- It binds the child to the exact QAC runner PID, executable path, QEMU profile,
  unique QEMU instance name, QMP port, fresh firmware-vars file, process
  creation time, and candidate session. Unrelated QEMU processes are not
  adopted or stopped.
- It reads the serial log through a shared read handle while QEMU is writing,
  records guest milestones as they appear, and keeps supervising after a guest
  PASS marker.
- The default 300-second deadline is measured from the managed child's actual
  creation time. The monitor corrects for any observation delay using the
  process creation timestamp, then advances the remaining budget with a
  monotonic `Stopwatch`.
- A guest PASS, a guest failure marker, or the runtime deadline starts bounded
  cleanup. The runner sends QMP `qmp_capabilities` and `quit` only after
  verifying the exact active child PID and its configured QMP command-line
  endpoint. A matching socket-table PID is recorded when available; when the
  OS query is unavailable, a valid QMP greeting and matching per-session
  `query-name` response are required before `quit`. Any socket-table row naming
  another or ambiguous owner blocks the command. Shutdown is retried while the
  child remains active, with a 45-second exit deadline.
- After child exit, QAC release has a separate 20-second deadline. If the QAC
  run client has exited but the exact child is verified gone and the reservation
  remains admitted, the runner may request QAC's documented `release` operation.
  It never releases while the child is active or its exit is ambiguous.
- QAC communication loss, missing identity, `Recovery Required`, or an
  unexpected process identity cannot produce a pass. If graceful cleanup cannot
  be verified, the evidence says that QAC still owns the child or reservation;
  the runner does not kill an unverified process.
- The session record preserves reservation and grant identity, QAC snapshots
  and transitions, child PID and launch time, live guest milestones, the
  runtime deadline, QMP attempts, separate runtime and cleanup durations, QAC
  and child exit codes, release status, final status/list, and partial log-write
  failures. Raw QAC and guest logs are written directly to the session folder.

The `Starting` → `Running` sequence follows QAC's implementation. The
`Running` → `Starting` regression is defensive: current QAC does not permit that
transition for one reservation, but a stale or out-of-order observation must
not drop active child supervision.

## Host regressions and preserved EL0 result

[`test-aarch64-runsafe2-qac-supervisor.ps1`](../../scripts/test-aarch64-runsafe2-qac-supervisor.ps1)
passes **32 deterministic assertions** covering startup and registration,
temporary states, normal child exit, PASS with pending shutdown, runtime and
cleanup deadlines, ambiguous child status, lost reservation ownership, QAC
communication failure, unexpected and unrelated process identities, repeated
QMP requests, queued cancellation, `Recovery Required`, failed log
finalization, missing terminal status, and the rule that a live owned child can
never qualify. Seven assertions also cover QMP endpoint identity and the socket
table fallback.

`scripts/test-aarch64-el0-host.ps1` was rerun and passed **169 assertions** plus
`AARCH64_EL0_SOURCE_CONTRACT_PASS`. It verifies the same production address-space
and exception code against structural host seams; it is not hardware permission
evidence. No kernel or exception source changed in R2, so ABI, Studio Run,
GICv3, DTB, and ARM64 build checks were not repeated.

The prior native EL0 result remains intact: positive SVC returned 42; kernel
data and GIC Distributor reads produced lower-EL level-3 permission faults with
correct attribution; each child restored the kernel root and cleaned its
pages; timer and scheduler continuation passed. Phase 9 NativeElf applications
remain privileged EL1. This work does not claim general application isolation.

## Candidate and guest execution evidence

The preserved r6 candidate remains at
`out/aarch64-runsafe2-phase12-negative-candidate-r6`:

| Artifact | SHA-256 | R2 check |
|---|---|---|
| Candidate manifest | `49a761874a25016f20a58c99f2b29932cd25e79193e5c7835a5478b240eaf1c7` | Match |
| Kernel ELF | `dc6ddd44161e399e8f2342f7bfbc5b4702610465205a35d82b9b03a6601372bb` | Match |
| EFI image | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` | Match |
| Ramdisk | `c06043cc48e8600b017325847827f564591335b5ce600e52bd394556997597a4` | Match |

The manifest is based on source HEAD `65f0a0d5e79b08ba6d74aa49971635bb0321f071`
with five captured source files. Those five file hashes match the current
`AARCH64_SUPPORT` tree at `b8983fb34395d246f628a51bdd6f9057cab04e11`; the
new runner checks those source hashes instead of incorrectly requiring the
current HEAD to equal the manifest's pre-commit base.

R2 guest evidence is in
`out/aarch64-runsafe2-r2-qac-20261111/el0-negative-20261111-084757-043`.
The QAC grant, child identity, diagnostics, and eventual graceful exit/release
are recorded there, including supplemental `r2-reconciliation.json`. The run
does not prove bounded cleanup: the monitor missed
its 45-second shutdown deadline before the fallback QMP stop. The runner is
configured for one attempt and does not requeue after an occupied-lane refusal;
the authorized guest attempt has been used.

## RUNSAFE3 readiness

The host harness contains the localized QMP correction and passes its
regressions. RUNSAFE3 may proceed with design work, but process scheduling and
cancellation qualification must remain separate. R2 adds no per-task TTBR
ownership, preemptive EL0 execution, or Studio Run path. Treat QAC lifecycle
qualification as incomplete until a separately authorized run exercises the
corrected QMP path within its cleanup deadline.

The runner's `try/catch/finally` preserves partial evidence and performs bounded
cleanup for handled PowerShell errors and cancellation paths. An external
forcible termination of the PowerShell host cannot execute that finalizer. The
QAC run client then exits with its owner, while the coordinator retains the
registered child identity in `Recovery Required` until the exact child exit can
be verified. That held reservation remains visible and blocks another
exclusive admission, but QAC 2.0 has no independent guest runtime-deadline
command. This host/process termination case remains an operational risk; the
runner does not kill an unverified QEMU process to hide it.
