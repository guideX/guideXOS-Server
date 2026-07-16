# NativeAOT GC Platform Events

## Completed generic scheduler-backed event status

The runtime-neutral scheduler wait foundation is now implemented under
`runtime/synchronization/guidexos_scheduler_wait.*`. The AMD64 freestanding
kernel provides TCB-owned wait nodes, FIFO wake-one/wake-all, absolute PIT
deadlines, signal-versus-timeout single completion, cancellation, and thread/
object teardown unlinking. The generic event consumes that contract without
adding runtime-specific scheduler knowledge. Deterministic bare-metal event
runtime tests pass; the inactive adapter compile/link/run probe also passes.
A booted QEMU concurrent test remains a harness limitation.

## GC event requirements

The selected NativeAOT Workstation GC source and runtime-pack baseline require
runtime-internal event operations for both auto-reset and manual-reset events:

- create an initially signaled or nonsignaled event;
- set and reset it;
- wait one indefinitely or with a finite timeout;
- close/destroy it deterministically;
- preserve an auto-reset pending signal as one bit, not an unbounded count.

The local source analysis shows `g_FinalizerEvent` is auto-reset and
`g_FinalizerDoneEvent` is manual-reset. A later low-memory notification path can
combine a finalizer wait with another event using wait-many.

## Mapping to the generic guideXOS event

The inactive adapter at
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_event_adapter.*`
maps the collector-facing concepts to `gxos::runtime::Event`:

| Runtime-pack operation | guideXOS operation |
| ---------------------- | ----------------- |
| create auto-reset event | `Event(EventMode::AutoReset, initial)` |
| create manual-reset event | `Event(EventMode::ManualReset, initial)` |
| set event | `Event::signal()` |
| reset event | `Event::reset()` |
| wait indefinitely | `Event::wait(WaitTimeout::infinite())` |
| wait with timeout | `Event::wait(WaitTimeout::signedMilliseconds(...))` |
| destroy event | `Event::close()` followed by native wrapper deletion |

The generic Server source does not export functions named after host event or
wait APIs. Only the isolated runtime-pack adapter owns runtime-internal names,
and it calls the generic event underneath.

## Required event modes

The event mode is fixed at construction. Auto-reset is required for the
finalizer request signal because each request wakes at most one helper waiter
and a pending request is retained when the helper is not waiting. Manual-reset
is required for a completion/done event that remains visible to all waiters
until reset.

These mappings are declarations and a non-operational compile/link probe only.
They do not change the current no-collection managed heap or select a live GC
platform implementation.

## Timeout conversion

The adapter receives signed millisecond values. Negative values are rejected as
`WaitResult::Invalid`; zero is a poll; positive values convert to an explicitly
finite monotonic duration with overflow validation. Indefinite waits use the
explicit `WaitTimeout::infinite()` value. The hosted implementation uses
`steady_clock`; bare metal converts the duration to an absolute monotonic PIT
scheduler deadline rather than wall-clock time or a polling loop.

The bounded adapter exposes non-alertable semantics: there is no callback,
managed exception, or asynchronous interruption in this pass. If the runtime
pack later requires alertable waits, that must be added as an isolated adapter
contract after the generic scheduler can represent interruption safely.

## Lifetime ownership

The GC/runtime platform layer owns each opaque `EventHandle`; the handle owns a
native guideXOS `Event`. Wrapper allocation uses native `new (std::nothrow)`.
Destroy first closes the event and then releases the wrapper. Hosted close wakes
active waiters with `Destroyed` while their shared state remains alive. The
bare-metal owner may close with active waiters: the scheduler-backed event
marks the object destroyed, wakes all with `Destroyed`, and removes queue/timer
links before returning. New operations remain invalid. This is proven for the
single-CPU contract; SMP lifetime synchronization remains future work.

The stock GC source uses long-lived event objects in some paths and deliberately
avoids destructors. That source-specific policy does not weaken the generic
event lifetime contract: each adapter-owned handle still has an explicit owner,
and any intentional process-lifetime object must remain reachable until its
waiters are quiescent.

## Wait-many status

Wait-many is not implemented in the generic event or adapter. The local source
analysis found an optional finalizer wait path that can wait on the finalizer
event and a low-memory event together. It is not required for this event
compile/link probe and is recorded as a later generic wait-set capability. No
single-event implementation should claim to satisfy that path.

## Finalizer-helper implications

The current pass does not start the finalizer helper and does not call GC
initialization. In the eventual initialization experiment, the source mapping
expects the helper request event to be auto-reset and the done event to be
manual-reset. The helper's thread creation, thread-store registration, process
cleanup, managed attach, and any wait-many path remain separate missing
capabilities. The generic start/join lifecycle and inactive adapter probe are
now documented in [NativeAOT GC Platform Threads](NATIVEAOT_GC_PLATFORM_THREADS.md).
This event work must not be used as evidence that the full Workstation GC is
ready.

## Adapter compile/link probe

`runtime/tests/guidexos_event_adapter_probe.cpp` is an opt-in native probe. It
creates an auto-reset adapter, verifies zero-timeout nonsignaled/signal/
consume behavior, creates a manual-reset adapter, verifies persistence and
reset, and destroys both. It has no call to GC startup, finalization startup,
collection, managed allocation, or a live runtime entry point.

The native smoke script reports the adapter compile/link/run independently of
the generic event tests. The probe is an inactive mapping check, not a GC
initialization test.

## Remaining GC initialization blockers

The event abstraction does not remove the remaining runtime-pack gaps:

- wait-many/wait-set support for optional GC waits;
- booted runtime validation of the scheduler-backed event path;
- critical-section and lock semantics expected by the collector;
- virtual-memory and allocation policy integration;
- FLS/thread-store behavior;
- managed ThreadStore attachment and the mandatory finalizer-helper lifecycle;
- exact EE/GC platform ABI wiring and process cleanup.

The current managed runtime remains the fixed no-collection heap, and no GC
startup or collection occurs in this pass.

## Exact next primitive

The next independently testable NativeAOT-facing primitive is the bounded
virtual-memory reserve/commit/decommit/release layer needed by Workstation GC
segment setup. Do not initialize the Workstation GC, start its finalizer helper,
or trigger collection while proving that memory layer. The generic thread
start/join follow-up is documented separately and remains inactive with
respect to GC startup.
