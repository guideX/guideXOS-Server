# NativeAOT R30 Thread Attachment Root Cause

## Result

**Outcome C — `THREAD_ATTACHMENT_CAUSE_PROVEN`; native repair qualification remains incomplete.** R29's first-failure record proves that `ThreadStore::GetCurrentThreadIfAvailable()` returned unavailable at the pre-allocation guard and that no later allocation-context or heap predicate was evaluated. The pinned runtime source shows why that result is expected when the current NativeAOT thread has not been initialized: `RawGetCurrentThread()` addresses `tls_CurrentThread`, while `GetCurrentThreadIfAvailable()` returns it only when `IsInitialized()` is true.

The source and linked-candidate evidence identify the missing operation. The generated `InitializeModules` entry is an `UnmanagedCallersOnly` boundary whose linked prologue calls `RhpReversePInvoke`. In the R29 real-GC profile, guideXOS's `RhpReversePInvoke` implementation continued through a TLS-cell-only bridge and did not invoke NativeAOT's stock reverse-P/Invoke transition. That bridge cannot attach a `Thread`, publish it to `ThreadStore`, or establish the runtime's mode transition. R30 routes the real-GC entry and return through the stock helpers and records the attachment boundary.

R30 source and hosted candidate qualification passed. The first one-shot runner attempt was stopped by an inherited R24 hosted-log gate before nested candidate integrity checks, QAC reservation, or guest launch. The gate rejected R30's explicit scoped R24 marker because it expected an R6-only assertion-count marker. This gate was corrected and a fresh hosted candidate was built, but no second runner invocation was made. **No QAC guest boot occurred; M1–M12 have no native result.** The real Settings factory has not been observed in R30.

This cause maps to the user's case **A — thread never attached** for the generated managed entry. The R29 record alone does not prove the raw TLS pointer value or identify the immediate managed caller above `RhpNewArray`; those limits are retained below. The native repair is source-backed and linked, but awaits a future authorized guest run.

## Repository and provenance

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `afe9989714b8d578dcf5ccaca309a2c7d9ee6989`
- Implementation HEAD before this report-only commit: `5829d635df33ca072ceaaf7a6a45b722fda1bced`
- No push was made.

The R30 implementation and qualification changes were committed in these steps:

| Commit | Change |
| --- | --- |
| `b9cea371` | Route real-GC reverse-P/Invoke entry and return through the stock NativeAOT transition; preserve stage attribution on reentry. |
| `b93464ba` | Add the persistent-profile R30 thread-attachment trace and propagate its profile through the builders. |
| `aa8e473c` | Add pinned runtime-pack ThreadStore include paths and compile flags for the R30 trace. |
| `3228318d` | Scope the legacy R6-only R24 shared-helper call-site test out of the explicit R30 profile. |
| `ee00e1b1` | Decode x64 instruction boundaries in the R25 linked guard analyzer and accept a linked stock reverse-return tail jump. |
| `5829d635` | Let the inherited R24 runner recognize the exact R30-scoped hosted marker while still requiring the snapshot pass. |

The report is committed separately after the implementation HEAD above. The final checkout tip is the report commit shown in the repository history and final handoff.

## R29 first-failure evidence

The preserved source of truth is [`NATIVEAOT_R29_PREALLOCATION_GC_STATE_ROOT_CAUSE.md`](NATIVEAOT_R29_PREALLOCATION_GC_STATE_ROOT_CAUSE.md). Its original schema-3 native record remains unchanged.

The valid 192-byte record says:

| Field | R29 observation |
| --- | --- |
| Original reason / fail-fast code | `0x8` / `7` |
| Guard location | Pre-allocation in the guideXOS `RhpNewArray` wrapper |
| First failed prerequisite | `THREADSTORE_CURRENT_THREAD_UNAVAILABLE` |
| Prerequisite evaluation mask | `1`; only ThreadStore lookup ran |
| ThreadStore lookup result | unavailable (`2`) |
| Allocation context / heap initialized / safe read | not evaluated (`0`) |
| Observed Thread / heap outputs | zero-initialized output fields; not read |
| Stock array allocator | not reached |

The zero outputs do **not** prove that the TLS slot or GC heap pointer was null. The reader returned as soon as the ThreadStore accessor returned unavailable. R29 did not record the raw TLS address, PAL FLS value, OS thread ID, transition frame, or a ThreadStore list scan. It also did not record the immediate caller above `RhpNewArray`, so that exact managed operation remains unidentified.

The R29 stage word must not be used to place the failure before generated initialization. R29's `initializeNativeAotModules` path rewrote the stage to “native module preparation” on a reentrant, already-initialized call. R30 changes that store to run only when modules have not already been initialized. The R29 record still establishes the failed prerequisite and pre-allocation boundary; its stage attribution is not reliable for the generated-entry ordering question.

The R29 diagnostic is first-writer-owned and publication-last. Its probe performs the prerequisite reads in order and does not allocate managed objects, request GC, acquire a blocking lock, mutate thread state, or bypass the fail-fast. The linked AMD64 guard and the Windows x64 classifier harness preserve the pre-stock failure edge. No evidence indicates that the instrumentation caused the null accessor result.

## Pinned ThreadStore and transition contract

The runtime source is pinned to `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

- `threadstore.inl`: `RawGetCurrentThread()` returns the address of `tls_CurrentThread`. `GetCurrentThreadIfAvailable()` returns that pointer only if `IsInitialized()` succeeds; otherwise it returns null. Thus a null accessor result is not the same claim as a null raw TLS address.
- `threadstore.cpp`: `ThreadStore::AttachCurrentThread()` obtains the raw thread, returns if it is already initialized, calls `PalAttachThread`, constructs the runtime `Thread`, sets `TSF_Attached`, and publishes it to the ThreadStore list under its lock.
- `thread.cpp`: the stock reverse-P/Invoke attachment path invokes `AttachCurrentThread()` when the thread is not attached, verifies the unmanaged-to-managed mode boundary, saves the prior transition frame, and sets the transition-frame state used by the managed side.
- The runtime's cooperative-mode predicate is based on `m_pTransitionFrame == nullptr`; it is not a PAL FLS check.

TLS and PAL FLS serve different roles. TLS provides the NativeAOT `Thread` storage addressed by `RawGetCurrentThread()`. PAL FLS is initialized as part of thread attachment and associates PAL state with the OS thread. The R29 record has no runtime values for either. The R30 trace captures both after the stock transition, but the R30 guest did not run, so no TLS/FLS identity or OS-thread identity is claimed as observed.

## Startup ordering and root cause

The relevant native preparation function builds the actual class-library function table, registers the real OS module/code manager, marks module initialization active, and calls the generated five-argument `InitializeModules` entry. The generated entry is `UnmanagedCallersOnly`. The linked R30 candidate shows the generated initializer calling `RhpReversePInvoke` before its first linked `RhpNewArray` call.

The R29 real-GC profile did not select the R12 ThreadStore-readiness branch. Its `RhpReversePInvoke` path therefore used the existing TLS-cell bridge. That bridge prepared guideXOS TLS/frame data but did not call the runtime's attachment routine, publish a NativeAOT Thread, or establish the stock execution-mode transition. Consequently, when generated managed initialization reached the first array-allocation guard, `GetCurrentThreadIfAvailable()` could still return null. This is a missing use of the supported transition, not evidence of a TLS/FLS pointer mismatch or a need to construct a second Thread.

R20 is the accepted reference. Its R12 readiness profile exercised the stock reverse-P/Invoke path and demonstrated native managed entry, managed allocation, a real GC collection, and survival of a live managed object. R29's real-GC profile differed at the reverse-entry selection: it did not include the readiness profile and fell through to the TLS-cell bridge. The divergence is at the generated entry transition, before the ThreadStore state required by a managed allocation.

R30 adds a real-GC branch that calls `guideXosStockRhpReversePInvoke(frame)` at the generated entry boundary and pairs it with `guideXosStockRhpReversePInvokeReturn(frame)`. It leaves the legacy fixed-heap TLS-cell path intact. It does not set Thread fields, force `IsInitialized`, fabricate a Thread, or relax the R29 guard.

The generated initializer's call to `RhpReversePInvoke` is linked before its first array request. In the R30 candidate, the return helper is emitted as a direct x64 tail jump (`E9`) to the stock NativeAOT reverse-return helper. The linked verifier now recognizes that exact transfer; it does not require an `E8` call when the compiler emits a tail-call stub.

### R30 trace and evidence limits

The strict R30 profile retains the production application and persistent composite lifecycle, R21–R29 prerequisites, real GC managed allocation, the actual selector-5 Settings factory, Workstation GC, and the accepted R20 unwind implementation. It excludes R12 readiness, repeated allocation, and R6 initialization attribution.

The trace records the pre-transition raw TLS Thread and state, PAL attach/FLS state, OS thread ID, post-transition accessor Thread, `TSF_Attached`, transition frame, pre-allocation guard result, stock allocator enter/return, wrapper return, and module-initializer boundaries. It does not take a ThreadStore list lock in the trace profile. Its publication event is contract-backed by the stock helper return, a non-null current-thread accessor, and `TSF_Attached`; it is not an independent list enumeration. Native confirmation of these fields is pending because no guest boot occurred.

## Final R30 candidate

The final fresh candidate was built after the R24 gate correction from implementation HEAD `5829d635df33ca072ceaaf7a6a45b722fda1bced`.

| Artifact | Value |
| --- | --- |
| Candidate root | `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r30-thread-attachment\candidate-20261115T211220185Z-33d0d240002c4be298cc6df59c3af9f8` |
| R30 manifest | `candidate-20261115T211220185Z-33d0d240002c4be298cc6df59c3af9f8-r30-candidate-manifest.json` |
| R30 manifest SHA-256 | `4D4AE0791FEAF1CA154A729DC0EF4E0D1A9840AC2067F98D499AE384438A7B38` |
| Result | `PASS_HOSTED_READY_FOR_QAC_ADMISSION` |
| Candidate manifest SHA-256 | `C310C15CB43EE6CB3C61ADBFDE39DB1723A7A261674C2CD722981BB979162F0C` |
| Integrity inventory SHA-256 | `93F633B550EC5F570958F747B18D99546EB98E9429F6A249EE48892296F38A3D` |
| Candidate files | `347` |
| Runtime-pack profile | `guidexos-nativeaot-runtime-pack-amd64-c188r30-thread-attachment-trace-v1` |
| Runtime-pack manifest SHA-256 | `4BE5F2AA05F487D07BEC7A6CBA3A43DDD26E57A9EC980C4E5A68F785E9CBAFA2` |
| Kernel SHA-256 | `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B` |
| Managed ELF SHA-256 | `61542816430D58FD72632FE0CC69C2A4D513D851E68154FF212E3EB8F01777D8` |
| R30 linked qualification SHA-256 | `4C3FB2AFC37194811FB63A909AEE7748E26D212F618AC61F93B9F5E7D31224A6` |
| Pinned runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Settings factory | `GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter` |
| Factory constructor | `ManagedSettingsCenter___ctor_0` |
| R20 unwind source SHA-256 | `9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E`; linked by managed proof |

The final candidate's linked R25 analyzer passed with schema 3, a writable 192-byte record, and these offsets within `RhpNewArray`: reader call `600`, conditional pre-stock branch `846`, stock allocator call `1111`, failure block `1931`, and fail-fast helper call `2001`. The R30 linked test passed the generated initializer → reverse-entry → stock attachment and reverse-return linkage checks. The final R24 hosted log contains the exact R30-scoped marker and a passing 14-assertion snapshot; the corrected gate accepts it.

The previous candidate and all earlier failed-build artifacts remain under the same artifact root. The relevant build progression is:

| Candidate / attempt | Result |
| --- | --- |
| `candidate-20261115T201925473Z-f2ab1056ec8842e68ae8d072feb682af` | Stopped at the R12-readiness profile guard before compilation; preserved. |
| `candidate-20261115T204213114Z-5b4510d4067b4de89921e5a322783b2d` | Runtime-pack compilation stopped on a missing `common.h` include; preserved. |
| `candidate-20261115T204416633Z-182b391b024a4720b78cb6479f0fed3a` | Reached R24 hosted qualification and exposed the R6-only call-site check mismatch; fixed by `3228318d`; preserved. |
| `candidate-20261115T204915565Z-4d743db680964377bebd6f106b15c385` | Exposed a byte-scanning false positive in the R25 linked analyzer; instruction-aware decoding fixed it. Its linked R30 check also exposed the stock-return tail-jump form. Preserved. |
| `candidate-20261115T210128187Z-649b5c32d33a4edfac1556532c36b6d1` | Hosted build passed, but its one-shot runner stopped before guest launch because the R24 runner rejected the R30-scoped hosted marker. Fixed in `5829d635`; evidence preserved. |
| `candidate-20261115T211220185Z-33d0d240002c4be298cc6df59c3af9f8` | Fresh final hosted candidate; linked and hosted qualifications passed. No runner invoked for this candidate. |

## Hosted verification

These focused commands were run after the related changes. Candidate-linked checks use the final candidate above unless noted.

| Command / suite | Result |
| --- | --- |
| `python scripts/dotnet/test-c188r30-thread-attachment.py --candidate-root <final-candidate>` | 21 tests passed, including generated-entry linkage, stock entry/return transfer, scoped R24 gate positive/negative cases, and TLS-only negative case. |
| `python scripts/dotnet/analyze-c188r25-failfast-origin.py --candidate-root <final-candidate>` | `PASS`; R29 schema-3 linked guard and pre-stock edge validated. |
| `python scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py` | 19 tests passed, including instruction-boundary false-positive regression and real terminating-instruction negatives. |
| `python scripts/dotnet/test-c188r28-gc-state-guard.py` | 16 tests passed. |
| `python scripts/dotnet/test-c188r25-failfast-origin.py` | 21 tests passed. |
| `python scripts/dotnet/test-c188r24-initializemodules.py` | 39 assertions passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r24-initializemodules-snapshot.ps1` | 14 assertions passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r21-settings-factory.ps1` | 9 cases passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1` | 29 cases passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r23-settings-module-preparation.ps1` | 12 cases passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1` | 34 assertions passed. |
| `python scripts/dotnet/test-c188r26-first-failure.py` | 13 tests passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1` | 13 assertions passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-threadstore-readiness.ps1` | 7 mode-model + 6 telemetry assertions passed. |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` | 35 cases passed. |
| R20 preserved-candidate replay via `test-c188r20-iterator-advancement.py` | 31 assertions passed; no guest launched. |
| PowerShell parsing for the edited R24 runner | `PARSE PASS`. |
| `git diff --check` | No whitespace errors; Git emitted only LF-to-CRLF working-copy notices. |

R22/R23/R24 preparation, R26/R27, R28/R29, R12 readiness/ownership, R12 first-failure, QAC admission, R20 replay, and the candidate builder's linked and integrity checks were also rerun in the hosted build sequence. The R30 builder produced the final `PASS_HOSTED_READY_FOR_QAC_ADMISSION` manifest.

### Archived R12 candidate-validation failures

The frozen R12 fixture failures remain separate and were not treated as passing regressions or weakened:

- `test-c188r12r4r5r2r3-transition-runner-contract.ps1` rejected its archived default candidate because it lacks the current application-manifest binding.
- Archived transition-attribution and managed-entry integration candidates fail input validation because their application manifest lacks `persistentCompositeLifecycle`.
- The archived managed-settings-object fixture lacks `managedMain.disassembly.txt`.
- An earlier R12 managed-entry invocation omitted required arguments and is not counted as a regression result.

No archived candidate hash validation was bypassed. The R24 adjustment accepts only its standard hosted marker or the exact R30 scope marker, and it always requires the passing snapshot marker. R30 independently verifies the generated initializer and stock transition in the linked image.

## One-shot QAC attempt and native milestones

The initial hosted-ready candidate at source HEAD `ee00e1b18088d4d37917ce47b7c2769fe5033a1c` received one invocation of `run-c188r30-thread-attachment.ps1 -RunOneBoot`. Its read-only QAC report returned `CLEAR`: the CLI status/list calls succeeded, coordinator was responsive, the complete host census found no QEMU process, and reservations/sessions were consistent. The runner then stopped in the inherited R24 hosted-log gate before its nested R25 integrity check and before QAC reservation.

Preserved evidence is in:

- `r30-native-evidence-3a5d036be0e34592bad226324b1fd323.json` — outcome `THREAD_ATTACHMENT_EVIDENCE_BLOCKED`, `guestBootCount=0`.
- `r30-native-evidence-3a5d036be0e34592bad226324b1fd323.json` — SHA-256 `D4DC4C80FE697984B6736389F5AACC42E31486AF3FBBB6C758F2B0BE03B95587`.
- `qac-preflight-r30-3a5d036be0e34592bad226324b1fd323/qac-preflight.json` — `CLEAR`, SHA-256 `23711A0EFC851F85E9929DB845E9C1490A8502477BF303C53E9702938C71E62B`, with `reservationRequested=false`, `attemptGuardCreated=false`, and `qemuLaunched=false`.
- `r25-run-evidence-8f88750ee2bc4a9198f4c932187d81c8.json` — SHA-256 `66F1DB46DFB1BD92D81A172F2143F435F006529297332984C730DE284CC4FE00`; outcome `INITIALIZEMODULES_EVIDENCE_BLOCKED`, failure `R24 hosted reconstruction or snapshot tests did not pass`, `nativeExecutionAttempted=false`, `candidateIntegrityVerified=false`, and nested `qacPreflight=not-run`.
- `r30-qac-postrun-3a5d036be0e34592bad226324b1fd323.json` — SHA-256 `40CCFFE800A4083AA7DACBC435DA86A3B81ABC613D2A4F6AB249D5BA3CCDBD49`, with a complete post-run host census and no QEMU process. No reservation existed to release.

The one-shot guard path in the R30 evidence was never created. There is no guest serial log, no native first-failure record for R30, no reservation ID, and no Settings-factory marker. The `qacReleaseVerified=false` field means the nested runner never reserved QAC; it is not evidence of a leaked reservation. After correcting the R24 marker gate, a fresh final candidate was built and its linked/hosted checks passed. No runner was invoked again, consistent with the no-retry instruction. The earlier `CLEAR` result applies only to the earlier candidate and is not a fresh QAC admission for the final candidate.

| Milestone | R30 result |
| --- | --- |
| M1 — NativeAOT runtime initialized | Not observed; no guest boot. |
| M2 — ThreadStore available | Not observed dynamically. |
| M3 — current Thread attached or attachment initiated | Not observed dynamically. |
| M4 — TLS/FLS identity and execution mode verified | Not observed dynamically. |
| M5 — pre-allocation state valid or exact attachment defect captured in R30 | Not observed dynamically; R29's prior schema-3 failure remains the reference. |
| M6 — stock `RhpNewArray` entered | Not observed. |
| M7 — array allocation completed | Not observed. |
| M8 — generated module initialization continued | Not observed. |
| M9 — `InitializeModules` returned | Not observed. |
| M10 — real Settings factory entered | Not observed; factory not entered. |
| M11 — factory construction/return | Not observed. |
| M12 — runtime state and QAC cleanup verified | No guest or reservation; native runtime state not evaluated. |

## Preserved objects and final working-tree state

The following three pre-existing modified object files remain unchanged and were never staged:

| Object | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CABF4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

Before the report commit, `git status --short` contained only those three modified `.obj` paths. No push was made. All failed candidate directories and R29/R20 artifacts remain preserved.

## Recommended next step

Use the final hosted candidate and corrected R24 gate for one fresh strict QAC preflight and, only if the current QAC state grants exclusive ownership, one newly authorized guest run. The immediate target is M5: capture the R30 raw TLS Thread, post-attachment `GetCurrentThreadIfAvailable()` result, `TSF_Attached`, transition frame, PAL FLS value/index, and OS thread ID before the first array allocation. If M5 passes, continue to the stock allocator and generated initializer return, then allow the actual selector-5 Settings factory to run. Do not infer native success from the linked candidate alone.
