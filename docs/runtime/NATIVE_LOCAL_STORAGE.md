# guideXOS Runtime-Neutral Local Storage

Status: implemented and independently tested on hosted builds; wired into the
bare-metal native-thread/TCB lifecycle. This facility is generic runtime
infrastructure. The NativeAOT-facing adapter is a separate opt-in layer under
`tools/dotnet/runtime-pack`.

## 1. Purpose

The manager supplies a small, deterministic, per-thread pointer table for
runtime components that need thread-local state with explicit attach, detach,
allocation, release, and teardown behavior. It is bounded so exhaustion,
shutdown, and stale handles are observable rather than hidden behind an
unbounded allocator.

## 2. Existing TLS/FLS state

The existing NativeAOT proof platform remains unchanged: it reserves fixed
index 0 and eight fixed cells in the existing TLS block. That compatibility
path is still used by the no-collection proof and is not converted to dynamic
allocation. The new manager owns a separate opaque `LocalStorageContext` and
does not add NativeAOT-specific fields to a generic TCB.

## 3. Generic API

The public API is in
`runtime/local_storage/guidexos_local_storage.h`:

* `initializeLocalStorage` / `shutdownLocalStorage`;
* `attachLocalStorage` / `detachLocalStorage`;
* `allocateLocalStorageIndex` / `releaseLocalStorageIndex`;
* `setLocalStorageValue` / `getLocalStorageValue`;
* `detachLocalStorageContext` for a context being torn down by its owner;
* `forceClearLocalStorageContext` for a last-resort TCB reclaim path.

Indices are opaque `{ slot, generation }` values. Callers provide an optional
`LocalStorageDetachCallback` at allocation time.

## 4. Index capacity

There are eight dynamic slots (`kLocalStorageCapacity = 8`) and at most 32
registered contexts (`kLocalStorageMaximumContexts = 32`). Each context has
eight pointer cells. Allocation returns `Exhausted` when all slots are active;
registration returns `Exhausted` when the context bound is full.

## 5. Index generations

Each slot has a nonzero generation. Release increments the generation and
clears the callback. A previously returned handle then fails validation as
`StaleIndex`, even if the same numeric slot is reused. Shutdown advances every
slot generation before a later initialize, preventing a pre-shutdown handle
from becoming valid accidentally.

## 6. Per-thread storage

An attached context contains only eight pointer values. Hosted threads use a
thread-local context; bare-metal `KernelThread` contains the same generic
context object. Values are never shared between contexts, and all cells are
cleared before a context is unregistered.

## 7. Initial-thread registration

The bootstrap/initial thread explicitly calls `attachLocalStorage` after the
manager is initialized and before it allocates or publishes runtime values.
An already attached, registered context is idempotent. The manager does not
implicitly attach a thread as a side effect of get/set.

## 8. Worker-thread registration

Hosted `guidexos_native_thread` attaches a worker before invoking its entry
function and detaches it after the entry returns. Bare-metal
`native_entry_dispatch` performs the same lifecycle around the native entry.
If attachment fails, the user entry is not run. A worker therefore starts with
null values, including when its scheduler slot or host thread identity is
reused.

## 9. Get/set semantics

Get and set require an initialized manager, an attached current context, and a
currently active generation-valid index. Get writes null to its output on
failure. Normal get/set do not allocate memory, acquire the hosted metadata
mutex, wait, or invoke callbacks. Context values and slot metadata use
acquire/release accesses on hosted builds. Index release and manager shutdown
are lifecycle-quiescent operations; callers must not race them with ordinary
get/set. During detach they return `Busy`; a set attempted by a detach callback
additionally records callback failure for the detach result.

## 10. Detach callbacks

Detach snapshots every active callback-bearing non-null value, clears all
context cells first, then invokes callbacks. A callback is invoked at most once
for each non-null cell in that detach. Callback failure is reported as
`CallbackFailed`; callback code cannot repopulate the context through the
manager during the teardown window.

## 11. Callback iteration rules

The manager scans slots in ascending slot order. This makes callback order
stable for tests and diagnostics. All values are cleared before the first
callback, so callbacks cannot observe another cell's still-live value through
the manager. There is no repeated callback pass when a callback attempts to
set a value. The generic policy is intentionally deterministic; consumers must
not depend on a platform-specific unspecified order.

## 12. Index release

Release clears every registered context cell at that index and invokes the
registered callback once for each non-null value, in ascending context-table
order. The cells are cleared before the first callback. The slot is then
invalidated and its generation advanced, even when a callback attempted a
manager set and the result is reported as `CallbackFailed`. Release is a
bounded, quiescent lifecycle operation; a stale, invalid, uninitialized, or
teardown-time release is rejected. A release callback is not required to run
on the context that originally owned the value, so it must not assume current
thread identity.

## 13. Thread exit integration

Normal thread exit detaches before publishing the exit result or making the
thread slot reusable. This ordering delivers callbacks while the context is
still valid and prevents a future worker from inheriting values. Bare-metal
non-current teardown uses `detachLocalStorageContext` before scheduler slot
reclaim; callbacks in that path execute on the current scheduler context and
must not assume they are running as the context owner.

## 14. TCB reuse

`KernelThread` owns a generic context, and `reclaim_slot` invokes the no-
callback `forceClearLocalStorageContext` guard before architectural destruction.
The normal exit path has already delivered callbacks. The guard makes stale
values impossible even if an earlier teardown path failed to unregister the
context. Reused slots therefore observe null values and new generation
handles, not the prior occupant's state.

## 15. Runtime shutdown

Shutdown succeeds only after all contexts have detached and all indices have
been released. It returns `Busy` otherwise, advances generations, clears slot
metadata, and marks the manager uninitialized. No live startup, collector,
finalizer, or collection path is part of this shutdown operation.

## 16. Hosted implementation

Hosted allocation, attach, release, and shutdown transitions use
`std::mutex g_metadataMutex`; the lock is not held while callbacks execute.
Ordinary get/set are lock-free with acquire/release metadata and pointer-cell
accesses, subject to the quiescent release/shutdown contract. Thread-local
contexts make worker isolation direct and allow the tests to exercise the same
attach/detach API as the runtime. The generic implementation does not use
native platform TLS/FLS.

## 17. Bare-metal implementation

Bare-metal builds compile without the C++ standard library. Platform hooks
map the current scheduler TCB to its generic context and attached bit. The
manager is used from serialized scheduler lifecycle code, not interrupt
handlers; ISR-side local-storage operations are outside this contract. TCB
termination detaches before reclaim and retains a force-clear safety guard.

## 18. Test coverage

`runtime/tests/guidexos_local_storage_tests.cpp` covers initialization,
allocation, exhaustion, generation-safe reuse, get/set isolation, callback
value and order, callback repopulation, index-release callbacks, worker and
detached-worker attach/detach, TCB reuse, and shutdown. The NativeAOT-shaped
adapter probe covers the same release/reuse and stale-index behavior.
`scripts/smoke-native-local-storage.ps1` builds/runs the hosted and adapter
probes and compiles/builds the bare-metal path. The opt-in QEMU test is
`kernel/core/native_local_storage_qemu_test.cpp`, driven by
`scripts/smoke-native-local-storage-qemu.ps1`; its final guest run also checks
detached-thread cleanup, process/runtime teardown, and the leak marker.

## 19. Known limitations

The capacity is intentionally eight dynamic indices and 32 contexts. Values
are opaque pointers, not owning references. Release callbacks may execute
outside the original owner context. The non-current bare-metal teardown helper
runs callbacks on the current scheduler execution context. There is no
fiber-specific context migration, wait-free concurrent bare-metal metadata
protocol, or implicit attachment. These are bounded primitive semantics, not a
general platform FLS emulation layer.

## 20. Future generic consumers

Potential consumers include scheduler diagnostics, native runtime bookkeeping,
event wait metadata, loader state, and future collector adapters. They should
allocate an index during their own initialization, release it only after all
contexts are detached, and treat `Busy`, `StaleIndex`, `Exhausted`, and
`CallbackFailed` as explicit lifecycle results. NativeAOT should continue to
use the separate adapter until the next startup gate proves the full PAL and
ThreadStore contracts.
