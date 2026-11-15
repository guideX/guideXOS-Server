# NativeAOT R29 Pre-Allocation GC-State Root Cause

## Result

**Outcome D — PREALLOCATION_SUBCONDITION_ATTRIBUTED.** The R29 schema-3 record identifies the first failed pre-allocation condition: **ThreadStore::GetCurrentThreadIfAvailable() returned null**. The R29 reader stopped at that prerequisite, before querying the allocation context or GC heap and before performing state reads. The linked failure edge preserved reason 8 and entered the first-failure capture/fail-fast path. The stock array allocator was not reached.

The exact condition is established, but its underlying cause is not. The record does not show whether ThreadStore absence is expected at this precise early-startup operation, whether module-preparation ordering failed to attach or publish a thread, or whether this guideXOS allocation wrapper is being entered earlier than the stock allocation contract supports. No speculative repair or guard bypass was made.

The original R29 native outcome and schema-3 record remain unchanged. The original wrapper inherited false M3–M5 values from R25; an offline reviewed-milestone artifact corrects those values from the preserved record and QAC snapshots.

## Repository and provenance

- Repository: D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT
- Branch: v1.1_DOTNET_SUPPORT
- Starting HEAD: de33d2d41a661855b02012cde73e91f97601199f
- Final implementation HEAD before this report-only commit: 7efe0c778815cfbc6195e62c77f5fded40027b3b
- Candidate source HEAD: 0e56e683110e0bff1aa0aba6d949cf8d75f4e73e
- No push was made.

The R29 implementation was committed in six commits: 01df700f, 2fc48ba5, 4dd124ae, f056b1e4, 0e56e683, and 7efe0c77. The last commit corrects R29 runner milestone mapping for future runs and supports the offline review below. It was made after the one native boot and did not rebuild or change the booted candidate.

## R28 boundary and linked R29 evidence

R28's caller PC 0x00000001000F3C20 resolved to RhpNewArray in guidexos_nativeaot_platform.obj. Its aggregate reason-8 predicate ran before guideXosStockRhpNewArray, while the schema-2 record had no pre-allocation discriminator values. The stock allocator and the post-allocation R28 guard were not reached. R28 therefore established the boundary but could not attribute the failed operand.

In the R29 candidate, the linked RhpNewArray begins at 0x00000001000F3680. It uses the Windows AMD64 ABI: RCX carries eeType, RDX carries the array length, and the generated frame preserves call-site stack alignment and home space. The observed record captures the helper return PC, not the array arguments; it does not claim values for eeType or length.

The linked-image analyzer resolves the R29 reader call at function offset 600, the pre-allocation conditional failure edge at offset 842, and the cold failure block at offset 1847. The normal-path stock allocator call is at offset 1066; the R29 fail-fast helper call is at offset 1907 in the cold block. Although the cold block follows the stock call in linear address order, the conditional branch leaves the normal path before that call. The analyzer verifies the branch target and fall-through, and a negative fixture rejects a post-allocation-only edge. The boot record's capture return PC 0x00000001000F3DEC resolves to RhpNewArray.

The captured record is a valid, published 192-byte schema-3 record:

| Field | Observed value |
| --- | --- |
| Original reason / fast-fail code | 0x8 / 7 |
| Guard location | PREALLOCATION |
| First failed condition | THREADSTORE_CURRENT_THREAD_UNAVAILABLE |
| GC-state reader status | -1 |
| Prerequisite evaluation mask | 1 — ThreadStore was evaluated |
| ThreadStore result | unavailable (2) |
| Allocation-context result | not evaluated (0) |
| GC-heap initialization result | not evaluated (0) |
| Safe-read result | not evaluated (0) |
| Observed current thread / heap | null / not read (zero-initialized output) |
| Stock allocator reached | No |

The zero heap output is not evidence that the heap pointer was null: the reader returned immediately after the null ThreadStore result. Similarly, no allocation-context, heap-initialization, safe-read, or identity result is inferred.

## Guard contract and diagnostic behavior

The R29 reader is qualification code wrapped around the guideXOS RhpNewArray entry in tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp:34724; it is not the stock NativeAOT array allocator. The reader is in tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_gc_allocation_probe.cpp:104, and the source-defined classifier is in tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_r29_preallocation_guard.h. The wrapper retains the original fail-fast reason and adds attribution. Its prerequisite order is:

1. Call ThreadStore::GetCurrentThreadIfAvailable(); stop if null.
2. Read Thread::GetAllocContext(); stop if null.
3. Check GCHeapUtilities::IsGCHeapInitialized(); stop if false.
4. Obtain GCHeapUtilities::GetGCHeap(); stop if null.
5. Read the allocation-context fields, GC counters, finalizable-object count, collection state, cooperative-mode value, context identity, and allocation-byte counters. An access violation is recorded as a safe-read failure; other exceptions continue to their owner.
6. After a completed read, classify null thread or heap outputs; otherwise classify the state read as valid.

The record proves that only step 1 ran and returned unavailable. It does not prove an identity mismatch or a later predicate failure. The R29 classifier adds no expected-thread or expected-heap comparison. PAL FLS identity, cooperative/preemptive mode, runtime-instance identity, transition-frame state, and module-registration state are not guard predicates. Cooperative mode is read as diagnostic state after prerequisites, not enforced by this guard.

The native wrapper's pre-allocation condition is a guideXOS integration guard, not an unconditional check at the stock allocator's source boundary. The underlying allocator does need usable thread allocation state and a GC heap to perform managed allocation, but this capture alone cannot determine whether the wrapper's exact timing requirement is valid during this module-preparation phase. No runtime ownership, ThreadStore, heap, or thread fields were changed to make the guard pass.

The R29 probe performs no explicit allocation, lock acquisition, GC request, recursive array allocation, mode transition, or ownership mutation. It calls the ThreadStore, allocation-context, heap-initialization, and heap accessors in order. The observed null return occurs before context or heap dereferences. The first-failure record remains static, first-writer-owned, and publication-last; terminal fail-fast remains active. The linked AMD64 code and Windows x64 classifier harness show no ABI mismatch. The record does not capture stack arguments or identify the caller above RhpNewArray, so it cannot name the specific module-preparation sub-operation that requested the array.

## Initialization stage and comparison with R27/R20

R29 record word 7 is 1, which means native module preparation. The runtime writes stage 2 immediately before calling generated InitializeModules, and stage 3 only after that call returns. Therefore this failure occurred before the stage-2 store and before the generated call. The stage-1 marker covers a broad preparation function: class-library function table setup, module discovery, code-manager registration, and nearby startup work. The evidence does not identify which of those operations reached RhpNewArray.

R27 attributed a later aggregate post-allocation GC-state guard. R28 moved observation to the pre-allocation guard but had no discriminator. R29 establishes that the pre-allocation path failed first at ThreadStore availability; the R27 post-allocation hypothesis does not describe this run.

The accepted R20 native qualification used Workstation GC, completed a real collection, retained a live managed object, and returned 1123. The R29 candidate retained the accepted R20 unwind source (9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E), Workstation GC, and the real selector-5 ManagedSettingsCenter factory. R20 proves the later managed path can allocate and collect with that runtime foundation; it does not prove that the earlier R29 module-preparation allocation has the required ThreadStore state. R29 did not observe FLS values, a current Thread, an allocation context, heap identity, or execution mode, so an exact cross-run identity comparison is unavailable.

The earliest established divergence is consequently the startup boundary: R20's accepted run reached managed execution and collection, while the R29 run failed during stage-1 preparation with no current Thread available. This is an ordering observation, not proof of whether the state was legitimately unavailable or defective.

## Candidate and native qualification

The independently built candidate preserved the actual selector-5 Settings factory, NativeAOT 9.0.0, Workstation GC, no C188 image-backed arena, and the pinned NativeAOT runtime source commit 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.

| Artifact | Identity |
| --- | --- |
| Candidate root | E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r29-preallocation-gc-state-guard\candidate-20261115T133148464Z-caaaf082929d4f448aabd1cc8ed9ac18 |
| Candidate source HEAD / files | 0e56e683110e0bff1aa0aba6d949cf8d75f4e73e / 347 |
| Candidate manifest SHA-256 | A14FA2E34B12D18CCC53D815B5D9B263C5C85F0DC52E28168B0EF3525EFA0F19 |
| Candidate integrity inventory SHA-256 | 205620AD5725D910D671BE0C9CEBDAA41915742EFF0CF65448E5010416C5BB18 |
| R29 candidate manifest SHA-256 | FF98D9ABA6CC195B7662043B2C1D1FE349AB39C3FEB5C78C2156E48EA1225AFE |
| Runtime-pack manifest SHA-256 | AEDD0C8DCDDFFA0B38E11029E3A57286D8F9DE39E414A379DAD1DEE84B372DD3 |

Strict QAC preflight returned CLEAR: candidate integrity passed, the coordinator and status/list responses were consistent, the complete host census found no QEMU process, and no reservation or boot guard was created by preflight. One fresh one-boot guard was then consumed. Exactly one QAC-owned guest boot ran, with one runner invocation and no retry. The R25 nested runner exited 1 after the captured genuine fail-fast; the R29 wrapper completed evidence packaging with exit 0 and result PREALLOCATION_SUBCONDITION_ATTRIBUTED.

Reservation 9763fe43b62e4f54b26064ccb42a9652 was released and independently verified. The after-run QAC snapshots show the coordinator online, zero reservations, zero managed sessions, and zero QEMU processes. The Settings factory was not entered. The stock array allocator was not called, and no allocation success is claimed.

Preserved native evidence:

- R29 native outcome JSON SHA-256: C83FD461BE185FC2AFF982FBF706449B830D12E635EFCC5A6AB14ACF748B08CA
- R25 run evidence SHA-256: C2E9D38237C70941050E1CE4A53189F1649CE7C921A2977C7DD4E9683393533F
- Analyzer output SHA-256: 37BCE7AA04A7A4CEE5F3E4A31A88FA5BDB24FB59D6D8A9630C3116EB53068E1D
- Captured serial SHA-256: CF1A873AEE121FADE5862BB8FD18B1679F9E885C3D3902A1E1AED622F2FA35DC

The original R29 outcome file inherited R25's milestone map and therefore showed M3–M5 as false despite the valid R29 record. That file was preserved. Offline review at implementation HEAD 7efe0c778815cfbc6195e62c77f5fded40027b3b used the committed corrected runner mapping and the raw schema-3 record to create r29-native-evidence-reviewed-milestones-56dcd368e968428f82e81a8f40e2b0ff.json (SHA-256 1D1B86A0AB0E1933AB5150BE519CFE2C26D59B0E88F2F3BFB7EEF263D1773F63). No guest was launched during that review.

| Milestone | Reviewed result | Evidence |
| --- | --- | --- |
| M1 — NativeAOT initialization reached | Yes | R25 run evidence |
| M2 — generated InitializeModules entered | No | Stage is 1; stage 2 is written immediately before its call |
| M3 — pre-allocation RhpNewArray guard entered | Yes | Valid schema-3 record says PREALLOCATION |
| M4 — state predicates evaluated or safely marked unavailable | Yes | ThreadStore evaluated; subsequent prerequisites not evaluated |
| M5 — exact failed condition established | Yes | THREADSTORE_CURRENT_THREAD_UNAVAILABLE |
| M6 — stock array allocator entered | No | Pre-allocation failure edge taken |
| M7 — array allocation completed | No | Stock allocator not reached |
| M8 — type-manager/module initialization continued | No | Failure occurred before generated InitializeModules |
| M9 — InitializeModules returned | No | Call boundary not reached |
| M10 — real Settings factory entered | No | Factory marker absent |
| M11 — factory initialized and returned | No | Factory was not entered |
| M12 — runtime/QAC cleanup verified | Yes | Release verified; after-run status/list are empty |

## Hosted verification

The R29, R28, R25, R26, and R27 regression commands were rerun after the implementation commits. The R20 replay used preserved candidate/run/analysis evidence and did not launch a guest.

| Command or suite | Result |
| --- | --- |
| python scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py | 17 passed, including MSVC AMD64 classifier execution, linked call-edge checks, prerequisite order, no identity invention, and post-allocation negative fixture |
| python scripts/dotnet/test-c188r28-gc-state-guard.py | 16 passed |
| python scripts/dotnet/test-c188r25-failfast-origin.py | 21 passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1 | 34 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r26-precreated-evidence-root.ps1 | 18 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1 | 13 assertions passed |
| Command or suite | Result |
| --- | --- |
| python scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py | 17 passed, including MSVC AMD64 classifier execution, linked call-edge checks, prerequisite order, no identity invention, and post-allocation negative fixture |
| python scripts/dotnet/test-c188r28-gc-state-guard.py | 16 passed |
| python scripts/dotnet/test-c188r25-failfast-origin.py | 21 passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1 | 34 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r26-precreated-evidence-root.ps1 | 18 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1 | 13 assertions passed |
| python scripts/dotnet/test-c188r20-iterator-advancement.py with preserved R18/R20 evidence paths and a fresh output directory | 31 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r21-settings-factory.ps1 | 9 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1 | 29 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r23-settings-module-preparation.ps1 | 12 cases passed |
| python scripts/dotnet/test-c188r24-initializemodules.py | 39 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r24-initializemodules-snapshot.ps1 | 14 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r11-runtime-lifecycle.ps1 | 7 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r11-output-bounds.ps1 | 6 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-threadstore-readiness.ps1 | 7 model + 6 telemetry assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1 | 35 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5-gc-ownership.ps1 | Hosted qualification passed; script emitted no assertion count |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4r5-gc-init-probes.ps1 | Hosted qualification passed; script emitted no assertion count |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff.ps1 | Hosted qualification passed; script emitted no assertion count |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4r5r2-semishared.ps1 | 30 assertions passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r1-settings-handoff-predicates.ps1 | 37 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r2r1-settings-dispatch-predicates.ps1 | 12 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r3-lifetime-predicates.ps1 | 13 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4-factory-predicates.ps1 | 9 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r1-link-boundary.ps1 | 9 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r1-init-attribution.ps1 | Hosted qualification passed; script emitted no assertion count |

The following legacy R12 integration attempts were not counted as passes because their frozen candidate inputs do not satisfy the current fixture contract:

- pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r2r3-transition-runner-contract.ps1 rejected the default frozen candidate at the valid-candidate acceptance check; that candidate manifest has no current application-manifest binding.
- The transition-attribution and managed-entry integration fixtures failed input validation because the frozen application manifest lacks the current persistentCompositeLifecycle property.
- The managed-settings-object fixture lacks the required managedMain.disassembly.txt input.

These fixture limitations do not affect the R29 candidate or its native record. No source changes were made to relax historical candidate integrity checks.

## Repository preservation and next phase

The three pre-existing modified object files remain modified and were not staged or included in the implementation commits. Their recorded SHA-256 values are:

| Object | SHA-256 |
| --- | --- |
| c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj | 3507A24E07257422C98CABF4F7430E094A2C83DF93647E6A02621010DD585A2BD |
| c188r12r4r5r4r5r2r2_native_alloc_record_test.obj | 605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3 |
| guidexos_nativeaot_native_alloc_record.obj | 2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183 |

The next phase should instrument the caller immediately above RhpNewArray or add bounded markers around the stage-1 module-preparation sub-operations. It should establish when ThreadStore publication/attachment is expected relative to the observed allocation and compare that point with the accepted R20 path. Preserve the fail-fast and allocation safety checks until that contract is established; do not infer a ThreadStore, heap, or allocation-context repair from this record alone.
