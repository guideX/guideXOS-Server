# C188R12R4R5R4R5R2R2R4R2R3R19 — GC Root Enumeration Completion

## Outcome

**Outcome D — `GC_ROOT_SCAN_DEFECT_PROVEN`.** The preserved R18 guest snapshots show `StackFrameIterator::Next()` returned to `Thread::GcScanRootsWorker` without changing the active iterator. The iterator still held the valid `RhCollect` frame, while the same candidate's unwind metadata and captured stack resolve its caller and the next stack pointer. The pinned source's normal `NextInternal` path should publish that unwound state.

This proves a concrete non-advancing iterator operation in the native root scan. It does not prove that the worker repeated the loop indefinitely, that any GC reference callback ran, that root enumeration returned, or that a collection completed. The exact failure inside the code-manager unwind implementation is not localized. No runtime repair was attempted.

## Repository state and preservation

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `b0b274ccf68055ddb3033aaecd30557c83bbcf0e`
- Final HEAD: recorded in the R19 closeout commit and final response.
- No push was performed.
- No runtime or managed probe source was changed. The R11 and R12 acceptance profiles remain independent.

The three pre-existing modified object files were left out of the commit and retain their recorded SHA-256 values:

| Object | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Candidate and R18 ownership evidence

R19 used the preserved R17 candidate and R18 guest capture for offline analysis. It did not create a candidate, reserve QAC, or boot a guest.

Candidate:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z`

| Artifact | SHA-256 / identity |
|---|---|
| Candidate manifest | `25B3C278B35AAD1A0B09060AE079C50112C78BA656F1339745EF4AE15D8D2619` |
| Candidate PE | `A82AFB9CDA5D9D39A7E31598CA8883CB2FB1ABA49CF78BE8F5D3D1A235385A76` |
| Candidate map | `216AE93CADF1140161ABFE6EF2780CD3A83E222C87551CE9F06DE43D470379A1` |
| Integrity inventory | `0A63EF85EBBB9AC20E02971B7EA77F86DE1867C89339E4557E24937734B124A5` |
| Pinned NativeAOT source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |

R18 recorded all 1,198 candidate inventory entries matching, with no unlisted payload, before and after its guest. The R19 analyzer rechecked the exact manifest, PE, map, pinned source commit, and clean source checkout. It did not rebuild or alter the candidate.

The R18 native run is preserved at:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa`

QAC reservation `8bf0459dac02478989ebe1d3eaa53571` owned QEMU PID `19296` under QAC owner PID `17980`. Ownership was checked before resume and before sample 2; QMP reported the guest running after resume. QAC release and the final process/reservation census were verified after exit. The R18 run manifest SHA-256 is `3D93A277CA4994D4CDA3CF35DBC480A6372DF943BF7BD6AC6F9ECFDF5A413FD1`. R18's candidate-bound one-boot guard is consumed and preserved; R19 did not retry it.

## R18 execution reconstruction

The two CPU captures were 5.419066 seconds apart. The corresponding stack windows were 5.404456 seconds apart. The guest was paused for each QMP capture; the pair proves ordered observations after resume, not continuous CPU execution throughout the interval.

| Sample | CPU 0 state | Stack capture |
|---|---|---|
| 1 | RIP `0x10005CFD0`, inside `LookupUnwindInfoForMethod`; RSP `0x52B5228`, RBP `0x52B5360` | `guest-stack-state-timeout.bin`, 3,544 bytes from `0x52B5228`, SHA-256 `5EBF16EFACE79C0CFEE8FEAE4C82EE567AB01F8C429DE364B4485AA1D9B7B271` |
| 2 | RIP `0x10005357A`, in `Thread::GcScanRootsWorker`; RSP `0x52B5290`, RBP `0x52B5360` | `guest-stack-state-timeout-sample-002.bin`, 3,440 bytes from `0x52B5290`, SHA-256 `0750B7E454E316965EA3E026E0D2F20A3B7FED3DFC2D6A0C4A21C7303A9573CA` |

Both windows report stack bounds `[0x52B2000, 0x52B6000)` and the same guest thread pointer `0x3D79360` in the stack-bound marker. The verified frame chains return through `Thread::GcScanRootsWorker`, `Thread::GcScanRoots`, `GCToEEInterface::GcScanRoots`, the WKS mark phase, and the `RhCollect` / `RhpCollect` caller path. They identify the same worker activation by its stack frame, live iterator address, and caller chain; the capture protocol does not provide a separate invocation sequence number.

The first sample was during `CalculateCurrentMethodState`'s method lookup. The second is at `0x10005357A`, immediately after the candidate instruction `call StackFrameIterator::Next()` at `0x100053575` and immediately before the worker's `IsValid()` call at `0x10005357D`. The candidate worker's normal frame loop calls `EnumGcRefs` before the optional conservative-range check and `Next()`. Therefore the current frame's `EnumGcRefs` invocation returned before sample 2. The captures do not contain a callback count, so they do not establish that any root callback was invoked.

In both stack windows, the same `StackFrameIterator` object is at `0x52B5340`; all `0x230` bytes are identical. Relevant fields after the `Next()` return are:

| Field | Captured value |
|---|---|
| `m_ControlPC` | `0x10001E956` |
| `m_RegDisplay.SP` | `0x52B5980` |
| `m_RegDisplay.IP` | `0x10001E956` |
| `m_pCodeManager` | `0x104666010` |
| flags | `0x8A` (`MethodStateCalculated` clear) |
| `m_OriginalControlPC` | `0x10001E956` |
| `m_pPreviousTransitionFrame` | null |

The iterator still describes the `RhCollect` frame. The R18 saved analysis resolved that frame's stack return to `0x100001A8E`. Independent offline replay of the exact candidate `.pdata` unwind using the captured `REGDISPLAY` and raw stack produces:

- Runtime-function begin RVA: `0x1E920` (`RhCollect`).
- Return-address slot: `0x52B5A18`, containing `0x100001A8E`.
- Caller SP after the return address: `0x52B5A20`.
- Caller: `__managedcode_a` in the same candidate PE.

The pinned `StackFrameIterator::NextInternal` calls `UnwindStackFrame`, then on the ordinary path sets `m_ControlPC` from the updated `REGDISPLAY.IP`. Its special transition-frame reset requires a non-null `m_pPreviousTransitionFrame` with `SkipNativeFrames`; the captured state has a null previous frame. The observed return from `Next()` with the old `ControlPC`, `IP`, and `SP` therefore contradicts the expected ordinary unwind to the verified caller. The worker's subsequent `IsValid()` sees a nonzero control PC, so the same frame remains eligible for another loop iteration; R18 did not capture that later iteration.

## Pinned root-scanning contract and completion boundary

The source checkout is clean at `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

- `Thread::GcScanRoots` establishes the iterator and calls `GcScanRootsWorker` (`Runtime/thread.cpp`, lines 393–402, 442–569).
- The ordinary worker loop calls `CalculateCurrentMethodState`, `EnumGcRefs`, optional conservative range enumeration, then `frameIterator.Next()` (lines 503–539). The second instruction pointer is in this loop after `Next()` returned.
- `StackFrameIterator::Next` delegates to `NextInternal` (lines 1523–1527). `NextInternal` calls the code manager's `UnwindStackFrame` (line 1565), selects a transition-frame reset only under the condition above (lines 1575–1590), and otherwise publishes the unwound IP (line 1600).
- After frame iteration, the worker enumerates exception objects in the `ExInfo` chain, registered `GCFrame` references, and the thread-abort exception (lines 542–569). The R18 worker snapshot proves none of these later phases completed.
- `GCToEEInterface::GcScanRoots` scans thread statics and TLS around each thread's `GcScanRoots` call (`Runtime/gcenv.ee.cpp`, lines 94–137). The GC mark phase then drains the mark queue and continues with handles, older-generation/card roots, dependent handles, and `AfterGcScanRoots` (`gc/gc.cpp`, lines 29899–30089). Root scan completion would not itself prove full collection completion.

| Milestone | R19 evidence |
|---|---|
| Runtime-function lookup returned to worker | Yes, sample 2 is later in the worker. |
| One frame's `EnumGcRefs` invocation returned | Yes, execution reached the subsequent `Next()` call and returned to its caller. Callback count is unknown. |
| Stack iterator advanced to caller | **No.** The valid iterator object is byte-for-byte unchanged across the lookup sample and post-`Next()` sample; candidate unwind predicts a different caller IP/SP. |
| `GcScanRootsWorker` returned | No. |
| Exception, registered-frame, and thread-abort roots completed | No evidence. |
| Enclosing thread root enumeration completed | No. |
| GC mark/collection completed | No collection-completion marker or counter evidence. |
| Live managed object survived and result `1123` returned | No evidence. |
| Post-return ThreadStore/FLS/transition invariants | Not reached. |
| QEMU exit and QAC release | Yes, for R18; independently verified there. |

## Instrumentation and native-run decision

No R19 candidate or runtime instrumentation was warranted to establish the observed non-advancing `Next()` operation: the existing authentic pair already captures the call's return state, and exact candidate unwind data plus the stack bytes establish the expected caller. R19 added only a hosted offline analyzer and its tests. No code was added to a GC loop, no Thread fields or root slots were written, and the managed workload was unchanged.

The implementation cause remains open. The next useful diagnostic should bracket only the `UnwindStackFrame` call for this exact `RhCollect` frame and record its return status, input/output `REGDISPLAY.IP` and `SP`, and returned previous-transition-frame value in bounded storage independent of NativeAOT `Thread` objects. It should establish whether the code manager returned without changing its input, whether the wrong runtime-function record was selected, or whether a later iterator step restored the old state. Do not add per-frame or per-reference logging. Any new native attempt requires an independent candidate and a fresh one-boot guard; R19 launched no guest.

## Hosted validation

R19's saved hosted evidence is under:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z\r19-hosted-tests-final2`

| Command / suite | Result |
|---|---|
| `python scripts/dotnet/test-c188r19-gc-root-completion.py` against the preserved candidate, R18 pair, and pinned source | PASS, 12 assertions. Covers lookup vs completion, frame progress vs full collection, identical/different CPU samples, missing root-scan exit, overflow/alias records, absent post-GC data, and Outcome A acceptance. No QEMU or QAC. |
| `python scripts/dotnet/test-c188r17-stackwalk-progress.py` using the preserved R16 capture and stack | PASS, 10 assertions. |
| `python scripts/dotnet/test-c188r17-runtime-function-lookup.py` using the preserved R16 candidate/capture | PASS, 625 assertions. |
| `scripts/dotnet/test-c188r17-runner-packaging.ps1` using the R17 candidate and R18 run | PASS, 34 assertions across five scenarios; no QEMU or QAC. |
| `scripts/dotnet/test-c188r13-managed-gc-root.ps1` against the R17 candidate inputs | PASS, 75 assertions, including diagnostic/production separation. |
| `scripts/dotnet/test-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1` against the original R12 candidate | PASS, 23 assertions; no guest or QAC. |
| `scripts/dotnet/test-c188r12r4r5r4-threadstore-readiness.ps1` | PASS, 7 mode-model and 6 telemetry assertions. |
| `scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` | PASS, 35 cases. |
| `scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff.ps1` | PASS, 22 analyzer cases and 10 source checks. |
| `scripts/dotnet/test-c188r11-runtime-lifecycle.ps1`; `test-c188r11-output-bounds.ps1` | PASS, 7 and 6 cases. |
| Preserved isolated native allocation executables: record, linked `operator new`, backend summary, and GC VM slot table | All four exited 0. The slot-table test reported 2,425 assertions and 1,024 reserve/release cycles. R18's saved suite also records production-profile isolation. |

R18's preserved hosted results record the R16 caller-attribution suite as PASS with 30 assertions. A fresh R19 rerun was not valid with the available artifacts: the R18 sample is at a different RIP/RSP than the R16 test's fixed R15 fixture, and the R15 mock register file lacks the `CPU#0` block required by that test. This is a fixture limitation, not a new runtime failure.

The following initial rerun attempts also used incompatible profiles and were then rerun with their matching preserved inputs: R17 progress/lookup tests given the later R18 CPU sample rejected its `LookupUnwindInfoForMethod` checkpoint; the R12 object test given the R13 GC candidate rejected its different generated managed entry. The correctly scoped R17 reruns passed 10 and 625 assertions, and the original R12 candidate rerun passed 23 assertions. These fixture-selection failures are preserved in the R19 hosted artifact tree and are not counted as product regressions.

## R19 offline analyzer

The new analyzer is `scripts/dotnet/analyze-c188r19-gc-root-completion.py`. It binds the exact candidate hashes and source commit, checks QAC ownership metadata, validates both CPU/stack capture hashes and bounds, compares the full live iterator object, verifies candidate worker call instructions, and replays the candidate unwind against the raw captured stack. It reports the furthest proven milestone without treating a different RIP, a lookup return, or an iterator call return as collection success.

Saved result:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z\r19-hosted-tests-final2\r19-offline-analysis.json`

Final R19 working-tree state and the closeout commit are recorded in the final response. The three pre-existing modified `.obj` files remain unstaged; no push was performed.
