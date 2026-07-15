# Native guideXOS Event Primitive

## Purpose

`gxos::runtime::Event` is a runtime-neutral native synchronization object for
Server subsystems, native applications, kernel/runtime services, IPC, process
waiting, and future language-runtime adapters. It deliberately exposes
guideXOS-owned names and result values rather than host API constants or host
handle types.

The implementation is a new small synchronization object. Existing condition
variables and mailboxes are private to their owning subsystems, and the
existing semaphore-like facilities do not provide the required event state
semantics. Reusing them would either expose unrelated ownership or turn an
event into a counting semaphore.

## Synchronization capability inventory

| Facility | Source path | Hosted | Bare metal | Timeout support | Reusable for events? |
| -------- | ----------- | -----: | ---------: | --------------: | -------------------: |
| Spinlocks / atomic state | `allocator.*`, subsystem-local code | Yes | Partial | No | No; useful only for short state protection |
| Mutexes / locks | `scheduler.*`, `process.*`, `ipc.*`, `ipc_bus.*` | Yes | No generic object found | No generic contract | No; ownership and wake semantics differ |
| Critical sections | subsystem-local hosted locks | Yes | No generic scheduler-facing wrapper | No | Only as a future bare-metal event hook |
| Semaphores | No generic reusable Server semaphore found | N/A | N/A | N/A | No existing primitive to reuse |
| Wait queues | No generic wait queue found; process table is a stub | No | No | No | No |
| Condition variables | `scheduler.cpp`, `process.cpp`, `ipc.h`, `ipc_bus.cpp` | Yes | No | Local finite waits | No; all are private subsystem protocols |
| Sleeping | `scheduler.cpp`, `ipc_bus.cpp`, compositor paths | Yes | `hlt` idle loop | Local only | No; polling is not event blocking |
| Yielding | scattered hosted scheduler/subsystem paths | Yes | No scheduler yield contract | No | No |
| Timers | `kernel/core/pit.cpp`, compositor Win32 timer paths | Yes | Periodic PIT ticks only | No generic wake registration | Not yet |
| Monotonic clocks | `std::chrono::steady_clock`, `kernel/core/pit.cpp` | Yes | Tick counter | Local | Clock source only |
| Thread wakeups | `std::condition_variable` notifications | Yes | No block/unblock scheduler API | No generic contract | Hosted mechanism only |
| Process wait | `process.cpp` completion condition variable | Yes | Process table stub | Yes, local | No; process lifecycle differs |
| IPC waits | `ipc.cpp`, `ipc_bus.cpp` mailbox/channel CVs | Yes | No generic blocking path | Yes, local | No; message ownership differs |
| Hosted wrappers | Private scheduler/process/IPC classes | Yes | N/A | Local | No generic event wrapper existed |
| Bare-metal scheduler integration | `kernel/core/process.cpp`, `kernel/arch/amd64/context_switch.cpp` | N/A | Context switch and PIT only | No | Missing wait queue and timer wake foundation |

The audit therefore selects a new event object for hosted semantics and an
explicit scheduler-hook boundary for bare metal. The kernel currently cannot
honestly provide a blocking event wait: `schedule()` halts, the timer tick has
no wait registration, and the architecture timer path has no scheduler wakeup
integration.

## Manual-reset semantics

An event is created with `EventMode::ManualReset` and an initial state. `signal`
sets the state to signaled and wakes all current waiters. The signaled state is
retained for current and future waiters until `reset` sets it to nonsignaled.
Signaling an already signaled event and resetting an already nonsignaled event
are harmless successful operations.

If `reset` wins the event-state lock before a waiter observes the signal, that
waiter does not succeed. A waiter that has already observed and returned
`Signaled` is not revoked by a later reset.

## Auto-reset semantics

An event is created with `EventMode::AutoReset` and an initial state. `signal`
sets one pending signaled bit and wakes at most one waiter. A successful waiter
consumes the bit and returns the event to nonsignaled. If no waiter exists, the
bit is retained for one future successful wait.

The state is a bit, not a count. Repeated `signal` calls while it is already
signaled do not create an unbounded pending signal count. One additional signal
after one successful consumption can release one additional waiter.

## Wait result model

The public result type is:

```cpp
enum class WaitResult {
    Signaled,
    TimedOut,
    Invalid,
    Interrupted,
    Destroyed
};
```

`Invalid` covers an uninitialized, closed, or malformed-timeout operation.
`Destroyed` is returned to hosted waiters released by `close`. `Interrupted` is
reserved for a future bare-metal scheduler hook that reports interruption;
the hosted condition-variable implementation does not manufacture it.

## Timeout semantics

`WaitTimeout::infinite()` is the only indefinite wait representation. A zero
timeout is a poll. Finite values use the hosted `std::chrono::steady_clock`
and never wall-clock time. Negative signed millisecond input and durations that
overflow the implementation's signed monotonic duration are invalid.

For a finite wait, the deadline is computed once. Every condition-variable
wakeup rechecks the event predicate while holding the event state lock, so
spurious wakeups cannot report success or extend timeout accounting. At the
timeout boundary, whichever operation acquires that lock first determines the
result: a signal already visible there is consumed/reported; a later signal
remains pending for a future wait.

Hosted timeout resolution is the resolution and scheduling behavior of
`steady_clock` plus the native condition-variable implementation. The event
does not promise millisecond-level precision. Bare-metal timeout resolution is
not implemented by the current kernel; a future scheduler hook must use the
kernel's monotonic timer and preserve the same absolute-deadline rule.

## Hosted implementation

Each initialized event owns a private state containing its mode, signaled bit,
destroyed bit, mutex, and condition variable. The public `Event` retains the
state through `std::shared_ptr`. Lifecycle locking separates publication and
closure of the state from state transitions. Waiters retain their state object
while blocked, so hosted close can mark it destroyed and notify all without a
use-after-free.

Manual-reset signaling uses `notify_all`; auto-reset signaling uses
`notify_one` only when the pending bit transitions from false to true. No
unrelated callback is invoked while the state mutex is held. The condition
variable predicate is checked in a loop, and cleanup is deterministic.

## Bare-metal implementation

The bare-metal build has the same public modes, state transitions, timeout
validation, and zero-timeout polling. It has no C++ standard-library dependency.
It accepts an explicit `EventSchedulerHooks` set for critical-section entry,
critical-section exit, blocking, one-waiter wakeup, and all-waiter wakeup.

With no installed block hook, a non-zero wait returns `Invalid`; it never falls
back to polling or busy-spinning. `signal` and `reset` remain usable for state
operations and polls. Destruction with an active bare-metal waiter is a
contract violation: `close` returns `Invalid`, and the destructor asserts/traps
because the current scheduler cannot wake and unregister that waiter safely.

This is compile/state validation only in this pass. The present bare-metal
kernel has no generic wait queue, runnable-state transition, wake-one/wake-all
operation, or timer-to-wait registration. Therefore no bare-metal runtime wait
success is claimed.

## Scheduler integration

The future scheduler hook contract must perform the following sequence while
preserving the event's state lock/critical-section protocol:

1. Enter the scheduler's short critical section.
2. Check the event state and enqueue the current waiter atomically with respect
   to `signal`, `reset`, `close`, and timeout registration.
3. Release the event critical section before blocking the thread.
4. Make the thread non-runnable and reschedule.
5. Wake one or all matching waiters from the event signal operation.
6. Remove the waiter from the queue exactly once on signal, timeout,
   interruption, or destruction.

The event object does not assume FIFO ordering or fairness. The scheduler owns
waiter ordering. The hook must use an absolute monotonic deadline internally so
repeated internal wakeups cannot extend a finite wait. Wait-many is outside
this object and remains a later generic capability.

## State transitions

| Operation | Manual reset | Auto reset |
| --------- | ------------ | ---------- |
| Initial nonsignaled | `N -> N` | `N -> N` |
| Initial signaled | `S -> S` | `S -> S` until one successful wait |
| `signal` from N | `N -> S`, wake all | `N -> S`, wake one |
| `signal` from S | `S -> S`, no extra state | `S -> S`, no count increment |
| Successful wait | `S -> S` | `S -> N` |
| `reset` | `S -> N` or `N -> N` | `S -> N` or `N -> N` |
| `close` | destroyed, wake hosted waiters | destroyed, wake hosted waiters |

All transitions are serialized by the hosted state mutex or the bare-metal
critical-section hook. Lifecycle publication/closure is serialized by the
hosted lifecycle mutex; a hosted waiter retains its state shared pointer after
publication.

## Race analysis

The bounded native tests cover signal before wait for both modes, wait before
signal, signal at the finite-timeout boundary, reset while a waiter is pending,
four manual waiters released by one signal, multiple auto waiters released one
per signal, repeated signal calls, and close with an active hosted waiter.

The event lock is the single serialization point for signaled/destroyed state
and auto-reset consumption. A signal that wins the lock before timeout is a
success; a signal that arrives after the timeout decision remains pending for
auto reset and remains visible for manual reset. `close` marks destroyed before
notification, so active hosted waiters return `Destroyed` and do not inspect a
freed object. `reset` cannot revoke a waiter that already consumed and returned
success.

## Lifetime and cleanup

`Event()` creates an uninitialized object. `initialize` is one-shot; the object
is non-copyable and non-movable. The mode and initial state are fixed at
initialization. `close` is the explicit destruction operation and is also
attempted by the destructor.

Hosted close is safe with active waiters: it detaches the lifecycle state,
marks that state destroyed, notifies all, and lets waiters finish from their
retained shared state. New operations after close are invalid. A closed object
cannot be reinitialized without reconstructing the `Event` object.

Bare-metal close requires caller-established waiter quiescence. It returns
`Invalid` while a waiter is active, and destruction asserts/traps under that
violation. This prevents silent use-after-free until the generic scheduler
wait-queue contract exists. Native adapter destruction follows the same owner
contract.

## Test coverage

`runtime/tests/guidexos_event_tests.cpp` provides bounded, runtime-neutral
hosted tests for initial states, persistence, reset, auto-reset one-bit
behavior, multi-waiter release, zero/small/long finite waits, signal-before-
timeout, timeout reuse, reset races, timeout-boundary races, and repeated
create/close cleanup. A live-state counter is test-only instrumentation for
bounded leak checking.

`runtime/tests/guidexos_event_adapter_probe.cpp` compiles and links the
inactive NativeAOT adapter, then creates, signals, resets, polls, and destroys
both event modes. It performs no runtime initialization or collection.

`runtime/tests/guidexos_event_baremetal_compile_probe.cpp` validates the
freestanding header/source contract and verifies that a non-zero wait is
reported unavailable without scheduler hooks.

## Known limitations

- Bare-metal indefinite and timed blocking are blocked by the missing generic
  scheduler wait queue and wakeup integration.
- Bare-metal timeout resolution and timer registration are not implemented.
- Wait-many is not part of the primitive.
- There is no fairness guarantee.
- The hosted diagnostic live-state counter is for tests only.
- Destruction with active bare-metal waiters remains prohibited/asserted.

## Future generic uses

The abstraction is suitable for native Server services, native applications
when exposed through an appropriate ABI, process completion, IPC readiness,
kernel/runtime services, and other language-runtime adapters. Those users must
preserve the event lifetime contract and must not reinterpret an auto-reset
event as a counting semaphore.

## NativeAOT adapter mapping

The isolated runtime-pack adapter in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_event_adapter.*`
owns a native `Event` inside an opaque `EventHandle`. It maps runtime-internal
create-auto, create-manual, set, reset, wait-indefinitely, finite-millisecond
wait, and destroy operations to this generic object. It uses native wrapper
allocation and has no host API names or numeric constants. The adapter probe is
opt-in and inactive with respect to GC startup, finalization, and collection.
