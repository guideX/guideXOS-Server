# NativeAOT C188 R4R3 ThreadStore Guest Acceptance

- **Phase:** C188R12R4R5R4R3
- **Date:** 2026-11-01 (America/Los_Angeles; execution records use UTC)
- **Outcome:** `QAC_OR_EXECUTION_BLOCKED`
- **Branch:** `v1.1_DOTNET_SUPPORT`
- **Starting HEAD:** `a54a8e6a0ec574b254c39e24b34f1f87ee608c46`
- **Candidate:** `out/dotnet/c188r12r4r5r4-threadstore-readiness/candidate-20261031T174601065Z/`
- **Candidate manifest SHA-256:** `36657A671D9088E51005D87F3D72033E88C65E234F932AFA32014F5858B9442E`

## Decision

No guest was requested or booted. The fresh QAC census at `2026-11-01T16:02:30Z` found one verified, running acceptance reservation. QAC status, list, reservation inspection, and the Windows QEMU process census agreed. The QEMU command line identifies Disk Manager DM37 continuation 14. The occupied lane failed the clear-lane admission gate, so this phase did not invoke `qac run` and did not create its one-boot guard.

The existing QAC owner and guest were left untouched. This phase has no NativeAOT guest result and makes no runtime, ThreadStore, PAL, transition, managed-entry, or GC finding.

## Baseline and preservation

- HEAD remained `a54a8e6a0ec574b254c39e24b34f1f87ee608c46` on `v1.1_DOTNET_SUPPORT` during qualification.
- `ESP/ramdisk.img` was already marked modified at the start of the phase. Its SHA-256 before and after the hosted work and census was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`, matching the required original hash. It was not edited or staged.
- Existing untracked `bin`, `obj`, and host-smoke outputs were preserved. Hosted outputs were written under unique `out/dotnet/...` paths.
- No reset, clean, stash, branch switch, push, ramdisk write, or foreign-process termination occurred.
- The candidate's recorded build source HEAD is `5038777b9f6b1b489ceb9de3e26cf6299b46956f`; it was reused without rebuilding.

## Candidate verification

The staged R4 candidate still matches its qualified identity. The manifest hash matched the expected value, and the fresh recheck verified all **34/34 artifact hashes**, **9/9 pinned NativeAOT runtime source hashes**, and **3/3 provenance hashes**. Candidate flags confirmed the ThreadStore readiness profile, initialization diagnostics, reverse-P/Invoke and PAL corrections, no forced `ManagedMain`, isolated proof configuration, and separation from proof-disabled production artifacts.

The recheck is recorded at `out/dotnet/c188r12r4r5r4-threadstore-readiness/preflight-r3-20261101T155948970Z/candidate-integrity-r3-recheck.json`. The first local summary evaluator in the same directory produced a failed summary because its expected-value expression inverted the production `noGcOwnershipProofProfileInProduction` flag. That was an evaluator error, not a candidate hash mismatch; its original `candidate-integrity-r3.json` is preserved alongside the successful recheck.

The stack-bounds-focused executable evidence from R2 remains valid: the pinned candidate sources and PAL replacement hashes were rechecked unchanged, and the R2 record reports passing host stack-bound tests, bare-metal stack/process compilation, and the inactive ThreadStore adapter probe. The R3 ThreadStore suite also passed its source, reverse-transition, and PAL replacement checks. The generic kernel smoke aggregate was not rerun because its previously documented failure is in unrelated NativeAOT desktop integration references.

## Hosted preflight

All focused suites completed successfully after the R3 runner correction:

| Check | Result |
|---|---|
| R4 QAC admission predicates | PASS, 32 cases, including Windows process census and inspection completeness gates |
| ThreadStore readiness and reverse-P/Invoke frame regression | PASS; `framePreserved=2`, `nested=1`, `overflowRejected=1` |
| NativeAOT `RhInitialize` return contract | PASS |
| NativeAOT initialization attribution | PASS |
| GC ownership/proof graph hosted tests | PASS |
| QAC ownership predicates | PASS, 15 cases |
| QAC stream capture and serial preservation | PASS, 2 cases |
| QAC runner predicates | PASS, 24 cases |
| QMP event capture | PASS |
| PowerShell parser and `git diff --check` | PASS |

These are hosted results only. The parser fixtures and module-registration checks do not establish guest initialization or attachment.

The consolidated hosted record is `out/dotnet/c188r12r4r5r4-threadstore-readiness/preflight-r3-20261101T155948970Z/hosted-preflight-r3.json`. The ThreadStore suite's first invocation caught an obsolete assertion for the previous preflight predicate; after updating it to check `Test-QacClearWithCensus`, the suite passed. The corrected run and the initial assertion failure were both preserved.

The R3-only runner path and shared admission predicate were added because the existing runner stopped at R2 and did not include a direct Windows QEMU census in its prelaunch clear-lane gate. R3 uses a distinct `qac-guest-003` directory and `guest-launch-attempt-C188R12R4R5R4R3.json` CreateNew guard. The preflight now records Windows QEMU processes and inspects any listed reservations; admission requires a complete empty census and complete reservation inspections. Post-run release verification also requires a complete empty Windows QEMU census. Hosted assertions cover these gates. None of those R3 execution paths was entered after the occupied live census.

## Live QAC census and admission

Complete evidence is under `out/dotnet/c188r12r4r5r4-threadstore-readiness/qac-live-prelaunch-r3-20261101T160229795Z/`:

- QAC 2.0.0 coordinator PID `24548` was online.
- Status reported 1 QEMU process, 0 foreign QEMU processes, 1 managed session, 0 queued reservations, and 0 blocked reservations.
- List reported exactly 1 reservation, 1 managed session, and 1 managed QEMU process. Status/list counts agreed and the list contained no census warnings.
- Reservation `d20a459368a44dbaae4b362a9a0d99b1` was `guideXOSServer` / `acceptance` / `Running`, queue position 0, with verified owner `LEON2_RDT\guideX` PID `10140` and child QEMU PID `15340`.
- QAC inspection matched that reservation, reported `degraded=false`, and identified `C:\Program Files\qemu\qemu-system-x86_64.exe`.
- The complete Windows process census found exactly one QEMU process, PID `15340`, whose parent was the verified owner PID `10140`. Its command line references `guideXOS_DiskManagerImprovements/out/dm37/continuation14` and the DM37 durable fixture.
- The captured status, list, inspection, and local process identity form a consistent occupied-lane snapshot. This is active Disk Manager DM37 ownership, not an empty lane or a QAC status/list race.

Because QAC was occupied, exclusive admission for R3 was not requested. The prior R2 reservation `5a1500b6e9ff45e48740dbaefd2af032` is no longer the active reservation; its disappearance did not imply that the lane had become free.

An initial read-only capture at `qac-live-prelaunch-r3-20261101T160216270Z/` saved status/list and then stopped on a relative module-import path error before process census or admission. It issued no QAC request and created no guard. The complete fresh census above was taken in a separate directory and is the admission decision evidence.

## Guard, guest execution, and runtime evidence

- R3 guard `out/dotnet/c188r12r4r5r4-threadstore-readiness/guest-launch-attempt-C188R12R4R5R4R3.json` was **not created**.
- Candidate `qac-guest-003/` was **not created**.
- No QAC `run`, reservation request, QMP command, guest boot, or serial capture was made by R3.
- There is no R3 raw serial, derived marker analysis, guest initialization trace, module registration, ThreadStore, PAL bounds, reverse-P/Invoke, managed-entry, allocation, callback, collection, or guest shutdown evidence.
- The existing R4R1 empty serial capture remains incomplete historical evidence and is not reinterpreted as an R3 result.
- R3 owned no QAC reservation or QEMU process, so there was nothing for this phase to release or shut down. The active DM37 reservation was left under its owner's control.

## Outcome and next action

**Outcome E — `QAC_OR_EXECUTION_BLOCKED`.** Safe guest admission could not proceed because the QAC lane was occupied. No first runtime defect is known; guest execution was not reached.

The smallest next action is to wait for reservation `d20a459368a44dbaae4b362a9a0d99b1` to complete and release. Then take a new complete status/list/inspection and Windows QEMU census. If that census is clear and consistent, run this unchanged candidate once with `-ThreadStoreAttempt R3`, allowing the runner to create the fresh R3 guard immediately before invocation. Do not rebuild the candidate or reuse an earlier guard.
