# NativeAOT Workstation GC: first real per-thread root provider

Date: 2026-08-04  
Outcome: **A — first real per-thread provider entered**  
Safe-stop marker: `C011EC04`

## 1. Scope and starting point

This bounded experiment advances the previously proven `C011EC03` checkpoint
from `GCToEEInterface::GcScanRoots` entry through the locked runtime's real
thread iterator, identifies the one registered guideXOS managed thread, and
enters the first runtime-selected per-thread provider. It stops before the
first managed-reference value is read. It does not implement root enumeration,
stack walking, marking, reclamation, restart, or managed resumption.

Starting committed HEAD was `9f0f0dbee83395b1271927a719d8da6e5ae2a18e`.
The tree was already dirty with the user's prior proof work; no unrelated
changes were discarded. No commit was created.

Locked identity:

| Item | Value |
|---|---|
| NativeAOT/runtime | `9.0.0`, AMD64 |
| GC | Workstation |
| GC/EE interfaces | `5.3 / 2` |
| locked CoreCLR source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| ordinary kernel/ESP expected SHA-256 | `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C` |
| active PAL archive SHA-256 | `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F` |

The prior checkpoint remains documented in
`NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md`:
`C011EC03` stopped at `GcScanRoots` entry immediately before `FOREACH_THREAD`.

## 2. Locked source trace

All paths below are relative to
`out/dotnet/pal-runtime-active-replacement-build/locked-source/src/coreclr`.

`nativeaot/Runtime/gcenv.ee.cpp:94-130` defines
`GCToEEInterface::GcScanRoots`. The exact order is:

1. `FOREACH_THREAD(pThread)` (`:98`).
2. Skip `pThread->IsGCSpecial()` (`:100-102`).
3. Require `IsThreadUsingAllocationContextHeap(...)` (`:104`).
4. Walk `GetInlinedThreadStaticList()` and call `EnumGcRef` for each inline
   root (`:106-112`).
5. Obtain `GetThreadStaticStorage()` and call `EnumGcRef` (`:114-115`).
6. Enter `pThread->GcScanRoots(fn, sc)` (`:122`).
7. Close `END_FOREACH_THREAD` (`:130`).

The source-order category is therefore thread statics before stack/frame
roots. Runtime instrumentation selected the thread-static category in this
configuration. The first candidate-access operations in the source are the
inline `EnumGcRef` at `:110` or the storage `EnumGcRef` at `:115`; the proof
stops before either call. If both metadata paths are absent, the next provider
would be `Thread::GcScanRoots` (`nativeaot/Runtime/thread.cpp:393-401`), whose
first candidate-capable path begins in `GcScanRootsWorker` at `:442-447` with
the hijacked-return-value location query.

The `FOREACH_THREAD` contract is the locked macro in
`nativeaot/Runtime/threadstore.h:73-80`:

```cpp
{ ThreadStore::Iterator __threads; Thread * p_thread_name;
  while ((p_thread_name = __threads.GetNext()) != NULL) {
```

`END_FOREACH_THREAD` closes the loop and macro scope. The iterator constructor
starts at `GetThreadStore()->m_ThreadList.GetHead()`
(`nativeaot/Runtime/threadstore.cpp:39-44`). `GetNext()` returns the current
node and advances to `pResult->m_pNext`; null terminates iteration
(`:53-59`). The constructor asserts that the thread-store lock is owned by the
current thread, or that the current thread is GC-special while GC is in
progress (`:46-50`).

The backing collection is the real `ThreadStore::m_ThreadList`
(`nativeaot/Runtime/threadstore.h:21-27`), not a synthetic proof array. The
guideXOS adapter registered the actual current `Thread*` before the locked
`SuspendEE` path. The experiment snapshots the raw list head/tail and follows
the real `m_pNext` links with bounded duplicate/cycle detection.

The only source filters reached here are `IsGCSpecial()` and the allocation-
context-heap test. The one registered managed thread was attached, non-GC-
special, and using the collection's allocation-context heap, so it was
included. Detached or GC-special entries would be recorded as exclusions;
none existed in this one-thread baseline. No synthetic current-thread
substitution or FLS-only identity inference was used.

## 3. Runtime evidence at the provider boundary

The established workload was preserved: 40 completed `byte[4096]` objects,
19 fast paths, 22 rare paths, 21 refills, two same-segment commitment
extensions, zero segment transitions, four live sentinels, and a generation-1
`reason_oos_soh` (5), blocking, noncompacting collection. There was one
collection request/entry, one successful `SuspendEE` return, and one
registered managed thread.

| Diagnostic | Result |
|---|---:|
| registered / enumerated / included / excluded | `1 / 1 / 1 / 0` |
| `GcScanRoots` request / entry | `1 / 1` |
| `FOREACH_THREAD` request / entry | `1 / 1` |
| iterator initialization | `1` |
| current / initiator / enumerated / lock-owner | `0x000000000392BC00` for all four |
| lifecycle / state flags | `0x1 / 0x1` |
| cooperative / preemptive | `1 / 0` |
| native thread ID | `0x00000000100CF8C0` |
| allocation-context identity | `0x000000000392BC00` |
| stored stack low / high | `0x0 / 0x0` (not requested or scanned) |
| list head/tail before and after | `0x000000000392BC00 / 0x000000000392BC00` |
| duplicate / list-integrity failures | `0 / 0` |
| registry mutation before / after | `0 / 0` |

The runtime-selected provider was `thread-static-provider`, exact function
`Thread::GetThreadStaticStorage`. The inline-list request was made first and
was skipped with reason `no inline thread-static roots`; the storage provider
was then entered. The provider thread matched the enumerated and collection
initiator thread.

| Provider diagnostic | Result |
|---|---:|
| source-order category / runtime category | `thread-static-provider / thread-static-provider` |
| provider requests / entries / skips | `2 / 1 / 1` |
| metadata containers | `1` |
| first metadata/container identity | `0x000000000392BC90` |
| candidate metadata locations identified | `1` |
| candidate values read / candidates discovered | `0 / 0` |
| root / promotion callbacks | `0 / 0` |
| marking | `0` |
| stack bounds requested / stack scanning | `0 / 0` |
| thread-static storage requested / scanned | `1 / 0` |
| object-memory mutation | `0` |
| restart requests / entries | `0 / 0` |
| managed resumes | `0` |

The first metadata address is recorded as metadata only. The storage slot was
not dereferenced and no managed reference value was loaded. The four
sentinels were validated at the existing pre-collection, suspension, fixup,
pre-root, post-enumeration, and provider-stop checkpoints. At the stop, all 40
object addresses, contents, layouts, allocation-context clearing, valid
extent, and duplicate-address checks remained valid.

Thread-store lock depth remained `1`; the lock remained held. EE suspension
remained active, managed entry remained prohibited, and no restart or managed
resume was attempted. Termination intentionally leaves the proof kernel in the
bounded stop loop.

## 4. QEMU proof runs

Fresh proof evidence is under
`out/dotnet/gc-first-per-thread-root-provider/run-20260804-202658280/`.
Each run used a fresh QEMU process, the locked source/runtime identity, and the
same `C011EC04` marker. The specialized proof kernel SHA-256 is
`862F54637BCF391AAF9ECD2230C2E58F46A5B2A966EA4F37236C885CCA25C6BE`.

| Run | Result | Serial SHA-256 |
|---|---|---|
| first-run | PASS | `EE157E2A3611BB07115AB73D14B2D081A3B9AACC8D71EA0289B4A21D4F0B1C94` |
| repeat-1 | PASS | `08763372DF7334E8A8606E24F93F8ABFD005C1F5EE1D9F776D08E76EB80FDF94` |
| repeat-2 | PASS | `B2730A2D6BB7619B19257711748041C7DF5BF72E16308582AB23C56EBE8FC23E` |

The run `commands.txt`, QEMU version, generated injected source, build logs,
per-run `serial.log`, `serial.sha256`, and restoration hash are retained in
that directory. The final manifest records the canonical thread record,
identities, flags, provider fields, all three serial hashes, active PAL hash,
proof hash, and regression results.

## 5. Regression and attempt record

| Check | Result / evidence |
|---|---|
| `C011EC03` fixup/root-dispatch | PASS, 3/3, `out/dotnet/gc-allocation-context-fixup-root-boundary/run-20260804-195714779/` |
| `C011EC02` single-thread `SuspendEE` | PASS, 3/3, `out/dotnet/gc-single-thread-suspend-ee/run-20260804-200047137/` |
| `C011EC01` first collection boundary | PASS, 3/3, `out/dotnet/gc-first-collection-boundary/run-20260804-200538369/` |
| first segment transition | PASS, 3/3, `out/dotnet/gc-first-segment-transition/run-20260804-200804865/` |
| multiple refills / first segment boundary | PASS, 3/3, `out/dotnet/gc-multiple-refills-first-segment-boundary/run-20260804-201336802/` |
| first refill | PASS, 3/3, `out/dotnet/gc-first-refill/run-20260804-201639325/` |
| first allocation | PASS, one fresh QEMU run, `out/dotnet/gc-first-allocation-closure/run-20260804-201956448/` |
| runtime-pack state | PASS |
| generic ELF | PASS |
| FLS/local-storage before init | PASS |
| hosted stack-bound assertions | PASS |
| native stack-bound wrapper | NON-CLEAN exit 1; not called passed |
| runtime-pack static nonallocating check | BLOCKED by the observed runtime-pack identity mismatch |

Failed and repeated attempts are retained. The first three new-provider
attempts include two compile failures and one serial-watchdog truncation; the
later watchdog fixes distinguish a complete diagnostic line from a prefix or
IRQ-interleaved line. Historical first-64-KiB execution failure, stale-cache
attempts, initial runtime-pack identity mismatch, and native-stack compiler-
stderr promotion remain documented and were not converted to passes.

The broader managed 4-KiB/64-KiB no-collection execution/static artifacts
remain historical source-of-record evidence; they were not relabeled as new
provider proof. The native-stack wrapper remains explicitly non-clean.

## 6. Restoration, files, and next milestone

After every proof/regression attempt, the ordinary kernel and ESP were restored.
The final `kernel.elf` and ordinary ESP payload both hash to
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`.
Proof-only behavior remains behind the proof-mode selectors and does not run
in an ordinary kernel.

Files changed for this experiment:

* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`:
  bounded dispatcher, registry, provider, candidate-boundary, and safe-stop
  fields.
* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`:
  real-list snapshots, actual-thread records, provider instrumentation,
  invariants, and C011EC04 stop.
* `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`:
  proof-mode source injection, completed-record watchdog, IRQ-safe validation,
  QEMU runs, manifest, and restoration.
* `scripts/smoke-nativeaot-gc-multiple-refills-first-segment-boundary-qemu.ps1`:
  narrow IRQ-token normalization needed to validate an existing regression.
* this report and the run manifests/evidence.

`git diff --check` passes. PowerShell parsing and final manifest JSON parsing
pass. No commit was created.

The next smallest bounded milestone is to enter the first real candidate-
preparation operation only far enough to identify its contract and stop before
the first slot/value load. For the selected thread-static path, that means
proving the actual slot/descriptor contract without calling `EnumGcRef`; it
does not authorize fake roots, stack walking, frame traversal, marking, or
continuing collection.
