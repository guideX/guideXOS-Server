# NativeAOT C188R12R4R5R4R1 ThreadStore Guest Qualification

**Date:** 2026-10-31 (America/Los_Angeles; execution records use UTC)
**Outcome E — `QAC_OR_EXECUTION_BLOCKED`**
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `8d4fab148ffd6df5e94d47e771b543974e57118d`
**Ending implementation HEAD:** `90f013cabc5ddbd03c47dafc9ee78422518695ac`

## Decision

The staged R4 candidate passed artifact verification and the focused hosted preflight. A fresh QAC admission check was online, empty, and matched an empty direct QEMU process census. QAC then granted one exclusive reservation and launched the owned QEMU process.

The runner's first during-run status/list pair disagreed: `status` still reported zero QEMU processes and sessions while `list` showed the newly admitted, verified reservation and its QEMU child. The runner shut down that owned QEMU through QMP. QEMU ran for about 1.1 seconds; the raw serial file is empty. The guest did not provide a NativeAOT startup marker, so this run establishes no `RhInitialize`, module-registration, ThreadStore, PAL stack-bounds, reverse-P/Invoke, managed-entry, or GC result.

The runner also failed while writing its final manifest because its R4 analyzer result has no `stages` property. A separate recovery manifest preserves the available evidence. I fixed both runner defects after the single launch and reran the relevant hosted checks. No second launch was made.

## Baseline and preservation

- Starting HEAD: `8d4fab148ffd6df5e94d47e771b543974e57118d` on `v1.1_DOTNET_SUPPORT`.
- The final implementation HEAD is `90f013cabc5ddbd03c47dafc9ee78422518695ac`, which contains the closeout and the narrowly scoped runner/test corrections. The later metadata-only update to this report does not change implementation code.
- Candidate build HEAD: `5038777b9f6b1b489ceb9de3e26cf6299b46956f`. The candidate-to-starting-HEAD committed diff was the R4 readiness closeout document; the staged binaries were reused without rebuilding.
- `ESP/ramdisk.img` remained at SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` before and after the run.
- Existing modified/untracked build outputs were preserved. No reset, clean, stash, branch switch, history rewrite, push, or foreign-process termination occurred.
- The R4R1 guard is consumed at `out/dotnet/c188r12r4r5r4-threadstore-readiness/guest-launch-attempt.json`; it records `maxGuestBoots=1` and `automaticRetry=false`.

## Candidate verification

Candidate root: `out/dotnet/c188r12r4r5r4-threadstore-readiness/candidate-20261031T174601065Z/`

- Manifest SHA-256: `36657A671D9088E51005D87F3D72033E88C65E234F932AFA32014F5858B9442E` — matched.
- All 34 declared `artifactHashes` roles matched their files. The nine pinned NativeAOT runtime source hashes, candidate-builder hash, runtime-pack-builder hash, and runtime-pack lock hash also matched.
- The first candidate-directory-only hash index did not resolve the ThreadStore header role because that header is a repository source file. A corrected check verified it at its declared source path; the complete 34-role verification had no hash mismatches.
- The candidate is the R4 `ThreadStoreReadiness` profile for NativeAOT 9.0.0 AMD64 Workstation GC, pinned to runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- The startup bridge derives the OS-module identity from `PalGetModuleHandleFromPointer(&InitializeModules)`, validates the linked module bookends and code ranges, registers the code manager with `RhRegisterOSModule`, then calls `InitializeModules`. These candidate startup and module-identity checks pass statically; guest registration remains unobserved.
- The runtime-pack archive validation reports `PASS`: the stock `PalRedhawkCommon.cpp.obj` member has removal count 0, and the PAL replacement object has archive-member count 1. The linked map resolves `PalGetMaximumStackBounds` to the replacement object and contains no stock `PalRedhawkCommon.cpp.obj` reference.
- The NativeAOT image is ELF64 AMD64. The PE import directory is zero-sized, so it has no Windows loader import table. The candidate records the exact native `extern "C" bool RhInitialize(bool isDll)` contract and maps native `true` to adapter status 0 and native `false` to status 1. The runtime interface remains the pinned GC 5.3 / EE 2 contract; no unintended public runtime ABI change was identified in the staged artifacts.
- Startup-stage and ThreadStore diagnostics, module-registration and managed-entry gates, and the reverse-P/Invoke transition instrumentation are present in the manifest and linked candidate. Their guest execution remains unobserved.
- The isolated proof profile excludes the image-backed arena. Production artifacts remain separate, proof-disabled, and retain `persistentCompositeLifecycle=true`.

The runner initially rejected the valid candidate because it expected a short symbolic PAL member name while the manifest records the actual CMake archive path. Before QAC invocation, I narrowed that gate to verify the recorded path, archive-removal/replacement counts, object identity, and hash. This was a runner-only correction; the candidate was not changed.

## Recorded caveats

The earlier R4 aggregate C167/C168 UI wrapper twice produced a blank nested `dotnet.exe` `NativeCommandError`; the direct C167 and C168 qualifications passed. The retained UI regression baseline remains 40 suites / 969 cases. No UI source changed in this phase.

The generic `smoke-native-stack-bounds.ps1` aggregate passed its hosted stack-bound checks, bare-metal compile, adapter probes, and inactive ThreadStore probe, then failed in an unrelated generic kernel build on existing NativeAOT desktop integration references. The separately built and hash-verified R4 diagnostic kernel and proof-disabled production kernel are the artifacts used here. These caveats do not invalidate the staged candidate or this admission check; they also do not turn the failed aggregates into full passes.

The diagnostic managed/runtime build recorded 66 warnings and zero errors; its specialized kernel build recorded 219 warnings and zero errors. Those build logs remain with the candidate.

## Hosted preflight

All eight focused suites passed sequentially. The QAC stream-capture suite was run sequentially because its prior concurrent run had a recorded live-output flake.

| Coverage | Result |
|---|---|
| R4 ThreadStore, frame integrity, PAL stack bounds, registration/entry gates, and synthetic trace parsing | PASS; `framePreserved=2`, `nested=1`, `overflowRejected=1` |
| R3 native `RhInitialize` Boolean and adapter-status contract | PASS |
| R1 initialization trace and failure gates | PASS |
| Hosted GC ownership graph and marker parsing | PASS; 3 callbacks, 17 observations, 2 hosted collections |
| QAC ownership predicates | PASS; 15 cases |
| QAC stream capture and serial preservation | PASS; 2 cases |
| QAC runner predicates | PASS; 24 cases |
| QMP event capture | PASS |
| PowerShell parser for the updated runner | PASS; zero syntax errors |
| `git diff --check` | PASS |

After the runner corrections, the R4 hosted suite and the 24 runner predicates, 15 QAC ownership predicates, and 2 stream-capture cases passed again. The final R4 hosted test also exercised a stale status/active-list rejection and a consistent verified-owner acceptance. Full logs and summaries are under `out/dotnet/c188r12r4r5r4-threadstore-readiness/preflight-*`.

## QAC admission and single launch

A fresh final preflight was captured at `2026-10-31 17:26:42 PDT` (`2026-11-01T00:26:42Z`):

- QAC 2.0.0 was online at coordinator PID `24548`; that PID resolved to `GuideXOS.QAC.Coordinator.exe`.
- QAC status/list reported zero QEMU processes, zero foreign QEMU processes, zero reservations, and zero managed/queued/blocked sessions.
- A direct Windows process census found zero `qemu-system-x86_64.exe` processes.

An earlier preflight record that day found an active Disk Manager reservation and correctly did not launch; the final fresh census above superseded it. The Disk Manager owner and process were left untouched.

The runner then created the R4R1 one-boot guard and made one QAC `run` invocation. QAC granted reservation `8e245f28163547e0832e2dede0d38393`. Its live list marked owner PID `22980` as verified and reported child QEMU PID `27920`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`. QAC recorded QEMU exit code 0 and reservation release.

The during-run snapshots were inconsistent:

- `qac-during.status.txt`: `QEMU processes: 0 (0 foreign)` and `Managed sessions: 0`.
- `qac-during.list.txt`: one running reservation/session with the verified owner and child PID above.

The runner therefore did not accept the census as a complete consistent active-session snapshot. QMP `query-status` returned `running=true`; the runner sent `quit`, and QMP recorded `SHUTDOWN` with reason `host-qmp-quit`. A post-run QAC status/list and direct process census all returned to zero. No foreign QEMU was stopped.

Key ownership records: `qac-guest-001/qac.stderr.log`, `qac-during.status.txt`, `qac-during.list.txt`, `qmp.log`, and `postrun-recovery-20261101T002858953Z/`.

## Guest boundary and evidence

- Raw serial: 0 bytes, SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`. It was not rewritten during recovery.
- Separate derived parser output: `serial-analysis.json`, SHA-256 `D46E19AE02AE8C0AF8EC9DA03C57D7CD869BEAE7C5C33B673B976961205D0C85`; outcome `INCOMPLETE_TRACE`, zero markers, no native Boolean or adapter status.
- QMP log SHA-256: `82F4D8DA014B615243746ECF0BA0DF2486A7C354F0110401726CD62AAEB26509`.
- QAC stderr SHA-256: `C2449CA6E84BBE717D5B08048F875DDC7559A033FF3F04D9DED23BB98ABF4016`.
- Guard SHA-256: `76B1D51E03321515DC670B6B306645B14072DDE9A53A1BB18D506EA5E0DFE37D`.
- The runner's `guest-manifest.json` was not written. After safely ending the owned QEMU, strict mode rejected access to the absent R4 analyzer property `stages`. `recovery-manifest.json` was written separately; the QAC CLI exit code was not persisted, while QAC's QEMU child exit code and release records were preserved.

No initialization, singleton, module registration, current-thread, PAL bounds, transition-frame, `ManagedMain`, allocation, callback, root, or collection marker was observed. The empty trace is incomplete evidence; it does not prove that `RhInitialize` or a ThreadStore operation failed.

## Runner corrections and closeout

After preserving the run, I made two narrow runner corrections and updated its hosted test:

1. During admission, poll the QAC status/list within the existing bounded admission window and set `admitted` only when both snapshots agree and identify the owned verified reservation. This handles the observed snapshot race without retrying QEMU.
2. Read the analyzer's optional `stages` member through `PSObject.Properties`, so a valid R4 analysis without that field does not prevent the recovery manifest from being written.

The updated runner parses cleanly, the focused hosted checks pass, and `git diff --check` is clean. These corrections do not change NativeAOT runtime or PAL code. The raw serial, QMP, QAC, guard, and post-run census evidence remain under `out/dotnet/c188r12r4r5r4-threadstore-readiness/`.

**Outcome E — `QAC_OR_EXECUTION_BLOCKED`.** This phase does not determine whether the R4 corrections carry NativeAOT into managed execution. Its one launch is consumed; do not reuse the guard or launch a second guest in this phase. The smallest justified next step is a separately scoped, freshly admitted one-boot qualification using the corrected runner and a new phase guard. No runtime correction is justified until a complete guest trace identifies a runtime boundary.
