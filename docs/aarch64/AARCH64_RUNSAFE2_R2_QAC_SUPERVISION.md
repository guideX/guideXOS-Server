# AArch64 RUNSAFE2 R2: QAC Supervision and Bounded EL0 Qualification

- **Repository:** `guideXOSServer_AARCH64`
- **Branch:** `AARCH64_SUPPORT`
- **Qualification starting HEAD:** `5d29d173129271e285c68d4344f686d693514a97` (three commits ahead of `origin/AARCH64_SUPPORT`)
- **Latest protocol-correct native outcome:** `QAC_BOUNDED_LIFECYCLE_VERIFIED` (R3 Outcome A)
- **Preserved R2 runner outcome:** `EL0_QAC_SUPERVISION_PARTIAL` (unchanged)
- **Earlier pre-fix outcome:** `QAC_SUPERVISION_DEFECT_LOCALIZED` (preserved unchanged)
- **Studio Run:** remains disabled
- **Scope:** QAC-owned launch supervision for the preserved trusted r6 EL0 diagnostic; not process scheduling

## Native recheck (2026-11-11)

One genuine QAC acceptance run used the corrected socket fallback and the
preserved R6 candidate. QAC was online and clear at preflight. The exact
reservation was `6f3c8aa0f49d442b801d7e484fad313d`, project
`guideXOSServer-AARCH64-RUNSAFE2-EL0-Negative`, mode `acceptance`. QAC CLI PID
`28948` was the exact parent of QEMU child PID `31372` at
`C:\Program Files\qemu\qemu-system-aarch64.exe`, created at
`2026-11-11T19:41:30.6868455Z`. The child used the per-session QEMU name
`el0-negative-20261111-114126-169` and QMP port `52007`.

The primary `Get-NetTCPConnection` lookup returned a no-match CIM error. The
corrected fallback then validated the active QAC child PID and command line,
received a valid QMP v11 greeting, and matched `query-name` to the exact unique
session name. The recorded discovery method is
`ExactChildCommandLineAndQmpInstanceName`; no other QEMU endpoint was adopted.
This run directly exercised the fallback.

After endpoint validation, the runner wrote QMP `quit`, then timed out waiting
for its response at `2026-11-11T19:41:58.4631874Z`. The raw session exception
is from `StreamReader.ReadLine`; the R2 review addendum records that the write
completed and the read failed after exact endpoint and instance-name
validation. The raw session's `qmp_quit_sent` field is false because that
runner only set the field after receiving a response. The preserved record has
no standalone QMP transcript or exact write timestamp. The addendum's recorded
write completion and the failure at the subsequent read establish transmission
through the verified connection, but do not provide a timestamp for it. The
attempt interval is `19:41:56.6347935Z`–`19:41:58.4631874Z`.

QEMU's QMP reference says `quit` requests graceful process termination, but
does not guarantee a response before termination; premature EOF is expected
in some cases ([QEMU QMP `quit` command](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#command-quit)).
The `SHUTDOWN` event can report a reason such as `host-qmp-quit`, but neither
the preserved session nor the QAC logs contain that event or a QAC exit-cause
field ([QEMU QMP `SHUTDOWN` event](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#event-shutdown)).
The separate earlier R2 fallback transcript records a `quit` return for a
different child after its cleanup deadline; it contains no `SHUTDOWN` event and
does not establish the cause of this run's exit.
Therefore, the evidence establishes a transmitted request and a bounded
process exit, not that the request caused the exit. The absent acknowledgement
remains recorded as absent.

QAC subsequently recorded exit code 0 at `2026-11-11T19:42:03.0492010Z` and
released the reservation. Final status/list showed zero reservations and zero
managed QEMU processes. The runner's monotonic measurements were 22.603 seconds
to guest completion/cleanup start, 12.078 seconds for child cleanup against the
45-second limit, and 1.446 seconds for reservation release against the
20-second limit. The exact QAC child start-to-exit interval is 32.362 seconds;
the old record's `managed_child_lifetime_seconds=34.807` included finalizer
time after exit. The runner now calculates that field from the verified QAC
launch and exit timestamps and leaves it unavailable when QAC exit time is not
verified.

Guest evidence passed: SVC returned 42; the kernel-data read at FAR
`0x40066170` and GICD read at FAR `0x08000000` each raised the expected
`0x9200000f` lower-EL permission fault; both children entered `FAULTED`,
restored the kernel root, and released two data and four table pages. Scheduler
continuation and both Phase 12 pass markers were present; the guest recorded
1,001 timer ticks, 1,000 preemptions, and zero unexpected IRQs or exceptions.

The original R2 result remains **Outcome B — `EL0_QAC_SUPERVISION_PARTIAL`** in
the raw session and first review addendum. Applying the R3 contract, the
preserved evidence now meets **Outcome A — `QAC_BOUNDED_LIFECYCLE_VERIFIED`**:
transmission is established by the existing review addendum and the read-timeout
sequence; QAC verified the exact child exit and release within bounds; the
missing acknowledgement is permitted by QEMU's protocol. The R3 decision is
recorded separately and does not rewrite the old outcome. No second guest was
launched. The raw run is preserved under
`out/aarch64-runsafe2-r2-qac-qualification-20261111/el0-negative-20261111-114126-169/`;
the committed [R2 review addendum](AARCH64_RUNSAFE2_R2_NATIVE_RECHECK_20261111.json)
retains its hashes and original analysis. The R3 disposition is in
[AARCH64_RUNSAFE2_R3_QMP_PROTOCOL_REVIEW_20261111.json](AARCH64_RUNSAFE2_R3_QMP_PROTOCOL_REVIEW_20261111.json).

## Earlier pre-fix native failure (preserved unchanged)

The earlier authorized session received reservation
`78b4fbd69662483586e6898f50ab31f2` and launched child PID `21912` at
`2026-11-11T16:48:08.3014738Z` on port `55496`. Its guest diagnostics completed
29.513 seconds after launch. The runner's socket-table query failed and was
treated as a hard gate, so no QMP command was sent before the 45-second cleanup
deadline. The child lifetime reached 154.018 seconds. This was the original
confirmed supervision defect, not an ARM64 kernel failure.

After the deadline, a separate evidence reconciliation verified the exact QAC
child and a graceful QMP `qmp_capabilities` / `quit` sequence, then observed
child exit code 0 and reservation release. The raw session record retains the
missed deadline; `qmp-fallback.json` and `r2-reconciliation.json` preserve the
later stop and ownership evidence. That post-deadline cleanup does not turn the
run into a bounded pass.

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

QAC 2.0 exposes no coordinator command to terminate an active `qac run` child.
The CLI documents Ctrl+C delivery to its inherited console process group and
continues waiting until child termination is observable. This unattended runner
starts QAC with redirected output and no attached interactive QAC console, so
it uses the validated QMP path and preserves QAC ownership if that path misses
its deadline. `cancel` applies to pending requests; `release` is not requested
while the child is active or ambiguous.

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
  another or ambiguous owner blocks the command. A connection failure before
  transmission may be retried only after fresh endpoint verification. Once the
  `quit` write path begins, the command is not repeated; the child still has a
  45-second exit deadline.
- The state machine records request transmission, protocol response, managed
  child exit status, reservation release, and both cleanup deadlines as
  independent evidence. Completion accepts an acknowledgement, EOF after a
  flushed request, or a read timeout after a flushed request, but only after
  QAC verifies that the exact child exited with code 0 and the reservation is
  released within bounds. A timeout while the child remains active, an explicit
  QMP error, an unverified exit, or failed release cannot qualify.
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
- Managed-child lifetime is calculated from the exact QAC child-start and
  child-exit timestamps. It is left unavailable if QAC did not verify an exit
  timestamp, rather than sampling the stopwatch during later finalization.

The `Starting` → `Running` sequence follows QAC's implementation. The
`Running` → `Starting` regression is defensive: current QAC does not permit that
transition for one reservation, but a stale or out-of-order observation must
not drop active child supervision.

## Host regressions and preserved EL0 result

[`test-aarch64-runsafe2-qac-supervisor.ps1`](../../scripts/test-aarch64-runsafe2-qac-supervisor.ps1)
passes **59 deterministic assertions** covering startup and registration,
temporary states, normal child exit, PASS with pending shutdown, runtime and
cleanup deadlines, ambiguous child status, lost reservation ownership, QAC
communication failure, unexpected and unrelated process identities, repeated
QMP requests, queued cancellation, `Recovery Required`, failed log
finalization, missing terminal status, and the rule that a live owned child can
never qualify. R3 adds acknowledged quit, EOF and timeout after transmission,
timeout with a live child, failure before transmission, explicit QMP error,
wrong instance identity, missing QAC exit evidence, release failure, cleanup
overruns, and isolation of another managed QEMU session.

`scripts/test-aarch64-el0-host.ps1` was rerun and passed **169 assertions** plus
`AARCH64_EL0_SOURCE_CONTRACT_PASS`. It verifies the same production address-space
and exception code against structural host seams; it is not hardware permission
evidence. The Studio Run fail-closed controls passed, and PowerShell parsing
passed for the runner, supervisor module, and relevant regression scripts. No
kernel or exception source changed, so the full Studio package was not rebuilt;
the preserved candidate was hash-checked and used unchanged in the native run.

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
`AARCH64_SUPPORT` tree at qualification starting HEAD
`5d29d173129271e285c68d4344f686d693514a97`; the runner checks those source
hashes instead of incorrectly requiring the current HEAD to equal the
manifest's pre-commit base.

R2 guest evidence is in
`out/aarch64-runsafe2-r2-qac-20261111/el0-negative-20261111-084757-043`.
The QAC grant, child identity, diagnostics, and eventual graceful exit/release
are recorded there, including supplemental `r2-reconciliation.json`. That
earlier run does not prove bounded cleanup: the monitor missed its 45-second
shutdown deadline before the fallback QMP stop. The separate native recheck
above proves that fallback discovery can be exercised, but does not verify the
`quit` acknowledgement. The authorized run for this task has been used; another
native attempt requires a distinct justification and fresh QAC grant.

## RUNSAFE3 readiness

The host harness passes 59 deterministic assertions, and the native fallback
path was exercised under a genuine exclusive grant. Host-only RUNSAFE3
implementation work is ready to proceed. Future RUNSAFE3 QAC guest acceptance
remains a separate gate for scheduler isolation, task address-space ownership,
and guest lifecycle evidence; R2 does not prove those properties. Studio Run
remains disabled, and legacy Phase 9 NativeElf applications remain privileged
EL1. A second native run was not launched.

The runner's `try/catch/finally` preserves partial evidence and performs bounded
cleanup for handled PowerShell errors and cancellation paths. An external
forcible termination of the PowerShell host cannot execute that finalizer. The
QAC run client then exits with its owner, while the coordinator retains the
registered child identity in `Recovery Required` until the exact child exit can
be verified. That held reservation remains visible and blocks another
exclusive admission, but QAC 2.0 has no independent guest runtime-deadline
command. This host/process termination case remains an operational risk; the
runner does not kill an unverified QEMU process to hide it.

## R3 protocol acceptance

QEMU documents that every effort is made to return a `quit` response before
termination, but that response is not guaranteed and premature EOF is
expected. Acknowledgement remains an independent evidence field, not a
completion prerequisite. A pass requires a verified QAC grant and exact child
identity, a flushed `quit` transmission, a permitted response result
(acknowledgement, EOF, or read timeout), QAC-recorded child exit code 0, exact
reservation release, final ownership reconciliation, successful evidence
finalization, and in-bound runtime and cleanup timings. Connection failure
before transmission, explicit QMP error, identity mismatch, timeout with a live
child, missing QAC exit record, nonzero exit, or failed release cannot pass.
The runner does not repeat a quit after its write path begins.

For this native run, transmission and endpoint identity are established by the
preserved R2 review addendum and its recorded `ReadLine` timeout after the
write; the raw `qmp_quit_sent` bit remains false and no exact send timestamp or
standalone transcript exists. QMP acknowledgement is absent. QAC recorded child
exit code 0 at `19:42:03.049201Z`, reservation release, 12.078 seconds of child
cleanup, and 1.446 seconds of reservation cleanup against 45- and 20-second
limits. No `SHUTDOWN` event or exit cause was preserved, so shutdown causation
remains unknown. The bounded lifecycle is verified independently of causation.

The review uses these outcome names:

- **A — `QAC_BOUNDED_LIFECYCLE_VERIFIED`:** historical native evidence meets
  the protocol-correct contract and hosted regressions pass.
- **B — `QAC_LIFECYCLE_EVIDENCE_PARTIAL`:** the contract is correct, but
  historical evidence does not establish every required operation.
- **C — `QAC_SUPERVISOR_DEFECT_REMAINS`:** an ownership, transmission,
  termination, or cleanup defect remains.

R3 is **Outcome A**. Host-only RUNSAFE3 implementation work is ready; future
guest acceptance and the external supervisor-termination risk remain separate.
