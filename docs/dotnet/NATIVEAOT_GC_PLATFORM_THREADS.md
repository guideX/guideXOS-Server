# NativeAOT GC Platform Threads

Status: inactive adapter-probe only. NativeAOT Workstation GC is not
initialized, no finalizer/helper thread is started, and no collection is
triggered by this pass.

The generic native thread lifecycle is now also validated on real bare-metal
AMD64 under QEMU 11.0.0 TCG: opaque context delivery, bounded stack/context
startup, result capture, join before and after exit, timed join retry, TCB
generation reuse, stale-handle rejection, detached reclamation, bounded
multiple workers, wait/timer cleanup, and the existing narrow process-teardown
policy all passed. This is Outcome A for the generic native thread primitive,
not activation evidence for NativeAOT GC. The NativeAOT-facing adapters remain
inactive probes only.

The inactive runtime-pack ThreadStore adapter now proves the startup-scoped
attachment contract, exact stack bounds, current-thread lookup, transition
sentinels, detach ordering, and bounded shutdown. Collection-safe suspension,
GC virtual memory, heap/root/write-barrier initialization, wait-many/low-memory
behavior, and a process-lifetime helper shutdown policy still require
independent validation.

## 1. Workstation GC thread requirements

The locked NativeAOT source is the .NET 9.0.0 runtime pack at commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, with the relevant source preserved
under `out/dotnet/gc-feasibility-baseline/source-extract`.

The stock Workstation path needs more than a host thread handle:

- a native entry callback and opaque startup context;
- a thread registered with `ThreadStore`;
- a runtime `Thread` with transition/cooperative/preemptive state;
- exact stack bounds for stack-map root enumeration and suspension;
- TLS/FLS attach and detach behavior;
- safe-point/trap/context cooperation with `SuspendAllThreads`;
- event waits and non-alertable timeout behavior;
- deterministic process-lifetime handling.

The generic guideXOS abstraction now supplies only the first, opaque entry,
result, Event completion, join/detach ownership, bounded stack range, and
generation-protected lifetime. It deliberately does not attach a managed
thread or expose a TCB.

## 2. Finalizer/helper thread requirements

The exact source path is `Runtime/FinalizerHelpers.cpp`:

```text
GC initialization
  -> RhInitializeFinalization
  -> g_FinalizerEvent.CreateAutoEventNoThrow(false)
  -> g_FinalizerDoneEvent.CreateManualEventNoThrow(false)
  -> PalCreateLowMemoryResourceNotification()
  -> PalStartFinalizerThread(FinalizerStart, finalizer-event)
```

`FinalizerStart(void*)` is a plain native callback. It receives the finalizer
request event context, calls `ThreadStore::AttachCurrentThread()`, obtains the
current runtime `Thread`, suppresses GC stress for that thread, publishes
`g_pFinalizerThread`, and waits indefinitely on the auto-reset request event.
After the first request it re-signals the auto-reset event and enters
`ProcessFinalizers()`. The source asserts that this call never returns.

`RhInitializeFinalization` is called by the normal Workstation initialization
order; it is not an optional no-finalizer mode for this stock source.

## 3. Thread stack requirements

The matching source uses the runtime stack/stack-map and suspension contracts,
including `PalGetMaximumStackBounds`, `Thread::GcScanRoots`,
`StackFrameIterator`, and AMD64 context capture. The source does not provide a
single application-facing finalizer stack byte constant that can be copied
into the generic Server API.

The guideXOS bare-metal primitive records exact TCB-owned bounds and preserves
AMD64 ABI alignment. The hosted provider uses the matching maximum-reservation
contract without a guessed range. This is enough for the inactive native
attachment probe, not evidence that a managed finalizer stack is large or safe
enough for GC root walking. A future adapter must still validate a
runtime-specific helper-stack policy without exposing the Server TCB layout.

## 4. Thread-store and managed-code behavior

The finalizer callback begins as native code but later calls managed
`ProcessFinalizers()`. Therefore the eventual helper is not merely a native
background worker. It must attach to `ThreadStore` before publishing itself,
have valid runtime transition state, and be visible to the GC's thread
enumeration/suspension paths.

The inactive probe intentionally does none of this. Its worker is a plain
native function that waits on one Event and returns an opaque context value.

## 5. Startup trigger and initialization order

The relevant order is:

```text
RhInitialize
  -> PalInit / InitDLL / RuntimeInstance / ThreadStore state
  -> InitializeGC
  -> GCHeapUtilities::InitializeGC
  -> GC_Initialize / GCToOSInterface::Initialize / WKS::CreateGCHeap
  -> GC heap initialization
  -> RhInitializeFinalization
  -> finalizer Event creation and helper-thread start
  -> normal module/type/static registration and managed entry
```

The exact repository feasibility analysis records the full startup flow in
`docs/dotnet/NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md`. No part of that flow is
entered by this thread pass.

## 6. Event coordination

The source declares `g_FinalizerEvent` as an auto-reset request event and
`g_FinalizerDoneEvent` as a manual-reset completion event. The request event
wakes the helper for finalization work. The done event is set after a
finalization pass and is used by `RhWaitForPendingFinalizers`.

`RhpWaitForFinalizerRequest` can also wait on the finalizer request and a low
memory notification through `PalCompatibleWaitAny`. That is a wait-many
requirement and is not implemented by the generic Event or this adapter.

The generic mapping is therefore:

| NativeAOT need | Generic guideXOS contract |
| --- | --- |
| request event | `Event(EventMode::AutoReset, false)` |
| done event | `Event(EventMode::ManualReset, false)` |
| request wait | `wait(WaitTimeout::infinite())` |
| finite native timeout | validated `WaitTimeout` |
| helper completion | native thread exit + `joinThread` |
| low-memory + request wait | future wait-many primitive |

## 7. Join and shutdown policy

The stock Windows `PalStartBackgroundWork` implementation creates the native
thread, applies `THREAD_PRIORITY_HIGHEST` for the finalizer path, resumes it,
and closes the creator's handle immediately. The source does not expose a
normal finalizer-thread join during startup; the helper is a long-lived
process/runtime thread and `FinalizerStart` is expected never to return.

Runtime shutdown also has a process-lifetime policy. `RuntimeThreadShutdown`
avoids detaching a thread while process shutdown is already in progress because
other threads may be terminated rudely. A future guideXOS shutdown design must
choose either a documented process-lifetime helper or an explicit joinable
shutdown protocol before activating GC.

The current scheduler has no thread priorities, managed ThreadStore, or safe
GC suspension protocol. The inactive adapter uses a normal joinable generic
thread only so its lifecycle can be independently tested; it is not a
finalizer-thread implementation.

## 8. Inactive adapter probe

The isolated files are:

- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_thread_adapter.h`;
- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_thread_adapter.cpp`;
- `runtime/tests/guidexos_native_thread_adapter_probe.cpp`.

The probe:

1. allocates an opaque adapter record with a borrowed runtime context pointer;
2. creates one generic joinable native thread;
3. starts it through one auto-reset Event;
4. runs a plain native worker function;
5. captures the context pointer as its pointer-sized exit result;
6. joins it through the generic completion Event; and
7. closes the Event and destroys the adapter record.

The separate stack/ThreadStore probe is described in
[NativeAOT ThreadStore Startup](NATIVEAOT_THREADSTORE_STARTUP.md). It has no
call to `RhInitialize`, `GC_Initialize`, `RhInitializeFinalization`,
`ProcessFinalizers`, `RhpCollect`, managed registration, or collection.

## 9. Remaining GC initialization blockers

Before the Workstation path can be activated, the platform still needs:

- GC-owned reserve/commit/decommit/release virtual memory;
- critical sections/locks and all initialization/destruction ordering;
- FLS slot allocation and detach callbacks;
- AMD64 context/suspension services and collection-safe enumeration;
- module/type-manager/static/frozen-root registration;
- collector allocation contexts, write barriers, and card-table setup;
- optional wait-many/low-memory event behavior;
- a process-lifetime or joinable helper shutdown policy; and
- initialization-only diagnostics proving the heap and finalizer thread are
  internally consistent.

The current fixed no-collection managed heap remains unchanged. The helper is
not started and collection remains disabled.

## Exact next primitive

Following the actual call order, the next independently testable primitive is
the bounded NativeAOT GC-owned virtual-memory reserve/commit/decommit/release
and timing layer used by `GCToOSInterface::Initialize` and Workstation heap
segment setup. It should be proved outside GC startup before any initialization
experiment. Synchronization locks remain downstream and must be tested as a
separate generic primitive.
