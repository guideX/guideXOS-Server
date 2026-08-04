# NativeAOT Workstation GC First Collection Boundary

Date: 2026-08-02
Result: **Outcome A — collection requested and entered**

## Objective

This bounded proof identifies the first real Workstation GC collection request
and collection entry under guideXOS, records the first runtime/EE contract
encountered, and stops before unsupported suspension or heap mutation. It does
not claim completed garbage collection or managed execution after collection.

The prior no-collection segment-transition proof remains authoritative:
continued pressure committed within the current SOH segment but recorded zero
collection requests, zero collection entries, and zero SOH segment transitions.

The next bounded continuation is
[NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md](NATIVEAOT_WORKSTATION_GC_ALLOCATION_CONTEXT_FIXUP_AND_FIRST_ROOT_BOUNDARY.md),
which preserves this pre-fixup boundary and advances only through allocation
context fixup to the first root-dispatch boundary.

## Locked identity

- NativeAOT/runtime-pack source commit:
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- Runtime-pack: `9.0.0`, AMD64, Workstation GC, GC interface `5.3`, EE `2`.
- Locked `gcenv.ee` source SHA-256:
  `EA603651D418F45F6847B7F3EC57C23BEB0A153ECCF94C2107EB5EB40B6C9A6B`
- Active adapted PAL archive SHA-256:
  `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`
- Proof kernel SHA-256:
  `052FFD2D2F33D4B6CA3D7A5D912DF39ED7137940B1BD24118280F6BBE3C2C9D5`
- QEMU: `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`)

## Source-derived path

The locked allocator path is:

```text
allocate_soh
  -> soh_try_fit
  -> a_fit_segment_end_p
  -> a_state_trigger_ephemeral_gc
  -> trigger_ephemeral_gc
  -> GCHeap::GarbageCollectGeneration(max_generation - 1, reason_oos_soh)
  -> GCToEEInterface::SuspendEE(SUSPEND_FOR_GC)
```

The first request is reached when the current SOH allocation context cannot
fit the aligned object and the segment-end path cannot legally satisfy the
request with another same-segment commitment. `GarbageCollectGeneration`
records the reason and generation, enters the GC control path, and requests
preemptive execution before calling `GCToEEInterface::SuspendEE`. The exact
locked EE implementation next signals the suspension event, locks the thread
store, and calls `SuspendAllThreads(true)`.

The first guideXOS contract actually reached is the entry to
`GCToEEInterface::SuspendEE`, before `ThreadStore::LockThreadStore`. The
proof-only safe stop is inserted at that entry. Consequently, no thread-store
lock, thread enumeration, stack walk, root enumeration, restart, or heap
mutation is reported as executed. Those contracts were inspected but were not
faked or advanced past the first boundary.

## Experiment configuration

- Dedicated compile-time mode:
  `GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST` and
  `HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION`.
- Managed workload: fixed `byte[4096]`, requested object size `0x1018`
  (4120 bytes aligned), below the LOH threshold.
- Hard allocation limit: 256 allocation attempts.
- Four early objects remain live as sentinels.
- Every returned object was checked for non-null, alignment, type/layout,
  zero initialization, deterministic pattern fill, live-object overlap, and
  valid committed/reserved range before the next allocation.
- Heap state at the stop: allocation pointer
  `0x100A285C8`, allocation limit `0x100A29040`, committed boundary
  `0x100A31000`, reserved boundary `0x100B00000`, current SOH segment
  `0x1004014730`.

## Boundary evidence

The first collection boundary occurred on allocation request ordinal `0x29`
(the 41st request), after 40 objects had been successfully returned. The
allocation/refill/commit and collection counters were:

| Field | Result |
| --- | ---: |
| Managed allocations returned | 40 |
| Fast allocations | 19 |
| Rare-path allocations | 22 |
| Allocation refills | 21 |
| Same-segment commitment extensions | 2 |
| Collection requests | 1 |
| Collection entries | 1 |
| Requested generation | 1 (`0x00000001`) |
| Collection reason | `reason_oos_soh`, value 5 (`0x00000005`) |
| Blocking mode | blocking (`1`) |
| Compacting mode | not selected before stop (`0`) |
| Suspension requests | 1 |
| Suspension entries | 0 |
| Restart/resume count | 0 |
| SOH segment transitions | 0 |
| Heap mutation started | no |
| Managed execution resumed | no |

The source condition and runtime marker agree: the collection was requested
and `GCHeap::GarbageCollectGeneration` was entered, but the first EE callback
was stopped before destructive or irreversible collection work.

## Safe stop and validation

The unique marker is `C011EC01`. It records:

```text
callback=GCToEEInterface::SuspendEE
requestCount=1 entryCount=1 requestedGeneration=1 reason=5
suspensionRequestCount=1 suspensionEntryCount=0
restartResumeCount=0 heapMutationStarted=0 managedExecutionResumed=0
unsupportedContract=GCToEEInterface::SuspendEE entry /
  proof stop before ThreadStore::LockThreadStore
```

The proof-only stop emits the marker directly to the serial port and parks the
experiment before returning to managed allocation. It therefore cannot return
an invalid object or pretend that a collection completed. All three processes
terminated cleanly through the existing proof termination path.

Object validation completed before the stop: 40 returned objects passed the
object/type/layout/zero/pattern/range/overlap checks; 160 sentinel checks
passed with zero failures; four sentinels remained live. No post-collection
sentinel claim is made because managed execution did not resume.

## Fresh QEMU runs

| Run | Safe marker | Alloc / fast / rare | Refills / commits | Requests / entries | Suspension req / entry | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `first-run` | `C011EC01` | 40 / 19 / 22 | 21 / 2 | 1 / 1 | 1 / 0 | PASS |
| `repeat-1` | `C011EC01` | 40 / 19 / 22 | 21 / 2 | 1 / 1 | 1 / 0 | PASS |
| `repeat-2` | `C011EC01` | 40 / 19 / 22 | 21 / 2 | 1 / 1 | 1 / 0 | PASS |

The run-specific manifest and serial logs are under:

`out/dotnet/gc-first-collection-boundary/run-20260802-161954254/`

Manifest:

`out/dotnet/gc-first-collection-boundary/run-20260802-161954254/manifest.json`

## Regression results

- First-collection-boundary proof: **PASS, 3/3 fresh QEMU runs**.
- Managed 4 KiB no-collection proof: **PASS**, 14 allocations, controlled OOM.
- Managed 64 KiB no-collection proof: static **PASS**, 234 allocations;
  hosted execution first attempt exited `-1073741819`, then the fresh repeat
  **PASS**ed with 234 allocations and controlled OOM. The failed attempt is
  retained as evidence and is not counted as a pass.
- First-refill QEMU regression: **PASS**.
- First post-startup commitment regression: **PASS**.
- First segment-transition regression after harness repair: **PASS**, 3/3;
  it preserves the historical **Outcome B** result.
- Runtime-pack static validation: **PASS** for both NonAllocating and
  Allocating locked identities.
- Runtime-pack state/hash validation: **PASS**; malformed metadata rejected.
- Generic ELF validation: **PASS**.
- FLS/local-storage QEMU validation: **PASS**.
- Virtual-memory QEMU validation: **PASS**, serial `ALL_PASS`.
- Native-thread QEMU validation: **PASS**.
- Build validation: **PASS**; direct `mingw32-make -C kernel ARCH=amd64`
  completed with exit code 0, and the specialized proof builds passed.
- `git diff --check`: **PASS** after final documentation changes.

The stack-bound contract assertions themselves all passed, including hosted
and worker-thread bounds, reuse, detach, and teardown. The stock
`smoke-native-stack-bounds.ps1` wrapper returned nonzero after those assertions
because its strict PowerShell handling treated compiler stderr as an error
during the final make step; the same kernel make command was rerun with
captured output and completed successfully. This wrapper result is recorded as
non-clean rather than claimed as a script PASS.

## Ordinary-kernel restoration

The ordinary kernel was restored in both required locations after the proof
and regression runs. Both hashes match the expected normal image:

- `kernel/build/amd64/bin/kernel.elf`:
  `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`
- `ESP/kernel.elf`:
  `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`

No ordinary-kernel source change was required, and no proof kernel remains
deployed as the ordinary kernel.

## Outcome and next milestone

This is **Outcome A**: a real Workstation GC collection request occurred and
the real collection-control path was entered. It is not completed GC support.
The first unsupported contract is the guideXOS EE suspension entry before
thread-store locking. The next smallest bounded milestone is a semantically
valid single-managed-thread `SuspendEE`/thread-store adapter that can observe
the first suspension entry and stop before root enumeration or heap mutation;
it must not fabricate thread enumeration, roots, restart, or collection
success.

## Single-managed-mutator SuspendEE follow-up - 2026-08-02

The follow-up bounded experiment is documented in
[NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md](NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md).
It advanced the same locked source path through the real `ThreadStore` lock
and single-mutator suspension, returned from `SuspendEE`, and stopped at the
next exact boundary after `DisablePreemptiveGC`, before root/stack/handle
enumeration or heap mutation. This report remains historical: its earlier
statement that execution stopped before `ThreadStore::LockThreadStore` has
not been rewritten.
