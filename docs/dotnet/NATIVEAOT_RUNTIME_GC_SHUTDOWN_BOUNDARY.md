# NativeAOT runtime GC shutdown boundary

Status: 2026-07-26. This audit covers the locked AMD64 `net9.0` NativeAOT
runtime-pack identity: source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, ILCompiler/runtime pack `9.0.0`,
Workstation GC, one heap, server/background/concurrent modes disabled, GC
interface 5.3, and EE 2. The source copy audited is
`out/dotnet/pal-runtime-active-replacement-build/locked-source`.

## Decision

The supported lifetime model is **Model C: process-lifetime GC state**.

Workstation GC initialization succeeds in disposable system-QEMU processes,
but the matching NativeAOT source does not provide a supported runtime-wide GC
shutdown, finalizer stop/join, FLS-index release, module/type-manager
unregister, write-barrier reset, or same-process reinitialization contract.
QEMU process exit is therefore the cleanup boundary for this branch. No
shutdown call, `GC.Collect`, managed entry, managed allocation through the
real collector, or second `RhInitialize` was executed in this audit.

This is not a claim that process exit is an orderly GC shutdown. It is a
deliberate containment rule for an initialization-only experiment whose
runtime state is not reusable in the same process.

## Source-backed shutdown audit

The following results come from the locked source, not from symbol-name
inference alone.

| Area | Matching source evidence | Result |
| --- | --- | --- |
| Public runtime entry points | `nativeaot/Runtime/startup.cpp`: `RhInitialize` calls `PalInit`, registers `atexit(OnProcessExit)` on Windows, and initializes the runtime. No `RhShutdown` or runtime uninitialize symbol exists. | No supported full shutdown |
| Process-exit callback | `OnProcessExit` marks the shutdown thread and only conditionally shuts down EventPipe/DiagnosticServer. It does not stop GC, finalization, ThreadStore, PAL workers, or write barriers. | Process exit only |
| Per-thread detach | `RuntimeThreadShutdown` detaches one current thread during normal thread/FLS teardown, but deliberately skips detachment while Windows DLL/process shutdown is in progress. | Not a runtime shutdown |
| FLS index | `PalInit` allocates one process-wide `g_flsIndex` with `FlsAlloc(FiberDetachCallback)`. The matching PAL source has no `FlsFree` path. | Process lifetime |
| Finalizer | `FinalizerStart` waits forever, re-signals the request event, and enters `ProcessFinalizers`; `RhInitializeFinalization` creates events and starts the helper without a stop flag or join handle. | No supported stop/join |
| PAL helper workers | `PalStartBackgroundWork` creates a thread and closes its handle immediately; the PAL header exposes start operations but no stop/join operations. | No supported worker drain |
| ThreadStore | `RuntimeInstance` owns a ThreadStore, but the source has no global shutdown call. `ThreadStore::Destroy` is an internal delete path, not a safe sequence for attached runtime threads. | Process lifetime |
| Runtime instance | `RuntimeInstance::Initialize` asserts that the global instance is null, while its module/type-manager registries are add-only. `Destroy` has no call site and does not reset all published runtime/GC globals. | No same-process reinit |
| GC initialization | `GC_Initialize` asserts the heap is null and says it should be called only once at startup. No matching `GC_Shutdown` export exists. | Single-use startup |
| GC heap teardown | `GCHeap::Shutdown` is not the full heap teardown. `GCHeap::StaticShutdown` and `gc_heap::shutdown_gc` exist internally, but have no caller in the matching NativeAOT runtime and assume shutdown-specific suspension/finalizer conditions. | Not a supported external API |
| Handle manager | `GCHandleManager::Shutdown` and `Ref_Shutdown` exist internally, have no runtime call site, and `Ref_Shutdown` leaves indexed handle tables to external destruction. | Not a complete runtime shutdown |
| GC platform teardown | `GCToOSInterface::Shutdown` is an adapter/platform method. The NativeAOT initialization sequence does not call it as a runtime shutdown, and it cannot stop finalization, unregister modules, or reset the EE write-barrier state. | Not sufficient |
| Write barriers | `GCToEEInterface::StompWriteBarrier` publishes card-table and heap-bound globals. The matching source has no reset/uninitialize operation; clearing them manually would be outside the contract. | Must remain live until exit |
| Module/type-manager lifetime | OS modules, code managers, and type managers are registered into add-only runtime lists; no matching unregister sequence was found. | Process lifetime |
| Image lifetime | `PalGetModuleHandleFromPointer` explicitly uses the pin flag because “The runtime is not designed to be unloadable today.” | No artifact unload |

The internal names above are not authorization to call them. In particular,
`GCHeap::StaticShutdown`, `GCHandleManager::Shutdown`, `Ref_Shutdown`,
`ThreadStore::Destroy`, and `RuntimeInstance::Destroy` are not promoted to a
public shutdown protocol by this audit.

## Candidate classification

Twenty-two candidate operations were checked for an external, ordered, repeatable
shutdown contract:

| Candidate | Classification | Why it is not the boundary |
| --- | --- | --- |
| `OnProcessExit` | Process-exit callback | Tracing-only conditional work; no GC teardown |
| `RuntimeThreadShutdown` | Per-thread callback | Current-thread detach only; skipped during process teardown |
| `FiberDetachCallback` | FLS/fiber callback | Dispatches one thread detach; does not release the process FLS index |
| `PalDetachThread` | Per-thread PAL operation | Does not drain the runtime or finalizer |
| `ThreadStore::DetachCurrentThread` | Per-thread runtime operation | Requires a known current thread; not global |
| `ThreadStore::Destroy` | Internal destructor | No call site; unsafe with attached/helper threads |
| `Thread::Detach` | Per-thread cleanup | Fixes one allocation context only |
| `Thread::Destroy` | Per-thread destructor | Does not destroy ThreadStore or GC state |
| `RuntimeInstance::Destroy` | Internal destructor | No call site; incomplete global/list cleanup |
| `RuntimeInstance::~RuntimeInstance` | Internal destructor | Only deletes its ThreadStore; does not reset runtime globals |
| `GCHeap::Shutdown` | Internal virtual method | Not full heap destruction |
| `GCHeap::StaticShutdown` | Internal static teardown | No caller; assumes a suspended, drained finalizer-safe state |
| `gc_heap::shutdown_gc` | Internal GC teardown helper | Only reached through unsupported static teardown |
| `GCHandleManager::Shutdown` | Internal handle teardown | No caller; dependent tables have separate lifetime assumptions |
| `Ref_Shutdown` | Internal handle-table helper | No caller; not the complete handle-manager sequence |
| `GCToOSInterface::Shutdown` | Platform adapter method | Not called by the NativeAOT runtime and not runtime-wide |
| `PalStartBackgroundWork` | Helper-start operation | No matching stop/join API; starting is not a shutdown protocol |
| `PalStartFinalizerThread` | Finalizer-helper start operation | No matching stop/join API; starting is not a shutdown protocol |
| `GCEvent::CloseEvent` / critical-section destruction | Platform-object operations | Individual object release cannot order runtime-wide quiescence |
| Module/type-manager registration | Add-only runtime registration | No matching unregister operation |
| `RhpUnregisterFrozenSegment` | Frozen-segment operation | Segment-specific; not module/type-manager or heap shutdown |
| `PalGetModuleHandleFromPointer` | Image lifetime operation | Pins the runtime image; no unload contract |
| CRT/`atexit` teardown | OS process teardown | Does not establish a reusable same-process runtime |

Supported full shutdown entry: **none found**.

## Resource accounting and lifetime obligations

| Resource or state | Required release/reset for reusable shutdown | Current disposition |
| --- | --- | --- |
| Collector heap and internal segments | Stop workers, reach a safe finalizer/GC point, destroy heap, release collector VM | Process exit; no supported runtime sequence |
| Card table and write-barrier globals | Stop all users, unpublish and repatch barriers | Kept live; never manually cleared |
| Finalizer event/helper stack/thread | Signal stop, observe helper exit, close/join | Parked helper; process exit |
| Other PAL workers | Stop, join, then release bridge records/handles | No NativeAOT stop/join contract |
| ThreadStore records and thread-local state | Detach every runtime thread before store destruction | Process exit; normal per-thread detach remains valid only for exiting threads |
| FLS index and callback values | Stop new attach/detach activity, clear values, call `FlsFree` | No matching `FlsFree`; process lifetime |
| Handle manager and indexed tables | Drain roots and destroy all dependent tables | Process exit; internal shutdown is incomplete/unwired |
| Modules, code managers, type managers | Unregister every entry before image unload | Add-only registrations; image remains pinned/mapped |
| PAL hook table and callbacks | Quiesce every callback/worker before uninstall | Kept installed until process exit |
| Virtual-memory reservations/commits | Release only after no collector or barrier access remains | Process exit for the live GC experiment |

The existing startup serial evidence shows one attached ThreadStore record and
one active parked helper before disposable QEMU termination, with zero callback
entries, zero collections, zero GC-backed allocations, and no managed finalizer
entry. The process teardown proof establishes that the disposable process ends
without a post-exit callback; it does not pretend that every internal GC byte
was independently enumerated after address-space destruction.

## Failure-path result

No failure injection was added to the runtime and no failure path was invoked
against a live GC instance. The source audit records the important bounded
failure behavior:

- `PalInit` can fail after `FlsAlloc` or after GC platform initialization; the
  matching source does not provide a complete rollback/uninitialization path.
- `InitializeGC` publishes and initializes multiple global subsystems in
  sequence, but no runtime-level rollback path is present for a later failure.
- `RhInitialize` has no supported retry/reinitialize contract after partial
  runtime publication.

The safe policy for these failures is disposable-process termination. A future
failure-injection experiment must fail before any attempt at managed execution
and must use a new disposable process for each case. It must not infer
same-process cleanup from process exit.

## Reusability decision

| Model | Decision | Reason |
| --- | --- | --- |
| A. Fully reusable | Rejected | No full shutdown, worker drain, FLS release, module unregister, or barrier reset contract |
| B. Orderly shutdown without reinit | Rejected as unproven | The source does not expose an ordered runtime shutdown sequence |
| C. Process-lifetime | **Selected** | Matches source lifetime assumptions and existing disposable QEMU evidence |
| D. Partial | Rejected as the primary model | Platform-object cleanup alone would leave runtime-owned state live and would create an unsafe half-shutdown |

## Validation boundary

The existing authoritative startup matrix remains the basis for the current
startup result: first, repeat, and fresh disposable QEMU processes pass;
`RhInitialize` returns zero; managed entry, collector allocation, collection,
and managed finalizer entry remain zero. This audit did not rerun a new live
QEMU image after the clean adapted-GC rebuild because that rebuild produced
archive hash `C2B4E7BA982D27D9C17B98CAA0ACD160584251C2569CB41F4FBA3EA658243068`
and replacement-object hash
`D3BBA664316C6DB860659EF7C501B0263621F6D6C227DCDA5D96EDD495E99DC4`, instead
of the locked baseline archive `BA847F225439A8D693CD975CCAACDD01264BDA88BE4F2BBF18D5A0E4DB0F1F52`
and object `1F255DFCF8CBEB0A93289EF0E160C04DA5410EB41F9B06113186BF9D3438F0CCB`.
The rebuild retained exact symbol binding and zero missing/duplicate symbols,
but the identity mismatch is recorded as a reproducibility gate; it is not
silently treated as the locked artifact.

## Exact next experiment

Because Model C is selected, the next authorized experiment is one disposable
QEMU init run with exactly one primitive `byte[]` allocated through the real
collector, no collection, no finalizer request, no second initialization, and
no live-process teardown attempt. The run must retain the current process-
lifetime cleanup boundary and must first pass the locked-artifact identity
gate.
