# NativeAOT C188 R4R3 ThreadStore Guest Acceptance

- **Phase:** C188R12R4R5R4R3
- **Date:** 2026-11-01 (America/Los_Angeles; execution records use UTC)
- **Outcome:** `RUNTIME_FAILURE_LOCALIZED`
- **Branch:** `v1.1_DOTNET_SUPPORT`
- **Starting HEAD:** `a54a8e6a0ec574b254c39e24b34f1f87ee608c46`
- **Candidate:** `out/dotnet/c188r12r4r5r4-threadstore-readiness/candidate-20261031T174601065Z/`
- **Candidate manifest SHA-256:** `36657A671D9088E51005D87F3D72033E88C65E234F932AFA32014F5858B9442E`

## Decision

The first R3 census at `2026-11-01T16:02:30Z` found active Disk Manager ownership and correctly stopped without a guard or QAC request. After the user asked to try again, a new complete census at `2026-11-01T18:10:40Z` showed a clear lane. The R3 runner then made exactly one guest invocation and consumed its new one-boot guard.

The guest reached NativeAOT startup. `RhInitialize` returned false, with the first explicit failure at `GCHeapInitialize` (`0x80004005`, `E_FAIL`). The candidate kept module registration and `ManagedMain` blocked. QAC shut down and released the owned guest. The runner failed afterward while serializing an optional analyzer field; the raw serial and derived analyses were preserved and a separate recovery manifest records the result.

## Baseline and preservation

- The initial R3 preflight started at `a54a8e6a0ec574b254c39e24b34f1f87ee608c46` on `v1.1_DOTNET_SUPPORT`.
- After the blocked closeout and R3 runner corrections were committed, the one-boot invocation used HEAD `e67a8104892996ef2b3becfcb2126466f5ddf37f`.
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

The consolidated hosted record is `out/dotnet/c188r12r4r5r4-threadstore-readiness/preflight-r3-20261101T155948970Z/hosted-preflight-r3.json`. The ThreadStore suite's first invocation caught an obsolete assertion for the previous preflight predicate; after updating it to check `Test-QacClearWithCensus`, the suite passed. After the guest run exposed the missing `managedMainDispatched` analyzer property, the runner was updated to read the profile-specific Boolean safely. The ThreadStore suite passed again at `out/dotnet/c188r12r4r5r4-threadstore-readiness/hosted-20261101T181258996Z/`, and the GC ownership suite passed again at `out/dotnet/c188r12r4r5-gc-ownership-proof/hosted-20261101T181311277Z/`. The recovery test summary is `out/dotnet/c188r12r4r5r4-threadstore-readiness/postrun-recovery-r3-20261101T181441952Z/runner-fix-hosted-tests.json`. The original assertion failure and corrected results remain recorded.

The R3-only runner path and shared admission predicate were added because the existing runner stopped at R2 and did not include a direct Windows QEMU census in its prelaunch clear-lane gate. R3 uses a distinct `qac-guest-003` directory and `guest-launch-attempt-C188R12R4R5R4R3.json` CreateNew guard. The preflight now records Windows QEMU processes and inspects any listed reservations; admission requires a complete empty census and complete reservation inspections. Post-run release verification also requires a complete empty Windows QEMU census. Hosted assertions cover these gates. The occupied first census did not enter these paths; after the user requested a retry, a fresh complete empty census admitted the single R3 invocation documented below.

## Initial QAC census (blocked before retry)

Complete evidence is under `out/dotnet/c188r12r4r5r4-threadstore-readiness/qac-live-prelaunch-r3-20261101T160229795Z/`:

- QAC 2.0.0 coordinator PID `24548` was online.
- Status reported 1 QEMU process, 0 foreign QEMU processes, 1 managed session, 0 queued reservations, and 0 blocked reservations.
- List reported exactly 1 reservation, 1 managed session, and 1 managed QEMU process. Status/list counts agreed and the list contained no census warnings.
- Reservation `d20a459368a44dbaae4b362a9a0d99b1` was `guideXOSServer` / `acceptance` / `Running`, queue position 0, with verified owner `LEON2_RDT\guideX` PID `10140` and child QEMU PID `15340`.
- QAC inspection matched that reservation, reported `degraded=false`, and identified `C:\Program Files\qemu\qemu-system-x86_64.exe`.
- The complete Windows process census found exactly one QEMU process, PID `15340`, whose parent was the verified owner PID `10140`. Its command line references `guideXOS_DiskManagerImprovements/out/dm37/continuation14` and the DM37 durable fixture.
- The captured status, list, inspection, and local process identity form a consistent occupied-lane snapshot. This is active Disk Manager DM37 ownership, not an empty lane or a QAC status/list race.

Because QAC was occupied, exclusive admission was not requested during this first census. The prior R2 reservation `5a1500b6e9ff45e48740dbaefd2af032` had been replaced by another DM37 reservation; its disappearance did not imply that the lane had become free.

An initial read-only capture at `qac-live-prelaunch-r3-20261101T160216270Z/` saved status/list and then stopped on a relative module-import path error before process census or admission. It issued no QAC request and created no guard. The complete occupied snapshot above is the first R3 admission decision evidence.

## Fresh QAC admission, one-boot guard, and release

- The retry census at `out/dotnet/c188r12r4r5r4-threadstore-readiness/qac-live-prelaunch-r3-20261101T181040127Z/` found QAC online with zero reservations, sessions, queued/blocked entries, QEMU processes, or foreign QEMU processes. The Windows QEMU census was complete and empty; the empty list required no reservation inspection. The runner repeated and recorded the same clear state immediately before admission.
- R3 guard `guest-launch-attempt-C188R12R4R5R4R3.json` was created at `2026-11-01T18:10:51.9956240Z` with `maxGuestBoots=1` and `automaticRetry=false`. No other R3 guard was used.
- QAC granted reservation `a280121633744e5b803b4f41ca5e6173` to owner PID `30388`. Child QEMU PID `29152` matched the inspected executable, parent, start time, and QMP port `58189`.
- During sample 003, QAC status/list disagreed and its list reported an incomplete process census and a degraded reservation. The runner did not accept that sample. During sample 004, status and list converged on the same verified running reservation; inspection reported `degraded=false`, and the local Windows child identity matched. This is the accepted admission sample.
- QMP reported the guest running, then the runner sent `quit`. QMP recorded `SHUTDOWN` with reason `host-qmp-quit`. QAC recorded QEMU exit code 0 and release of reservation `a280121633744e5b803b4f41ca5e6173`.
- The runner's postflight status/list were empty and its complete Windows process census found zero QEMU processes. The postflight clear-lane predicate passed. No foreign process was stopped.

## Guest result and first failure boundary

Raw serial is `out/dotnet/c188r12r4r5r4-threadstore-readiness/candidate-20261031T174601065Z/qac-guest-003/serial.log`: 13,495 bytes, SHA-256 `BAEF3ACE33F44924DA99DEC535EF52401075F125DBF9D05E04138B2812DFD8E9`. It was not rewritten. The R4 ThreadStore analyzer is `serial-analysis.json`; the initialization-stage analysis is `serial-init-trace-analysis.json`. Recovery metadata and hashes are in `postrun-recovery-r3-20261101T181441952Z/recovery-manifest.json`.

The guest evidence establishes these milestones:

- Native adapter and PAL initialization ran. `PalFls` reported success; startup import lookup emitted an observation with result code zero.
- Runtime instance allocation, `ThreadStoreCreate`, and runtime-instance publication reported success. The trace then reported GC heap and handle-manager publication, with singleton bits `0x1F`; the ThreadStore pointer was available. This does not prove current-thread attachment.
- The first explicit failure was stage `GCHeapInitialize` (`0x00000032`), event `fail`, code `0x80004005` (`E_FAIL`). It propagated through `GCInitialize`, `InitDLL`, and `RhInitialize`.
- Raw `RhInitialize` Boolean was false (`0`); the adapter mapped it to status `1`. The kernel emitted `managed_entry_blocked_init_failure` and `managed_main_gate=blocked`.
- Module registration and initialization, ThreadStore attachment, PAL stack-bounds query, reverse-P/Invoke entry/return, `ManagedMain`, GC-owned allocation, callback persistence, and guest collection were not reached. No GC collection occurred.

The initialization analyzer found 54 trace records while `trace_end` declared 55 (`0x37`), so `initTraceComplete=false`; ring overflow and serial-sink drops were both zero. The explicit `GCHeapInitialize` failure record is present, but the complete-stage-trace claim is withheld. The first observed failing boundary remains concrete; its lower-level cause is not yet established.

The R4 analyzer successfully wrote `serial-analysis.json` with outcome `INIT_FAILURE_BLOCKED`, but the runner then failed while building `guest-manifest.json`: under strict mode it accessed `managedMainDispatched`, which is absent from the R4 analyzer schema (`managedEntryReached` is the R4 field). QAC cleanup had already completed. The CLI wrapper exit code was not persisted; QAC's log records child exit code 0 and reservation release. A separate recovery manifest preserves the evidence. The runner's profile-aware missing-field access is now fixed and its focused hosted regressions pass. The consumed guard remains in place and was not retried.

## Outcome and next action

**Outcome D — `RUNTIME_FAILURE_LOCALIZED`.** One safely admitted guest localized the first explicit initialization failure to NativeAOT `GCHeapInitialize` returning `0x80004005`. ThreadStore creation/publication succeeded before that point. Attachment and all later managed/GC proof stages remain unqualified.

The smallest next action is a targeted read-only audit of the pinned GC heap initialization and its VM/configuration prerequisites to explain the `E_FAIL`. Do not rerun this R3 candidate: its one-boot guard is consumed. Any guest confirmation requires a separately scoped phase with a new live QAC census and guard.
