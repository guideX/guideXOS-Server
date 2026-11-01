# NativeAOT C188 R4R2 QAC Re-entry and ThreadStore Qualification

- **Phase:** C188R12R4R5R4R2
- **Outcome:** `QAC_OR_EXECUTION_BLOCKED`
- **Date:** 2026-11-01
- **Branch:** `v1.1_DOTNET_SUPPORT`
- **Starting HEAD:** `11ad33a4f2b4b33b9472bc5ebb3043a3357dd949`
- **Candidate:** `out/dotnet/c188r12r4r5r4-threadstore-readiness/candidate-20261031T174601065Z/`
- **Candidate manifest SHA-256:** `36657A671D9088E51005D87F3D72033E88C65E234F932AFA32014F5858B9442E`

## Result

The R4R1 status/list mismatch is not evidence of a QAC defect by itself. QAC `status` and `list` each build a fresh snapshot on a separate IPC request, so the pair is not an atomic view. The retained mismatch is consistent with the two requests straddling a reservation/process state change, but the old pair cannot establish exactly when that happened. The revised runner rejects that pair and accepts a run only when complete status, list, reservation inspection, and local process identity converge on the same owned QEMU child.

The fresh R2 preflight did not find an empty lane. At `2026-11-01T15:24:01Z`, QAC had one verified, running acceptance reservation and one managed QEMU child; QAC status and list agreed. Windows process inspection tied the child to the reservation owner and showed a Disk Manager `DM37` guest command line. This occupied lane failed the required clear-lane gate, so I did not invoke QAC `run`, create the R2 launch guard, or boot a guest. No foreign process was stopped and the existing owner was left untouched.

No QAC accounting defect is proven, and the NativeAOT runtime and candidate were not changed. The remaining prerequisite is that the existing acceptance owner complete and release its session. Then a new live QAC census must show a clear lane before the qualified runner can make its single R2 attempt.

## Baseline and preservation

- The repository was on `v1.1_DOTNET_SUPPORT` at starting HEAD `11ad33a4f2b4b33b9472bc5ebb3043a3357dd949`.
- `ESP/ramdisk.img` was already modified in the worktree. Its current SHA-256 was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`, matching the required source hash. It was not edited or staged.
- Existing generated `bin`, `obj`, and `c182-host-smoke` directories were present before this phase and were preserved. Hosted test outputs were written under ignored `out/dotnet/...` directories.
- The separate `D:\dev\guideXOS_QAC` checkout was inspected at HEAD `1ea193258690f9e7b39390b101a117166ef8bbdd`; it remained clean. No QAC repository changes were made.

## QAC semantics and prior mismatch

Source inspection covered `GuideXOS.QAC.Cli/Program.cs`, `GuideXOS.QAC.Coordinator/CoordinatorServer.cs`, `GuideXOS.QAC.Core/QemuCensus.cs`, and `docs/QAC2-design.md` in the separate QAC checkout.

- `status` reports the current QEMU census count and foreign count, plus active managed sessions, queued reservations, and blocked reservations.
- `list` reports process rows, nonterminal reservation rows, and managed session rows. A reservation can be active before it owns a running child, so the fields do not all describe the same category.
- Both commands independently read a process census, refresh foreign-process state, and build their response. Their consecutive CLI responses do not carry a shared snapshot identifier and are not transactionally aligned.
- The QAC design documents exclusive admission among QAC clients and says foreign QEMU processes block admission; QAC does not terminate foreign processes.

The retained R4R1 pair (`candidate-20261031T174601065Z/qac-guest-001/qac-during.status.txt` and `qac-during.list.txt`) showed status at zero QEMU/zero managed sessions, while list showed one owner-verified Running reservation and its QEMU child. The child identity and safe QMP shutdown were separately recorded. A matching child does not make the contradictory pair safe to accept. Since status and list are independent fresh requests, a timing race is the source-supported explanation to test; the retained data does not prove the precise inter-request transition or rule out every coordinator issue. There is no evidence of a stale, cached, or improperly scoped status response, and no same-snapshot contradiction from which to prove an accounting defect.

The R2 runner therefore requires a bounded, complete, convergent sample. It does not treat status/list counters as interchangeable. It additionally verifies the exact reservation ID, project, mode, state, queue position, owner PID and account, QAC inspection fields, managed process row, session row, and local QEMU executable, parent, start time, and assigned QMP port. Any incomplete census, warning, mismatch, foreign process, or lost ownership prevents admission or owned-child control.

## Runner corrections and regression coverage

The runner changes are in `scripts/dotnet/run-c188r12r4r5-gc-ownership-qac-guest.ps1`, with shared predicates in `scripts/dotnet/c188r12r4r5r4-qac-admission.psm1`.

- Added an explicit `-ThreadStoreAttempt R2` mode limited to the ThreadStore candidate, with a distinct `guest-launch-attempt-C188R12R4R5R4R2.json` CreateNew guard and `qac-guest-002` evidence directory. The existing R4R1 guard and evidence paths remain separate.
- Candidate roots are canonicalized and checked to remain beneath the phase root, with reparse-point checks. Admission and release require complete status/list fields and a clean process census.
- Every QAC snapshot and inspection is saved to a unique file; existing evidence is never overwritten. Admission polling is bounded and only succeeds on an exact owner-verified active reservation plus independently corroborated local QEMU identity.
- QMP greeting and `query-status` responses are parsed as structured JSON. Shutdown is sent only after rechecking that the QEMU child still matches the reservation, direct parent, executable, start time, and assigned QMP port. Error recovery only cancels a reservation when it is this runner's verified Waiting or Blocked reservation.
- Analyzer access to optional `stages` is safe under strict PowerShell mode. The hosted analyzer test verifies that raw serial evidence is unchanged and distinct from derived analysis output.

`scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` passed **27 cases**, including replay of the exact retained R4R1 mismatch, clear lane, reservation without a child, verified running ownership, convergent snapshots after a transient mismatch, foreign/conflicting processes, malformed or incomplete fields, census warnings, service failure, owner loss, release, candidate containment, and strict QMP responses.

Other hosted checks passed:

| Check | Result |
|---|---|
| `test-c188r12r4r5r4-threadstore-readiness.ps1` | PASS; reverse-P frame preservation `2`, nested calls `1`, overflow rejected `1`; synthetic trace and missing-`stages` cases passed |
| `test-c188r12r4r5r3-init-return-contract.ps1` | PASS |
| `test-c188r12r4r5r1-init-attribution.ps1` | PASS |
| `test-c188r12r4r5-gc-ownership.ps1` | PASS |
| `test-c188r11r4-qac-ownership-predicates.ps1` | PASS; 15 cases |
| `test-c188r11r4-qac-stream-capture.ps1` | PASS; 2 cases |
| `test-c188r11r2-runner-predicates.ps1` | PASS; 24 cases |
| `test-c181-qmp-event-capture.ps1` | PASS |
| PowerShell parser for the changed scripts | PASS; no syntax errors |
| `git diff --check` | PASS |

The focused stack-bounds qualification at `out/dotnet/c188r12r4r5r4-threadstore-readiness/preflight-r2-20261101T152132662Z/stackbounds-focused/` passed host stack-bound tests, bare-metal stack/process compilation, and the ThreadStore adapter probe. The probe covered initial and worker attachment, lookup, duplicate attach rejection, detach ordering, bounds validation, and transition readiness; it explicitly reports `RhInitialize called: no`, `GC initialized: no`, and zero collections. This is hosted evidence, not guest qualification. The generic kernel aggregate was not rerun because its known unrelated NativeAOT integration failure would rebuild generic outputs; the focused relevant checks and the already built specialized candidate were verified instead.

## Candidate integrity

The existing candidate was reused without rebuilding. `candidate-integrity-r2.json` at `out/dotnet/c188r12r4r5r4-threadstore-readiness/preflight-r2-20261101T152132662Z/` records `PASS`:

- Manifest SHA-256 matched the expected `36657A671D9088E51005D87F3D72033E88C65E234F932AFA32014F5858B9442E`.
- All **34/34** artifact hash roles matched, including the NativeAOT PE/ELF, diagnostic kernel and staged ESP files, runtime-pack objects, and ramdisk.
- All **9/9** pinned NativeAOT source hashes and **3/3** provenance hashes matched.
- The candidate flags confirmed ThreadStore, reverse-P/Invoke, stack-bounds, and initialization diagnostics; archive validation; managed entry not forced; production/diagnostic separation; proof disabled in production; and preserved production lifecycle.
- The candidate manifest source ramdisk hash matched the required value.

## Fresh admission evidence

Evidence is under `out/dotnet/c188r12r4r5r4-threadstore-readiness/preflight-r2-20261101T152132662Z/`.

The initial fresh prelaunch capture at `qac-live-prelaunch-20261101T152312353Z/` showed QAC online (version 2.0.0, coordinator PID 24548), one QEMU process, zero foreign QEMU processes, one managed session, and one Running reservation. The follow-up read-only owner inspection at `qac-occupancy-final-20261101T152400514Z/` reconfirmed:

- Reservation `5a1500b6e9ff45e48740dbaefd2af032`, project `guideXOSServer`, mode `acceptance`, state `Running`, queue position 0.
- Verified owner `LEON2_RDT\guideX`, PID `28608`; owner process was `GuideXOS.QAC.Cli.exe`.
- QAC child PID `24888`, `C:\Program Files\qemu\qemu-system-x86_64.exe`, direct parent PID `28608`; QAC inspection and CIM process creation time agreed.
- The child command line referenced the Disk Manager `DM37` continuation 14 responsiveness guest and QMP port `59305`.
- Status and list agreed on one managed QEMU, one reservation, and one managed session; direct CIM census also found exactly one QEMU.

This is a valid occupied QAC lane, not a status/list discrepancy. The R2-specific guard did not exist, `qac-guest-002` did not exist, and the evidence summary records `runLaunchAttemptedByThisPhase=false`. No QAC reservation request, guest boot, QMP command, or serial capture was made in R2. The prior R4R1 guard and raw evidence were preserved.

## Guest, cleanup, and next action

No guest ran in R2, so there is no R2 raw serial, ThreadStore guest result, PAL guest result, managed-entry result, or GC result. The active QAC owner and child remain under their owner's control; there was nothing from this phase to shut down or release. The prior R4R1 raw serial remains empty and incomplete evidence; it is not treated as proof of a runtime failure.

**Outcome E — `QAC_OR_EXECUTION_BLOCKED`.** The admission gate correctly prevented an unsafe second owner from being started while the exclusive QAC lane was occupied. No QAC accounting defect has been proven. The smallest next action is to let reservation `5a1500b6e9ff45e48740dbaefd2af032` complete and release; then take a new live status/list/foreign-process census. If it is clear and consistent, run the already qualified ThreadStore candidate once with `-ThreadStoreAttempt R2`, which creates the fresh R2-only guard immediately before launch. Do not reuse the R4R1 guard.
