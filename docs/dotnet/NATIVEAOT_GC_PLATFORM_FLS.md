# NativeAOT GC Platform FLS Mapping

Status: the bounded dynamic local-storage prerequisite is implemented and
independently probed. NativeAOT `RhInitialize` remains intentionally gated.

## 1. Matching NativeAOT FLS requirements

The locked .NET 9.0 NativeAOT Workstation source requires a PAL-managed index
with allocation, release, per-thread get/set, and detach callback delivery.
The callback type is a `void*` payload callback. Failure from allocation is
represented by the out-of-indexes sentinel; get/set operate on the current
thread's value for the allocated index.

## 2. Source call sites

The matching extracted source has one startup allocation call:

```text
Runtime/windows/PalRedhawkMinWin.cpp
  PalInit -> g_flsIndex = FlsAlloc(FiberDetachCallback)
```

The same source uses `FlsGetValue`/`FlsSetValue` in `PalAttachThread` and
`PalDetachThread`. `FiberDetachCallback` receives the value, checks it against
the current FLS value, and calls `RuntimeThreadShutdown` for a non-null value.
The relevant cleanup reaches `ThreadStore::DetachCurrentThread`.

## 3. Startup allocation order

`PalInit` allocates the callback-bearing index before `RuntimeInstance` and
`ThreadStore` initialization. The initial NativeAOT thread is attached later
through the PAL attach path; startup must not assume the fixed proof index is a
valid substitute for the dynamic allocation contract.

## 4. Required callback semantics

The callback must run once for a non-null value when the owning thread/fiber is
detached, and it must not lose the value before cleanup receives it. Index
release must not leave stale per-thread values behind. The generic manager
clears all cells before invoking callbacks and reports a callback's attempted
repopulation as `Busy`/`CallbackFailed`, giving this branch deterministic
bounded behavior for the stock cleanup callback.

## 5. Mapping to generic guideXOS local storage

`LocalStorageIndex` maps a slot plus generation to a numeric adapter index.
`LocalStorageContext` is the per-thread value vector. The platform hooks map
the current scheduler TCB to that context on bare metal; hosted tests use a
thread-local context. The adapter translates the generic result values to the
NativeAOT-shaped sentinel and boolean get/set behavior.

## 6. Initial-thread behavior

The adapter probe initializes the manager, attaches the initial thread,
allocates two indices, and verifies set/get values before detaching. The
initial thread's values remain isolated from worker values and are cleared by
its explicit detach.

## 7. Helper-thread behavior

The generic worker wrapper attaches before invoking an entry and detaches after
return. A newly attached helper/worker context starts null. The adapter probe
verifies that a worker can set its own values and that its callback observes
the worker value without changing the initial thread's values. This is only a
primitive lifecycle probe; it is not proof that NativeAOT's finalizer helper
thread can start.

## 8. Thread detach behavior

Normal worker exit runs detach before the host/scheduler slot is published as
reusable. Callbacks receive the staged non-null values exactly once. Bare-metal
non-current teardown has a separate context API and must use callbacks that do
not depend on current-thread identity.

## 9. Index release behavior

The adapter's numeric index is released only after all contexts have cleared
that slot. The generic generation advances on release, and the adapter records
the next generation handle on reuse. A stale generic handle is rejected even
when its numeric slot has been reused; a released NativeAOT-shaped index is
returned to the bounded pool.

## 10. Inactive adapter probe

`runtime/tests/guidexos_nativeaot_fls_adapter_probe.cpp` calls the C adapter
entry points directly. It covers initial values, two-index allocation, worker
isolation, callback value/count, detach, release/reuse, stale-index rejection,
and shutdown. It contains no `RhInitialize`, GC initialization, finalizer, or
collection call and is not linked into the live NativeAOT proof image.

## 11. Updated startup-readiness status

The FLS row is now independently PASS for the generic manager, hosted tests,
bare-metal build, opt-in QEMU test, and inactive adapter probe. The fixed-index
proof path remains separately compatible. This does not change any startup
`entered` field: the readiness audit still stops before `RhInitialize`.

## 12. Exact next blocker or dry-run authorization status

This branch is Outcome B. The next mandatory blocker is:

```text
NativeAOT ThreadStore attachment and exact stack-bound PAL contract
```

No NativeAOT startup-and-shutdown dry run is authorized by the FLS result
alone. The next experiment is to implement and independently prove only that
ThreadStore/thread registration and stack-bound contract, then rerun the full
readiness gate. `RhInitialize` remains uncalled in this pass.
