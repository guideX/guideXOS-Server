# NativeAOT GC Platform Events

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
`steady_clock`, and a future bare-metal implementation must convert the
duration to an absolute monotonic scheduler deadline rather than wall-clock
time or a polling loop.

The bounded adapter exposes non-alertable semantics: there is no callback,
managed exception, or asynchronous interruption in this pass. If the runtime
pack later requires alertable waits, that must be added as an isolated adapter
contract after the generic scheduler can represent interruption safely.

## Lifetime ownership

The GC/runtime platform layer owns each opaque `EventHandle`; the handle owns a
native guideXOS `Event`. Wrapper allocation uses native `new (std::nothrow)`.
Destroy first closes the event and then releases the wrapper. Hosted close wakes
active waiters with `Destroyed` while their shared state remains alive. The
bare-metal owner must establish waiter quiescence before destruction because
the current scheduler cannot unregister active waiters safely.

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
cleanup, and any wait-many path remain separate missing capabilities. This
event work must not be used as evidence that the full Workstation GC is ready.

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

- generic bare-metal wait queue, wake-one/wake-all, and timer wakeup;
- wait-many/wait-set support for optional GC waits;
- critical-section and lock semantics expected by the collector;
- virtual-memory and allocation policy integration;
- FLS/thread-store behavior;
- native thread-start/join and mandatory finalizer-helper lifecycle;
- exact EE/GC platform ABI wiring and process cleanup.

The current managed runtime remains the fixed no-collection heap, and no GC
startup or collection occurs in this pass.

## Exact next experiment

Because the generic bare-metal scheduler foundation is missing, the next
bounded experiment is to implement and independently validate the smallest
generic scheduler wait-queue plus monotonic timer wakeup capability. Once that
foundation exists, complete bare-metal event timeout/indefinite waits and
waiter cleanup. Only after those generic primitives pass should the isolated
NativeAOT platform layer be compiled against them again. Do not initialize the
Workstation GC in that experiment; the subsequent GC milestone remains a
non-operational link probe until thread lifecycle and the other platform gaps
are resolved.
