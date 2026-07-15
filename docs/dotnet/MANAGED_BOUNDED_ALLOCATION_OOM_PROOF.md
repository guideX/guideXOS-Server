# Bounded Repeated Managed Allocation with Controlled OOM

Future GC status: this proof remains intentionally no-collection. The Workstation GC feasibility and future one-collection design are documented in [NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md](NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md). No GC behavior is enabled by this proof.

## 1. Executive summary

The AMD64 NativeAOT `HostLogProof` repeated mode creates real managed
`byte[]` objects through ordinary C# array allocation until a fixed,
image-backed no-collection heap is full. The primary 64 KiB run creates 234
arrays of 280 bytes, verifies monotonic allocation and sampled object
integrity, then reports a proof-specific nonfatal OOM result before the next
`newarr` can be attempted.

The successful result is called **bounded repeated managed allocation with
controlled OOM and collection disabled**. It is not garbage-collection
support and is not a general managed heap.

**Decision: Outcome A — repeated managed allocation reaches controlled OOM.**

## 2. Prior single-allocation baseline

The prior milestone remains preserved in
[`MANAGED_SINGLE_ALLOCATION_PROOF.md`](MANAGED_SINGLE_ALLOCATION_PROOF.md).
Its original result is one real managed `byte[24]`, reported as a 40-byte,
8-byte-aligned object with data at `+0x10`, executing twice in one Server
process and in a second process. This document does not rewrite that result.

The repeated proof uses the hydrated generated NativeAOT metadata envelope for
`byte[256]`; that envelope is 280 bytes. The two measurements are retained as
separate milestone configurations.

## 3. Transient `0xC0000409` investigation

The historical hosted `0xC0000409` was reproduced during the bounded
pre-repeated investigation, but it was not fully symbolized. Four bounded
single-allocation replays are preserved under
`out/dotnet/repeated-allocation-comparison/transient-failure/`; replays 1, 3,
and 4 failed while replay 2 passed both launches. The old failing output stops
at hosted dispatch before the managed host callback and contains no reliable
faulting instruction pointer, stack, or return transition.

The captured classification is therefore:

> Reproduced but not fully diagnosed

The later bounded diagnostic replays (`transient-failure/current-single-replays/`)
completed three clean two-launch sessions. This reduces current instability
but does not retroactively classify the historical termination as diagnosed.

Captured facts for the historical `0xC0000409`:

| Diagnostic | Result |
| --- | --- |
| Fault/fast-fail location | No symbolized IP in the preserved `0xC0000409` log |
| Managed entry | Failure occurred before managed callback evidence |
| Host callback | Not reached in the failing replay |
| Reverse-P/Invoke return | No evidence that return began |
| TLS runtime cell / initialized flag / transition frame | Not emitted before the failure |
| Allocation pointer / limit | Not emitted before the failure |
| Launch number | Bounded replay number is preserved in each log |
| Explicit guideXOS fail-fast helper | Not established for this historical failure |
| Security-cookie helpers | Reachable in stock generated artifacts; no exact failing path proved |

The search covered `__security_check_cookie`, `__report_gsfailure`,
`RaiseFailFastException`, `RhpFallbackFailFast`, `RhpFailFast`, `abort`, and
`terminate`. Security-cookie and stack-check behavior was not weakened.

A separate old `0xC0000005` is diagnosed in
`out/dotnet/reverse-pinvoke-comparison/failing/clean-current-server-rebuilt-20260713-062415/runtime.err.log`:
the faulting instruction was the stock `__imp_FlsGetValue` path at RIP
`0x8dc44`, before the callback. That is a distinct pre-runtime FLS failure,
not evidence that the repeated OOM path failed. A temporary metadata-hydration
fault is also preserved in
`out/dotnet/repeated-allocation-comparison/repeated-allocation/repeated-rehydration-fault-diagnostic.log`;
it was caused by passing an overlong ReadyToRun section length and was fixed by
using the exact dehydrated-data section table entry.

## 4. Repeated managed-source design

Repeated mode is selected by `HOSTLOGPROOF_REPEATED_ALLOCATION` and preserves
the non-allocating and single-allocation compile modes. The managed method:

1. asks the proof helper whether a 256-byte array envelope fits;
2. allocates `new byte[arrayLength]` through ordinary C# array allocation;
3. checks zero initialization, writes a sequence number and deterministic
   pattern, and validates the object;
4. validates four retained sample references before the next allocation and at
   OOM; and
5. reports one bounded final message through the existing synchronous host
   callback.

The loop is bounded at 512 iterations, so a broken boundary cannot become an
unbounded stress test.

## 5. Generated allocation path

The static map and linked disassembly preserve the real path:

```text
ManagedMain
  -> RhpNewArray
  -> guideXosStockRhpNewArray
  -> RhpNewFast
```

Repeated mode hydrates the exact generated ReadyToRun dehydrated-data section
once per process before inspecting the generated array EEType. No generated
machine code was patched and no branch bypass was added.

## 6. Heap geometry

Primary evidence (`evidence-final/`) reports:

| Field | Value |
| --- | --- |
| Heap base | `0x100a14b0` in the captured run; image address is build-dependent |
| Initial allocation pointer | `0x100a14b0` |
| Heap limit | `0x100b14b0` |
| Usable byte count | `65536` |
| Array length | `256` |
| Object size | `280` |
| Alignment | `8` |
| Allocation-context padding | None observed beyond the aligned object envelope |
| Runtime-owned managed allocations | None observed on the direct proof path |
| Sentinel/guard space | No separate sentinel; the exact limit comparison is authoritative |

The image-backed BSS heap is reset per managed launch. The runtime cell stores
the current pointer and limit for that launch.

## 7. Theoretical allocation count

The runtime uses an aligned object size and rejects a request when the full
object would exceed the limit. Therefore:

```text
floor((heapLimit - initialAllocationPointer) / objectSize)
= floor(65536 / 280)
= 234 allocations

remaining = 65536 - (234 * 280) = 16 bytes
```

No extra runtime prefix was deducted from the measured initial pointer; the
initial pointer is the beginning of the per-launch usable region.

## 8. Observed allocation count

The first launch, second in-process launch, and both launches in the second
Server process each reported:

```text
Managed allocations completed: 234
heap=65536; object=280; remaining=16
```

The observed count exactly matches the derived envelope.

## 9. Object alignment and layout

For the hydrated generated `byte[]` EEType:

| Layout item | Value |
| --- | --- |
| Object address | 8-byte aligned |
| Array length field | `+0x08` |
| Array data | `+0x10` |
| Component size | 1 byte |
| Generated base/envelope used for this proof | `0x18` |
| `byte[256]` aligned object size | `0x118` = 280 |

The native validation helper checks alignment, full range, length, data offset,
and the expected sequence/pattern after managed initialization.

## 10. Non-overlap evidence

Every successful `RhpNewArray` call records the allocation pointer before and
after the stock helper. The wrapper requires:

```text
after == before + alignedObjectSize
after <= heapLimit
```

The object validator also requires the returned object address to equal the
preceding allocation boundary and its full range to end at the new pointer.
The final reports show `monotonicity=PASS` and `nonOverlap=PASS` for all four
observed launch results.

## 11. Sampled-object integrity

The first four arrays are retained as managed references. Each has a sequence
number in the first four data bytes and a deterministic byte pattern in the
remaining data. They are validated before every subsequent allocation and
again immediately before OOM. The final reports show
`sampledIntegrity=PASS` and `zeroInit=PASS`; no earlier sample was corrupted.

## 12. OOM contract selected

The selected contract is **Behavior B: runtime-controlled nonfatal OOM return
through a proof-specific helper**. It is deliberately not a null-return change
to the generated `RhpNewArray` contract. The stock NativeAOT allocation helper
still has its normal nonrecoverable failure behavior if called without the
proof preflight.

## 13. OOM execution path

The first request that cannot fit follows this path:

```text
GuideXosManagedAllocationCanFit(256) == 0
  -> no newarr/RhpNewArray call is attempted
  -> no allocation pointer advancement
  -> retained samples are validated
  -> GuideXosManagedAllocationReport(status=0)
  -> managed method returns 0
  -> host callback reports controlledOom=1
```

No fake object and no managed exception are used.

## 14. Allocation pointer after failure

The primary report is:

```text
pointerBeforeFailure=0x100b14a0
pointerAfterFailure=0x100b14a0
currentPointer=0x100b14a0
currentLimit=0x100b14b0
```

The pointer remains 16 bytes below the limit and is unchanged by the failed
fit check.

## 15. Collection-path assertions

The diagnostic counters and live reports show:

```text
collectionEntered=0
```

No path entered GC suspension, collection initiation, marking, sweeping,
compaction, card processing, finalization, or a GC poll/wait at exhaustion.
The runtime pack remains a no-collection configuration.

## 16. Heap-expansion assertions

The fixed image-backed region was not expanded:

```text
heapExpansionOccurred=0
```

There were no additional managed regions, virtual-memory growth operations, or
reclamation events. The 64 KiB primary heap remains the tested configuration.

## 17. First launch result

Primary 64 KiB: PASS. The first launch completed 234 arrays, reached the
controlled OOM result, returned zero, logged once, cleaned up, and exited
normally.

## 18. Second in-process launch result

Primary 64 KiB: PASS. The same Server process launched the proof again with a
new runtime ID. The heap was reset to the same initial pointer and reproduced
234 successful arrays and the same 16-byte boundary remainder. No stale object
reference or allocation pointer survived the first launch.

## 19. Cross-process result

Primary 64 KiB: PASS. A newly launched experimental Server process reproduced
the same two successful launches, count, geometry, pointer behavior, integrity
flags, controlled OOM result, and normal cleanup.

The preserved logs and maps are under
`out/dotnet/repeated-allocation-comparison/repeated-allocation/evidence-final/`.

## 20. Small-heap variant result

The opt-in `Small4KiB` configuration passed without changing the default
configuration:

```text
heap=4096; object=280; expected count=floor(4096/280)=14; remaining=176
```

Both in-process launches reached 14 arrays and the same controlled OOM
boundary semantics. Arbitrary heap sizes are not accepted by the build/stage
scripts; only the locked primary and small diagnostic configurations are
available.

## 21. Import/helper drift

The three PE import sets were compared after clean builds:

| Mode | New KERNEL32 imports versus non-allocating |
| --- | --- |
| Non-allocating | None |
| Single allocation | `FreeLibrary`, `SetThreadErrorMode` |
| Repeated allocation/OOM | `FreeLibrary`, `SetThreadErrorMode` |

These are generated `__Internal` resolver imports and remained unentered on
the guideXOS path. `FlsGetValue` and `FlsSetValue` are absent from the custom
PE imports. No new CRT, FLS, or GC import was reached by the repeated proof.

The repeated map contains the four proof-specific helpers and the expected
`RhpNewArray` wrapper. The static repeated smoke also checks that the ELF has
no dynamic dependencies or relocations and that no generated-code bypass is
present.

## 22. Non-allocating regression

PASS. A clean non-allocating runtime-pack and managed artifact executed twice
in one experimental Server process. The host callback remained
`Hello from managed guideXOS code`; default inventory isolation also passed.

## 23. Single-allocation regression

PASS. A clean single-allocation runtime-pack and managed artifact linked after
the repeated-only helper declarations were corrected, then executed twice in
one Server process. The existing 40-byte `byte[24]` milestone remains intact.
The static single-allocation smoke and runtime-pack static smoke both passed.

## 24. What this proves

This pass proves bounded repeated real managed-object allocation through the
NativeAOT array helper, monotonic non-overlapping placement, zero-initialized
and sampled object integrity, exact fixed-heap boundary enforcement, a
proof-specific nonfatal OOM result, no collection, no heap expansion, Server
stability, and fresh per-launch heap behavior.

## 25. What remains unsupported

This is not a general managed heap and does not provide garbage collection,
object reclamation, compaction, finalization, weak references, managed OOM
exceptions, multiple managed threads, thread pool, tasks, async, or a public
permanent .NET runtime type. The stock allocation failure contract remains
nonrecoverable outside this explicit proof preflight.

## 26. Exact next experiment

Begin minimal real-GC feasibility work: identify the smallest matching
NativeAOT Workstation GC platform surface required to reclaim unreachable
objects, without implementing collection or claiming full collection support
in this pass.

## Validation summary

| Check | Result |
| --- | --- |
| Runtime-pack clean build | PASS |
| Non-allocating regression | PASS |
| Single-allocation regression | PASS |
| Repeated allocation start | PASS |
| Allocation monotonicity | PASS |
| Object non-overlap | PASS |
| Sampled object integrity | PASS |
| Controlled OOM reached | PASS |
| Pointer unchanged after OOM | PASS |
| Collection entered | No |
| Heap expanded | No |
| Second in-process OOM run | PASS |
| Second-process OOM run | PASS |
| Small-heap boundary | PASS |
| Generic Native ELF smoke | PASS |
| Normal Server build | PASS |
| Experimental Server build | PASS |
| Default inventory isolation | PASS |
| Historical `0xC0000409` | Reproduced but not fully diagnosed; not reproduced in three current bounded replays |
