# NativeAOT Workstation GC allocation-context fixup and first root boundary

Status: 2026-08-04. This report continues
[NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md](NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md).
The earlier C011EC02 evidence is retained: this report adds a separate,
bounded proof mode and does not rewrite the earlier result.

## 1. Outcome

**Outcome A — real `fix_allocation_contexts(TRUE)` completed and the first
root-dispatch boundary was reached.** Three fresh QEMU processes each recorded
one real allocation-context enumeration, one visited context, one changed
context, and one cleared context. The proof then stopped at the entry to
`GCToEEInterface::GcScanRoots`, immediately before the locked source's
`FOREACH_THREAD` root iteration.

This is not a claim that root scanning, marking, sweeping, compaction,
relocation, restart, or managed resumption works. None of those phases was
entered by this proof.

## 2. Locked identity and scope

| Item | Value |
| --- | --- |
| NativeAOT/runtime-pack | 9.0.0, AMD64 |
| Collector | Workstation GC, one heap |
| GC / EE interfaces | 5.3 / 2 |
| Locked runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Active PAL archive SHA-256 | `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F` |
| Managed workload | 40 `byte[4096]` objects, four live sentinels |
| Safe-stop marker | `C011EC03` |
| Specialized proof kernel SHA-256 | `7792BC73ECA50D586ACEB6E1AACE633B14CEDB01EEAD4304043D40074871AEEC` |
| Ordinary kernel restored SHA-256 | `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C` |

The collector source, runtime-pack identity, and adapted archive remain locked.
The only source changes are proof-mode platform/probe diagnostics, generated
EE-source callback injection, and the existing test-workload build plumbing.
The ordinary kernel is not changed by the proof mode and was restored to the
recorded hash in both `kernel\build\amd64\bin\kernel.elf` and
`ESP\kernel.elf`.

## 3. Source-backed sequence

The locked collector sequence is:

1. `GCHeap::GarbageCollectGeneration` calls `SuspendEE(SUSPEND_FOR_GC)` at
   `gc.cpp:50960-51031`, including the real `ThreadStore` lock and
   `SuspendAllThreads` path.
2. `gc_heap::garbage_collect` calls `fix_allocation_contexts(TRUE)` at
   `gc.cpp:24077-24084`.
3. `fix_allocation_contexts` enumerates EE contexts and then fixes the
   youngest allocation area at `gc.cpp:7957-7971`.
4. `GCHeap::FixAllocContext` converts the non-null argument to `TRUE` and
   calls the collector's `fix_allocation_context` at `gc.cpp:50002-50022`.
5. `fix_allocation_context` consumes the allocation range, updates
   `alloc_bytes`, clears `alloc_ptr` and `alloc_limit`, and records the
   context as used at `gc.cpp:7858-7915`.
6. Only after that does the collector call `GcStartWork` at
   `gc.cpp:24256-24258`, then `BeforeGcScanRoots` at `gc.cpp:29778`, and then
   `GCScan::GcScanRoots` at `gc.cpp:29899`.

The locked NativeAOT EE implementation defines the relevant boundaries in
`nativeaot/Runtime/gcenv.ee.cpp`: `SuspendEE` is at lines 37-53,
`GcStartWork` at 78-82, `BeforeGcScanRoots` at 84-92,
`GcScanRoots` at 94-133, and `GcEnumAllocContexts` at 135-142. In particular,
the first source-defined root categories inside `GcScanRoots` are thread
static roots followed by the thread stack walk; the proof stops before that
`FOREACH_THREAD` body.

## 4. Proof instrumentation

The new append-only diagnostic records are in
`guidexos_nativeaot_allocation_diagnostics.h:51-508`. They include bounded
before/after context snapshots, 64-object history, exact pointer/limit and
segment fields, fixup/enumeration counters, object/sentinel validation,
root-phase counters, mutation flags, and the C011EC03 marker. The stage is
`F22` at line 549 and the marker constant is `0xC011EC03` at line 559 of the
same header.

The runtime probe now publishes context identity, `alloc_bytes`, and
`alloc_bytes_uoh` in
`guidexos_nativeaot_gc_allocation_probe.cpp:37-91`. The platform proof records
the pre-fixup request at `guidexos_nativeaot_platform.cpp:992-1020`, observes
the real EE enumeration at `1023-1051`, captures the post-fixup state at
`1054-1098`, records `BeforeGcScanRoots` at `1102-1108`, and stops at the
`GcScanRoots` boundary at `1112-1158`.

The source-injection mode is selected by
`smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1:1-28`. Its new
proof-only injections cover `GcEnumAllocContexts`, `GcStartWork`,
`BeforeGcScanRoots`, and `GcScanRoots` at lines 197-270. The original default
mode retains the C011EC02 injection and validation path.

No fake root provider, root callback, stack scan, marking callback, or
collector restart was added. The root boundary helper only records evidence,
validates read-only state, emits a bounded serial line, and parks forever.

## 5. Fresh run evidence

Fresh run directory:

`out\dotnet\gc-allocation-context-fixup-root-boundary\run-20260804-042803786\`

| QEMU run | Serial SHA-256 | Marker | Allocations | Contexts visited / changed / cleared | Objects before / after | Root providers / candidates / callbacks / marking |
| --- | --- | --- | --- | --- | --- | --- |
| first-run | `4661325FE9E2B3B82D07E447F0276A0BFB613206BDE83D32FC9ABD213918F876` | C011EC03 | 0x28 | 1 / 1 / 1 | 0x28 / 0x28 | 0 / 0 / 0 / 0 |
| repeat-1 | `627998229F27FF02470DEAE9DAD870494BE933A6A075FD701209E0CA1579FF04` | C011EC03 | 0x28 | 1 / 1 / 1 | 0x28 / 0x28 | 0 / 0 / 0 / 0 |
| repeat-2 | `454D26DE52C1870750E1CC1A7137660B70A5C1943D11AE0D544813286E8EBB33` | C011EC03 | 0x28 | 1 / 1 / 1 | 0x28 / 0x28 | 0 / 0 / 0 / 0 |

All three runs report `fixupMode=1`, `enumerationComplete=1`,
`metadataMutation=1`, `metadataComplete=1`, `objectMutation=0`,
`fixupFailures=0`, `rootFailures=0`, `objectFailuresBefore=0`,
`objectFailuresAfter=0`, `boundaryFailures=0`, `patternFailures=0`, and
`addressChanges=0`. The four sentinels were checked 0xA0 times across the
bounded workload and remained valid at the root boundary.

The first-run exact geometry record is:

| Field | Before fixup | After fixup |
| --- | --- | --- |
| allocation context identity | `0x0000000003929C00` | `0x0000000003929C00` |
| allocation pointer | `0x0000000100A285C8` | `0x0000000000000000` |
| allocation limit | `0x0000000100A29040` | `0x0000000000000000` |
| valid object extent | `0x0000000100A285C8` | `0x0000000100A285C8` |
| unused tail recorded | `0xA78` | `0x0` |
| heap allocation counter | `0x28E38` | `0x283C0` |
| `alloc_bytes` | `0x28E38` | `0x283C0` |
| segment allocated | `0x100A00028` | `0x100A285C8` |
| segment committed | `0x100A31000` | `0x100A31000` |
| segment reserved | `0x100B00000` | `0x100B00000` |

The pointer/limit clearing, `alloc_bytes` decrement, and segment allocated
change are the source-defined fixup effects. Object headers, array lengths,
payload patterns, ownership, alignment, non-overlap, and addresses were read
and validated before and after; object memory was never written by the proof.

## 6. Root-boundary decision

The dispatcher entry is a stronger boundary than `BeforeGcScanRoots`: it is
inside the locked `GCToEEInterface::GcScanRoots` function and before the first
`FOREACH_THREAD` iteration. Therefore the run proves root-dispatch entry, but
not a root provider entry or a root candidate. The selected category value
`1` is only the recorded source-order classification “thread statics then
stack”; it is not a claim that either provider executed.

The following remained zero in all runs: provider requests and entries, first
root candidates, root callbacks, promotion callbacks, stack scans, static-root
entries, handle-root entries, finalizer-root entries, marking, sweeping,
compaction, relocation, restart, and resume. The thread-store lock remained
held at the safe stop, with one registered managed thread, zero peers, and no
registry mutation while locked.

## 7. Regression and retained non-clean history

The default C011EC02 mode was rerun with three fresh QEMU processes after the
mode split. It passed with the preserved 0x28 / 0x13 / 0x16 / 0x15 allocation,
fast, rare, refill, and two same-segment-commit counters. Its fresh serial
hashes and manifest are under
`out\dotnet\gc-single-thread-suspend-ee\run-20260804-042051721\`.

The focused regression set was the new proof mode, the preserved C011EC02
mode, script/manifest/serial validation, and `git diff --check`. The broader
first-collection, segment/refill, runtime-pack, ELF/FLS/VM, native-thread,
stack, and general regression suites were not rerun in this focused pass; their
prior evidence remains the source of record.

The prior report's retained non-clean checks remain classified as historical,
not silently converted to passes: the first 64 KiB execution attempt, stale
cache attempts, the initial runtime-pack identity mismatch, and the native
stack PowerShell wrapper's exit 1 caused by compiler-stderr promotion. The
native-stack wrapper result remains non-clean and is not counted as a clean
wrapper pass. Those details remain in section 9 of
`NATIVEAOT_WORKSTATION_GC_SINGLE_THREAD_SUSPEND_EE.md`.

## 8. Artifacts and next boundary

The machine-readable manifest is in the fresh run directory as
`manifest.json`; each run contains `serial.log`, `serial.sha256`, and
`watchdog.json`, while the run root contains the generated source, exact
commands, build logs, selectors, kernel symbols, and restoration hash.

The next unsupported boundary is the first actual thread-static or stack-root
provider inside the locked `GcScanRoots` body. This report intentionally stops
before that boundary and does not authorize continuing the collection.

## First real per-thread provider follow-up - 2026-08-04

The next bounded result is documented in
`NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md`. It advances from
`C011EC03` through the real `ThreadStore::Iterator`, enumerates the registered
managed thread, enters the runtime-selected thread-static provider, and stops
before any candidate value is read. This report remains the historical
pre-`FOREACH_THREAD` checkpoint.

The follow-on candidate-load boundary is documented in
`NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md`; it preserves this
report's C011EC03 and stops before root semantics.
