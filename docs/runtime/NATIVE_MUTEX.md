# Runtime-Neutral Mutex and Critical Section

## 1. Purpose

This document defines the guideXOS runtime-neutral mutex used by future platform adapters that need ownership, recursion, blocking acquisition, and nonblocking acquisition. It is a synchronization object, not a replacement for the scheduler's internal critical section or for subsystem-local spin/atomic protocols.

The implementation is available in hosted builds and in the single-CPU `GXOS_BARE_METAL` build. It does not initialize a collector, start a finalizer, allocate managed objects, or trigger collection.

## 2. Existing locking facilities

The repository already contains several deliberately scoped mechanisms:

- `scheduler_wait::enterCritical()` / `leaveCritical()` protect scheduler state, timer links, runnable queues, and wait-queue mutation by nesting a single-CPU interrupt-disabled critical section.
- `scheduler_wait::WaitQueue` and the one reusable `WaitNode` in each scheduler TCB provide FIFO publication, wake-one/wake-all, timeout cleanup, and thread teardown.
- Hosted scheduler, allocator, IPC, mailbox, and event implementations use private C++ standard-library locks and condition variables.
- Device code contains bounded local polling/spin delays for hardware readiness.

There was no existing reusable mutex object with a portable ownership contract. The new mutex uses the scheduler wait foundation and leaves the existing scheduler critical section, allocator locks, IPC locks, and hardware polling policies unchanged.

## 3. Generic API

The public header is `runtime/synchronization/guidexos_mutex.h` and exposes only fixed-width values, enums, and the opaque `Mutex` object:

    Mutex()
    Mutex(MutexMode)
    initialize(mode) -> bool
    isInitialized() -> bool
    lock() -> MutexResult
    tryLock() -> MutexResult
    unlock() -> MutexResult
    destroy() -> MutexStatus
    notifyOwnerExit() -> MutexStatus

`MutexResult` distinguishes `Acquired`, `Released`, `WouldBlock`, `Invalid`, `Destroyed`, `NotOwner`, `AlreadyOwned`, `RecursionLimit`, and `Interrupted`. `MutexStatus` distinguishes successful destruction, invalid/destroyed state, `Busy`, and the explicit owner-exit diagnostic.

There is intentionally no timed lock, wait-many operation, semaphore, reader/writer mode, priority inheritance, or recursive RAII wrapper in this primitive.

## 4. Ownership identity

Every acquisition is tied to `MutexOwnerIdentity { value, generation }`.

- Hosted builds assign a stable nonzero per-thread value from an atomic sequence; the thread-local identity remains stable for that host thread.
- Bare-metal builds obtain `{tid, generation}` through a platform hook installed by the kernel process scheduler. The TCB itself is not exposed through the generic header.
- The generation prevents a reused bare-metal TCB slot from inheriting the previous occupant's ownership.
- An invalid identity is rejected rather than treated as an anonymous owner.

## 5. Recursive and nonrecursive modes

`MutexMode::NonRecursive` reports `AlreadyOwned` when the owner attempts to acquire the same mutex again. `MutexMode::Recursive` increments a bounded recursion count and requires a matching number of releases.

The maximum recursion count is the explicit portable constant `kMutexMaximumRecursion` (`1024`). Overflow returns `RecursionLimit` without changing ownership. This bounded policy is tested rather than relying on an unbounded host primitive.

The inactive NativeAOT critical-section adapter selects `Recursive`, because the matching `Crst`/`CLRCriticalSection` source wraps a recursive host critical-section primitive. The generic API still retains an explicit nonrecursive mode for components that require self-lock rejection.

## 6. Waiter model

Bare-metal contention embeds one scheduler `WaitQueue` in each mutex. Each TCB contributes its already-owned reusable `WaitNode`; no waiter node is allocated by `lock()`.

Hosted contention uses a private condition variable plus monotonically assigned tickets. The ticket state gives FIFO service and prevents a new contender from stealing a released mutex while an older waiter is present. The condition-variable state is private to the implementation and does not leak host types through the API.

## 7. Acquisition

`lock()` first validates initialization, destruction state, and the current owner identity. A free mutex with no queued waiter is acquired immediately. A recursive owner is handled locally. A nonrecursive self-acquisition fails explicitly.

When another owner holds the mutex, the call publishes the waiter before releasing the state-protection critical section. This publication order closes the signal-versus-park lost-wakeup window.

## 8. Contended parking

Bare-metal `lock()` calls `scheduler_wait::prepareWait()` under the scheduler critical section, releases that section, and calls `scheduler_wait::parkWait()`. The park hook removes the current TCB from the runnable queue and context-switches to another runnable TCB or the existing interrupt-enabled halt path.

The mutex never polls a lock word in a loop and never busy-spins while another owner holds the object. A wake reason other than `Signaled` is returned as interruption/destruction and does not fabricate ownership.

Hosted `lock()` waits on its condition variable in a predicate loop while holding the private state mutex. Spurious wakeups do not grant ownership.

## 9. Ownership transfer

The bare-metal unlock path performs direct handoff. Before waking the FIFO head, it copies that waiter's opaque owner token into the mutex owner fields and sets recursion to one. The scheduler wait node carries the same token only as uninterpreted data; the scheduler does not know mutex semantics.

The woken waiter therefore verifies that the mutex already names its identity and returns `Acquired`. A newly arriving contender cannot steal the handoff between wakeup and waiter resumption.

Hosted ticket waiters use the same logical rule: the next serving ticket is the only waiter permitted to acquire after the owner releases.

## 10. Try-lock

`tryLock()` never parks and never waits for a timer. It returns `WouldBlock` when another owner holds the mutex or when queued waiters make immediate acquisition unfair. The current recursive owner may increment recursion; a nonrecursive owner receives `AlreadyOwned`.

On bare metal, `tryLock()` requires the scheduler critical hooks but does not require the full park/context-switch hook set. This makes it suitable for a restricted nonblocking path, subject to the interrupt-context rules below.

## 11. Unlock

`unlock()` validates the current owner. A non-owner receives `NotOwner`; an invalid or destroyed object is not modified. Recursive mode decrements the recursion count until the final release.

The final release either transfers ownership to the FIFO head or clears the owner and leaves the mutex free. Exactly one bare-metal waiter is woken for a handoff. Hosted waiters are notified after the state transition and recheck their ticket predicate.

## 12. Lifetime and destruction

Initialization is one-shot. Destruction is quiescent:

- `destroy()` returns `Busy` while the mutex is owned or has waiters;
- no waiter is woken by destruction;
- successful destruction marks the object closed and future operations return `Destroyed`;
- a failed `Busy` destruction leaves the mutex usable;
- reinitialization of a closed object is rejected.

This is stricter than assuming that a source-level static critical section is always destroyed safely. Static-lifetime adapters may simply remain live until process teardown; heap-owned adapters must first establish quiescence.

## 13. Thread-exit policy

The mutex does not silently release ownership when a thread exits. A thread that exits while owning a mutex violates its synchronization contract. `notifyOwnerExit()` lets a known owner record that violation and returns `OwnerExitViolation`; it deliberately leaves the mutex locked.

There is no abandoned-owner recovery, recursive owner search, or unsafe forced wake. The hosted and QEMU tests verify that an orphaned owner remains non-acquirable and that destruction remains `Busy`. Recovery, if ever required, belongs to a higher-level ownership protocol with an explicit lease or process teardown boundary.

## 14. Lock ordering

The current single-CPU ordering rule is:

1. scheduler/interrupt critical section (internal rank 1);
2. mutex state and its wait queue (rank 2);
3. subsystem data protected by the mutex (rank 3 or higher).

Code must not block while holding the scheduler critical section or another lower-rank object. A mutex must not be introduced into the scheduler's own ready-queue, TCB, timer, address-space teardown, or interrupt-dispatch lock ordering without a reviewed rank assignment.

## 15. Interrupt-context restrictions

Blocking `lock()` is prohibited in interrupt context and while the caller has entered the scheduler's interrupt-disabled critical section. The caller must be ordinary thread context with the full scheduler wait contract installed.

`tryLock()` is the only permitted mutex operation in a restricted path, and only when its state-protection critical hook is valid and the caller can tolerate `WouldBlock`. Interrupt handlers must not call `unlock()` for a mutex owned by another execution context.

## 16. Hosted implementation

`guidexos_mutex.cpp` stores a private state containing `std::mutex`, `std::condition_variable`, owner identity, recursion, and ticket counters. The state is allocated once during initialization; lock and unlock do not allocate or free waiter storage.

Lifecycle protection uses a separate private mutex so a caller can safely obtain a state snapshot while another caller attempts a quiescent destroy. Destruction marks the state destroyed only after confirming no owner and no waiter.

## 17. Bare-metal implementation

`guidexos_mutex_baremetal.cpp` stores all state inline: mode, owner, recursion, a destroyed/diagnostic flag, and an embedded scheduler wait queue. `process::init()` installs the owner hook using the scheduler's TID and generation.

All state transitions occur under `scheduler_wait`'s nested critical section. Blocking uses the already validated scheduler hooks, and final unlock performs the direct handoff described above. The implementation is for the current single-CPU scheduler; it does not claim SMP safety.

## 18. Test coverage

Hosted and compile/probe coverage is provided by:

- `scripts/dotnet/smoke-native-mutex.ps1`;
- `runtime/tests/guidexos_mutex_tests.cpp`;
- the freestanding scheduler/process compile checks;
- the inactive critical-section adapter link/probe.

The AMD64 QEMU run is provided by `scripts/smoke-native-mutex-qemu.ps1`. It boots a normal baseline, then the opt-in mutex image and checks basic ownership, recursion, try-lock, one waiter, FIFO waiters, direct handoff, protected counter `expected=3 observed=00000003`, TCB/wait-node cleanup, non-owner unlock, destruction rules, and owner-exit diagnostics.

The validated QEMU artifact directory is printed by the script as `out/runtime/native-mutex-qemu-validation/smoke-<timestamp>/` and contains the serial and fault/debug logs.

## 19. Known limitations

The current primitive intentionally does not provide SMP atomics, priority inheritance, deadlock detection, lock ranking enforcement, timed mutex waits, cancellation tokens, robust/leased ownership, wait-many, or process-wide owner-exit notification.

It also does not replace the existing event abstraction, scheduler critical section, local allocator/IPC locks, or device-specific bounded polling. Workstation GC initialization still requires separate FLS, stack-bound, suspension/context, thread-store, module, write-barrier, and startup validation.

## 20. Future SMP considerations

An SMP implementation would need an atomic state word or spin-based internal guard, per-CPU interrupt/preemption rules, memory-ordering proofs, a wakeup mechanism that cannot depend on one current TCB, and an ownership handoff protocol valid across CPUs. It would also need a reviewed scheduler lock hierarchy and likely priority-inheritance or priority-ceiling policy for real-time paths.

The current API is intentionally narrow enough that those mechanisms can be added behind the same platform boundary, but the present bare-metal implementation must not be used as an SMP mutex by inference.
