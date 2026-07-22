# NativeAOT Workstation GC Startup Readiness

Status: gated readiness audit rerun complete. The bounded runtime-neutral FLS/local-storage, exact stack-bound, and minimal ThreadStore prerequisites are PASS. The collector was not started. The audit result is Outcome B: one next NativeAOT startup prerequisite remains at GC-owned virtual-memory PAL integration.

Date: 2026-07-19

Machine-readable result: `out/dotnet/gc-startup-dry-run/readiness/gc-startup-readiness.json`

## Scope and stop rule

This audit targets the exact locked .NET 9.0 NativeAOT Workstation GC and the current guideXOS AMD64 experimental branch. It validates the runtime-pack lock, source identity, normal and experimental Server baselines, existing no-collection proofs, and the generic event, virtual-memory, thread, mutex, and QEMU regressions. The final local-storage guest artifact is `out/runtime/native-local-storage-qemu-validation/smoke-20260719-215002-880-4189/native-local-storage.serial.log`; the final mutex artifact is `out/runtime/native-mutex-qemu-validation/smoke-20260719-215440-132-9923/native-mutex.serial.log`.

The live startup dry run is permitted only after every mandatory prerequisite is proven. Exact initial/worker stack bounds and startup-safe ThreadStore attachment now pass through an inactive runtime-pack adapter. NativeAOT GC-owned virtual-memory PAL integration is the single next mandatory blocker, so this audit still stops before `RhInitialize`. No startup tracing mode was added, and `NATIVEAOT_GC_STARTUP_DRY_RUN.md` was intentionally not created.

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
| FLS index allocation/release/detach callbacks | PASS | Yes | Yes | Yes (inactive probe) | Yes | `runtime/local_storage` manager, hosted tests, bare-metal kernel build, opt-in QEMU guest, and NativeAOT adapter probe all pass, including release callbacks, detached-thread cleanup, process/runtime teardown, and leak checks. The fixed-index proof path remains unchanged. |
| Stack-bound PAL adapter | PASS | Yes | Yes | Inactive adapter | Yes | Hosted Windows and bare-metal providers return exact `[low, high)` bounds and validate current RSP; see [Native Stack Bounds](../runtime/NATIVE_STACK_BOUNDS.md). |
| Initial-thread exact stack bounds | PASS | Yes | Yes | Inactive adapter | Yes | Boot linker symbols `boot_stack_bottom`/`boot_stack_top`; QEMU exact-source and RSP checks pass. |
| Initial RSP validation | PASS | Yes | Yes | Inactive adapter | Yes | The sampled bootstrap RSP is inside the exact boot-stack interval. |
| Worker-thread exact stack bounds | PASS | Yes | Yes | Inactive adapter | Yes | TCB-owned worker stack interval is published before entry and remains valid through detach. |
| Worker RSP validation | PASS | Yes | Yes | Inactive adapter | Yes | Hosted and QEMU workers validate their sampled RSP inside the TCB-owned interval. |
| Initial/worker ThreadStore attachment and stack bounds | PASS | Yes | Yes | Inactive adapter | Yes | Minimal opaque runtime records attach, lookup, isolate, detach, and clear bounds before reuse; see [NativeAOT ThreadStore Startup](NATIVEAOT_THREADSTORE_STARTUP.md). |
| ThreadStore global initialization | PASS | Yes | Yes | Inactive adapter | Yes | Explicit application-scoped initialization, duplicate-init rejection, bounded registry, and attached-count checks pass. |
| Initial-thread attachment | PASS | Yes | Yes | Inactive adapter | Yes | Requires generic local storage, exact bounds, current-RSP validation, transition sentinel, preemptive initial state, and FLS publication. |
| Worker-thread attachment | PASS | Yes | Yes | Inactive adapter | Yes | Plain native worker attaches and detaches without managed work, allocation, or collection. |
| Current-thread lookup | PASS | Yes | Yes | Inactive adapter | Yes | FLS-backed lookup is owner-checked and isolated between initial and worker threads. |
| Transition-frame readiness | PASS | Yes | Yes | Inactive adapter | Yes | Empty/sentinel state is initialized; live transition frames are rejected on detach. |
| ThreadStore detach | PASS | Yes | Yes | Inactive adapter | Yes | Exactly-once ownership validation, unlink, FLS clearing, generation retirement, and post-detach null lookup pass. |
| FLS/ThreadStore detach ordering | PASS | Yes | Yes | Inactive adapter + QEMU | Yes | Runtime record remains live for callback delivery; stack/TCB reclamation follows both detach layers. |
| Startup-safe thread enumeration | PASS | Yes | Yes | Inactive adapter | Yes | Bounded mutex-protected registry/count only; this is not collection-safe enumeration. |
| Collection-safe suspension/enumeration | BLOCKED | No | No | No | No | Explicitly outside startup attachment scope; no suspension, hijacking, or collection-safe snapshot is claimed. |
| GC event/critical-section startup lock | BLOCKED | Generic primitive only | Yes for generic event | No | Yes | Generic event/lock evidence passes; NativeAOT platform startup wiring remains downstream of the next GC PAL integration. |
| GC-owned virtual memory/timing | FAIL | Generic primitive only | Yes for generic VM and QEMU VM | No | Yes | Single next blocker: NativeAOT GC-owned reserve/commit/decommit/release and timing integration is not connected. |
| Write barriers/card-table publication | BLOCKED | No | No | No | Yes | Blocked by the GC-owned PAL/startup boundary; current proof heap is not a real GC heap. |
| Module/type-manager/runtime state | BLOCKED | No | No | No | Yes | Blocked by live GC startup; current proof pack does not register stock NativeAOT module/type-manager state. |
| Mandatory finalizer/helper thread | BLOCKED | No | No | No | Yes | Blocked by live GC startup; the real helper remains intentionally unstarted. |
| GC handle-manager initialization | BLOCKED | Source/library present | No | Library linked, startup not reached | Yes | Source path exists, but live initialization is blocked by the single GC-owned PAL blocker. |
| Clean startup shutdown | BLOCKED | No | No | No | Yes | Not attempted because startup eligibility failed. |

Generic primitive evidence is retained separately. The explicit QEMU thread and VM runs passed. The final local-storage QEMU guest emitted PASS for every listed lifecycle check, including `Detached-thread cleanup`, `Index release callback`, `Process/runtime teardown`, and `Leak check`. The final mutex guest emitted PASS for every listed behavioral check and the protected counter; the anchored parser reports both `Guest marker: PASS` and `Runner parsed result: PASS`. The hosted event, mutex, thread, VM, ELF, and local-storage suites and their inactive adapter compile/link probes pass.

## Exact next blocker

The ThreadStore/stack-bound prerequisite is now complete. The one next missing
mandatory prerequisite is:

```text
NativeAOT GC-owned virtual-memory PAL integration
```

The generic FLS/local-storage manager, exact stack-bound API, bare-metal boot
stack source, hosted stack provider, inactive PAL adapter, and minimal
ThreadStore record now provide dynamic index allocation/release, per-thread
get/set, detach-callback delivery, generation-safe reuse, exact initial and
worker bounds, current-RSP validation, startup-safe registry/count checks, and
bounded ThreadStore attach/detach. The exact locked FLS source mapping remains
recorded in [NativeAOT GC Platform FLS](NATIVEAOT_GC_PLATFORM_FLS.md); the fixed
index proof path remains unchanged. Starting stock `RhInitialize` before the
GC-owned virtual-memory PAL is connected would not be a valid dry run.

The remaining GC heap, write-barrier, module/type-manager, finalizer-helper,
handle-manager, and shutdown rows are downstream of this one next blocker. They
are not additional next blockers and are not being collapsed into a claim that
the current runtime is startup-ready.

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

Outcome B. Exact stack-bound reporting and minimal ThreadStore lifecycle are complete; the one next mandatory blocker is NativeAOT GC-owned virtual-memory PAL integration. No collector initialization, allocation through the real GC, collection, managed finalizer execution, or GC shutdown was attempted in this pass.
