# NativeAOT Workstation GC single-mutator SuspendEE

Status: 2026-08-02. This report continues the first collection-control
boundary in [NATIVEAOT_WORKSTATION_GC_FIRST_COLLECTION_BOUNDARY.md](NATIVEAOT_WORKSTATION_GC_FIRST_COLLECTION_BOUNDARY.md).
It does not rewrite that earlier result: the earlier proof stopped before
`ThreadStore::LockThreadStore`; this proof reaches and uses the real lock and
single-thread suspension path.

The next bounded continuation is documented in
[NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md](NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md).
That report preserves this C011EC02 result and continues through real
`fix_allocation_contexts(TRUE)` to the first root-dispatch boundary.

## 1. Outcome

**Outcome A — single-thread suspension completed.** A real Workstation GC
request entered `GCHeap::GarbageCollectGeneration`, entered
`GCToEEInterface::SuspendEE`, acquired the locked runtime's real
`ThreadStore`, returned from `SuspendAllThreads`, returned from `SuspendEE`,
and reached the next verified boundary. The process then stopped before
allocation-context fixing, root enumeration, stack walking, handle scanning,
or heap mutation. This is not a claim that full collection is functional.

## 2. Locked identity and source contract

The locked identity is unchanged:

| Item | Value |
| --- | --- |
| NativeAOT/runtime-pack | 9.0.0, AMD64 |
| GC | Workstation, GC interface 5.3 |
| EE interface | 2 |
| locked runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| locked `gcenv.ee.cpp` SHA-256 | `EA603651D418F45F6847B7F3EC57C23BEB0A153ECCF94C2107EB5EB40B6C9A6B` |
| active normalized PAL archive SHA-256 | `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F` |

The locked source sequence is:

```text
GCHeap::GarbageCollectGeneration
  -> GCToEEInterface::SuspendEE(SUSPEND_FOR_GC)
     -> ThreadStore::LockThreadStore()
     -> GCHeap::SetGCInProgress(TRUE)
     -> ThreadStore::SuspendAllThreads(true)
     -> GCSuspendEEEnd
  -> return from SuspendEE
  -> DisablePreemptiveGC returns
  -> GCHeap::GarbageCollect before fix_allocation_contexts(TRUE)
```

In the locked `nativeaot/Runtime/gcenv.ee.cpp`, `SuspendEE` fires the begin
event, locks the thread store, publishes `GCInProgress`, suspends all other
threads, fires the end event, and returns. `RestartEE` is the inverse: clean
up, resume all threads, clear `GCInProgress`, unlock, and fire the end event.
The interface reason is `SUSPEND_FOR_GC = 1`; the collection's `gc_reason` is
`reason_oos_soh = 5`, so the proof records both as `suspendReason=1` and
`reason=5`.

The source locations used for the trace are:

- `nativeaot/Runtime/gcenv.ee.cpp:37-88` — `SuspendEE`, `RestartEE`, and
  `GcStartWork`.
- `nativeaot/Runtime/threadstore.cpp:39-58` — iterator starts at the real
  `m_ThreadList` head and advances through each thread's `m_pNext`.
- `nativeaot/Runtime/threadstore.cpp:202-238` — lock, ownership-preserving
  cooperative/preemptive transition, and suspension entry.
- `nativeaot/Runtime/threadstore.cpp:320` — the normal resume path.
- `nativeaot/Runtime/threadstore.inl:12-35` — current thread comes from
  `tls_CurrentThread`; `GetCurrentThreadIfAvailable` only requires
  `IsInitialized()`.
- `nativeaot/Runtime/thread.h:87-121` — runtime-thread fields, including
  state flags, transition frame, `m_pNext`, stack bounds, and native thread ID.
- `gc/gc.cpp:50960` and `gc/gc.cpp:24257` — collection entry and the later
  `GcStartWork` call.

`GcStartWork` is not the next boundary in this proof. The collector performs
  internal preparation between `DisablePreemptiveGC` and the later
  `GcStartWork`; the exact safe boundary observed here is earlier, at
  `GCHeap::GarbageCollect` before `fix_allocation_contexts(TRUE)`.

## 3. Thread-store adapter and lock contract

The guideXOS startup path had already constructed the current TLS `Thread` and
made it initialized, but it had not called the locked runtime's
`ThreadStore::AttachCurrentThread`. The source implementation returns early
when `IsInitialized()` is already true, leaving the real intrusive list empty.

The proof therefore adds one narrow adapter in
`guidexos_nativeaot_platform.cpp`: before the real lock, it observes the
locked runtime layout (`ThreadStore` begins with `SList<Thread> m_ThreadList`),
sets the current `RuntimeThreadLocals::m_pNext` to null, and links that actual
current `Thread` as the real list head only when the list is empty. It records
one adapter registration. It does not create a fake record, replace the
iterator, report an empty root set, or mutate the registry while the lock is
held.

The real `ThreadStore::LockThreadStore` remains responsible for ownership: if
the current thread is cooperative it temporarily enters preemptive mode,
enters the actual `Crst`, and restores cooperative mode after ownership is
obtained. The proof records the real lock owner and depth. The subsequent
`ThreadStore::SuspendAllThreads(true)` publishes `RhpSuspendingThread`, sets
`RhpTrapThreads|=TrapThreads`, flushes process write buffers, and iterates the
real registry while skipping the collector thread. No general scheduler,
recursive lock, or alternate unlock path was added.

## 4. Single-mutator semantics

The current TLS thread is obtained from `ThreadStore::GetCurrentThreadIfAvailable`.
It is initialized, has a stable runtime pointer, native ID, stack bounds,
transition-frame state, and cooperative/preemptive state. The collection
initiator is the same pointer as the current thread and lock owner. The proof
architecture has one managed mutator and one native execution thread; the
runtime trap flag plus the held thread-store lock prevent a new managed entry
or registry mutation during this bounded window. The collector thread is not
counted as a stopped peer: it remains the executing collector and is recorded
as exempt.

The adapter and instrumentation are gated by
`GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION` and the kernel test
selectors. Ordinary kernels do not activate this behavior.

## 5. Workload and observed counts

The established `byte[4096]` workload and single-heap Workstation
configuration were reused unchanged:

| Field | Result |
| --- | ---: |
| managed allocations | `0x28` (40) |
| fast paths | `0x13` (19) |
| rare paths | `0x16` (22) |
| refills | `0x15` (21) |
| same-segment commits | `0x2` (2) |
| SOH segment transitions | `0x0` |
| returned objects validated | 40 |
| sentinel checks | 160 |
| sentinel failures | 0 |
| live sentinels | 4 |

The collection request occurred at the established request ordinal after 40
returned objects. It recorded one request and one collection entry, generation
1, `reason_oos_soh` 5, blocking mode, and compaction not selected.

## 6. Suspension and boundary evidence

Each fresh QEMU run recorded:

| Field | Result |
| --- | ---: |
| registered managed threads | 1 |
| adapter registrations | 1 |
| current/initiator identity match | 1 |
| expected other mutators | 0 |
| stopped other mutators | 0 |
| lock requests/acquisitions/failures/unlocks | 1 / 1 / 0 / 0 |
| lock recursion depth | 1 |
| registry mutation attempts while locked | 0 |
| `SuspendEE` entries / successful returns | 1 / 1 |
| suspension requests / completed suspension entries | 1 / 1 |
| current collector exempt from peer stop | 1 |
| managed entry prohibited | 1 |
| EE suspended | 1 |
| restart requests/entries | 0 / 0 |
| managed resume count | 0 |

The current runtime-thread pointer, collection-initiator pointer, and lock
owner are identical in every run (`0x000000000392AC00` in the serial evidence);
the native thread ID is `0x00000000100CE8C0`. The full pointer and stack/state
diagnostics are retained in each serial log and the generated diagnostics
record.

The unique safe-stop marker is `C011EC02`. The exact marker callback is:

```text
callback=GCHeap::GarbageCollect before fix_allocation_contexts
nextBoundary=2
rootRequests=0 rootEntries=0
stackWalkRequests=0 stackWalkEntries=0
handleScanRequests=0 handleScanEntries=0
heapMutationStarted=0 restartRequests=0 restartEntries=0 managedResumeCount=0
```

The proof deliberately does not enter root enumeration, stack scanning, handle
scanning, marking, sweeping, compaction, relocation, or restart. It does not
claim that the four live sentinels have been proven safe across a collection;
they are precisely why an empty-root continuation is not allowed.

## 7. Fresh QEMU proof runs

Run directory:
`out/dotnet/gc-single-thread-suspend-ee/run-20260802-213154626/`

| Run | Marker | Serial SHA-256 | Result |
| --- | --- | --- | --- |
| `first-run` | `C011EC02` | `97A5C584600B88C258B7C64621E63712A67D0B7B4F47BF1F3B63491DAE6BC77B` | PASS |
| `repeat-1` | `C011EC02` | `EAC656E2178985AE55F11D2B448E67C2D5423F001FB4FF86EC506B7F88ECA37D` | PASS |
| `repeat-2` | `C011EC02` | `616447287A2B661443F4345999FA384675B549BC79FE16AC2B8CF3AAAB77B9BC` | PASS |

QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. The exact command lines are
in `commands.txt`; each run used one AMD64 QEMU guest, one vCPU, and the
freshly staged proof kernel/ESP.

## 8. Regression results

| Check | Result | Evidence |
| --- | --- | --- |
| first-collection boundary | PASS, 3/3; historical `C011EC01` behavior preserved | `out/dotnet/gc-first-collection-boundary/run-20260802-210449513/` |
| multiple-refills/first-segment boundary | PASS, 3/3 | `out/dotnet/gc-multiple-refills-first-segment-boundary/run-20260802-210704441/` |
| first post-startup allocation | PASS | `out/dotnet/gc-first-allocation-closure/run-20260802-211206962/` |
| first refill | PASS | `out/dotnet/gc-first-refill/run-20260802-211325750/` |
| first segment transition | PASS, 3/3; historical Outcome B preserved | `out/dotnet/gc-first-segment-transition/run-20260802-211456629/` |
| managed no-collection 4 KiB | PASS, 14 allocations, controlled OOM | `out/dotnet/repeated-allocation-comparison/repeated-allocation/` |
| managed no-collection 64 KiB static | PASS, 234 allocations, controlled OOM | `out/dotnet/repeated-allocation-comparison/repeated-allocation-static/` |
| managed no-collection execution, 4 KiB | PASS, 14 allocations, controlled OOM | same repeated-allocation evidence root |
| managed no-collection execution, 64 KiB | PASS, 234 allocations, controlled OOM | same repeated-allocation evidence root |
| runtime-pack static NonAllocating/Allocating | PASS | `out/dotnet/managed-hostlog/` |
| runtime-pack state | PASS | `scripts/smoke-dotnet-runtime-pack-state.ps1` output and managed-hostlog evidence |
| generic ELF | PASS | `scripts/smoke-native-elf-generic.ps1` |
| FLS/local-storage QEMU | PASS | `out/runtime/native-local-storage-qemu-validation/smoke-20260802-212202-819-4905/` |
| virtual-memory QEMU | PASS | `out/runtime/native-virtual-memory-qemu/smoke-20260802-212555/` |
| native-thread QEMU | PASS | `out/runtime/native-thread-qemu-validation/smoke-20260802-212817/` |
| native stack assertions | PASS | `out/runtime/native-stack-bounds/` |
| exact kernel build | PASS, direct `mingw32-make -C kernel ARCH=amd64` exit 0 | `out/runtime/native-stack-bounds/direct-kernel-build.log` |

## 9. Failed, repeated, and non-clean checks

The historical first 64 KiB execution attempt remains retained in the prior
collection-boundary evidence and is still classified as a failure, not a pass;
its repeat passed with 234 allocations and controlled OOM. The fresh 64 KiB
execution in this pass passed and did not reproduce that access violation.

`smoke-native-stack-bounds.ps1` returned its actual nonzero exit status (`1`)
because PowerShell promoted compiler stderr to a native-command error. All
hosted stack assertions passed, and the exact direct kernel build passed
independently. This wrapper result is **NON-CLEAN**, not counted as a clean
wrapper pass.

Additional retained wrapper attempts include the stale-cache `-SkipBuild`
first-allocation and local-storage attempts; the first-allocation script then
passed after a clean kernel build, and the local-storage script passed after a
full baseline/test build. The initial cached runtime-pack static identity
mismatch was likewise rerun without `-SkipBuild` and passed.

## 10. Ordinary-kernel restoration

The ordinary kernel was restored in both the build and ESP locations. Both
locations have SHA-256:

`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`

The final single-thread manifest records the before/after hashes and confirms
that the proof kernel was not left in an ordinary location.

## 11. Manifest, source changes, and next milestone

Manifest:
`out/dotnet/gc-single-thread-suspend-ee/run-20260802-213154626/manifest.json`

The proof-specific source changes are:

- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp` —
  locked-runtime ThreadStore adapter, real lock/suspension observers, and
  post-disable safe stop.
- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h` —
  append-only suspension, lock, identity, boundary, and adapter fields.
- `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1` — generated
  source injection, build/link/ELF/QEMU proof, validation, manifest, and
  restoration.
- `kernel/Makefile`, `kernel/core/main.cpp`,
  `kernel/core/nativeaot_pal_qemu_test.cpp`, and
  `kernel/core/include/kernel/nativeaot_pal_qemu_test.h` — proof-mode selector
  plumbing.
- The three related boundary scripts preserve IRQ separators in serial-field
  normalization so asynchronous diagnostic output remains parseable.

The next smallest bounded milestone is a source-backed first root boundary:
instrument the first thread/stack/root callback after
`fix_allocation_contexts(TRUE)` and stop before scanning. Do not return an
empty root set, begin marking, mutate heap state, or implement restart until
that root contract is separately established.

No commit was created.
