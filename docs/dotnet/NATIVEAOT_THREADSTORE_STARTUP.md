# NativeAOT ThreadStore Startup Readiness

Status: minimal application-scoped ThreadStore lifecycle implemented as an
inactive runtime-pack adapter and independently probed. It is not wired into
`RhInitialize`, the Workstation heap, the finalizer thread, managed code, or
collection.

## 1. Matching NativeAOT ThreadStore requirements

The locked NativeAOT source is commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The relevant source is preserved
under `out/dotnet/gc-feasibility-baseline/source-extract/src/coreclr/nativeaot`.
`ThreadStore::AttachCurrentThread` attaches the OS thread, constructs a
`Thread`, records maximum stack bounds, sets the attached flag, and links the
runtime thread. `Thread::Construct` initializes transition-frame markers,
invalidates the PAL handle, records the OS thread identity, queries stack
bounds, and starts with a zero allocation context. Detach removes the thread
from the store, clears the platform association, finalizes the record, and
destroys it only after the detach contract completes.

The locked source order is more specifically `PalAttachThread` first, followed
by `Thread::Construct`; construction then calls `PalGetMaximumStackBounds` and
caches the result before the record is published as attached. The isolated
adapter follows the same publication rule: it completes the exact bounds
query before exposing its bounded runtime record.

## 2. Startup call flow

The minimal proven flow is:

```text
generic local-storage initialize
  -> generic current-thread attach
  -> ThreadStore initialize
  -> exact current stack-bound query
  -> ThreadStore attach current thread
     -> bind runtime record through the dynamic FLS adapter
  -> current-thread lookup / snapshot
  -> plain native worker attach, lookup, and detach
  -> worker exit and join
  -> initial ThreadStore detach
  -> ThreadStore shutdown
  -> generic local-storage detach and shutdown
```

The stock flow remains gated before `RhInitialize`; this adapter flow only
proves the prerequisite lifecycle.

## 3. Runtime thread-record shape

The opaque runtime-pack record contains native thread identity, exact low/high
stack bounds, the sampled current SP, transition and deferred-transition frame
fields, an explicit allocation-context field, a generation, attached and
preemptive flags, and bounded ThreadStore links. It is a fixed application-
scoped registry of 32 records. The type is private to
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.cpp`
and is not added to the generic TCB or public Server ABI.

## 4. FLS association

Initialization requires the completed generic local-storage manager and
allocates one dynamic index through the real NativeAOT FLS adapter. Each
attached runtime record is stored as the current-thread FLS value. Lookup
validates that the record is attached and owned by the current native thread.
FLS allocation, per-thread isolation, callbacks, release, generation-safe
reuse, and teardown are provided by the generic manager; the existing fixed
index-zero reverse-P/Invoke proof remains separate and unchanged.

## 5. Stack bounds

Attach queries the isolated PAL stack-bound adapter before publishing the
runtime record. It rejects malformed intervals and a current SP outside
`[low, high)`. The initial thread uses the exact boot-stack symbol interval on
bare metal; workers use their TCB-owned stack allocation; hosted Windows uses
the private exact maximum-stack query. See
[Native Stack Bounds](../runtime/NATIVE_STACK_BOUNDS.md).

## 6. Initial-thread attachment

The initial attach requires generic local storage, exact bounds, and a valid
current pointer. It creates one record, initializes the transition markers,
sets preemptive mode, leaves allocation context explicitly zero/uninitialized,
publishes the FLS association, and links the record. Duplicate attach is
rejected while the existing record remains intact.

## 7. Worker/helper attachment

The probe uses a normal guideXOS worker rather than the stock finalizer/helper
thread. The worker begins with generic local-storage state, queries its own
stack, attaches to the adapter, verifies isolated lookup against the initial
thread, performs no managed work or allocation, detaches, exits, and is joined.
Multiple workers also receive distinct records and stack intervals. This is
the future helper-thread path's attachment evidence, not a finalizer-thread
implementation.

## 8. Cooperative/preemptive state

The minimal startup-ready state is `preemptive=true`. The thread is attached
but is not claimed to be safe for collector suspension or managed transition
execution. The allocation-context field is zero and explicitly unsupported in
this probe. Reverse-P/Invoke and later allocation require the exact NativeAOT
transition/context helpers and are outside this lifecycle proof.

## 9. Transition frames

The record starts with the NativeAOT-style top-of-stack sentinel in both the
current and deferred transition-frame fields. No worker attach leaves a live
frame. Detach rejects a record with a live transition frame. The adapter keeps
the existing fixed reverse-P/Invoke frame representation untouched; this probe
does not weaken its push/restore validation.

## 10. Current-thread lookup

Lookup reads the current native thread's FLS value and validates ownership,
attachment, generation, and registry membership. An initial thread cannot
resolve a worker record and a worker cannot resolve the initial record. After
detach, lookup returns null/failure and no stale record survives TCB or registry
reuse.

## 11. Detach ordering

The explicit worker order is:

```text
validate current ownership and transition state
  -> clear runtime-thread FLS association
  -> unlink and retire runtime record
  -> generic local-storage detach callbacks
  -> publish native worker result
  -> signal native completion Event
  -> join/reclaim TCB and stack
```

For the callback path, generic local-storage clears values before invoking the
registered callback. The callback receives the still-live runtime-record
pointer, retires it exactly once, and leaves the current stack valid until the
generic detach returns. Thus the runtime record exists while its callback needs
it, no callback observes a reused TCB, and ThreadStore contains no detached
thread before native reclamation.

## 12. ThreadStore shutdown

Shutdown is explicit and application-scoped. It rejects shutdown while any
runtime thread remains attached, frees the dynamic FLS index after the registry
is empty, and rejects a second shutdown. The probe verifies shutdown with an
attached worker fails, then detaches the initial record and shuts down cleanly.
No collection, suspension, or process-lifetime finalizer policy is implied.

## 13. Startup-safe enumeration

The adapter maintains a bounded linked registry and an attached count under a
mutex. This supports startup diagnostics and count checks. It is not a
suspension-safe snapshot: there is no stop-the-world handshake, hijacking,
safe-point rendezvous, or collector enumeration guarantee.

## 14. Collection-time features still missing

The following remain outside this pass: collector-safe thread suspension and
resumption, context capture/redirection, hijacking, precise stack-map root
walking, GC-owned virtual memory, GC event/critical-section startup wiring,
write barriers/card tables, module/type-manager publication, finalizer/helper
thread startup, allocation contexts, handle-manager entry, and clean live GC
shutdown.

## 15. Inactive adapter probe

`runtime/tests/guidexos_nativeaot_threadstore_adapter_probe.cpp`, run by
`scripts/smoke-native-stack-bounds.ps1`, uses the real generic local-storage,
FLS, stack-bound PAL, and ThreadStore adapter entry points. It covers initial
and worker attach, duplicate attach, lookup isolation, invalid bounds,
callback detach, multiple-worker count, generation reuse, stale-state clearing,
detach ordering, attached-thread shutdown rejection, and double-shutdown
rejection. It prints `RhInitialize called: no`, `GC initialized: no`, `Finalizer
thread started: no`, `Collections entered: 0`, and `GC-backed allocations: 0`.

## 16. Updated readiness status

The stack PAL and minimal ThreadStore rows now pass: global initialization,
initial and worker attachment, current lookup, transition-frame readiness,
detach, FLS ordering, and startup-safe registry/count checks. The existing
fixed reverse-P/Invoke and no-collection proofs remain the active proof path.
Collection-safe suspension/enumeration remains blocked and is not required for
this attachment-only readiness step.

## 17. Exact next blocker or dry-run authorization

The gate does not authorize `RhInitialize` yet. Exactly one next mandatory
blocker remains: **NativeAOT GC-owned virtual-memory PAL integration** for the
source-order Workstation initialization and heap segment contract. The next
experiment is to implement and independently prove that bounded reserve,
commit, decommit, release, and timing integration outside live GC startup.
