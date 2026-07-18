# NativeAOT Workstation GC Critical Sections

## 1. Matching requirements

The locked NativeAOT Workstation source uses a critical-section abstraction for runtime and collector state that may be entered by more than one native thread. The guideXOS mapping must provide initialization, destruction, blocking enter, leave, ownership identity for diagnostics, and a nonblocking try-enter capability for the platform boundary.

The current experiment provides those semantics through the runtime-neutral `gxos::runtime::Mutex`. It remains a platform primitive only: this pass does not call NativeAOT startup, initialize a collector, allocate managed objects, run a finalizer, or collect.

## 2. Matching symbols and call sites

The source audit used the exact NativeAOT commit recorded by the runtime-pack lock. The relevant source surfaces are:

- `nativeaot/Runtime/Crst.h` and `nativeaot/Runtime/Crst.cpp`: `Crst`, `CrstStatic`, `CrstHolder`, `CrstHolderWithState`, `CrstStatic::Init`, `Destroy`, `Enter`, and `Leave`;
- `nativeaot/Runtime/gc/env/gcenv.os.h` and `nativeaot/Runtime/gc/env/gcenv.sync.h`: `CLRCriticalSection`, `CrstStatic`, and GC-side lock wrappers;
- `nativeaot/Runtime/gc/windows/gcenv.windows.cpp`: `CLRCriticalSection::Initialize`, `Destroy`, `Enter`, and `Leave`;
- `nativeaot/Runtime/allocheap.cpp`: `CrstAllocHeap` around `AllocHeap::_Alloc`;
- `nativeaot/Runtime/threadstore.cpp`: `CrstThreadStore`, `AttachCurrentThread`, `DetachCurrentThread`, explicit `LockThreadStore`, and `UnlockThreadStore`;
- `nativeaot/Runtime/clrgc.enabled.cpp`: `CrstGcEvent` around GC event state;
- `nativeaot/Runtime/RestrictedCallouts.cpp`: the static restricted-callout lock;
- `nativeaot/Runtime/handletable.cpp`: handle-table lock initialization and holder use.

The source path has no direct `TryEnterCriticalSection` or timed critical-section call in the matching files. The generic try-lock is retained as a safe capability for the adapter boundary and future callers, not claimed as an observed collector call.

## 3. Recursion behavior

NativeAOT's `Crst` flags are zero-valued in this source variant, while its Windows implementation calls `PalInitializeCriticalSectionEx` and `PalEnterCriticalSection`. The underlying Windows critical-section behavior is recursive. Call sites often avoid recursive entry by using `CrstHolderWithState` when the caller already owns the lock, but the primitive itself does not support a nonrecursive assumption safely.

The selected guideXOS adapter mode is therefore `MutexMode::Recursive`. The generic object also exposes `NonRecursive` for components that want `AlreadyOwned` on self-entry, and its recursion count is bounded at 1024 with an explicit `RecursionLimit` result.

## 4. Mapping to guideXOS

The inactive adapter in `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_critical_section_adapter.*` maps:

| NativeAOT-shaped operation | guideXOS operation |
| --- | --- |
| initialize a critical section | construct `Mutex(Recursive)` |
| enter | `Mutex::lock()` |
| try-enter | `Mutex::tryLock()` |
| leave | `Mutex::unlock()` |
| delete | `Mutex::destroy()` followed by handle deletion only on `Ok` |

The adapter handle is opaque. It does not expose host critical-section structures, TCBs, scheduler internals, collector entry points, or runtime startup symbols.

## 5. Initialization order

Generic mutex construction is allocation-free on bare metal and can initialize object storage before scheduler hooks are installed. Acquisition is intentionally stricter: a bare-metal lock requires a valid owner hook and scheduler critical/wait contract. `process::init()` installs the scheduler wait hooks, then the mutex owner hook using the current TID and TCB generation.

The future NativeAOT startup mapping must initialize the platform scheduler/thread identity and critical-section support before `RuntimeInstance::Initialize`, `InitializeGCEventLock`, `RestrictedCallouts::Initialize`, or any `CrstHolder` path can block. No bootstrap spin fallback is added by this experiment.

## 6. Ownership and destruction policy

Ownership is `{value, generation}`. Hosted values are unique per native thread; bare-metal values are the scheduler TID plus its reuse generation. This prevents stale ownership from surviving TCB-slot reuse.

Destruction is quiescent. `destroy()` reports `Busy` if the lock is owned or has waiters and does not wake or release anyone. This is safer for a heap-owned adapter than deleting a contended state. NativeAOT source-level static locks generally live for the process lifetime; a future adapter may use that lifetime policy where quiescence cannot be proven.

## 7. Inactive adapter boundary

The adapter is intentionally inactive. It is not linked into the managed runtime-pack startup path and does not reference `GC_Initialize`, `RhInitialize`, `PalStartFinalizerThread`, `GarbageCollect`, or equivalent operational entry points. It is a compile/link/runtime-neutral probe of the critical-section mapping only.

Blocking use remains subject to the runtime mutex rules: ordinary thread context, initialized scheduler hooks, and no caller-held lower-rank scheduler critical section. The adapter does not claim that the complete GC thread-store suspension contract exists.

## 8. Probe result

The probe is `runtime/tests/guidexos_critical_section_adapter_probe.cpp` and is run by `scripts/dotnet/smoke-native-mutex.ps1`. It verifies:

- handle construction and recursive enter;
- recursive try-enter by the same owner;
- a second native thread blocks until all recursive releases complete;
- the waiter acquires and releases after handoff;
- quiescent deletion succeeds.

The hosted mutex suite, freestanding mutex/process compile, generic coupling check, adapter isolation check, and the AMD64 QEMU mutex suite all pass in the current bounded validation. The QEMU serial metric is `Protected counter: expected=3 observed=00000003`.

## 9. Remaining blockers

Passing the critical-section primitive does not make Workstation GC startup valid. The feasibility record still requires a real NativeAOT `Thread`/`ThreadStore` registration path, FLS allocation and detach semantics, stack bounds, suspension/context capture, module/static/frozen-root registration, write-barrier setup, GC PAL segment lifecycle, and the unconditional finalizer helper thread.

The adapter also does not implement source-specific lock ranking, deadlock diagnostics, wait-many, timed critical sections, SMP synchronization, or automatic process-wide owner-exit recovery. Those must remain explicit future contracts rather than inferred from the probe.

## 10. Exact next experiment

After this mutex outcome, re-audit the complete Workstation GC startup call order with the already validated virtual-memory, event, native-thread, timing, and critical-section contracts. If the FLS, stack, suspension/context, thread-store, module, and write-barrier prerequisites are each separately satisfied, the next bounded experiment is a GC platform-initialization dry run that constructs the collector and immediately shuts it down without managed objects or collection.

That experiment must remain opt-in, preserve the no-collection managed proofs, and report its first failing startup symbol or lifecycle contract without adding finalizer recovery or collection behavior implicitly.
