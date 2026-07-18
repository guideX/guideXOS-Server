# NativeAOT Workstation GC Startup Readiness

Status: gated readiness audit rerun complete. The bounded runtime-neutral FLS/local-storage prerequisite is PASS. The collector was not started. The audit result is Outcome B: the next NativeAOT startup prerequisite is still missing at the ThreadStore/stack-bound boundary.

Date: 2026-07-18

Machine-readable result: `out/dotnet/gc-startup-dry-run/readiness/gc-startup-readiness.json`

## Scope and stop rule

This audit targets the exact locked .NET 9.0 NativeAOT Workstation GC and the current guideXOS AMD64 experimental branch. It validates the runtime-pack lock, source identity, normal and experimental Server baselines, existing no-collection proofs, and the generic event, virtual-memory, thread, mutex, and QEMU regressions.

The live startup dry run is permitted only after every mandatory prerequisite is proven. The dynamic FLS/local-storage contract is now independently proven, but the next required NativeAOT PAL/runtime contract is not present. This audit therefore still stops before `RhInitialize`. No startup tracing mode was added, and `NATIVEAOT_GC_STARTUP_DRY_RUN.md` was intentionally not created.

## Exact stock startup order

The matching NativeAOT source establishes this order:

```text
RhInitialize
  -> PalInit
     -> Windows FLS allocation and GCToOSInterface initialization
  -> InitDLL
     -> InitializeGCEventLock
     -> RestrictedCallouts::Initialize
     -> RuntimeInstance::Initialize
        -> ThreadStore::Create
     -> InitializeGC
        -> GCHeapUtilities::InitializeGC
           -> GC_Initialize(nullptr, &heap, &manager, &g_gc_dac_vars)
           -> WKS::CreateGCHeap
        -> g_pGCHeap->Initialize
        -> RhInitializeFinalization
           -> PalStartFinalizerThread
        -> GCHandleManager::Initialize
```

Relevant source evidence is in the matching extracted checkout under `out/dotnet/gc-feasibility-baseline/source-extract/src/coreclr/`, including `nativeaot/Runtime/startup.cpp`, `GCHelpers.cpp`, `gcheaputilities.cpp`, `FinalizerHelpers.cpp`, `threadstore.cpp`, `thread.cpp`, `gcenv.ee.cpp`, and `windows/PalRedhawkMinWin.cpp`.

## Readiness matrix

`Entered` is intentionally false for every startup operation: this was a readiness audit, not a startup attempt.

| Mandatory item | Status | Implemented | Independently tested | Adapter linked for stock startup | Expected in stock order | Exact evidence/limitation |
| --- | --- | --- | --- | --- | --- | --- |
| Locked runtime-pack/source identity | PASS | Yes | Yes | Yes | Yes | Lock hashes and clean static smoke pass. |
| Normal Server build | PASS | Yes | Yes | N/A | No | `baseline/normal-server-build.log`. |
| Experimental Server build | PASS | Yes | Yes | N/A | No | `baseline/experimental-server-build.log`. |
| Existing no-collection proofs | PASS | Yes | Yes | Proof adapter | No | Nonallocating, single-allocation, repeated 64 KiB, and repeated 4 KiB remain passing with no collection entry. |
| FLS index allocation/release/detach callbacks | PASS | Yes | Yes | Yes (inactive probe) | Yes | `runtime/local_storage` manager, hosted tests, bare-metal kernel build, opt-in QEMU guest, and NativeAOT adapter probe all pass. The fixed-index proof path remains unchanged. |
| Current-thread ThreadStore attachment and stack bounds | FAIL | No | No | No | Yes | Exact next blocker. Stock `ThreadStore::AttachCurrentThread`, `Thread::Construct`, and the stack-bound PAL contract are not reachable. |
| GC event/critical-section startup lock | FAIL | No | Yes for generic event | No | Yes | Generic hosted, bare-metal, and inactive adapter probes pass; NativeAOT startup remains blocked by ThreadStore/stack state. |
| GC-owned virtual memory/timing | FAIL | No | Yes for generic VM and QEMU VM | No | Yes | Generic hosted/QEMU VM passes; NativeAOT GC PAL is not connected. |
| Write barriers/card-table publication | FAIL | No | No | No | Yes | Stock `GCToEEInterface::StompWriteBarrier` is not reached; current proof heap is not a real GC heap. |
| Module/type-manager/runtime state | FAIL | No | No | No | Yes | Current proof pack does not register the stock NativeAOT module/type-manager state. |
| Mandatory finalizer/helper thread | FAIL | No | No | No | Yes | Stock `RhInitializeFinalization` unconditionally calls `PalStartFinalizerThread`; no live helper lifecycle exists. |
| GC handle-manager initialization | FAIL | Source/library present | No | Library linked, startup not reached | Yes | The source path exists, but initialization has not been entered. |
| Clean startup shutdown | FAIL | No | No | No | Yes | Not attempted because startup eligibility failed. |

Generic primitive evidence is retained separately. The explicit QEMU thread and VM runs passed. The local-storage QEMU guest emitted PASS for every listed lifecycle check. The mutex guest emitted PASS for every listed behavioral check and the protected counter; after the parser fix, the rerun reports both `Guest marker: PASS` and `Runner parsed result: PASS`. The hosted event, mutex, thread, VM, ELF, and local-storage suites and their inactive adapter compile/link probes pass.

## Exact next blocker

The next missing prerequisite is one NativeAOT runtime/PAL capability:

```text
NativeAOT ThreadStore attachment and exact stack-bound PAL contract
```

The FLS prerequisite now provides dynamic index allocation/release, per-thread get/set, detach-callback delivery, generation-safe reuse, and bounded failure reporting through `runtime/local_storage` and the inactive adapter. The current fixed-index `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp` remains the proof path and is unchanged. Starting stock `RhInitialize` without the real current-thread `ThreadStore` registration and exact stack bounds would not be a valid dry run.

The missing ThreadStore, stack-bound, GC PAL, card-table, module/type-manager, finalizer-helper, and shutdown items are downstream gates recorded in the machine report. They are not being collapsed into a claim that the current runtime is startup-ready.

## Preserved no-collection contract

The current proof remains the control condition:

- nonallocating managed entry: PASS;
- single allocation through the proof heap: PASS;
- repeated 64 KiB bounded allocation: 234 objects of size 280, 16 bytes remaining, controlled OOM, `collectionEntered=0`, no heap expansion: PASS;
- repeated 4 KiB bounded allocation: 14 objects of size 280, 176 bytes remaining, controlled OOM, `collectionEntered=0`, no heap expansion: PASS;
- no managed finalizers and no real GC collection were entered.

These proofs were not changed by the readiness audit.

## Historical fault classification

Prior diagnostics remain classified as historical and unresolved where previously unresolved: `0xC0000409`, `0xC0000374`, and `0xC0000005`. None was reproduced by this gated audit. They are not evidence of a successful or failed live GC startup because no live startup was attempted.

## Result

Outcome B. The dynamic FLS/local-storage manager is complete, but the exact next mandatory blocker is NativeAOT ThreadStore attachment and stack-bound PAL integration. No collector initialization, allocation through the real GC, collection, managed finalizer execution, or GC shutdown was attempted in this pass.
